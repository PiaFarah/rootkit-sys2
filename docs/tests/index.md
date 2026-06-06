# Tests

La suite de tests automatisés est dans `tests/run_tests.py`. Elle tourne sur la **machine hôte**, sans VM, sans charger de module kernel.

---

## Lancer les tests

```bash
python3 tests/run_tests.py
```

Sortie attendue (tout passe) :

```
test_c2_builds ......................................... ok
test_c2_requires_port_and_password ..................... ok
test_c2_sends_auth_hash ................................ ok
test_rootkit_makefile_compile_feature .................. ok
test_wlkom_connection_source ........................... ok
test_persistence_installer_source ...................... ok
test_c2_fnv1a_source ................................... ok
test_wlkom_password_auth_source ........................ ok
test_wlkom_exec_source ................................. ok
test_c2_exec_protocol .................................. ok
----------------------------------------------------------------------
Ran 10 tests in X.XXXs

OK
```

---

## Pourquoi Python ?

Python 3 est disponible sur la machine hôte (Arch Linux) sans installation supplémentaire. La bibliothèque standard permet de :

- Lancer `make` et vérifier le binaire produit (`subprocess`)
- Ouvrir un port local, lancer le C2, et lire sa première trame TCP (`socket`, `subprocess`)
- Inspecter le code source pour vérifier la présence de patterns importants (`open`, `re`)

Aucune dépendance externe n'est nécessaire.

---

## Ce que les tests couvrent

### Build et structure

| Test | Vérifie |
|---|---|
| `test_c2_builds` | `make` dans `attacking_program/` produit le binaire `c2` |
| `test_c2_requires_port_and_password` | `./c2` sans arguments affiche l'usage et sort avec code ≠ 0 |
| `test_rootkit_makefile_compile_feature` | `rootkit/Makefile` déclare `wlkom.o` comme LKM et délègue au build system kernel |

### Analyse statique du code source

| Test | Vérifie dans le source |
|---|---|
| `test_wlkom_connection_source` | Présence de kthread, socket TCP kernel, `connect`, `recv`, `shutdown`, retry |
| `test_persistence_installer_source` | `install_persistence.sh` crée un service systemd |
| `test_c2_fnv1a_source` | Constantes FNV-1a correctes (`2166136261`, `16777619`) |
| `test_wlkom_password_auth_source` | Validation de la trame `AUTH` côté module |
| `test_wlkom_exec_source` | Présence de `call_usermodehelper` et de la logique de capture |

### Tests réseau réels (sans VM)

| Test | Ce qu'il fait |
|---|---|
| `test_c2_sends_auth_hash` | Lance le C2, se connecte dessus en TCP local, vérifie que la première trame reçue est `AUTH <hash_correct>\n` |
| `test_c2_exec_protocol` | Simule un rootkit : se connecte au C2, envoie une fausse réponse, vérifie que le C2 la gère |

---

## Ce que les tests ne couvrent pas

Les tests locaux sont un filet de sécurité rapide, pas une suite d'intégration complète. Plusieurs choses ne peuvent être testées que dans la VM :

| Fonctionnalité | Pourquoi non testée localement |
|---|---|
| Compilation de `wlkom.ko` | Nécessite les headers du kernel de la VM Victime |
| Chargement du module (`insmod`) | Nécessite le kernel de la VM Victime et les droits root |
| Connexion réelle TCP inter-VM | Nécessite les deux VMs en cours d'exécution |
| Retry automatique après déconnexion | Test manuel entre les deux VMs |
| Persistance après reboot | Test manuel : reboot + vérification SSH |

Ces tests d'intégration sont décrits dans les guides respectifs du [Guide d'installation](../guide/index.md).

---

## Philosophie des tests

Les tests locaux valident ce qui peut l'être sans infrastructure — la compilation, la structure du code, la logique du protocole. Ils servent de vérification rapide avant de recompiler et recharger le module dans la VM. Ils ne remplacent pas les tests manuels dans QEMU.
