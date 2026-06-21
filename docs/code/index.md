# Implémentation

APIs utilisées, flux d'exécution, pièges évités : ce qui se passe vraiment à l'intérieur de chaque composant, pour quelqu'un qui lirait le code sans contexte.

---

## Les composants et leurs rôles

WLKOM a trois composants distincts qui se coordonnent :

```
rootkit/wlkom.c                    ← module kernel, côté victime
attacking_program/c2.c             ← serveur C2, côté attaquant
rootkit/install_persistence.sh     ← script d'installation systemd
```

Ils ne tournent pas sur la même machine, ni dans le même espace :

| Composant | Espace | Machine |
|---|---|---|
| `wlkom.c` | Kernel space | VM Victime |
| `c2.c` | User space | VM Attaquante |
| `install_persistence.sh` | User space (root) | VM Victime |

---

## Pages de cette section

- **[wlkom.c](wlkom.md)** : Le module kernel, de l'initialisation à l'exécution de commandes. La partie la plus complexe du projet.
- **[c2.c](c2.md)** : Le serveur C2 en user space. Comparativement simple.
- **[install_persistence.sh](install-persistence.md)** : Le script d'installation du service systemd.
- **[Protocole réseau](protocol.md)** : Spécification du protocole de communication entre wlkom et c2.
