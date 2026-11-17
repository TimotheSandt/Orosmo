# 01 · Vision et périmètre

## Objectif

Un moteur 3D en C++23 qui sert de base à des projets de génération procédurale : terrains, planètes entières, puis simulation d'un monde vivant. Il doit être :

- **Compréhensible** : modules petits, interfaces publiques claires, documentation par module.
- **Portable** : le code du moteur ne connaît pas l'API graphique ; changer de backend ne touche pas le reste.
- **Utile tôt** : chaque jalon livre une démo qui tourne, à commencer par un générateur de terrain avec visualiseur.
- **Déterministe** : même graine, même résultat, sur toutes les plateformes.

## Utilisations visées (elles dictent les priorités)

1. **Générateur de terrain et de planètes** avec visualiseur interactif, export et mode sans fenêtre (ligne de commande).
2. **Monde vivant** (plus tard) : écosystème, civilisations, agents, simulés sur un terrain généré. Le moteur n'a qu'à fournir une boucle de simulation à pas fixe, des données et un rendu. La simulation elle-même est un autre projet.
3. **Sources de données pour des objets physiques** (plus tard) : cartes de relief, pistes de circuit, globe : le mode sans fenêtre doit pouvoir produire des cartes de hauteur, des images et des maillages imprimables.
4. **Un moteur de jeu complet à terme** : rendu 3D et 2D mélangeables, objets importés (glTF), animations, instancing, lumières et ombres statiques, dynamiques ou en cache, effets atmosphériques, compute, interface en langage maison avec thèmes JSON, logique de jeu rechargeable à chaud. Plus tard : voxels et ray tracing, ajoutés comme plugins sans toucher au cœur.

## Plateformes

| Plateforme | Backend principal | Notes |
| --- | --- | --- |
| Windows | Vulkan | OpenGL 4.3+ en secours |
| Linux | Vulkan | OpenGL 4.3+ en secours |
| macOS | Vulkan via MoltenVK | OpenGL macOS plafonne à 4.1 et n'a ni SSBO ni compute : **le backend OpenGL ne vise pas macOS**. Metal natif : décision au jalon M7, avec mesures. |

Contraintes de base : C++23, GLFW pour la fenêtre et les entrées, GLSL comme langage de shader source, FreeType pour le texte, vcpkg pour les dépendances.

## Hors périmètre (pour l'instant)

- Éditeur de scène complet, réseau, audio (priorité 3, voir checklist). Pas de langage de script embarqué pour l'instant : la logique est en C++ rechargeable (voir `03` §13), un script type Lua restant une option si les temps de compilation deviennent un frein.
- Implémentation des voxels et du ray tracing : seules les extensions de l'architecture sont prévues (`03` §14).
- Physique complète. Seuls le lancer de rayon et la collision avec le terrain sont prévus.
- Mobile, consoles, WebGPU (l'architecture ne doit pas les empêcher, rien de plus).
- Ray tracing matériel.

## Principes directeurs

1. **Le moteur grandit à partir de démos.** Aucune fonctionnalité n'est écrite « au cas où » : elle sert la démo du jalon en cours ou elle attend.
2. **Données d'abord.** Les systèmes lisent et écrivent des données simples (structures, tableaux), pas des hiérarchies d'objets profondes. Cela rend la simulation, le multithreading et la sérialisation faciles.
3. **Une seule façon de faire** par problème (une seule file de tâches, un seul gestionnaire de ressources, un seul logger).
4. **Les frontières sont des interfaces.** Entre le moteur et le GPU : le RHI. Entre le moteur et le système : la couche plateforme. Entre le jeu et le moteur : l'API publique de chaque module.
5. **Le coût est visible.** Profilage CPU et GPU intégré dès le départ.
