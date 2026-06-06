# Persistance

## Le problème

La persistance signifie que `wlkom.ko` doit se charger **automatiquement au démarrage** de la VM Victime, avec les bons paramètres, et uniquement après que le réseau est prêt.

---

## Approches envisagées

### Approche A — Service systemd ✅ Choisie

Créer un service systemd qui charge le module via `modprobe`. Les paramètres sont stockés dans `/etc/modprobe.d/wlkom.conf`.

```ini
[Unit]
After=network-online.target
Wants=network-online.target

[Service]
Type=oneshot
ExecStart=/sbin/modprobe wlkom
```

**Avantages :**
- Contrôle précis de l'ordre de démarrage via `After=network-online.target`
- `modprobe` lit automatiquement les paramètres depuis `/etc/modprobe.d/`
- `systemctl status wlkom.service` donne l'état du service
- `systemctl enable/disable` gère l'activation au boot proprement

**Inconvénient :**
- Le service est visible dans `systemctl list-units` — pas discret. Acceptable dans ce contexte pédagogique.

---

### Approche B — `/etc/modules` + `/etc/modprobe.d/`

Ajouter `wlkom` dans `/etc/modules` pour qu'il soit chargé au boot.

**Problème :** `/etc/modules` est traité très tôt dans le boot, avant que le réseau soit configuré. Le module démarrerait, tenterait de se connecter, échouerait, et retenterait grâce au mécanisme de retry du kthread. Ça fonctionnerait, mais avec un délai et une tentative de connexion ratée au démarrage.

Moins propre que systemd, et sans contrôle fin de l'ordre de démarrage.

**Non retenue.**

---

### Approche C — Cron `@reboot`

```cron
@reboot root insmod /path/to/wlkom.ko password_hash=xxx ...
```

**Problème :** cron démarre tôt, souvent avant que le réseau soit disponible. Même problème qu'avec `/etc/modules`. De plus, passer le `password_hash` en clair dans la crontab n'est pas idéal même pour un projet pédagogique.

**Non retenue.**

---

## Pourquoi `modprobe` et pas `insmod` dans le service ?

`modprobe` lit automatiquement `/etc/modprobe.d/wlkom.conf` pour les paramètres. `insmod` nécessite de les passer explicitement en ligne de commande dans le service. Avec `modprobe`, les paramètres sont centralisés dans un seul fichier et le service reste simple.

## Pourquoi `After=network-online.target` ?

`network-online.target` est atteint quand systemd considère le réseau comme opérationnel (au moins une interface avec une adresse IP configurée). En attendant ce target, on garantit que l'interface `vmnet` (192.168.100.x) est active avant que le module tente sa première connexion vers le C2.

Sans ça, le module démarrerait avant que `vmnet` soit configurée et ne trouverait pas la route vers `192.168.100.10`. Le kthread retenterait toutes les 5 secondes et finirait par se connecter quand le réseau serait prêt — mais ce n'est pas propre.

## Pourquoi `RemainAfterExit=yes` ?

`ExecStart` lance `modprobe wlkom`, qui retourne dès que le module est chargé (le kthread démarre en arrière-plan). Sans `RemainAfterExit=yes`, systemd verrait que le processus `ExecStart` s'est terminé et marquerait le service comme `inactive`. Avec cette option, le service reste `active (exited)` tant qu'il n'a pas été explicitement stoppé.
