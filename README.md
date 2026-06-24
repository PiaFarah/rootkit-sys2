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
cd /mnt/vmshare/attacking_program && make run

# ── Terminal 4 : SSH vers la VM VICTIME ─────────────────────────────────────
ssh -p 10022 epita@localhost   # mdp: epita

# Compiler le module
cd /mnt/vmshare/rootkit && make persistence

```

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
