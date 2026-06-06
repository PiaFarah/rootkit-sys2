# Créer les VMs

Le script `vm.sh` est **idempotent** : si les VMs existent déjà, le relancer les démarre simplement sans rien recréer. S'il manque quelque chose (image disque, ISO cloud-init), le script le recrée.

---

## Pourquoi ces distributions ?

**VM Victime : Debian 12 (Bookworm)**

Debian 12 embarque le kernel **6.1 LTS** par défaut. "LTS" signifie Long Term Support : l'API kernel ne change pas de façon incompatible entre deux sessions de travail. Si la VM Victime était sous Arch Linux (rolling-release), une mise à jour du kernel casserait le `.ko` compilé — il faudrait recompiler à chaque session. Avec Debian 12, le kernel reste stable.

**VM Attaquante : Arch Linux**

Le C2 est un programme userland en C. Il n'a aucune contrainte kernel — n'importe quelle distro suffit. Arch Linux est choisi car c'est l'environnement habituel de l'équipe (même distro que la machine hôte), avec des outils de développement récents.

**Pourquoi le kernel 6.1 spécifiquement ?**

C'est le kernel inclus dans Debian 12 stable. Nous n'avons pas choisi ce numéro : il est imposé par le choix de Debian 12. L'avantage est qu'il n'a pas besoin d'être mis à jour et que les headers correspondants sont disponibles via `apt` sans manipulation.

---

## Lancer les VMs

Ouvrez **deux terminaux** dans le répertoire du projet.

**Terminal 1 — VM Attaquante (doit démarrer en premier)**

```bash
./vm.sh attacker
```

**Terminal 2 — VM Victime**

```bash
./vm.sh victim
```

!!! warning "Ordre important"
    La VM attaquante doit démarrer **avant** la victime. Le réseau inter-VM utilise un socket QEMU en mode listen/connect : l'attaquante ouvre le socket serveur, la victime s'y connecte. Si la victime démarre en premier, elle ne trouve pas le socket et le réseau vmnet ne s'établit pas.

---

## Premier démarrage — ce qui se passe

Le premier lancement prend **3 à 5 minutes** par VM. Voici ce que fait le script :

| Étape | Action |
|---|---|
| 1 | Télécharge l'image cloud Debian 12 ou Arch Linux (une seule fois, conservée dans `vms/`) |
| 2 | Crée un disque `victim.qcow2` ou `attacker.qcow2` à partir de l'image de base |
| 3 | Génère un ISO cloud-init (`*-seed.iso`) avec la configuration initiale |
| 4 | Lance QEMU avec KVM, 2 CPUs, RAM adaptée, et deux interfaces réseau |

**Qu'est-ce que cloud-init ?**

Les images cloud sont des disques pré-installés, sans interface graphique, conçus pour être configurés au premier boot via un mécanisme appelé cloud-init. En pratique, `vm.sh` génère un ISO contenant deux fichiers (`user-data` et `network-config`) que cloud-init lit au premier démarrage pour :

- créer l'utilisateur `epita` avec le mot de passe `epita`
- configurer les interfaces réseau
- installer les paquets nécessaires (`build-essential`, `linux-headers-amd64`, etc.)
- configurer SSH

C'est ce qui rend le setup zéro-clic et 100% reproductible.

**Attendre le prompt `login:`**

Une fenêtre QEMU/GTK s'ouvre pour chaque VM. Attendez que la VM affiche `login:` dans la fenêtre. C'est le signe que cloud-init a terminé.

---

## Connexion SSH aux VMs

Une fois le `login:` affiché, vous pouvez vous connecter en SSH depuis la machine hôte :

```bash
# VM Attaquante
ssh -p 10023 epita@localhost   # mot de passe : epita

# VM Victime
ssh -p 10022 epita@localhost   # mot de passe : epita
```

SSH est généralement plus pratique que la fenêtre QEMU pour taper des commandes.

---

## Architecture réseau

Chaque VM a deux interfaces réseau :

```
Machine hôte
│
├─ SLIRP (eth0 dans les VMs) ──► NAT vers internet / SSH depuis l'hôte
│   Attaquante : ssh -p 10023 epita@localhost
│   Victime    : ssh -p 10022 epita@localhost
│
└─ Socket QEMU (vmnet dans les VMs) ──► réseau L2 direct entre les deux VMs
    Attaquante : 192.168.100.10/24
    Victime    : 192.168.100.20/24
```

L'interface `vmnet` est un réseau isolé, invisible depuis la machine hôte. C'est sur cette interface que transite le trafic C2 entre les deux VMs.

**Pourquoi deux interfaces séparées ?**

Si on utilisait la même interface pour SSH et pour le C2, le trafic rootkit passerait par la pile réseau de l'hôte, ce qui est indésirable dans un scénario réaliste. L'interface `vmnet` crée une connexion L2 directe entre les VMs, simulant un réseau local isolé.

---

## Dossier partagé (vmshare)

Le dossier `vmshare/` à la racine du projet est monté dans les deux VMs via VirtFS (9p, un protocole de partage de fichiers pour QEMU) :

```
Hôte     : ./vmshare/
Victime  : /mnt/vmshare/
Attaquant: /mnt/vmshare/
```

C'est le moyen de transférer les sources depuis l'hôte vers les VMs sans passer par SSH/SCP. Typiquement :

```bash
# Depuis l'hôte, copier les sources
cp -r rootkit/ vmshare/
cp -r attacking_program/ vmshare/

# Depuis la VM victime, compiler
cd /mnt/vmshare/rootkit && make
```

Le montage est configuré automatiquement par cloud-init dans `/etc/fstab`. Si la VM est déjà démarrée et que le montage n'est pas actif :

```bash
sudo mount -t 9p -o trans=virtio hostshare /mnt/vmshare
```

---

## Recréer une VM

Le script est idempotent : les images de base (`debian-12-base.qcow2`, `arch-base.qcow2`) ne sont jamais supprimées. Seules les images des VMs elles-mêmes et les ISOs cloud-init sont à supprimer pour forcer une recréation.

```bash
# Recréer la victime uniquement
rm vms/victim.qcow2 vms/victim-seed.iso
./vm.sh victim

# Recréer l'attaquante uniquement
rm vms/attacker.qcow2 vms/attacker-seed.iso
./vm.sh attacker

# Recréer les deux (sans re-télécharger les images de base)
rm vms/victim.qcow2 vms/victim-seed.iso vms/attacker.qcow2 vms/attacker-seed.iso
./vm.sh attacker   # terminal 1
./vm.sh victim     # terminal 2
```

---

## Erreur SSH "REMOTE HOST IDENTIFICATION HAS CHANGED"

Après une recréation de VM, les clés SSH de la VM changent. SSH refuse la connexion par sécurité. Supprimez l'ancienne entrée dans `~/.ssh/known_hosts` :

```bash
ssh-keygen -R "[localhost]:10022"   # victime
ssh-keygen -R "[localhost]:10023"   # attaquante
```

---

## Vérification

À la fin de cette étape, vous devez pouvoir :

- [x] `ssh -p 10022 epita@localhost` → connecté à la VM Victime (Debian 12)
- [x] `ssh -p 10023 epita@localhost` → connecté à la VM Attaquante (Arch Linux)
- [x] Sur la victime : `uname -r` affiche `6.1.x-xx-amd64`
- [x] Sur les deux VMs : `ls /mnt/vmshare/` est accessible
- [x] Sur la victime : `ping 192.168.100.10` répond (atteignable depuis la victime)
