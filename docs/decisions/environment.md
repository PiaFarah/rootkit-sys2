# Environnement

## Pourquoi QEMU/KVM ?

Le sujet l'impose. QEMU/KVM est la référence pour la virtualisation sur Linux : performances natives grâce à KVM, large adoption, documentation abondante. Nous l'utilisons en ligne de commande directe.

---

## Pourquoi Debian 12 pour la victime ?

La contrainte principale est la **stabilité du kernel** : `wlkom.ko` embarque un `vermagic`, une empreinte de la version exacte du kernel contre lequel il a été compilé. Si le kernel change, `modprobe` refuse de charger le module.

| Distro | Kernel | Risque |
|---|---|---|
| Arch Linux | Rolling | Mis à jour à chaque `pacman -Syu` → recompilation obligatoire |
| Ubuntu LTS | Mis à jour fréquemment | Peut changer sans intervention |
| **Debian 12** | **6.1, figé** | **Ne change pas sans intervention explicite** |

Debian 12 était la version stable la plus récente au moment du projet. `vm.sh` y installe le kernel standard (qui autorise les modules non signés) en remplacement du kernel cloud au premier boot. Le sujet impose `kernel >= 5.0.0` ; 6.1 satisfait cette contrainte et les APIs utilisées (kthread, socket TCP) sont stables depuis le kernel 5.x. Nous n'avons pas rencontré de blocage qui aurait justifié de descendre vers une version plus ancienne.

---

## Pourquoi Arch Linux pour l'attaquante ?

Le C2 est un programme userland en C. Il n'a aucune contrainte kernel, n'importe quelle distro Linux suffit. Arch Linux est choisi pour deux raisons :

1. C'est l'environnement de la machine hôte
2. Les outils de développement (`gcc`, `make`, etc.) sont récents et facilement installables

La VM Attaquante ne nécessite pas de stabilité kernel : on n'y compile pas de module.

---

## Pourquoi cloud-init plutôt qu'une installation manuelle ?

cloud-init configure automatiquement une VM au premier boot à partir de fichiers texte (`user-data`, `network-config`). Cela rend `vm.sh` entièrement automatique : une seule commande, aucune interaction.

L'alternative, un ISO d'installation standard, demanderait de répondre manuellement à des dizaines de questions (langue, partition, user, password...) pour chaque nouvelle VM.

---

## Pourquoi un dossier partagé VirtFS (9p) ?

`vmshare/` est un dossier de l'hôte monté directement dans les VMs via QEMU. Les fichiers sont visibles des deux côtés sans copie ni SSH. On édite sur l'hôte, on compile sur la VM Victime.

---

## Pourquoi deux interfaces réseau par VM ?

Deux besoins distincts, deux interfaces :

- `eth0` : connexion SSH depuis l'hôte pour travailler dans la VM
- `vmnet` : câble virtuel direct entre la VM Attaquante et la VM Victime, pour le trafic C2, l'hôte ne le voit pas
