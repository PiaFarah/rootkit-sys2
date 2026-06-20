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

### Connexion inverse — Reverse Connection
Mode de connexion où c'est la cible (le rootkit) qui initie la connexion vers l'attaquant, et non l'inverse.

---

## F

### FNV-1a — Fowler–Noll–Vo (variante 1a)
Algorithme de hachage non cryptographique, très léger et sans dépendance externe.

---

## K

### Kthread — Kernel Thread
Thread géré directement par le noyau Linux (pas un thread userland). WLKOM utilise un kthread pour que la boucle de connexion s'exécute en arrière-plan sans bloquer le chargement du module.

---

## L

### LKM — Loadable Kernel Module
Module kernel Linux chargeable dynamiquement (`insmod`/`rmmod`) sans recompiler le noyau. WLKOM se présente sous forme de LKM (`wlkom.ko`), ce qui lui donne accès à tous les privilèges du noyau.

---

## P

### Protocole réseau
Ensemble de règles définissant comment le rootkit et le C2 communiquent. Voir [Protocole réseau](code/protocol.md).

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

---

## V

### VirtFS / 9P
Protocole de partage de fichiers (issu de Plan 9, Bell Labs) utilisé par QEMU pour partager un dossier entre la machine hôte et une VM guest. Dans ce projet, le dossier `./vmshare/` de l'hôte est accessible dans la VM victime via `/mnt/vmshare/`, ce qui facilite le transfert du module compilé.

### VM — Machine Virtuelle
Environnement d'exécution isolé simulant un ordinateur complet. Ce projet utilise deux VMs QEMU/KVM : une VM victime (Debian 12) et une VM attaquante (Arch Linux).

### VFS — Virtual File System
Couche d'abstraction du noyau Linux qui unifie l'accès à tous les systèmes de fichiers derrière une interface commune (`open`, `read`, `write`…).
