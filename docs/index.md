# WLKOM : Wild Linux Kernel Object Module

WLKOM est un rootkit Linux pédagogique développé dans le cadre du projet SYS2 à l'EPITA par The Non‑Malicious Team (NMT), une équipe de 4 étudiants. Le module s'installe sur une machine victime, établit une connexion TCP persistante vers un programme attaquant distant, s'authentifie par hash de mot de passe, et exécute les commandes envoyées par l'attaquant.

Cette documentation est construite pour que n'importe qui puisse tester et comprendre le projet, étape par étape. La machine hôte doit tourner sur Arch Linux avec la virtualisation KVM disponible.

---

## Architecture

```
┌─────────────────────┐          réseau vmnet           ┌──────────────────────┐
│    VM Victime       │      192.168.100.x/24           │    VM Attaquante     │
│    Debian 12        │ ◄──────────────────────────────►│    Arch Linux        │
│                     │    TCP 4444 (reverse conn.)     │                      │
│  wlkom.ko (kernel)  │                                 │  c2 (userland)       │
│  - kthread          │                                 │  - écoute TCP        │
│  - socket kernel    │                                 │  - envoie commandes  │
│  - auth FNV-1a      │                                 │  - affiche résultats │
│  - exec commandes   │                                 │                      │
└──────────┬──────────┘                                 └──────────┬───────────┘
           │ VirtFS / 9p                                           │ VirtFS / 9p
           │ /mnt/vmshare/                                         │ /mnt/vmshare/
           ▼                                                       ▼
┌──────────────────────────────────────────────────────────────────────────────┐
│                           Machine hôte — Arch Linux                          │
│                                  ./vmshare/                                  │
└──────────────────────────────────────────────────────────────────────────────┘
```

Le rootkit initie la connexion (reverse connection) : c'est la victime qui appelle l'attaquant, pas l'inverse. Le C2 écoute, attend qu'un rootkit se connecte, demande le mot de passe, et après authentification réussie propose un shell interactif.

---

## Explorer

<div class="grid cards" markdown>

-   :material-rocket-launch:{ .lg .middle } **Guide**

    ---

    Documentation pas à pas pour installer, configurer et utiliser le projet de zéro.

    [:octicons-arrow-right-24: Accéder au guide](guide/index.md)

-   :material-lightbulb:{ .lg .middle } **Décisions de conception**

    ---

    Chaque décision importante est justifiée avec les alternatives envisagées.

    [:octicons-arrow-right-24: Voir les décisions](decisions/index.md)

-   :material-code-braces:{ .lg .middle } **Implémentation**

    ---

    APIs kernel, flux d'exécution complet et protocole réseau expliqués composant par composant.

    [:octicons-arrow-right-24: Voir l'implémentation](code/index.md)

-   :material-book-alphabet:{ .lg .middle } **Glossaire**

    ---

    Définitions des termes techniques et acronymes utilisés dans le projet.

    [:octicons-arrow-right-24: Voir le glossaire](glossaire.md)

</div>

---

## Pourquoi MkDocs + Material ?

La documentation était initialement dans un README surchargé : présentation, tutoriel, architecture et justifications mélangés, sans navigation. Le sujet impose de documenter l'installation complète et les justifications de choix, un README ne suffit pas.

L'objectif était un outil qui laisse écrire librement en Markdown (documentation narrative, pas une génération depuis les commentaires), sans introduire de dépendance hors de l'écosystème du projet. Doxygen génère depuis les commentaires uniquement. Sphinx avec Breathe représente un pipeline de trois outils à configurer ensemble. Docusaurus nécessite Node.js, absent du projet (C + Python + Shell).

MkDocs + Material est Python pur, Markdown natif, déploiement GitHub Pages en une commande, configuration en un seul fichier.
