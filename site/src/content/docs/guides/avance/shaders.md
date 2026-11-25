---
title: "Écrire et recharger des shaders"
description: "Écrire un shader GLSL 450 qui tourne sur Vulkan et OpenGL, l'inclure, le charger avec ShaderLibrary, le recharger à chaud et utiliser la boîte à outils shaders/include/orosmo."
sidebar:
  order: 1
  label: Shaders
---

Les shaders sont écrits une seule fois en GLSL 450, avec la syntaxe Vulkan (`layout(set, binding)`, push constants,
`gl_VertexIndex`). Le moteur les compile en SPIR-V, et traduit en GLSL 430 pour OpenGL. Vous ne maintenez pas deux versions.
Les conventions de liaison sont dans [Conventions GLSL](/reference/conventions-glsl/), la chaîne de compilation dans
[Pipeline de shaders](/interne/shaders/).

## Exemple minimal

Dans les extraits, `device`, `shaders`, `pipelines`, `renderer` et `shaderDir` suivent [Lire la boucle d'une application](/demarrer/premiere-application/) (section « Conventions des exemples »).

Un programme depuis des sources en mémoire, adapté de `tests/renderer/render_graph_tests.cpp`. Le sommet dessine un triangle
plein écran sans tampon de sommets ; le fragment lit le temps dans `FrameData`.

```cpp
#include <orosmo/renderer/shader_library.hpp>
#include <orosmo/renderer/pipeline_cache.hpp>

namespace rd = orosmo::renderer;

rd::ShaderLibraryConfig config;
config.includeRoots = {shaderDir};          // permet #include "frame_data.glsl"
rd::ShaderLibrary shaders(device, config);
rd::PipelineCache pipelines(device);

constexpr const char *vs = R"(#version 450
void main() { vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2); gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); })";
constexpr const char *fs = R"(#version 450
#include "frame_data.glsl"
layout(location = 0) out vec4 color;
void main() { color = vec4(fract(frame.time.x), 0.0, 0.0, 1.0); })";

auto program = shaders.graphicsFromSource("game.pulse", vs, fs);
if (!program) { /* program.error().message : fichier:ligne du compilateur */ }

rd::GraphicsState state;
state.raster.cull = orosmo::rhi::CullMode::kNone;
state.colorTargets = {orosmo::rhi::ColorTargetState{.format = orosmo::rhi::Format::kRGBA8Unorm}};
auto pipeline = pipelines.graphics(**program, state);   // rhi::PipelineHandle, mis en cache
```

La déclaration `#extension GL_GOOGLE_include_directive` n'est pas nécessaire : `#include` marche tel quel.

## Charger des fichiers

| Appel de `ShaderLibrary` | Effet |
|---|---|
| `loadGraphics(vertexFile, fragmentFile, defines)` | Programme sommets + fragments depuis des fichiers ; surveillés pour le rechargement à chaud. |
| `loadCompute(computeFile, defines)` | Programme de calcul. |
| `graphicsFromSource(name, vs, fs, defines)` | Sources en mémoire ; pas de rechargement. |

`defines` est un `std::vector<rd::Define>` de paires (nom, valeur), par exemple `{{"OROSMO_SHADOWS", "1"}}`. Chaque jeu de
définitions donne un programme distinct. La liste des définitions du moteur est dans
[Conventions GLSL](/reference/conventions-glsl/).

Règle d'ordre : configurez d'abord le renderer (plugins, AOV, vue de debug), puis compilez les programmes de maillage avec les
définitions qui en résultent (`SceneRenderer::aovShaderDefines()`, `forwardPlusDefines()`, `iblDefines()`). Un programme compilé
avant donne une image fausse ou un pipeline refusé, sans toujours d'erreur claire.

`ShaderLibraryConfig` :

| Champ | Type | Défaut | Effet |
|---|---|---|---|
| `cacheDirectory` | `std::filesystem::path` | vide | Dossier du cache SPIR-V sur disque ; vide : mémoire seulement. La démo le place dans le dossier de données de l'utilisateur. |
| `includeRoots` | `std::vector<std::filesystem::path>` | vide | Dossiers de recherche des `#include`, après le dossier du fichier qui inclut. |

## Inclure

`#include "x.glsl"` cherche d'abord à côté du fichier qui inclut, puis dans les `includeRoots`. Les shaders du moteur incluent la
boîte à outils par `#include "../include/orosmo/math.glsl"` ; avec `shaders/include` dans `includeRoots`, `#include "orosmo/math.glsl"`
suffit. Un `#include` dans une source en mémoire demande un `includeRoots`.

La boîte à outils (`shaders/include/orosmo/`) :

| Fichier | Contenu |
|---|---|
| `math.glsl` | Constantes (`kPi`, `kEpsilon`...), `saturate`, `sqr`, `remap`, `safeDiv`, `safeNormalize`, `luminance`, `srgbToLinear`, `linearToSrgb`, `orthonormalBasis`. |
| `coords.glsl` | `uvToNdc`, `ndcToUv`, `pixelToUv`, `octEncode`, `octDecode`, `sphericalToDir`, `dirToSpherical`, `tangentToWorld`. |
| `depth.glsl` | `viewPositionFromDepth`, `worldPositionFromDepth`, `linearDepth`, `linearDepthInfiniteReverseZ`, `linearDepthReverseZ`. |
| `noise.glsl` | Hachage PCG (`pcgHash`, `hash11`, `hash21`, `hash31`), `valueNoise`, `gradientNoise`, `fbm`, `interleavedGradientNoise`. Déterministe. |
| `pbr.glsl` | `distributionGGX`, `visibilitySmithGGXCorrelated`, `fresnelSchlick`, `brdfGGX`. |
| `lights.glsl` | `lightRange`, `lightAttenuation`, `spotCone` (forward et forward+). |
| `shadow.glsl` | `shadowSlopeBias`, `shadowInsideMap`, `shadowPcf`. |

Ces fichiers ne contiennent que des fonctions pures, sans liaison. Chacun est compilé sur chaque backend par
`tests/renderer/shader_toolbox_tests.cpp`.

## Recharger à chaud

Le rechargement ne se fait que pour les programmes chargés depuis des fichiers (`loadGraphics`, `loadCompute`). L'application
appelle `ShaderLibrary::pollHotReload()` une fois par image : elle recompile les fichiers modifiés (et leurs `#include`) et
rend le nombre d'étages rechargés. La démo ne l'appelle pas : à vous de le faire dans votre boucle.

```cpp
// Dans la boucle, une fois par image.
const std::size_t reloaded = shaders.pollHotReload();
if (shaders.lastError()) { /* la compilation a échoué : message du compilateur */ }
// Rien d'autre : PipelineCache::graphics reconstruit le pipeline à la demande quand la révision du programme a changé.
```

Cela ne vaut que si l'on **redemande** le pipeline : `PipelineCache::graphics` reconstruit à la demande, mais un `PipelineHandle`
conservé (comme celui que reçoit `PulseFeature` dans [Écrire un plugin de rendu](/guides/avance/plugin-de-rendu/)) n'est pas mis
à jour tout seul. Il reste valide seulement jusqu'à ce que quelqu'un redemande le pipeline : la reconstruction
réussie **détruit** l'ancien pipeline (`PipelineCache` le libère), et tout handle conservé devient invalide. Un plugin qui veut suivre le rechargement garde le `ShaderProgram`
et l'état, puis rappelle `pipelines.graphics(*program, state)` quand `pollHotReload()` a rendu plus de 0 (ou à chaque image :
le cache répond sans reconstruire tant que rien n'a changé) pour remplacer son handle.

Si la compilation échoue, ou si les étages ne se lient plus (une sortie `vec4` devenue `vec3`), le programme garde son ancienne
version, l'erreur est dans `lastError()` et `PipelineCache::lastError()`, et l'image continue avec l'ancien pipeline. Les étages
modifiés ensemble sont validés ensemble : aucun n'est installé si l'un échoue. Corriger un seul étage suffit à reprendre.

## Cache

Le cache SPIR-V (`cacheDirectory`) est indexé par le hachage du source prétraité, de l'étage et de la version du cache : un
shader modifié est recompilé, un fichier corrompu aussi. Le cache de pipelines natifs de Vulkan est écrit sur disque ; il n'y en a
pas pour OpenGL.

## Limites

- La boîte à outils n'a pas d'atmosphère ; pas de cache de pipelines natifs pour
  OpenGL.
- Un même shader doit donner la même image sur Vulkan et OpenGL : les tests tournent sur les deux backends.

## Voir aussi

- [Conventions GLSL et définitions](/reference/conventions-glsl/)
- [Écrire un plugin de rendu](/guides/avance/plugin-de-rendu/)
- [Pipeline de shaders](/interne/shaders/)
