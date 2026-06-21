# Glossaire

Définitions des termes techniques et acronymes utilisés dans ce projet.

---

## A

### Auth / Authentification
Mécanisme permettant de vérifier qu'un client est bien autorisé à communiquer avec le rootkit. Dans WLKOM, l'authentification repose sur la comparaison d'un hash [FNV-1a](#fnv-1a) du mot de passe.

---

## C

### C2 — Command & Control
Serveur contrôlé par l'attaquant qui reçoit les connexions des implants (rootkits) et leur envoie des commandes.

### call_usermodehelper
Fonction du kernel Linux permettant d'exécuter un programme userland depuis le code kernel. Avec le flag `UMH_WAIT_PROC`, elle bloque jusqu'à la fin du processus fils. Dans WLKOM, c'est le mécanisme utilisé par `wlkom.ko` pour exécuter les commandes shell reçues du C2 et capturer leur sortie.

### cloud-init
Outil standard de provisionnement de VMs. Il configure automatiquement une machine au premier démarrage à partir de fichiers texte (`user-data`, `network-config`). Dans ce projet, il rend la création des VMs entièrement automatique : une seule commande `vm.sh`, sans interaction manuelle.

### Connexion inverse — Reverse Connection
Mode de connexion où c'est la cible (le rootkit) qui initie la connexion vers l'attaquant, et non l'inverse.

---

## D

### depmod
Commande Linux qui reconstruit l'index des modules disponibles pour le kernel courant. À appeler après avoir copié un `.ko` dans `/lib/modules/$(uname -r)/`, sinon `modprobe` ne trouve pas le module même si le fichier est au bon endroit.

---

## F

### FNV-1a — Fowler–Noll–Vo (variante 1a)
Algorithme de hachage non cryptographique, très léger et sans dépendance externe.

---

## I

### insmod
Commande Linux pour charger un module kernel directement depuis un fichier `.ko`. Les paramètres doivent être passés explicitement en ligne de commande (`insmod wlkom.ko password_hash=...`). Contrairement à [modprobe](#modprobe), `insmod` ne lit pas `/etc/modprobe.d/`. Voir aussi `rmmod` pour décharger un module.

---

## K

### Kernel panic
Arrêt brutal et immédiat du système Linux déclenché par une erreur fatale dans le kernel (dépassement de pile, accès mémoire invalide...). Contrairement à un plantage userland qui ne touche qu'un seul processus, un kernel panic arrête toute la machine. C'est pourquoi les buffers en kernel space sont alloués sur le heap (`kmalloc`) et non sur la pile.

### Kernel space / User space
Le processeur distingue deux niveaux d'exécution. Le **kernel space** (ring 0) est réservé au noyau : accès direct au matériel, aucune restriction. L'**user space** (ring 3) est l'environnement des programmes normaux : l'accès aux ressources passe obligatoirement par des appels système. Un plantage en user space ne touche que le processus concerné ; un plantage en kernel space provoque un [kernel panic](#kernel-panic). `wlkom.ko` tourne en kernel space, `c2.c` en user space.

### Kthread — Kernel Thread
Thread géré directement par le noyau Linux (pas un thread userland). WLKOM utilise un kthread pour que la boucle de connexion s'exécute en arrière-plan sans bloquer le chargement du module.

---

## L

### LKM — Loadable Kernel Module
Module kernel Linux chargeable dynamiquement (`insmod`/`rmmod`) sans recompiler le noyau. WLKOM se présente sous forme de LKM (`wlkom.ko`), ce qui lui donne accès à tous les privilèges du noyau.

---

## M

### modprobe
Commande Linux pour charger un module kernel par son nom (sans chemin). Contrairement à [insmod](#insmod), il lit automatiquement les paramètres dans `/etc/modprobe.d/` avant de charger le module. C'est ce qu'utilise le service systemd de WLKOM : `modprobe wlkom` suffit, les paramètres sont lus depuis `/etc/modprobe.d/wlkom.conf`.

### Multiplexage I/O
Technique permettant d'attendre simultanément des événements sur plusieurs sources (sockets, stdin...) sans bloquer sur l'une d'elles. L'appel système `select()` retourne dès que l'une des sources est prête. Dans `c2.c`, utilisé pour détecter une déconnexion du rootkit pendant qu'on attend une saisie de l'opérateur.

---

## P

### Protocole réseau
Ensemble de règles définissant comment le rootkit et le C2 communiquent. Voir [Protocole réseau](code/protocol.md).

---

## Q

### QEMU/KVM
QEMU est un logiciel d'émulation de machine virtuelle. KVM (Kernel-based Virtual Machine) est un module kernel Linux qui permet à QEMU d'exécuter du code à vitesse quasi-native, en s'appuyant sur les instructions de virtualisation du processeur. Ce projet utilise QEMU/KVM pour les deux VMs, piloté depuis la machine hôte Arch Linux.

---

## R

### Rootkit
Logiciel malveillant qui s'exécute avec des privilèges élevés (ici, ring 0 / kernel space) et peut masquer sa présence, exécuter des commandes à distance, et persister après un redémarrage.

### Reverse Connection
Voir [Connexion inverse](#connexion-inverse--reverse-connection).

---

## S

### Systemd
Système d'init standard sur les distributions Linux modernes. WLKOM utilise une unité systemd pour se charger automatiquement au démarrage (persistance).

### Socket kernel
Interface réseau utilisée directement depuis le code kernel (sans passer par userland). WLKOM crée un socket TCP depuis le module kernel via `sock_create` / `kernel_connect`.

---

## T

### TCP — Transmission Control Protocol
Protocole réseau orienté connexion garantissant la livraison des données dans l'ordre. WLKOM utilise TCP pour la communication entre le rootkit et le C2.

### TTY
Abréviation de *TeleTYpewriter*, désigne aujourd'hui un terminal interactif. Tester si stdin est un TTY permet de savoir si le programme est piloté par un humain ou par un script. Dans `c2.c`, ce test conditionne la désactivation de l'écho lors de la saisie du mot de passe : inutile (et risqué) de modifier le terminal si l'entrée vient d'un pipe.

---

## V

### vermagic
Empreinte intégrée dans chaque fichier `.ko` qui identifie exactement la version et la configuration du kernel contre lequel il a été compilé. Si le kernel de la machine cible ne correspond pas à cette empreinte, `modprobe` refuse de charger le module. C'est la raison principale du choix de Debian 12 pour la VM Victime : son kernel 6.1 est figé et ne change pas sans intervention explicite.

### VirtFS / 9P
Protocole de partage de fichiers (issu de Plan 9, Bell Labs) utilisé par QEMU pour partager un dossier entre la machine hôte et une VM guest. Dans ce projet, le dossier `./vmshare/` de l'hôte est accessible dans la VM victime via `/mnt/vmshare/`, ce qui facilite le transfert du module compilé.

### VM — Machine Virtuelle
Environnement d'exécution isolé simulant un ordinateur complet. Ce projet utilise deux VMs QEMU/KVM : une VM victime (Debian 12) et une VM attaquante (Arch Linux).

### VFS — Virtual File System
Couche d'abstraction du noyau Linux qui unifie l'accès à tous les systèmes de fichiers derrière une interface commune (`open`, `read`, `write`…).

---

## W

### Workqueue
Mécanisme du kernel Linux pour exécuter du travail différé dans un pool de threads partagés entre plusieurs sous-systèmes. Adapté pour des tâches courtes et non bloquantes. WLKOM a opté pour un [kthread](#kthread--kernel-thread) dédié à la place : `kernel_recvmsg` est bloquant par conception, et bloquer un worker du pool impacterait tout le reste du kernel.
