---
title: Couches et modules
description: Les couches du moteur, la règle de dépendance, quel niveau une application utilise, et les contrôles automatiques qui font respecter la règle.
sidebar:
  order: 1
---

Le moteur est découpé en modules rangés en couches. Un module ne dépend que des modules de couche inférieure ; un
contrôle automatique (`make check`) refuse le reste. Sources : `docs/03-ARCHITECTURE_ET_PORTABILITE.md` §1, §2, §7, §8 et
[`tools/layers.json`](https://github.com/TimotheSandt/Orosmo/blob/dev/tools/layers.json).

## La règle de dépendance

Niveaux de `tools/layers.json`, du bas vers le haut. Un fichier n'inclut que des fichiers de son niveau ou d'un niveau
inférieur.

| Niveau | Module | Contenu | État |
| --- | --- | --- | --- |
| 0 | `core` | erreurs, journal, tâches, temps, événements, maths, ressources, VFS, réflexion, sérialisation | présent |
| 1 | `platform` | fenêtre et entrées (GLFW caché), services du système déclarés par `core` | présent |
| 2 | `rhi` | interface graphique abstraite ; backends dans `src/rhi/opengl/` et `src/rhi/vulkan/` | présent (Metal absent) |
| 3 | `renderer` | graphe de passes, plugins, scène, matériaux, lumières, ombres, effets, 2D | présent |
| 4 | `assets` | import glTF, textures, cuisson | présent |
| 5 | `anim` | squelettes, clips, machines à états, courbes, foules | présent |
| 6 | `scene` | entités, composants, transformations | prévu (M8) |
| 7 | `ui` | widgets, mise en page, thèmes, code généré par le langage `.cui` | présent |
| 8 à 10 | `world`, `sim`, `game` | monde, boucle de simulation, logique de jeu rechargeable | prévus ou archivés |
| 11 | `apps` | la démo `apps/demo`, les jeux | présent |

Chaque module présent est une bibliothèque statique : `libcore.a`, `libplatform.a`, `librhi.a`, `librenderer.a`,
`libassets.a`, `libanim.a`, `libui.a`, plus `libthirdparty.a`. `ui` produit des listes de dessin (`renderer::DrawList`) que
le renderer affiche. `platform` et `rhi` ne se rencontrent qu'à la création de la surface : la fenêtre donne un pointeur
opaque (`Window::getWindow()`) que `rhi::SurfaceDesc::nativeWindow` reçoit.

La liste des en-têtes de chaque module est dans l'[index des en-têtes](/reference/en-tetes/).

## Quel niveau une application utilise

Une application 3D se construit avec `platform`, `renderer`, `assets`, `anim` et `ui`. Du `rhi`, elle n'utilise que le
strict nécessaire pour faire tourner une image : créer le `rhi::Device`, ouvrir un `CommandEncoder` par image, `submit` et
`present`, et passer le périphérique (`device`) aux objets du renderer.

| Besoin | Niveau | Exemples de types |
| --- | --- | --- |
| Fenêtre, clavier, souris | `platform` | `Window`, `InputManager` |
| Créer le GPU, cycle d'une image | `rhi` (peu) | `createDevice`, `Device::beginFrame`, `createCommandEncoder`, `submit`, `present` |
| Dessiner une scène | `renderer` | `SceneRenderer`, `Camera`, `Mesh`, `PbrMaterial`, `LightManager` |
| Charger un modèle, des textures | `assets` | `loadGltf`, `RenderModel`, `TextureCache` |
| Animer | `anim` | `Clip`, `StateMachine`, `SkinnedModel`, `CrowdRenderer` |
| Interface | `ui` | `Ui`, widgets, thèmes, vues `.cui` |
| Nouvel effet de rendu | `renderer` + `rhi` | `RenderFeature`, `RenderGraph`, pipelines et groupes de liaison |

Les handles, pipelines et groupes de liaison du RHI ne servent directement qu'à deux endroits : un plugin de rendu
(`RenderFeature`, [guide](/guides/avance/plugin-de-rendu/)) et un dessin externe joué dans la passe de scène
(`SceneRenderer::drawCustom`). Le reste passe par les objets du renderer, qui possèdent leurs ressources GPU.

Les objets d'une application 3D et leur ordre de création :

```cpp
namespace rd = orosmo::renderer;

platform::Window window("jeu", 1280, 720);           // 1. fenêtre, créée avant le périphérique
auto created = rhi::createDevice(desc);              // 2. GPU : Result<std::unique_ptr<rhi::Device>>
rhi::Device &device = **created;
const fs::path shaderDir = GET_RESOURCE_PATH("shader/rhi");
rd::ShaderLibrary shaders(device);                 // 3. partagés par tous les renderers,
rd::PipelineCache pipelines(device);               //    vivants jusqu'à la fin
auto sceneResult = rd::SceneRenderer::create(device, shaders, pipelines, {.shaderDirectory = shaderDir});
rd::SceneRenderer &renderer = **sceneResult;
// ensuite : Mesh ou RenderModel (contenu), LightManager, Camera, Ui (interface)
```

Source : [`tools/engine-sdk/template/src/main.cpp`](https://github.com/TimotheSandt/Orosmo/blob/dev/tools/engine-sdk/template/src/main.cpp).
Qui possède quoi et dans quel ordre tout se détruit : [Durées de vie](/interne/rhi/#durées-de-vie-et-ordre-de-destruction).

## Organisation des fichiers

```text
include/orosmo/<module>/*.hpp     API publique (aucun type d'API graphique, ni GLFW, ni <windows.h>)
src/<module>/                     implémentation ; un README.md par module
src/rhi/{vulkan,opengl}/          backends
shaders/rhi/                      GLSL source
tests/<module>/                   tests (doctest)
apps/demo/                        la démo unique
tools/                            compilateur .cui, outil orosmo, SDK, contrôles
```

## Conventions

- C++23, `-Wall -Wextra -Wpedantic -Werror`. Pas d'exception dans le flux normal : `Result<T>` (`std::expected`) et le
  journal ([Erreurs et journal](/guides/noyau/erreurs-journal/)).
- Types en `PascalCase`, fonctions et variables en `camelCase`, constantes en `kPascalCase`, fichiers en `snake_case`. Le
  code repris (`Logger`, `Profiler`, `FPSCounter`) garde ses anciens noms jusqu'à la réécriture de son jalon.
- Pas de singleton caché (sauf le journal et le `Profiler`) : les services sont passés explicitement.
- Capacités optionnelles toujours lues dans `rhi::Capabilities`, jamais supposées ([RHI](/interne/rhi/#capacités-optionnelles)).

## Contrôles automatiques

`make check` lance les quatre contrôles ; la CI les exécute à chaque poussée et ils sont bloquants.

| Cible | Script | Ce qui échoue |
| --- | --- | --- |
| `make check-layers` | `tools/check_layers.py --strict` | Un `#include` vers un module de niveau supérieur (règles de `tools/layers.json`). |
| `make check-api` | `tools/check_api_isolation.py --strict` | `gl*`, `GL_*`, `Vk*`, `vk*`, `VK_*`, `MTL*`, `glfw*`, `<vulkan/…>`, `<GL/…>`, `<windows.h>`… hors de `src/rhi/`, `src/platform/` et `include/orosmo/platform/`. |
| `make check-headers` | `tools/check_headers.py --strict` | Un en-tête public qui ne se compile pas seul. |
| `make check-repo` | `tools/check-repo.sh` | Binaire suivi, collision de casse, fin de ligne CRLF. |

```bash
make check
```

## Voir aussi

- [Index des en-têtes publics](/reference/en-tetes/)
- [RHI](/interne/rhi/) et [Renderer](/interne/renderer/)
- [Tests, contrôles et images de référence](/interne/tests-controles/)
