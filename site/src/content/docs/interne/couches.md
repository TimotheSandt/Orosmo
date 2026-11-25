---
title: Couches et modules
description: Les couches du moteur, la règle de dépendance et les contrôles automatiques qui la font respecter.
sidebar:
  order: 1
---

*(Source : `docs/03-ARCHITECTURE_ET_PORTABILITE.md` §1, §2, §7, §8 ; `tools/layers.json`.)*

## La règle de dépendance

Un module ne dépend que des modules **du dessous**, jamais l'inverse :

```text
 apps/        la démo unique (apps/demo), les jeux
 game/        logique de jeu (bibliothèque rechargeable en développement)        [prévu, M8]
 ─────────────────────────────────────────────────────────────
 sim          boucle à pas fixe, accès aux données du monde                    [prévu]
 ui           Ui, widgets, thèmes, code généré par le langage d'interface
 scene        entités, composants, transformations                             [prévu, M8]
 anim         squelettes, clips, machines à états, foules                      [en préparation, M6]
 assets       import glTF, textures, cuisson, rechargement                     [en préparation, M6]
 renderer     graphe de passes, plugins de rendu, éclairage, ombres, effets, 2D
 ─────────────────────────────────────────────────────────────
 rhi          interface graphique abstraite (aucune API ici)
   rhi/vulkan   rhi/opengl   (rhi/metal prévu)  ← seuls endroits où les API graphiques existent
 ─────────────────────────────────────────────────────────────
 platform     fenêtre, entrées, fichiers, horloge (GLFW ici)
 core         logger, erreurs, mémoire, tâches, maths, ressources, sérialisation
```

Les modules présents aujourd'hui sont `core`, `platform`, `rhi`, `renderer` et `ui`, chacun compilé en bibliothèque
statique (`libcore.a`, `libplatform.a`, `librhi.a`, `librenderer.a`, `libui.a`, plus `libthirdparty.a`).
`ui` produit des listes de dessin ; le renderer les affiche. `platform` et `rhi` ne se rencontrent qu'à la création de la
surface de fenêtre.

## Organisation des fichiers

```text
include/orosmo/<module>/*.hpp     API publique (aucun type d'API graphique, ni GLFW, ni <windows.h>)
src/<module>/                     implémentation ; un README.md par module
src/rhi/{vulkan,opengl}/          backends
shaders/                          GLSL source
tests/<module>/                   tests (doctest)
apps/demo/                        la démo unique
tools/                            compilateur .cui, outil orosmo, SDK, contrôles
```

## Les modules

| Module | Rôle | README |
| --- | --- | --- |
| `core` | `Result<T>` (= `std::expected`), logger, tâches, allocateurs, configuration, temps, événements, maths (positions en `double`), ressources, VFS, réflexion et sérialisation, surveillance de fichiers | `src/core/README.md` |
| `platform` | `Window` (GLFW caché derrière une poignée opaque), `InputManager`, services du système déclarés par `core` | `src/platform/README.md` |
| `rhi` | [interface graphique abstraite](/interne/rhi/) et backends | `src/rhi/README.md` |
| `renderer` | [graphe de passes, plugins, scène, 2D](/interne/renderer/) | `src/renderer/README.md` |
| `ui` | [widgets, mise en page, thèmes](/interne/ui/) | `src/ui/README.md` |

## Conventions

- C++23, `-Wall -Wextra -Wpedantic -Werror` ; pas d'exception dans le flux normal : `std::expected` et le logger.
- Types en `PascalCase`, fonctions et variables en `camelCase`, constantes en `kPascalCase`, fichiers en `snake_case`.
- Pas de singleton caché (le logger excepté) : les services sont passés explicitement.
- Capacités optionnelles (ray tracing, bindless, mesh shaders…) toujours lues dans `Capabilities`, jamais supposées.

## Contrôles automatiques (`make check`, bloquant en CI)

1. **Couches** (`tools/check_layers.py`) : lit les `#include` et échoue si un module inclut un module de niveau supérieur.
2. **Isolation des API** (`tools/check_api_isolation.py`) : `gl*`, `Vk*`, `MTL*`, `<GLFW/…>`, `<windows.h>` interdits hors
   de `src/rhi/<backend>/` et `src/platform/`.
3. **En-têtes autonomes** (`tools/check_headers.py`) : chaque en-tête public se compile seul.
4. **Hygiène du dépôt** (`tools/check-repo.sh`) : pas de binaire suivi, pas de collision de casse, pas de CRLF.
