---
title: Renderer, graphe de passes et plugins
description: Les objets du renderer et ce qu'ils possèdent, l'image de SceneRenderer, les règles de dépendance du graphe de passes, les plugins et leur ordre, les données par image (FrameRing, BatchArena).
sidebar:
  order: 3
---

`orosmo::renderer` (couche 3) se construit sur le RHI : ressources RAII, scène, graphe de passes et plugins. Il dépend de
`rhi`, `platform` et `core` seulement. Sources : [`src/renderer/README.md`](https://github.com/TimotheSandt/Orosmo/blob/dev/src/renderer/README.md),
`docs/03` §10 et §11.

## Les objets et ce qu'ils possèdent

| Objet | Rôle | Possession |
| --- | --- | --- |
| `GpuBuffer`, `Texture`, `RenderTarget` | tampon, texture 2D + échantillonneur, cibles couleur (MRT) + profondeur | RAII ; le périphérique doit leur survivre |
| `ShaderLibrary` | programmes GLSL 450, cache SPIR-V, rechargement à chaud (`pollHotReload`) | objet explicite, pas de singleton ; rend des `std::shared_ptr<ShaderProgram>` |
| `PipelineCache` | un pipeline par (programme, état fixe), reconstruit quand le programme change | objet explicite |
| `Mesh` | sommets, index, transformation, textures du groupe 1, constantes | RAII ; textures et groupe 1 non possédés |
| `LightManager` | lumières et leur tampon de stockage | valeur (`Result<LightManager>`) ; passé par pointeur à `setLights` |
| `SceneRenderer` | cible de scène, groupe 0, dessins, graphe de l'image, plugins | `std::unique_ptr` ; possède ses plugins |
| `RenderFeature` | plugin qui ajoute des passes | possédé par le `SceneRenderer` après `addFeature` |

Aucun de ces objets ne se « lie » à un état global : tout passe par des handles RHI, des groupes de liaison et des passes
explicites. L'ordre de destruction et les durées de vie sont détaillés dans
[RHI, durées de vie](/interne/rhi/#durées-de-vie-et-ordre-de-destruction).

## L'image de SceneRenderer

```cpp
auto encoder = device.createCommandEncoder();
(void)renderer.beginFrame(*encoder);             // l'encodeur doit vivre jusqu'à endFrame
(void)renderer.setCamera(camera);
(void)renderer.drawMesh(cube, camera);            // enregistre seulement
(void)renderer.endFrame({texture, format, size}); // construit et exécute le graphe de l'image
(void)device.submit(std::move(encoder));
```

Noms et types comme dans les [conventions des exemples](/demarrer/premiere-application/#conventions-des-exemples) : `device`, `renderer` et `cube` sont des références, comme dans ce tableau. Les `(void)` raccourcissent ; une vraie boucle teste chaque `Result`.

`drawMesh` enregistre le dessin : pipeline, constantes et transformation sont résolus à l'appel, mais le dessin est joué à
`endFrame`. Les tampons du maillage doivent donc rester valides jusque-là. Chaque dessin copie sa transformation dans sa
propre tranche de l'image (`recordModelSlice`, bloc de 208 octets, groupe 2 avec décalage dynamique) : un maillage dessiné
plusieurs fois dans l'image garde chaque pose.

`endFrame` déclare, dans cet ordre :

1. `depth-prepass`, seulement avec `setDepthPrepass(true)` ;
2. `scene` : couleur, vecteurs de mouvement, profondeur ; joue les dessins enregistrés ;
3. `upscale` (mode actif ou copie 1:1), `frame-generation`, `history` ;
4. les passes des plugins, dans l'ordre d'enregistrement par `addFeature` ;
5. `post.<nom>`, un par `IPostProcessPass`.

`upscale`, `frame-generation` et `history` lisent la scène avec `readFinal` : bien que déclarées avant les plugins, elles
s'exécutent après tout plugin qui réécrit la couleur de scène. Le cycle de vie : `beginFrame` (enregistrement), `endFrame`
(exécution ; un `beginFrame` y est refusé), puis nettoyage, que l'image réussisse ou non : dessins vidés,
`RenderFeature::frameEnded` de chaque plugin. Chaque passe est mesurée par le `Profiler` sous `RenderGraph/<passe>`.

## Le graphe de passes

`RenderGraph` connaît des ressources nommées (textures et tampons, importés ou transitoires) et des passes `kRaster` (le
graphe ouvre la passe de rendu sur les attachements déclarés) ou `kGeneric` (la passe enregistre ce qu'elle veut : ses
propres passes, du calcul, des copies). Il déduit l'ordre des accès déclarés, écarte les passes inutiles, refuse les cycles
et réutilise les transitoires.

```cpp
RenderGraph &graph = builder.graph();                                       // dans RenderFeature::declarePasses
const RgTexture scene = builder.resources().sceneColor;                     // ressource importée par le renderer
const RgTexture bright = graph.createTexture("bloom.bright", {.format = rhi::Format::kRGBA16Float,
                                                               .size = builder.resources().renderSize}); // transitoire
graph.addPass("bloom.extract", PassType::kRaster, [=](RenderPassContext &ctx) -> core::Result<> {
         ctx.encoder().setBindGroup(0, ctx.frame()->group);                 // groupe 0 : caméra, lumières, FrameData
         /* lier ctx.texture(scene) au groupe 1, dessiner un triangle plein écran */ return {};
     })
    .read(scene)                 // entrée : la scène telle qu'elle est à ce point de la déclaration
    .writeColor(bright);         // sortie : attachement
graph.addPass("bloom.apply", PassType::kGeneric, applyFn).read(bright).write(scene);   // réécrit la scène
```

Dans une passe, `ctx.texture(h)` et `ctx.buffer(h)` donnent la ressource GPU, `ctx.desc(h)` sa description, et
`graph.findTexture(nom)` retrouve une ressource déclarée par une autre passe.

### Règles de dépendance

Elles s'appliquent ressource par ressource ; l'ordre de déclaration ne compte qu'entre les accès à une même ressource.

- Les écrivains d'une ressource (`write`, `writeColor`, `writeDepth` ; une passe qui lit et écrit est un écrivain)
  s'exécutent dans leur ordre de déclaration. Un écrivain ne dépend du précédent (qui est alors gardé) que s'il consomme
  l'ancien contenu : attachement en `kLoad`, `write` générique, ou lecture dans la même passe. Un attachement en `kClear`
  ou `kDontCare` ne fait qu'ordonner : la passe écrasée est écartée si rien d'autre ne la lit.
- `read` voit la version laissée par le dernier écrivain déclaré avant la passe, et l'écrivain suivant attend cette
  lecture. Sans écrivain déclaré avant, la passe voit le contenu final.
- `readFinal` voit toujours le contenu final, après tous les écrivains de l'image, même déclarés plus tard.
- Une ressource importée sans écrivain est lue telle quelle (contenu d'avant l'image). Une ressource transitoire lue sans
  producteur par une passe gardée est refusée par `compile` (`kInvalidArgument`) : son contenu serait indéfini.

Sont gardées : les passes à effet de bord (`sideEffect`), celles qui écrivent une ressource importée, et tout ce dont elles
consomment les données. Un cycle est refusé avec le chemin des passes. À l'exécution, une passe en erreur arrête l'image :
l'erreur rendue est préfixée du nom de la passe.

Les usages d'une texture transitoire sont déduits des accès (attachement : `kRenderTarget`, lecture : `kSampled`) ; un
accès en stockage ou en copie se déclare dans `RgTextureDesc::usage`. Les transitoires non utilisées sont libérées après
`kTransientKeepFrames` images.

## Plugins de rendu

Le renderer ne connaît que l'interface `RenderFeature` : `name`, `supports(Capabilities)`, `prepare(FrameData)`,
`declarePasses(RenderGraphBuilder &)`, `frameEnded()`, et des options (`requiredAovs`, `wantsJitter`,
`postProcessesScene`). `RenderGraphBuilder` donne le graphe et les ressources connues (`sceneColor`, `sceneDepth`,
`motionVectors`, `output`, tailles), `addSceneInput` (une passe que la scène lit s'exécute avant elle et n'est jamais
écartée) et `addFrameBinding` (liaisons 3 à 7 du groupe 0).

| Plugin | Rôle |
| --- | --- |
| `ForwardPlusLighting` | éclairage forward+ en clusters (grille 16 × 9 × 24 par défaut, calcul), avec pré-passe de profondeur |
| `ShadowFeature` | ombres en cascade (PCF), atlas, budget, cache, projecteurs, ombres des transparents |
| `IblFeature` | éclairage par l'environnement et sondes |
| `TonemapFeature`, `BloomFeature`, `AutoExposureFeature`, `ColorGradingFeature`, `VignetteFeature` | HDR et post-traitements |
| `FxaaFeature`, `SmaaFeature`, `TaaFeature` | anticrénelage : FXAA, SMAA 1x, TAA (jitter de Halton) |
| `GpuDrivenFeature` | rendu piloté par le GPU : scène GPU persistante, culling en calcul, occlusion Hi-Z, multi-draw indirect |
| `TransparentFeature` | transparents triés, 8 modes de fusion (`BlendMode`) |
| `Layer2DFeature`, `ViewsFeature` | 2D du monde (superposition ou texture), plusieurs vues |

### Enregistrement et ordre

`SceneRenderer::addFeature(std::unique_ptr<RenderFeature>)` rend un `Result<RenderFeature *>` : le pointeur n'est pas
possédé, le renderer garde le plugin. Pour appeler les fonctions propres au plugin, on convertit le pointeur :

```cpp
auto shadows = rd::ShadowFeature::create(device, shaders, pipelines, shaderDir);
if (!shadows) return std::unexpected(shadows.error());
auto added = renderer.addFeature(std::move(*shadows));      // kUnsupported si supports() est faux, kAlreadyExists si le nom est pris
if (!added) return std::unexpected(added.error());
auto *shadowFeature = static_cast<rd::ShadowFeature *>(*added);   // valide tant que le plugin est enregistré
```

Source : [`apps/demo/src/Game/Game.cpp`](https://github.com/TimotheSandt/Orosmo/blob/dev/apps/demo/src/Game/Game.cpp).

L'ordre des passes des plugins est l'ordre des appels à `addFeature`, pas un ordre déclaré par le plugin. Ordre conseillé,
celui de `docs/03` §11 : ombres et éclairage, transparents et 2D du monde, post-traitements et anticrénelage, puis ce qui
écrit la sortie (interface, présentation). Un plugin qui doit passer après d'autres ajoutés plus tard est retiré
(`removeFeature(nom)`) puis réenregistré ; la démo le fait pour ses passes d'interface.

Un plugin qui écrit `resources().sceneColor` s'exécute avant la mise à l'échelle ; un plugin qui écrit
`resources().output` s'exécute après (interface, superpositions). C'est ainsi que la démo branche l'interface
(`CanvasRenderer`) et la présentation (passes `ui` et `present`).

## Données par image

`FrameData` (std140, 640 octets) est défini une seule fois pour le C++ et le GLSL (`shaders/rhi/frame_data.glsl`), avec des
`static_assert` sur la taille et les décalages : matrices (et inverses, et celles de l'image précédente), origine de la
caméra pour le rendu relatif (scindée en deux `float`), jitter, viewport, temps, soleil, exposition. Il est au groupe 0,
liaison 2.

### FrameRing

Les données réécrites à chaque image (`FrameData`, caméra, tranches de modèle, lignes de debug) partent en une seule
écriture, à `endFrame`, dans un `FrameRing` : un tampon `rhi::MemoryKind::kUpload` par image en vol
(`kDefaultFramesInFlight` = 2, celui des deux backends). L'image `n` écrit la zone `n % framesInFlight` ; cette zone a été
lue par l'image `n - framesInFlight`, que le RHI a déjà attendue dans `beginFrame` / `present`. Aucune écriture ne vise donc
une plage que le GPU lit encore. `SceneRendererConfig::framesInFlight` règle la profondeur (au moins 2).

`FrameRing::reserve` rend `true` quand le tampon de la zone a été recréé : les groupes de liaison qui le visent sont à
refaire. L'ancien tampon est détruit par destruction différée.

### BatchArena

Sur Vulkan, `updateBuffer` est un transfert soumis avant les commandes de l'encodeur : deux écritures au même endroit avant
`submit` donneraient à tous les dessins les données de la dernière. `BatchArena` donne donc à chaque envoi sa propre plage
(alignée sur 256 octets) tant que l'encodeur est le même, avec une zone par image en vol. Un nouvel encodeur
(`CommandEncoder::id()`) passe à la zone suivante ; un encodeur dont la zone a été reprise reçoit `kBusy`. Au plus
`framesInFlight` encodeurs distincts peuvent s'en servir entre deux fins d'image. Le dessin 2D (`Renderer2D`,
`CanvasRenderer`) s'en sert.

## Instancing, culling, LOD, 2D

`InstanceRenderer` et `ObjectSet` dessinent par instances (10 000 instances en 2 draw calls dans la démo), avec culling CPU
par frustum et BVH et choix du LOD avec fondu tramé. `GpuDrivenFeature` produit le même ensemble visible et la même image
sur le GPU (vérifié par les tests sur OpenGL et Vulkan). En 2D : `Renderer2D` (lots de sprites, formes, lignes),
`Camera2D`, `CanvasRenderer` (listes de dessin de l'interface).

## Limites

Absents : ombres avancées, extensions de matériaux, réflexions et illumination globale, éclairage précalculé,
transparence indépendante de l'ordre ; ciel, brouillard, eau, particules et occlusion ambiante sont prévus comme modules
installables (M13). Le graphe n'a que des transitoires 2D (pas de tableau, cube, 3D ni MSAA) et tout s'exécute sur
un seul encodeur (pas de file asynchrone). Détail : [État du projet](/projet/etat/) et `docs/GAPS.md`.

## Voir aussi

- [Écrire un plugin de rendu](/guides/avance/plugin-de-rendu/)
- [RHI](/interne/rhi/)
- [Dessiner une scène](/guides/rendu-3d/scene/)
