# Protocole réseau

Le protocole de communication entre `wlkom.ko` (kernel, VM Victime) et `c2` (userland, VM Attaquante) est un protocole applicatif minimaliste sur TCP. Le contenu logique reste textuel (`AUTH`, commandes, sorties), mais les octets envoyés sur le réseau sont chiffrés par un flux XOR symétrique dérivé du hash FNV-1a du mot de passe.

---

## Phase 1 — Connexion

Le rootkit initie la connexion (reverse connection) : c'est la victime qui appelle l'attaquant. Le C2 écoute sur `0.0.0.0:<port>` et `accept()` la connexion entrante.

---

## Phase 2 — Authentification

Après connexion, le C2 affiche `WLKOM password:` et attend que l'opérateur saisisse le mot de passe (écho désactivé). Il calcule le hash FNV-1a et envoie :

```
AUTH afd071e5\n
```

Format logique avant chiffrement : `AUTH ` (avec espace) suivi du hash FNV-1a 32-bit en hexadécimal minuscule sur 8 caractères, suivi de `\n`. La trame fait toujours 14 octets avant chiffrement.

Le rootkit lit exactement ces 14 octets chiffrés, les déchiffre, puis vérifie que :
1. La ligne commence par `AUTH `
2. Les caractères suivants correspondent à `password_hash` (passé en `module_param`)

Lire une taille fixe pendant l'authentification évite un blocage quand le mot de passe est faux : avec une mauvaise clé, le `\n` chiffré ne se déchiffre pas forcément en saut de ligne. Si la vérification échoue, le rootkit ferme la connexion immédiatement et retente une nouvelle connexion. Le C2 affiche `[-] Authentication failed: invalid password or connection lost` et remet le socket en attente.

!!! note "C'est le C2 qui s'authentifie, pas le rootkit"
    Le rootkit ne prouve pas son identité au C2. C'est l'attaquant qui doit prouver qu'il connaît le bon mot de passe. Le rootkit est passif : il vérifie l'authentification de quiconque se connecte à lui.

---

## Phase 3 — Commandes

Après authentification réussie, le C2 envoie des commandes sous forme logique textuelle, puis chiffrées sur le réseau :

```
uname -a\n
```

Le rootkit reçoit la ligne avec `recv_line`, l'exécute via `call_usermodehelper` et renvoie la réponse dans le format suivant :

```
[Exit Status: 0]\n
--- STDOUT ---\n
Linux epita-victim 6.1.0-35-amd64 #1 SMP x86_64 GNU/Linux\n
--- STDERR ---\n
\n
--- End of Output ---\n
\n
```

### Détail du format de réponse

| Champ | Description |
|---|---|
| `[Exit Status: N]` | Code de retour UNIX du processus (0 = succès) |
| `--- STDOUT ---` | Marqueur début de stdout |
| *(contenu stdout)* | Sortie standard de la commande, peut être multiligne |
| `--- STDERR ---` | Marqueur début de stderr |
| *(contenu stderr)* | Sortie d'erreur, peut être vide |
| `--- End of Output ---` | Marqueur de fin de réponse |
| *(ligne vide)* | Ligne vide finale obligatoire |

Le C2 lit ligne par ligne jusqu'à trouver `--- End of Output ---\n` suivi d'une ligne vide pour détecter la fin de la réponse.

---

## Phase 4 — Transferts de fichiers

### Téléchargement (`DOWNLOAD`)

Le C2 envoie une commande de la forme :

```
DOWNLOAD /home/epita/test.txt
```

Le rootkit répond ensuite avec une ligne de taille :

```
SIZE 1234
```

Puis il envoie les octets du fichier sur la même connexion chiffrée, et termine par un marqueur de fin :

```
--- End of Output ---

```

Le C2 lit la taille, récupère les octets du fichier puis enregistre le contenu localement.

### Téléversement (`UPLOAD`)

Le C2 envoie une commande de la forme :

```
UPLOAD /tmp/remote.txt 1234
```

Le rootkit répond `UPLOAD_READY` pour signaler qu'il est prêt à recevoir les octets. Le C2 envoie ensuite le contenu du fichier via le même flux chiffré, puis le rootkit écrit les données sur le chemin demandé et renvoie `UPLOAD_OK`.

Le format binaire est donc traité par le même mécanisme de chiffrement que les commandes textuelles : aucun octet n'est envoyé en clair sur le réseau.

---

## Déconnexion

Si le C2 est coupé (Ctrl+C, crash), `kernel_recvmsg` dans le kthread retourne 0 (connexion fermée proprement) ou une valeur négative (erreur réseau). Le kthread sort de la boucle de commandes, ferme le socket, et recommence la phase de connexion après 5 secondes.
