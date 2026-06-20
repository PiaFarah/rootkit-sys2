# Créer les VMs

!!! question "Pourquoi ces choix ?"
    Voir [Environnement](../decisions/environment.md).

Le script `vm.sh` est **idempotent** : si les VMs existent déjà, le relancer les démarre simplement sans rien recréer. S'il manque quelque chose, le script le recrée.

---

## Lancer les VMs

Ouvrez deux terminaux dans le répertoire du projet.

**Terminal 1 — VM Attaquante**

```bash
./vm.sh attacker
```

**Terminal 2 — VM Victime**

```bash
./vm.sh victim
```

!!! warning "Ordre important"
    Lancer l'attaquante **avant** la victime. Elle ouvre le socket réseau inter-VM que la victime doit rejoindre.

!!! info "Premier lancement"
    Le script télécharge l'image et configure la VM : prévoir **3 à 5 minutes**.

Attendez le prompt `login:` dans la fenêtre QEMU avant de vous connecter en SSH.

---

## Connexion SSH aux VMs

Une fois le `login:` affiché, vous pouvez vous connecter en SSH depuis la machine hôte (mot de passe : `epita`).

**Terminal 3 — VM Attaquante**

```bash
ssh -p 10023 epita@localhost
```

**Terminal 4 — VM Victime**

```bash
ssh -p 10022 epita@localhost
```

SSH est généralement plus pratique que la fenêtre QEMU pour taper des commandes.

---

## Dossier partagé vmshare

Le dossier `vmshare/` à la racine du projet est monté sous `/mnt/vmshare` dans les deux VMs. C'est par ce dossier que vous copierez les sources depuis l'hôte pour les compiler dans les VMs.

---

!!! success "Vérification"
    À la fin de cette étape, vous devez pouvoir :

    - [x] `ssh -p 10022 epita@localhost` → connecté à la VM Victime
    - [x] `ssh -p 10023 epita@localhost` → connecté à la VM Attaquante
    - [x] Sur la victime : `uname -r` affiche `6.1.x-xx-amd64`
    - [x] Sur la victime : `ping 192.168.100.10` répond (atteignable depuis la victime)
