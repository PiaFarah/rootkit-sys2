# Environnement et VMs

## Pourquoi QEMU/KVM ?

Le sujet l'impose (section 4.5). QEMU/KVM est la référence pour la virtualisation sur Linux : performances natives grâce à KVM (extension de virtualisation matérielle), large adoption, documentation abondante. Nous l'utilisons en ligne de commande directe, sans libvirt.

**Pourquoi pas libvirt ?**

libvirt est une couche d'abstraction au-dessus de QEMU. Elle gère les VMs via un daemon (`libvirtd`), des fichiers XML de configuration, et des réseaux virtuels gérés par son propre DHCP. Pour deux VMs locales de dev, c'est une complexité inutile. QEMU en ligne de commande dans `vm.sh` suffit et évite de nécessiter les droits d'administration pour gérer le daemon.

---

## Pourquoi deux VMs séparées ?

Le sujet demande deux machines : une victime et une attaquante. Dans un scénario réaliste, le rootkit et le C2 ne sont jamais sur la même machine. Avoir deux VMs séparées permet de :

- Tester le trafic réseau réel entre deux machines distinctes
- Vérifier que rien ne "court-circuite" localement (pas de loopback)
- Simuler un scénario d'attaque crédible

---

## Pourquoi Debian 12 pour la victime ?

Le choix de la distro pour la victime est contraint par une exigence technique : **le kernel doit rester stable entre les sessions de travail**.

Un module kernel compilé (`wlkom.ko`) est lié à un kernel précis. Si le kernel change (mise à jour), le `.ko` ne se charge plus — il est rejeté par le vérificateur de signature de module. Il faut recompiler.

| Distro | Type de release | Risque |
|---|---|---|
| Arch Linux | Rolling-release | Kernel mis à jour à chaque `pacman -Syu` → recompilation obligatoire |
| Ubuntu LTS | LTS, mais mises à jour de sécurité fréquentes | Kernel peut changer |
| **Debian 12** | **Stable, freeze des versions** | **Kernel 6.1 LTS ne change pas sans intervention explicite** |

Debian 12 embarque le kernel **6.1 LTS**. "LTS" (Long Term Support) signifie que ce kernel reçoit uniquement des patches de sécurité, sans changements d'API. La version `vermagic` dans le `.ko` reste compatible.

**Pourquoi le kernel 6.1 précisément ?**

C'est le kernel inclus par défaut dans Debian 12 stable. Nous ne l'avons pas choisi nous-mêmes — c'est un sous-produit du choix de Debian 12. L'avantage : les headers sont disponibles via `apt install linux-headers-$(uname -r)` sans manipulation.

**Sécurités kernel désactivées ?**

Sur certaines configurations, `CONFIG_MODULE_SIG` ou `CONFIG_LOCKDOWN_KERNEL` empêchent le chargement de modules non signés. Ces options sont désactivées dans la configuration par défaut de Debian 12 (kernel cloud → kernel standard). C'est voulu et documenté : WLKOM est pédagogique, la sécurité offensive est assumée.

---

## Pourquoi Arch Linux pour l'attaquante ?

Le C2 est un programme userland en C. Il n'a aucune contrainte kernel — n'importe quelle distro Linux suffit. Arch Linux est choisi pour deux raisons :

1. C'est l'environnement de la machine hôte (même distro) → familiarité, pas de dépaysement
2. Les outils de développement (`gcc`, `make`, etc.) sont récents et facilement installables

La VM Attaquante ne nécessite pas de stabilité kernel : on n'y compile pas de module.

---

## Pourquoi cloud-init plutôt qu'une installation manuelle ?

Les images cloud sont des disques pré-installés conçus pour être configurés au premier boot via cloud-init. Cela permet à `vm.sh` d'être entièrement automatique : une seule commande crée et configure la VM sans aucune interaction.

Un ISO d'installation standard nécessiterait de répondre manuellement à des dizaines de questions (langue, partition, user, password...) pour chaque nouvelle VM. Avec cloud-init, tout ça est dans `user-data` et `network-config`, des fichiers texte versionnables et reproductibles.

---

## Pourquoi un dossier partagé VirtFS (9p) ?

Le dossier partagé `vmshare/` permet de transférer les sources entre l'hôte et les VMs sans passer par SSH/SCP. L'édition se fait sur l'hôte (avec son éditeur favori), la compilation se fait sur la VM Victime (avec les bons headers).

VirtFS utilise le protocole 9p, supporté nativement par QEMU et le kernel Linux. C'est plus simple qu'un partage NFS ou Samba pour un usage local.

---

## Pourquoi deux interfaces réseau par VM ?

| Interface | Rôle | Mécanisme |
|---|---|---|
| `eth0` (user0) | SSH depuis l'hôte | SLIRP + `hostfwd` |
| `vmnet` | Trafic C2 entre VMs | Socket QEMU (L2 direct) |

Séparer les deux trafics évite que le trafic C2 transite par la pile réseau de l'hôte. Le réseau socket QEMU crée un lien L2 direct entre les deux VMs, invisible depuis l'hôte — ce qui simule mieux un réseau local isolé.

**Pourquoi des MACs explicites sur `eth0` ?**

cloud-init écrit les fichiers de configuration réseau pendant le stage `init-local`, avant que `systemd-networkd` démarre. Ce mécanisme ne fonctionne que pour les interfaces matchées par MAC dans `network-config` : les matchs par nom (`eth*`) sont traités trop tard. Un MAC fixe dans la config QEMU, référencé dans `network-config`, garantit que systemd-networkd reconnaît l'interface au bon moment.
