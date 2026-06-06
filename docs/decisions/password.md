# Authentification

## Le problème

Le module kernel doit vérifier que le programme qui se connecte est bien notre C2 et non un tiers qui essaierait de prendre le contrôle du rootkit. Il faut un mécanisme d'authentification qui :

- Ne stocke pas le mot de passe en clair dans le binaire
- Ne compare pas le mot de passe en clair sur le réseau
- Reste simple à implémenter en C sans bibliothèque externe, côté kernel

---

## Choix du hash : FNV-1a 32-bit

FNV-1a (Fowler–Noll–Vo) est une fonction de hash non cryptographique. Elle est choisie pour sa **simplicité d'implémentation** : une dizaine de lignes en C, zéro dépendance externe, résultat identique quel que soit le langage ou la plateforme.

```c
uint32_t fnv1a_hash(const char *str) {
    uint32_t h = 2166136261U;
    while (*str)
        h = (h ^ (uint8_t)*str++) * 16777619U;
    return h;
}
```

La même implémentation existe en Python (dans `install_persistence.sh`) et en C (dans `c2.c`). Les trois produisent exactement le même résultat pour la même entrée.

**FNV-1a n'est pas cryptographiquement sûr.** Il est trivial à inverser si on connaît une liste de mots de passe courants (attaque par dictionnaire). Pour un rootkit de production, on utiliserait SHA-256 ou Argon2. Ici, c'est un choix délibéré pour la lisibilité du code et l'absence de dépendances — c'est un projet pédagogique.

---

## Passage du hash via `module_param`

Le module ne connaît que le hash, jamais le mot de passe brut. Le hash est fourni à `insmod` ou stocké dans `/etc/modprobe.d/wlkom.conf`.

**Pourquoi `module_param` et pas un hardcode dans le `.ko` ?**

Un mot de passe ou hash hardcodé dans le binaire est extractible par `strings wlkom.ko`. `module_param` fait que le `.ko` compilé ne contient aucune valeur sensible — le secret est fourni à l'exécution.

**Pourquoi `0400` comme permissions sysfs ?**

Sans permissions explicites, le paramètre serait lisible dans `/sys/module/wlkom/parameters/password_hash` par tout utilisateur. `0400` le rend accessible uniquement par root.

---

## Protocole d'authentification

À chaque connexion, le C2 envoie en premier :

```
AUTH afd071e5\n
```

Le rootkit compare `afd071e5` avec son `password_hash`. Si ça correspond, la session continue. Sinon, le socket est fermé et le rootkit retente une connexion après 5 secondes.

**Pourquoi c'est le C2 qui s'authentifie et pas le rootkit ?**

Dans notre modèle de menace, le risque est qu'un tiers prenne le contrôle du rootkit déjà installé sur la victime. L'attaquant doit prouver qu'il connaît le secret pour avoir accès au shell. Le rootkit n'a pas besoin de prouver son identité au C2 — le C2 sait qu'il a lancé le rootkit sur cette machine.

**Pourquoi pas d'authentification mutuelle ?**

Le rootkit ne peut pas authentifier le C2 de façon sûre sans un mécanisme de challenge-response asymétrique (clés publiques/privées). C'est possible mais hors périmètre ici.

---

## Alternatives envisagées

| Alternative | Pourquoi non retenue |
|---|---|
| Mot de passe en clair sur le réseau | Exposé à la capture réseau (même locale) |
| Mot de passe hardcodé dans le `.ko` | Extractible par `strings` |
| SHA-256 | Pas disponible nativement dans le kernel sans `crypto/sha2.h` — ajoute une dépendance |
| Pas d'authentification | N'importe qui pourrait se connecter au rootkit |
