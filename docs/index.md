# WLKOM — Wild Linux Kernel Object Module

**WLKOM** est un rootkit Linux pédagogique développé dans le cadre du projet SYS2 à l'EPITA. C'est un module kernel (LKM) qui s'installe sur une machine victime, établit une connexion TCP persistante vers un programme attaquant distant, s'authentifie par hash, et exécute les commandes que l'attaquant lui envoie.

Le projet est pédagogique par nature : chaque choix est documenté et justifié, les "mauvaises pratiques" de sécurité offensive sont assumées et expliquées.

---

## Architecture

```
┌─────────────────────┐          réseau vmnet           ┌─────────────────────┐
│    VM Victime        │      192.168.100.x/24           │    VM Attaquante     │
│    Debian 12         │ ◄──────────────────────────────► │    Arch Linux        │
│                      │    TCP 4444 (reverse conn.)     │                      │
│  wlkom.ko (kernel)   │                                 │  c2 (userland)       │
│  - kthread           │                                 │  - écoute TCP        │
│  - socket kernel     │                                 │  - envoie commandes  │
│  - auth FNV-1a       │                                 │  - affiche résultats │
│  - exec commandes    │                                 │                      │
└─────────────────────┘                                  └─────────────────────┘
         ▲
         │ VirtFS / 9p
         │ /mnt/vmshare/
         ▼
┌─────────────────────┐
│   Machine hôte       │
│   Arch Linux         │
│   ./vmshare/         │
└─────────────────────┘
```

Le rootkit initie la connexion (reverse connection) : c'est la victime qui appelle l'attaquant, pas l'inverse. Le C2 écoute, attend, et dès qu'un rootkit se connecte il envoie le hash d'authentification puis propose un shell interactif.

---

## Composants

| Composant | Langage | Rôle |
|---|---|---|
| `rootkit/wlkom.c` | C (kernel) | Module LKM — connexion, auth, exécution de commandes |
| `attacking_program/c2.c` | C (userland) | Programme C2 — serveur TCP interactif |
| `rootkit/install_persistence.sh` | Bash | Installe wlkom comme service systemd au boot |
| `vm.sh` | Bash | Crée et lance les deux VMs QEMU/KVM |
| `tests/run_tests.py` | Python 3 | Tests automatisés (sans VM) |

---

## État des features

| Feature | Points | État |
|---|---|---|
| Compile | 0.5 | ✅ Terminé |
| Connexion TCP + retry | 3 | ✅ Terminé |
| Persistance (systemd) | 1.5 | ✅ Terminé |
| Authentification (FNV-1a) | 1 | ✅ Terminé |
| Exécution de commandes | 5 | ✅ Terminé |
| Upload / Download | 3 | ⬜ À faire |
| Hide (fichiers, lignes, lsmod) | 5 | ⬜ À faire |
| Chiffrement réseau | 1 | ⬜ À faire |

---

## Navigation

<div class="grid cards" markdown>

- **[Guide d'installation](guide/index.md)**

    Tout ce qu'il faut pour installer, configurer et utiliser le projet de zéro. Prérequis, création des VMs, compilation, chargement du module, utilisation du C2.

- **[Code expliqué](code/index.md)**

    Documentation narrative du code source. Chaque fichier est expliqué bloc par bloc : ce que fait chaque fonction, pourquoi elle est écrite ainsi, les pièges évités.

- **[Décisions de conception](decisions/index.md)**

    Pourquoi ces choix ? Chaque décision importante (distro, kernel, protocole, algo de hash…) est justifiée avec les alternatives envisagées.

- **[Tests](tests/index.md)**

    Comment lancer la suite de tests, ce qu'elle couvre et ce qu'elle ne couvre pas.

</div>
