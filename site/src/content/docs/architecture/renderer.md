---
title: Renderer, graphe de passes et plugins
description: Le graphe de passes, les plugins de rendu, l'image de SceneRenderer, l'éclairage, les ombres, le post-traitement et la 2D.
sidebar:
  order: 3
---

`orosmo::renderer` (couche 3) se construit sur le RHI : ressources, scène, graphe de passes et plugins.
*(Sources : `src/renderer/README.md`, `docs/03` §10, §11.)*

## Couche de ressources et de scène

`GpuBuffer`, `Texture`, `RenderTarget` (MRT + profondeur), `ShaderLibrary` (compilation, cache, rechargement à chaud),
`PipelineCache` (un pipeline par programme et état fixe, reconstruit quand le programme change), `Image` (captures et
comparaison), `GpuTimer` ; puis `Camera`, `LightManager`, `Mesh`, `SceneRenderer`, `DebugDraw`. Rien ne se « lie » : tout
passe par des handles RHI, des groupes de liaison et des passes explicites.

## Le graphe de passes (REN-01)

`RenderGraph` connaît des ressources nommées (textures et tampons, importés ou transitoires) et des passes `kRaster` (le
graphe ouvre la passe de rendu sur les attachements déclarés) ou `kGeneric` (la passe enregistre ce qu'elle veut). Il
**déduit l'ordre** des accès déclarés, **écarte les passes inutiles**, **refuse les cycles** (avec le chemin des passes)
et réutilise les transitoires.

```cpp
RenderGraph &graph = builder.graph();                                       // dans RenderFeature::declarePasses
const RgTexture scene = builder.resources().sceneColor;                     // ressource importée par le renderer
const RgTexture bright = graph.createTexture("bloom.bright", {.format = rhi::Format::kRGBA16Float,
                                                               .size = builder.resources().renderSize});
graph.addPass("bloom.extract", PassType::kRaster, [=](RenderPassContext &ctx) -> core::Result<> {
         ctx.encoder().setBindGroup(0, ctx.frame()->group);                 // groupe 0 : caméra, lumières, FrameData
         /* lier ctx.texture(scene) au groupe 1, dessiner un triangle plein écran */ return {};
     })
    .read(scene)
    .writeColor(bright);
graph.addPass("bloom.apply", PassType::kGeneric, applyFn).read(bright).write(scene);
```

## Plugins de rendu

Le renderer ne connaît que l'interface `RenderFeature` : `supports`, `prepare(FrameData)`, `declarePasses(RenderGraphBuilder&)`.
Un plugin lit `Capabilities` et se désactive proprement si une capacité manque. Les plugins livrés :

| Plugin | Rôle |
| --- | --- |
| `ForwardPlusLighting` | éclairage **forward+ clusterisé** (grille 16×9×24, compute), avec pré-passe de profondeur |
| `ShadowFeature` | **ombres en cascade** (PCF), atlas, budget, cache, projecteurs, ombres des transparents |
| `TonemapFeature`, `BloomFeature`, `AutoExposureFeature`, `ColorGradingFeature`, `VignetteFeature` | **HDR** et post-traitements |
| `FxaaFeature`, `SmaaFeature`, `TaaFeature` | anticrénelage : FXAA, SMAA 1x, TAA (jitter de Halton) |
| `GpuDrivenFeature` | **rendu piloté par le GPU** : scène GPU persistante, culling en compute, occlusion Hi-Z, multi-draw indirect |
| `TransparentFeature` | transparents triés, 8 modes de fusion |
| `Layer2DFeature` | **2D** du monde, en superposition ou vers une texture |

## L'image de `SceneRenderer`

`beginFrame` → `setCamera` → `drawMesh` (enregistre) → `endFrame`, qui construit et exécute le graphe : `depth-prepass`
(si activée), `scene` (couleur, vecteurs de mouvement, profondeur), `upscale`, `frame-generation`, `history`, puis les
passes des plugins dans l'ordre d'enregistrement, puis les post-traitements. L'ordre de rendu suit `docs/03` §11 : culling,
ombres, pré-passe, clusters, passe principale (opaque, transparent, 2D du monde), effets, HDR, tonemapping, 2D en
superposition (interface), présentation.

## Données par image (REN-03)

`FrameData` (std140, 640 octets) est défini **une seule fois** pour le C++ et le GLSL (`shaders/rhi/frame_data.glsl`),
avec des `static_assert` sur la taille et les décalages : matrices (et leurs inverses, et celle de l'image précédente),
origine de la caméra pour le rendu relatif (positions monde en `double` côté CPU, scindées en deux `float`), jitter,
viewport, temps, soleil, exposition.

## Instancing, culling, LOD

`InstanceRenderer` et `ObjectSet` (instancing : 10 000 instances en 2 draw calls dans la démo), culling CPU par frustum et
BVH, sélection de LOD avec fondu tramé, et le chemin GPU de `GpuDrivenFeature`, qui produit le même ensemble visible et la
même image que le culling CPU (vérifié par les tests, sur OpenGL et Vulkan).

## 2D

`Renderer2D` (batch de sprites, formes, lignes), `Camera2D` (mode pixel parfait), `CanvasRenderer` (affiche les listes de
dessin de l'interface). L'interface est passée de 143 à 18 draw calls grâce au batch.

## Limites

Lumières de surface, ombres de contact, MSAA et imposteurs automatiques sont absents ; les matériaux PBR et l'import de
modèles arrivent en M6. Le détail et les identifiants sont dans `docs/STATUS.md` et `docs/GAPS.md`.
