---
title: État du moteur
description: Ce qui est livré, partiel ou absent dans la version 0.6.2, par section de la checklist, avec où trouver les preuves.
sidebar:
  order: 3
---

Cette page résume l'état à la version 0.6.2 (M6 livré le 2026-11-24, M14, distribution du SDK le 2026-11-25). La référence est le dépôt :

- [`docs/02-CHECKLIST_FONCTIONNALITES.md`](https://github.com/TimotheSandt/Orosmo/blob/dev/docs/02-CHECKLIST_FONCTIONNALITES.md) : un identifiant par fonction, avec son critère d'acceptation ;
- [`docs/STATUS.md`](https://github.com/TimotheSandt/Orosmo/blob/dev/docs/STATUS.md) : l'état de chaque identifiant et sa preuve (fichier, symbole, test) ;
- [`docs/GAPS.md`](https://github.com/TimotheSandt/Orosmo/blob/dev/docs/GAPS.md) : ce qui manque et les limites connues.

Un identifiant n'est « Fait » que s'il a un test automatisé ou une commande de démo, et le résultat de ce test.

:::note
Lire les identifiants : `REN-…`, `RHI-…`, `PLAT-…`, `ASSET-…`, `ANIM-…`, `UI-…` et leurs semblables sont les lignes de la
checklist (`docs/02`). `Gnnn` est une limite connue dans `docs/GAPS.md`. `Ann` est une décision ou une question dans
`docs/QUESTIONS.md`. `Mn` est un jalon du plan (`docs/05`) : M8 est la scène, la simulation et la logique de jeu
rechargeable ; M9, macOS et Metal ; M11, les modules et addons.
:::

## Décompte par section

167 identifiants. Les états sont Fait, Partiel et Absent, plus trois cas : Archivé (retiré du dépôt principal et conservé
sur une branche), Hors moteur (ne sera pas dans le moteur) et Incompatible (existe mais viole l'architecture).

| Section | Identifiants | Fait | Partiel | Absent | Autres |
|---|---|---|---|---|---|
| A. Noyau (`core`) | 13 | 10 | 3 | 0 | |
| B. Plateforme et entrées | 5 | 0 | 4 | 1 | |
| C. Couche graphique (RHI) | 19 | 7 | 10 | 2 | |
| D. Rendu 3D | 41 | 16 | 11 | 14 | |
| E. Rendu 2D et mélange 2D/3D | 7 | 5 | 0 | 2 | |
| F. Contenu et animation | 12 | 8 | 1 | 3 | |
| G. Terrain et monde | 14 | 0 | 0 | 0 | 9 archivés, 5 hors moteur |
| H. Scène et simulation | 6 | 0 | 0 | 4 | 2 hors moteur |
| I. Interface et langage `.cui` | 13 | 5 | 4 | 4 | |
| J. Logique de jeu rechargeable | 7 | 0 | 0 | 7 | |
| K. Voxels et ray tracing | 5 | 0 | 0 | 4 | 1 incompatible |
| L. Outils, qualité, construction | 11 | 1 | 8 | 0 | 2 archivés |
| M. Modules et addons | 9 | 0 | 0 | 9 | |
| N. Site et déploiement | 5 | 1 | 3 | 1 | |
| Total | 167 | 53 | 44 | 51 | 11 archivés, 7 hors moteur, 1 incompatible |

## Jalons livrés

| Jalon | Contenu | Version |
|---|---|---|
| M00, audit, M0 | nettoyage du dépôt, audit, base solide (tests, sanitizers, contrôles de couches) | |
| M1, M1 bis | noyau `core`, remise en ordre des couches | |
| M2 | RHI et backend OpenGL, pipeline de shaders, couche de rendu, plateforme | |
| M3, M3 bis | interface `orosmo::ui`, cœur générique distribuable, outil `orosmo init` | `v0.3.1` (première release) |
| M4, 0.4.1 | backend Vulkan, `orosmo doctor`, support CMake | `v0.4.0`, `v0.4.1` |
| M5 | cœur du rendu : graphe de passes, forward+, ombres, HDR, post-traitements, anticrénelage, culling GPU, 2D | `v0.5.0` |
| M6 | import et export glTF 2.0, textures et KTX2, cuisson de maillages, matériaux PBR, éclairage par environnement, skinning GPU, machine à états, courbes, foules | `v0.6.0` |
| M14 | ce site | |
| SDK | installeur, outil `orosmo` (install, build, run, migrate…), moteur précompilé avec ses dépendances (`deps/`), installé une fois par machine (A76) | `v0.6.2` |

Notes détaillées de chaque jalon : `docs/releases/`. Le jalon M7 a été supprimé (le moteur reste générique) et son numéro
n'est pas réattribué.

## Ce qui marche, avec preuve

Chaque ligne cite l'identifiant de la checklist.

- Noyau : journal avec pile d'appels, tâches, allocateurs, configuration, temps à pas fixe, événements, réflexion et
  sérialisation (CORE, 10 sur 13).
- RHI : Vulkan 1.3 et OpenGL 4.3 derrière une interface commune, compute, rendu vers plusieurs cibles, dessin indirect.
- Rendu 3D : graphe de passes et plugins, forward+, ombres en cascade avec cache, HDR et bloom, instancing, culling CPU et GPU, LOD, transparents triés, matériaux PBR, éclairage par environnement (REN-10, REN-13).
- Rendu 2D : sprites en lots, caméra 2D, formes, texte, mélange 2D/3D.
- Contenu : glTF 2.0 en lecture et en écriture (ASSET-01), textures sRGB ou linéaires (ASSET-02), KTX2 (ASSET-03), cuisson de
  maillages (ASSET-04).
- Animation : skinning GPU (ANIM-01), clips et machine à états (ANIM-02), courbes de propriétés (ANIM-03), foules (ANIM-04).
- Interface : widgets, mise en page, vues `.cui` compilées en C++, thèmes JSON rechargés à chaud.
- Outillage : installeurs, `orosmo install`, `init`, `update`, `migrate`, `build`, `run`, `doctor`, paquet avec `deps/`, `engine.mk` et `OrosmoConfig.cmake` (`make e2e-init`). Windows et macOS non essayés (G216).

## Partiel

- Plateformes (PLAT-01, 02, 04, 05) : Windows et macOS sont compilés par la CI mais pas vérifiés pour le code récent ;
  `--headless` n'est vérifié que sous Linux ; pas de composition IME (G107).
- RHI : Vulkan n'est vérifié que sous Linux avec un GPU (G142) ; Metal est reconnu mais refusé.
- Rendu : anticrénelage FXAA, SMAA 1x et TAA livrés, MSAA absent (REN-19) ; lumières de surface (REN-05), ombres de contact (REN-08), variantes de matériaux sans constantes de
  spécialisation (REN-11, A71), imposteurs (REN-31).
- Assets : le rechargement à chaud ne concerne que les textures, pas les modèles glTF (ASSET-05, G200) ; pas d'exécutable de cuisson.
- Interface : thèmes avec des tailles encore en dur dans certains widgets (UI-07), rendu économe sans cache par couche (UI-13).
- Outils (TOOL-03, TEST, DOC-01, BLD-01, BLD-02) : le parcours est vérifié sous Linux seulement.

## Absent

- Scène et simulation : entités et composants, transformations hiérarchiques, chargement de scène, boucle de simulation
  (ECS-01 à ECS-03, SIM-01). L'application garde ses propres objets.
- Logique de jeu rechargeable (GAME-01 à GAME-07).
- Entrées : manettes (PLAT-03).
- Rendu : occlusion ambiante, réflexions, éclairage précalculé, ciel, brouillard, nuages, eau, particules, décalques,
  réfraction, transparence indépendante de l'ordre (REN-14 à REN-16, REN-24 à REN-27, REN-33, REN-38, REN-40). Plusieurs sont prévus
  comme modules préinstallés (M13).
- 2D : cartes de tuiles, éclairage 2D (R2D-04, R2D-07).
- Contenu : import OBJ, cinématique inverse, morph targets, retargeting (ASSET-06, ANIM-05, ANIM-06).
- Interface : outils de debug, rechargement à chaud de la vue elle-même, navigation à la manette, inspecteur (UI-04, 08, 09, 10).
- Backend Metal (RHI-13), transferts asynchrones (RHI-19).
- Voxels et ray tracing (EXT-01 à EXT-05).
- Modules, addons et registre (MOD-01 à MOD-09).

Le son, le réseau et un éditeur de scène sont hors périmètre pour l'instant.

## Ce site

Le site est le jalon M14 (WEB-01 Fait ; WEB-02 à WEB-04 Partiel). Il est construit en CI à chaque PR qui touche `site/` ;
rien n'est encore publié, car la publication attend un tag `v*` sur `main` et le choix d'un hébergement (G203, G204).
