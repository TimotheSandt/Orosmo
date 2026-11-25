---
title: État du moteur
description: Jalons livrés, fonctions partielles ou absentes, et où trouver les preuves dans le dépôt.
sidebar:
  order: 1
---

La référence est le dépôt : la checklist `docs/02-CHECKLIST_FONCTIONNALITES.md` (un identifiant par fonction, avec son
critère d'acceptation), `docs/STATUS.md` (état et preuve de chaque identifiant) et `docs/GAPS.md` (ce qui manque). Cette
page en est un résumé, mis à jour à chaque jalon.

## Jalons livrés

| Jalon | Contenu | Version |
| --- | --- | --- |
| M00, audit, M0 | nettoyage du dépôt, audit, base solide (tests, sanitizers, contrôles de couches) | — |
| M1, M1 bis | noyau `core`, remise en ordre des couches | — |
| M2 | RHI et backend OpenGL, pipeline de shaders, couche de rendu, plateforme | — |
| M3, M3 bis | interface `orosmo::ui`, cœur générique distribuable, outil `orosmo init` | `v0.3.1` (première release publiée) |
| M4, 0.4.1 | backend Vulkan, `orosmo doctor`, support CMake | `v0.4.0`, `v0.4.1` |
| M5 | cœur du rendu : graphe de passes, forward+, ombres, HDR, post-traitements, anticrénelage, culling GPU, 2D | `0.5.0` |

Les releases publiées et leurs paquets sont sur
[GitHub](https://github.com/TimotheSandt/Orosmo/releases) ; les notes détaillées de chaque jalon sont dans
`docs/releases/`.

## Partiel ou absent (extraits)

- **Plateformes** : Windows et macOS sont construits en CI mais pas vérifiés pour tout le code récent ; le backend
  **Metal** n'existe pas encore (MoltenVK puis natif, jalon M9).
- **Rendu** : lumières de surface, ombres de contact, MSAA, imposteurs automatiques, matériaux PBR (M6).
- **Interface** : pas de rechargement à chaud de la vue elle-même (le thème se recharge), pas de navigation à la
  manette, composition IME non affichée ; cache de rendu par couche non livré.
- **Assets et animation** : import glTF, textures compressées, squelettes et animation : **en préparation (M6)**.
- **Logique de jeu rechargeable, scène (ECS), simulation** : jalon M8 ; son, réseau et éditeur de scène : hors périmètre
  pour l'instant (`docs/01-VISION_ET_PERIMETRE.md`, `docs/05-PLAN_PAR_JALONS.md`).

## Ce site

Le site et le wiki sont le jalon **M14** (`WEB-01` à `WEB-05`). Ils sont construits par la CI à chaque PR qui touche
`site/`, et l'image Docker est publiée sur le registre GitHub (GHCR) à chaque tag de version.
