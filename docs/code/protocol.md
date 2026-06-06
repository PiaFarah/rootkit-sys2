# Protocole réseau

Le protocole de communication entre `wlkom.ko` (kernel, VM Victime) et `c2` (userland, VM Attaquante) est un protocole texte minimaliste sur TCP. Les messages sont délimités par des sauts de ligne (`\n`), sans encodage, sans chiffrement.

---

## Vue d'ensemble

```
VM Victime (wlkom.ko)               VM Attaquante (c2)
────────────────────                ─────────────────
                     ◄── connexion TCP ──
                     ── AUTH <hash>\n ──►
            vérifie hash
            si OK: continue
            si KO: ferme connexion
                     ◄── <commande>\n ──
            exécute commande
            capture stdout/stderr/exit
                     ─── [Exit Status: N]\n ──►
                     ─── --- STDOUT ---\n ──►
                     ─── <stdout>\n ──►
                     ─── --- STDERR ---\n ──►
                     ─── <stderr>\n ──►
                     ─── --- End of Output ---\n\n ──►
                     ◄── <commande suivante>\n ──
                     ...
```

---

## Phase 1 — Connexion

Le rootkit initie la connexion (reverse connection) : c'est la victime qui appelle l'attaquant. Le C2 écoute sur `0.0.0.0:<port>` et `accept()` la connexion entrante.

---

## Phase 2 — Authentification

Immédiatement après connexion, **le C2 envoie** le hash d'authentification :

```
AUTH afd071e5\n
```

Format : `AUTH ` (avec espace) suivi du hash FNV-1a 32-bit en hexadécimal minuscule sur 8 caractères, suivi de `\n`.

Le rootkit lit cette ligne avec `recv_line`, vérifie que :
1. La ligne commence par `AUTH `
2. Les 8 caractères suivants correspondent à `password_hash` (passé en `module_param`)

Si la vérification échoue, le rootkit ferme la connexion immédiatement et retente une nouvelle connexion. Le C2 affiche `[-] Authentication failed` et remet le socket en attente.

!!! note "C'est le C2 qui s'authentifie, pas le rootkit"
    Le rootkit ne prouve pas son identité au C2. C'est l'attaquant qui doit prouver qu'il connaît le bon mot de passe. Le rootkit est passif : il vérifie l'authentification de quiconque se connecte à lui.

---

## Phase 3 — Commandes

Après authentification réussie, le C2 envoie des commandes en texte libre :

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

## Déconnexion

Si le C2 est coupé (Ctrl+C, crash), `kernel_recvmsg` dans le kthread retourne 0 (connexion fermée proprement) ou une valeur négative (erreur réseau). Le kthread sort de la boucle de commandes, ferme le socket, et recommence la phase de connexion après 5 secondes.

---

## Choix de conception

**Pourquoi du texte plutôt qu'un protocole binaire ?**

Un protocole texte est lisible avec `nc` ou `telnet` — utile pour le debug. Les messages sont courts, les performances ne sont pas un critère ici.

**Pourquoi pas de préfixe `CMD` pour les commandes ?**

Le C2 envoie la commande telle quelle, sans en-tête. Le rootkit lit tout ce qui arrive après l'authentification comme une commande. Cela simplifie le parsing côté kernel : pas de parsing de token, pas de switch sur le type de message.

**Pourquoi `--- End of Output ---` comme marqueur de fin ?**

Un marqueur fixe est plus simple qu'un protocole avec longueur en en-tête. La probabilité qu'une sortie de commande contienne exactement `--- End of Output ---` suivi d'une ligne vide est négligeable. En cas de collision, la commande suivante serait mal parsée — c'est un risque accepté dans ce contexte pédagogique.

**Pourquoi pas de chiffrement ?**

Le trafic est en clair sur le réseau `vmnet` isolé entre les deux VMs. Dans un contexte réel, ce serait une faille critique. Pour une implémentation chiffrée, voir la feature Crypto (optionnelle, non implémentée).
