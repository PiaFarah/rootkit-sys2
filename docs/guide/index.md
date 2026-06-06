# Guide d'installation

Ce guide couvre tout ce dont vous avez besoin pour faire fonctionner WLKOM de zéro, sur une machine hôte Arch Linux vierge. Il n'y a aucun prérequis en dehors d'un accès internet et des droits sudo.

Chaque étape est numérotée et indépendante. Lisez-les dans l'ordre.

---

## Vue d'ensemble

L'environnement WLKOM repose sur **deux machines virtuelles** qui communiquent entre elles :

- La **VM Victime** (Debian 12) — là où le module kernel `wlkom.ko` tourne
- La **VM Attaquante** (Arch Linux) — là où le programme C2 écoute et envoie des commandes

Les deux VMs sont créées et gérées par le script `vm.sh` via QEMU/KVM. Elles s'exécutent localement sur votre machine hôte Arch Linux.

```
Machine hôte (Arch Linux)
├── VM Attaquante (Arch)  — terminal 1   ./vm.sh attacker
└── VM Victime (Debian)   — terminal 2   ./vm.sh victim
```

---

## Prérequis sur la machine hôte

### 1. Vérifier la virtualisation matérielle

```bash
grep -c -E 'vmx|svm' /proc/cpuinfo
```

Le résultat doit être supérieur à 0. Si c'est 0, activez la virtualisation dans le BIOS (VT-x pour Intel, AMD-V pour AMD).

### 2. Installer les paquets nécessaires

```bash
sudo pacman -S qemu-full cdrtools wget
```

| Paquet | Rôle |
|---|---|
| `qemu-full` | Hyperviseur QEMU avec KVM |
| `cdrtools` | Fournit `mkisofs`, utilisé par `vm.sh` pour générer les ISOs cloud-init |
| `wget` | Téléchargement des images disque cloud |

### 3. Cloner le projet (si pas déjà fait)

```bash
git clone <url-du-repo>
cd rootkit
```

---

## Suite du guide

Une fois les prérequis installés, suivez les étapes dans l'ordre :

1. [Créer les VMs](vms.md) — téléchargement des images, configuration réseau, premier boot
2. [Compiler le module](compile.md) — compiler `wlkom.ko` sur la VM victime
3. [Charger le module](load.md) — charger `wlkom.ko` manuellement avec `insmod`
4. [Installer la persistance](persistence.md) — faire démarrer le module automatiquement au boot
5. [Utiliser le C2](c2.md) — compiler et utiliser le programme attaquant
