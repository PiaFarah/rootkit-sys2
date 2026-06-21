# Authentification

Le rootkit se connecte au C2. Sans authentification, n'importe qui qui écouterait sur le bon port avant l'opérateur légitime pourrait prendre le contrôle du rootkit. Le C2 doit donc prouver qu'il connaît le secret avant d'obtenir l'accès au shell.

## Pourquoi FNV-1a 32-bit ?

FNV-1a est choisi pour sa simplicité : une dizaine de lignes, zéro dépendance, et le même résultat en C et en Python. C'est important parce que le hash doit être produit à trois endroits : dans `c2.c` (envoyé au rootkit), dans `install_persistence.sh` (stocké dans `modprobe.d`), et potentiellement vérifié manuellement. Les trois doivent produire exactement le même résultat pour la même entrée.

FNV-1a n'est pas cryptographiquement sûr (vulnérable aux attaques par dictionnaire). SHA-256 serait plus robuste mais nécessite `crypto/sha2.h` côté kernel, une dépendance non justifiée pour un projet pédagogique.

## Pourquoi `module_param` avec permissions `0400` ?

Le hash est lu par `modprobe` depuis `/etc/modprobe.d/wlkom.conf` et passé au module au chargement. Il n'est jamais codé en dur dans le `.ko`, ce qui empêche de l'extraire avec `strings wlkom.ko`.

La permission `0400` sur le paramètre empêche la lecture depuis `/sys/module/wlkom/parameters/password_hash` par un utilisateur non-root.

Pour le protocole d'échange au moment de la connexion : [Protocole réseau](../code/protocol.md)
