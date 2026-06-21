# Persistance

`wlkom.ko` doit se charger automatiquement au démarrage, avec les bons paramètres, et seulement après que le réseau est prêt.

## Pourquoi systemd et pas `/etc/modules` ?

`/etc/modules` est traité trop tôt dans le boot, avant que le réseau soit disponible. Le kthread tenterait sa première connexion sans interface réseau active et échouerait. Il finirait par réussir après les retries, mais c'est une erreur au démarrage qui n'a pas de raison d'exister.

Systemd permet de contrôler précisément l'ordre de démarrage. `After=network-online.target` garantit que l'interface réseau est active avant que le module soit chargé.

## Pourquoi pas cron ?

Cron démarre aussi trop tôt et n'a pas de notion de dépendances entre services. Il faudrait bricoler un script qui sonde le réseau avant d'appeler `modprobe`, là où systemd gère ça nativement avec `After=` et `Wants=`.

Pour l'implémentation du service et les détails des options `Type=oneshot` et `RemainAfterExit` : [install_persistence.sh](../code/install-persistence.md)
