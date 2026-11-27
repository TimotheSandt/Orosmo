---
title: Fonctionnalités
description: "Fonctions livrées et testées du moteur de jeu Orosmo (C++23, Vulkan, OpenGL) : rendu 3D et 2D, glTF, animation, interface .cui, outils. Puis ce qui est prévu."
sidebar:
  order: 2
---

Cette page liste ce que le moteur fait à la version 0.7.0. Une fonction n'y figure que si elle est « Fait » dans la
checklist du dépôt
([`docs/02-CHECKLIST_FONCTIONNALITES.md`](https://github.com/TimotheSandt/Orosmo/blob/dev/docs/02-CHECKLIST_FONCTIONNALITES.md)),
c'est-à-dire avec un test automatisé ou une commande de démo et son résultat
([`docs/STATUS.md`](https://github.com/TimotheSandt/Orosmo/blob/dev/docs/STATUS.md)). Les fonctions partielles et
absentes sont sur la page [État du moteur](/projet/etat/).

## Rendu

Le rendu passe par une couche d'abstraction graphique, le RHI, avec deux backends.

- Backend OpenGL 4.3 et backend Vulkan 1.3. Vulkan est vérifié sous Linux avec un GPU seulement. Voir
  [Choisir le backend graphique](/guides/application/peripherique/).
- Pipelines graphiques et de calcul. Calcul (compute), rendu vers plusieurs cibles et dispatch indirect : vérifiés
  avec OpenGL. Voir [RHI](/interne/rhi/).
- Capacités optionnelles (ray tracing, bindless, mesh shaders…) lues dans `Capabilities` et à `false` tant qu'elles ne
  sont pas implémentées.
- Graphe de passes et plugins de rendu : [Écrire un plugin de rendu](/guides/avance/plugin-de-rendu/).
- Caméra et données par image : [Dessiner une scène](/guides/rendu-3d/scene/).
- Éclairage forward+ en grappes, mobilité et mode d'ombre des lumières : [Lumières et forward+](/guides/rendu-3d/lumieres/).
- Ombres en cascade : [Ajouter des ombres](/guides/rendu-3d/ombres/).
- Matériaux PBR : [Matériaux PBR](/guides/rendu-3d/materiaux-pbr/).
- Éclairage par environnement (IBL) et sondes de lumière : [Éclairage d'environnement et sondes](/guides/rendu-3d/environnement-ibl/).
- HDR, tonemapping et post-traitements courants (bloom, exposition automatique, étalonnage par LUT, vignettage) : [HDR et post-traitement](/guides/rendu-3d/post-traitement/).
- Instancing et culling sur le CPU : [Beaucoup d'objets : instancing et culling](/guides/rendu-3d/instancing-culling/).
- Rendu piloté par le GPU (culling et dessin indirect) : [Culling sur le GPU](/guides/rendu-3d/rendu-gpu/).
- Tri des objets transparents et ombres des transparents : [Objets transparents](/guides/rendu-3d/transparence/).
- Dessin de debug (lignes, boîtes, sphères, repères) : [Dessin et vues de debug](/guides/rendu-3d/debug-visuel/).

## 2D

- Sprites dessinés par lots et caméra 2D : [Sprites, formes et caméra 2D](/guides/rendu-2d/sprites-formes/).
- Formes, lignes et texte : [Texte et polices](/guides/rendu-2d/texte/).
- Mélange 2D et 3D dans une même image, plusieurs vues (caméras et zones d'affichage) : [Mélanger 2D et 3D](/guides/rendu-2d/melange-2d-3d/).

Exemple complet : [Application 2D complète](/guides/rendu-2d/application-2d/).

## Contenus et animation

- Import et export glTF 2.0 : [Importer un modèle glTF](/guides/contenus/gltf/).
- Textures PNG et JPG avec mips, en sRGB ou linéaire, et textures compressées KTX2 : [Textures](/guides/contenus/textures/).
- Cuisson des maillages (format `.omesh`) : [Cuire les maillages](/guides/contenus/cuisson/).
- Squelettes et skinning sur le GPU, clips, mélange et machine à états : [Animer un personnage](/guides/animation/squelettes/).
- Animation de propriétés par courbes : [Animer des propriétés](/guides/animation/proprietes/).
- Foules instanciées : [Foules instanciées](/guides/animation/foules/).

## Interface et langage .cui

- Widgets de base : [Catalogue des widgets](/guides/interface/widgets/).
- Mise en page : boîtes verticales et horizontales, grille, superposition, marges, tailles fixes, flexibles et
  minimales. Voir [Construire une interface en C++](/guides/interface/premiere-interface/).
- Langage `.cui` compilé en C++ ; les événements appellent des fonctions C++ : [Écrire une vue en .cui](/guides/interface/langage-cui/).
- Grammaire du langage spécifiée et testée : [Langage .cui : grammaire et composants](/reference/langage-cui/).
- Panneau de réglages généré depuis une structure réfléchie : [Configuration, réflexion, sérialisation](/guides/noyau/configuration/).
- Vues `.cui` rechargées à chaud en développement : [Vues rechargées à chaud](/guides/interface/rechargement-vues/).
- Navigation au clavier et à la manette, ordre de tabulation, anneau de focus : [Naviguer au clavier et à la manette](/guides/interface/navigation/).
- Outils de debug : console, inspecteurs des widgets et des entités, profileur : [Outils de debug](/guides/interface/outils-debug/).

## Scène, simulation et logique de jeu

- Entités, composants enregistrés par nom, hiérarchie et transformations en double : [Entités, composants et transformations](/guides/scene/entites-composants/).
- Sauvegarde et chargement de scène en JSON ou MessagePack, avec versions : [Sauvegarder et charger une scène](/guides/scene/sauvegarde/).
- Simulation à pas fixe, rejouable avec la même graine, thread de simulation optionnel : [Simulation à pas fixe](/guides/scene/simulation/).
- Logique de jeu rechargée sans redémarrage ni perte d'état, migration des composants, paramètres JSON, version finale
  statique : [Logique de jeu rechargeable](/guides/scene/logique-rechargeable/).
- Façade d'application (fenêtre, périphérique, rendu, interface, boucle) : [La façade app::Application](/guides/application/facade/).

## Noyau

- Journal avec pile d'appels : [Erreurs et journal](/guides/noyau/erreurs-journal/).
- Tâches en parallèle et allocateurs (arène, pool d'objets) : [Tâches en parallèle](/guides/noyau/taches/).
- Configuration, réflexion des champs et sérialisation JSON ou binaire : [Configuration, réflexion, sérialisation](/guides/noyau/configuration/).
- Temps à pas fixe, bus d'événements et mathématiques (positions mondiales en `double`) : [Temps, pas fixe et événements](/guides/noyau/temps-evenements/).
- Système de fichiers virtuel et gestionnaire de ressources : [Fichiers, ressources, rechargement](/guides/noyau/fichiers-ressources/).

## Outils et SDK

Ces éléments sont « Partiel » dans la checklist : ils sont vérifiés sous Linux seulement.

- Outil `orosmo` : `install`, `init`, `build`, `run`, `doctor`, `update`, `migrate`. Voir [Commande orosmo](/reference/commande-orosmo/).
- Moteur précompilé avec ses dépendances, installé une fois par machine : [Installer Orosmo](/demarrer/installation/).
- Compilation d'un projet avec Make ou CMake : [Compiler avec Make ou CMake](/reference/construction/).

## Plateformes

Aucune plateforme n'est encore « Fait » dans la checklist. Linux est la seule vérifiée. Windows et macOS sont compilés
par la CI, mais le code récent n'y a pas été observé. Le mode sans fenêtre (`--headless`) est vérifié sous Linux :
[Cadence d'images, mode sans fenêtre, captures](/guides/application/cadence-captures/).

## Prévu

Ces fonctions n'existent pas encore. Le plan est dans
[`docs/05-PLAN_PAR_JALONS.md`](https://github.com/TimotheSandt/Orosmo/blob/dev/docs/05-PLAN_PAR_JALONS.md), les limites
connues dans [`docs/GAPS.md`](https://github.com/TimotheSandt/Orosmo/blob/dev/docs/GAPS.md).

- Validation sur macOS et backend Metal (jalon M9). Metal est reconnu par le RHI mais refusé : il n'est pas disponible.
- Extensions de rendu : voxels et ray tracing, ajoutés comme plugins du renderer (jalon M10).
- Modules et addons, puis registre d'addons (jalons M11 et M12).
- Effets livrés comme modules : brouillard, particules, occlusion ambiante, eau, réfraction (jalon M13).
- Manettes au-delà de la navigation de l'interface (vibrations, remappage), après le jalon M10.
