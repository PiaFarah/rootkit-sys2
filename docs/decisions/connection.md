# Connexion TCP

## Le problème

Le module kernel doit établir une connexion TCP vers le C2 (reverse connection), la maintenir en vie, et la rétablir automatiquement si elle tombe — tout en restant déchargeable proprement avec `rmmod`.

---

## Approches envisagées

### Approche A — Socket kernel + kthread ✅ Choisie

Créer un kthread au chargement du module. Ce thread tourne en boucle et gère tout : connexion, retry, lecture des commandes.

```
wlkom_init()
  └─ kthread_run(connection_thread)
        ├─ sock_create + kernel_connect
        ├─ auth + boucle recv/exec
        └─ retry sur déconnexion
```

**Avantages :**
- Tout se passe dans le kernel, aucun processus userland visible
- Le kthread est invisible dans `ps aux`
- Les API kernel réseau (`sock_create`, `kernel_connect`, `kernel_recvmsg`, `kernel_sendmsg`) sont stables et bien documentées
- Le même socket sert à la fois pour la connexion et les commandes

**Inconvénient :**
- API kernel ésotérique (pas de `connect(2)`, pas de `recv(2)` — des équivalents kernel moins connus)

---

### Approche B — `call_usermodehelper` pour lancer un script userland

Au `init`, lancer un script shell ou Python qui gère la connexion TCP.

**Problème principal :** comment faire remonter les I/O entre le userland et le kernel ? Le script peut se connecter au C2, mais les commandes envoyées par le C2 doivent être exécutées par le kernel et leurs résultats renvoyés. Ça nécessite un canal de communication kernel ↔ userland (pipe nommé, Netlink, etc.) — une complexité bien supérieure à l'approche A.

Par ailleurs, un processus userland est visible dans `ps aux`. Pour un rootkit, c'est un défaut.

**Non retenue.**

---

### Approche C — Netlink socket (kernel ↔ daemon userland)

Le module crée un socket Netlink et communique avec un daemon userland qui gère le TCP.

**Problème :** deux composants à maintenir et déployer, protocole Netlink verbeux, et le daemon est visible en userland. La complexité est bien supérieure à l'approche A pour aucun gain réel dans ce contexte.

**Non retenue.**

---

## Pourquoi un kthread et pas un workqueue ?

Un workqueue est adapté pour des tâches courtes et occasionnelles (quelques ms). Notre boucle de connexion peut bloquer indéfiniment sur `kernel_recvmsg` en attente d'une commande — ce n'est pas compatible avec un workqueue (qui bloquerait le thread du pool de workers). Un kthread dédié est le bon choix pour une tâche longue durée avec des opérations bloquantes.

## Pourquoi `kernel_connect` bloquant depuis un kthread et pas depuis `init` ?

`kernel_connect` est bloquant par défaut. Si on l'appelait depuis `wlkom_init()` directement, le chargement du module (`insmod`) bloquerait jusqu'à ce que la connexion réussisse ou échoue — ce qui peut prendre plusieurs secondes. Avec un kthread, `wlkom_init()` retourne immédiatement et le module est chargé. La connexion se fait en arrière-plan.

## Comment le kthread est-il arrêté proprement au `rmmod` ?

C'est le problème le plus délicat. Le kthread peut être bloqué sur `kernel_recvmsg` quand `rmmod` est invoqué. On ne peut pas juste appeler `kthread_stop` — il bloquerait indéfiniment en attendant que le thread sorte.

La solution : appeler `kernel_sock_shutdown(sock, SHUT_RDWR)` **avant** `kthread_stop`. `kernel_sock_shutdown` ferme le socket côté kernel, ce qui force `kernel_recvmsg` à retourner avec une erreur. Le thread peut alors tester `kthread_should_stop()` et sortir proprement.

```
wlkom_exit():
    1. kernel_sock_shutdown(sock)  ← débloque recvmsg
    2. kthread_stop(task)          ← attend que le thread sorte
    3. sock_release(sock)          ← libère le socket
```

L'ordre 1→2→3 est obligatoire. Inverser 1 et 2 causerait un blocage. Faire 3 avant 2 libérerait un socket encore utilisé par le thread.
