# Choix de l'outil de documentation

## Contexte et besoins

### Le problème de départ

La documentation était dans un README surchargé à la racine, mêlant présentation, tutoriel, architecture et justifications. Aucune navigation, des répétitions, lecture difficile pour quelqu'un qui cherche une section précise. Le sujet impose de documenter l'installation complète, les justifications de choix, et insiste fortement sur l'importance de la documentation.

### Pourquoi une documentation narrative

Le sujet n'impose pas d'expliquer le code bloc par bloc. C'est un choix volontaire.

Le projet est pédagogique : comprendre le code signifie être capable d'expliquer pourquoi il est écrit ainsi : les choix d'API, les conventions, les pièges évités. Rédiger la documentation est un moyen de vérifier cette compréhension. C'est aussi utile pour un lecteur qui n'a jamais touché au projet et qui lirait le code sans contexte.

Cette approche nécessite un outil qui laisse écrire librement en Markdown, pas un outil qui génère de la documentation automatiquement depuis les commentaires.

---

## Comparaison des outils

| Critère | README simple | Doxygen + Awesome | Sphinx + Breathe | Docusaurus | MkDocs + Material *(choisi)* |
|---|---|---|---|---|---|
| Dépendances nouvelles | :material-check:{ .icon-check } | :material-check:{ .icon-check } | :material-check:{ .icon-check } | :material-close:{ .icon-close } Node.js | :material-check:{ .icon-check } |
| Markdown natif | :material-check:{ .icon-check } | :material-close:{ .icon-close } | :material-alert:{ .icon-alert } | :material-check:{ .icon-check } | :material-check:{ .icon-check } |
| Doc narrative | :material-alert:{ .icon-alert } | :material-close:{ .icon-close } | :material-check:{ .icon-check } | :material-check:{ .icon-check } | :material-check:{ .icon-check } |
| Navigation multi-pages | :material-close:{ .icon-close } | :material-check:{ .icon-check } | :material-check:{ .icon-check } | :material-check:{ .icon-check } | :material-check:{ .icon-check } |
| Qualité visuelle | :material-close:{ .icon-close } | :material-alert:{ .icon-alert } | :material-alert:{ .icon-alert } | :material-check:{ .icon-check } | :material-check:{ .icon-check } |
| Recherche | :material-close:{ .icon-close } | :material-check:{ .icon-check } | :material-check:{ .icon-check } | :material-check:{ .icon-check } | :material-check:{ .icon-check } |
| Complexité config | :material-check:{ .icon-check } | :material-alert:{ .icon-alert } | :material-close:{ .icon-close } | :material-alert:{ .icon-alert } | :material-check:{ .icon-check } |
| Déploiement | :material-check:{ .icon-check } Natif | :material-close:{ .icon-close } Complexe | :material-alert:{ .icon-alert } Manuel | :material-check:{ .icon-check } Simple | :material-check:{ .icon-check } Simple |

**Doxygen + Awesome** : Awesome est un thème CSS moderne qui améliore le rendu de Doxygen, mais le problème reste fondamental : Doxygen génère depuis les commentaires de code uniquement, pas de documentation narrative.

**Sphinx + Breathe** : Breathe connecte Doxygen à Sphinx pour combiner API C et guides narratifs, mais le pipeline Doxygen → Breathe → Sphinx représente trois outils à configurer et maintenir ensemble.

**Docusaurus** : Générateur de documentation Meta, moderne et complet, mais éliminé car il requiert Node.js et npm, absents du projet (C + Python + Shell).

**MkDocs + Material** : Python pur, Markdown natif, déploiement GitHub Pages intégré, configuration en un fichier. Material theme ajoute onglets, sidebar, thème sombre, copie de code, Mermaid et annotations de code inline.

