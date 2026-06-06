# Exécution de commandes

## Le problème

Depuis le C2, l'attaquant envoie une commande shell arbitraire. Le rootkit (dans le kernel) doit l'exécuter côté victime et renvoyer stdout, stderr et l'exit code séparément.

La contrainte : tout ça se passe depuis l'espace kernel. Il n'y a pas de `fork()`, pas de `pipe()`, pas de `read()` standard.

---

## Approches envisagées

### Approche A — `call_usermodehelper` + fichiers temporaires ✅ Choisie

```
C2 → "id\n"
kthread → construit "/bin/sh -c '(id) > /tmp/.wlkom_out 2> /tmp/.wlkom_err'"
       → call_usermodehelper(..., UMH_WAIT_PROC)
       → filp_open("/tmp/.wlkom_out") + kernel_read()
       → filp_open("/tmp/.wlkom_err") + kernel_read()
       → kernel_sendmsg(résultat formaté)
       → call_usermodehelper("rm -f /tmp/.wlkom_out /tmp/.wlkom_err")
```

**Avantages :**
- `call_usermodehelper` est l'API officielle du kernel pour exécuter du userland depuis le kernel
- La redirection shell (`>`, `2>`) gère la capture sans code kernel supplémentaire
- `UMH_WAIT_PROC` bloque jusqu'à la fin de la commande — pas de race condition entre l'exécution et la lecture des fichiers
- stdout et stderr restent séparés (deux fichiers distincts)
- L'exit code est récupéré via la valeur de retour de `call_usermodehelper`

**Inconvénient :**
- Les fichiers temporaires sont visibles en userland pendant l'exécution (quelques millisecondes)
- Traitement séquentiel uniquement (une commande à la fois)

---

### Approche B — `call_usermodehelper` + pipes anonymes kernel

Créer des pipes en espace kernel, attacher les fd du processus fils aux pipes, lire directement en mémoire.

**Problème :** la gestion des pipes en espace kernel est extrêmement complexe. Il n'existe pas d'API de haut niveau équivalente à `pipe()`. Il faut manipuler des `struct file *` internes, des `struct pipe_inode_info`, etc. Très peu d'exemples disponibles, haut risque de crash kernel. La complexité n'est pas justifiée ici.

**Non retenue.**

---

### Approche C — Reverse shell direct sur le socket

Dupliquer les fd stdin/stdout/stderr du processus vers le socket TCP, lancer `/bin/sh`.

**Problème :** stdin/stdout/stderr sont mélangés sur le même fd. On ne peut plus séparer stdout et stderr, et il n'y a pas d'exit code. Le sujet demande explicitement les trois séparément.

De plus, un reverse shell classique ne retourne pas la main au kthread après chaque commande — la session est interactive mais difficile à contrôler programmatiquement.

**Non retenue** pour cette feature (mais une idée valide pour une feature "shell interactif" distincte).

---

## Pourquoi `UMH_WAIT_PROC` et pas `UMH_NO_WAIT` ?

`UMH_NO_WAIT` lance le processus et retourne immédiatement, sans attendre la fin. Si on lisait les fichiers temporaires juste après, ils seraient vides ou incomplets — la commande est encore en train d'écrire dedans. `UMH_WAIT_PROC` bloque le kthread jusqu'à ce que le processus se termine, garantissant que stdout/stderr sont disponibles en totalité.

## Pourquoi allouer les buffers sur le tas (`kmalloc`) et pas sur la pile ?

Les stacks kernel sont petites : 4 Ko ou 8 Ko selon la configuration. Une sortie de commande (ex: `find / -name "*.conf"`) peut facilement dépasser ça. Un dépassement de pile kernel provoque une panic immédiate du système. `kmalloc` alloue sur le heap kernel, sans limite autre que la RAM disponible.

## Pourquoi décoder l'exit status avec `(ret >> 8) & 0xFF` ?

`call_usermodehelper` retourne le statut brut au format `wait4` du kernel. Ce format encode deux informations : l'exit code dans les 8 bits de poids fort, et le signal de terminaison dans les 8 bits de poids faible. `(ret >> 8) & 0xFF` extrait les 8 bits de poids fort — c'est exactement ce que `$?` en shell affiche, la valeur entre 0 et 255 que le processus a passée à `exit()`.

## Les fichiers dans `/dev/shm` plutôt que `/tmp` ?

`/dev/shm` est un système de fichiers en RAM (tmpfs) — les écritures ne touchent pas le disque. C'est plus propre pour des fichiers temporaires qui doivent disparaître. Dans l'implémentation actuelle, on utilise `/tmp` qui peut être sur disque. C'est une amélioration possible mais non critique pour un usage pédagogique.
