# Décisions de conception

Cette section justifie les choix techniques importants du projet. Chaque décision est présentée avec le contexte, les alternatives considérées, et la raison du choix retenu.

---

## Résumé des choix

| Sujet | Choix retenu | Raison principale |
|---|---|---|
| Outil de documentation | MkDocs + Material | Python natif, Markdown pur, simplicité |
| VM Victime | Debian 12 / kernel 6.1 LTS | Stabilité du kernel entre les sessions |
| VM Attaquante | Arch Linux | Même env que l'hôte, outils récents |
| Hyperviseur | QEMU/KVM direct | Imposé par le sujet, sans couche libvirt |
| Connexion kernel | `sock_create` + kthread | Tout dans le kernel, pas de dépendance userland |
| Persistance | Service systemd | Contrôle de l'ordre de démarrage (réseau d'abord) |
| Exécution de commandes | `call_usermodehelper` + fichiers tmp | Capture stdout/stderr/exit séparés |
| Hash d'authentification | FNV-1a 32-bit | Simple, reproductible, pas de dépendance externe |
| Passage du hash | `module_param` | Le secret n'est jamais dans le binaire |
| Tests | Python 3 stdlib | Disponible partout, tests réseau simples |

---

## Pages de cette section

- [Outil de documentation](tooling.md) — Pourquoi MkDocs + Material et pas Doxygen, Sphinx ou Docusaurus
- [Environnement et VMs](environment.md) — Pourquoi Debian 12, Arch Linux, QEMU/KVM, ce kernel
- [Connexion TCP](connection.md) — Pourquoi kernel sockets + kthread
- [Persistance](persistence.md) — Pourquoi systemd et pas cron ou `/etc/modules`
- [Exécution de commandes](exec.md) — Pourquoi `call_usermodehelper` + fichiers temporaires
- [Authentification](password.md) — Pourquoi FNV-1a et `module_param`
