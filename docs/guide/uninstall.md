# Désinstaller

Cette page regroupe toutes les opérations de nettoyage et de désinstallation, dans l'ordre logique inverse du déploiement.

---

## Désactiver la persistance et décharger le module

Sur la **VM Victime**, supprime le service systemd, la configuration modprobe, le `.ko` installé, et décharge le module du noyau en cours :

```bash
cd /mnt/vmshare/rootkit
make uninstall
```

### Vérifier la désinstallation

Le module ne doit plus apparaître :

```bash
lsmod | grep wlkom
```

Le service doit être introuvable :

```bash
sudo systemctl status wlkom.service
```

### Vérifier après un reboot

```bash
sudo reboot
```

Après reconnexion SSH :

```bash
lsmod | grep wlkom
sudo systemctl status wlkom.service
```

Les deux commandes doivent retourner vide, le module ne se recharge pas.

---

## Nettoyer les artefacts de compilation du rootkit

Sur la **VM Victime**, supprime les fichiers générés par la compilation :

```bash
cd /mnt/vmshare/rootkit
make clean
```

---

## Nettoyer les artefacts de compilation du C2

Sur la **VM Attaquante**, supprime le binaire compilé :

```bash
cd /mnt/vmshare/attacking_program
make clean
```
