# renderer

Couche 3 de `docs/03` : ce qui se construit **sur** le RHI. Archive : `librenderer.a` (`src/renderer/`), API publique
`include/orosmo/renderer/`. Dépend de `rhi`, `platform` et `core` seulement. Namespace `orosmo::renderer`.

Sous-lots B3, B4 et B5 de M2 : la **couche de ressources** qui remplace, objet par objet, les anciennes façades à état global (`Buffer`,
`Texture`, `RenderTarget`, `ShaderProgram`, `ShaderLibrary`, `Screenshot`, `GPUTimestampQuery`). Elles **coexistent** avec les anciennes
jusqu'à B4/B5 (les consommateurs `Mesh`, `Sprite`, `Light`, `Renderer*`, texte et interface migrent) ; les anciennes sont supprimées
en B6. Aucune ne se « lie » : tout passe par des handles RHI, des groupes de liaison et des passes explicites.

| En-tête | Rôle |
| --- | --- |
| `gpu_buffer.hpp` | `GpuBuffer` : tampon RAII (`create`, `update`, `read`, `resize`) |
| `texture.hpp` | `Texture` : texture 2D RAII + échantillonneur, mips selon `FilterMode`, `upload`, `resize`, `readback`, `fromFile` (stb, RGBA8, retournée comme l'ancien chargeur ; le retournement est fait ici, le réglage global de stb n'est jamais modifié) |
| `render_target.hpp` | `RenderTarget` : cibles couleur (MRT) + profondeur (lisible ou non), `passDesc` (description de passe), `resize`/`reconfigure` (sans fuite, la cible reste valide si la recréation échoue), `readPixelUInt` / `readPixelRgba8` (origine en haut à gauche) ; un format de profondeur parmi les formats couleur est refusé |
| `shader_library.hpp` | `ShaderLibrary` (explicite, pas de singleton) et `ShaderProgram` : GLSL 450 depuis des fichiers ou des sources, réflexion fusionnée des étages, cache SPIR-V disque, **rechargement à chaud** (`pollHotReload`) : les étages modifiés d'un programme dans un même poll sont validés puis installés ensemble, et si l'un ne compile pas aucun n'est installé (après un échec, la modification suivante recompile tous les étages depuis leurs fichiers) ; un échec reste dans `lastError()` même si un autre programme se recharge dans le même poll ; les fichiers d'un programme libéré (ou d'un chargement en échec) ne sont plus surveillés (`watchedStageCount`). Identifiant de programme unique dans le processus |
| `pipeline_cache.hpp` | `PipelineCache` : un pipeline par (programme, état fixe), reconstruit quand la révision du programme change ; si la nouvelle révision ne se lie pas, l'ancien pipeline est rendu et l'échec gardé dans `lastError()` (pas de nouvel essai pour cette révision) ; `pushConstantSize(pipeline)` donne la taille des push constants de la révision qui l'a construit (c'est elle que `SceneRenderer` envoie) |
| `image.hpp` | `Image` RGBA8 origine en haut, `readImage`, `savePng` / `loadPng`, `compareImages` (tolérance, écart max et moyen) : sert aux captures et aux images de référence (RHI-14, B6) |
| `gpu_timer.hpp` | `GpuTimer` : deux horodatages, `isReady` / `elapsed` sans blocage |

### Couche de scène (B4)

| En-tête | Rôle |
| --- | --- |
| `camera.hpp` | `Camera` : position, orientation, perspective (mêmes formules que l'ancienne), jitter, `data()` = `CameraData` std140 (80 octets). Aucune entrée, aucun GLFW : le pilotage clavier/souris revient à l'application (G80) |
| `light.hpp` | `Light`, `LightManager` : lumières ambiante, directionnelle, ponctuelle, projecteur ; SSBO std430 dont la disposition est vérifiée par `static_assert` (`GpuLight` 96 octets, `GpuLightHeader` 48) ; le tampon grandit au besoin. `light(i)` en lecture seule, `setLight` renvoie kOutOfRange pour un index périmé |
| `mesh.hpp` | `Mesh` : sommets, index (chacun vérifié contre le nombre de sommets), instances optionnelles, attributs décrits par leurs tailles en flottants, transformation (translation, rotations X/Y/Z, échelle) et matrice des normales (inverse transposée, calculée par objet), textures liées (groupe 1), constantes de shader (push constants) |
| `fullscreen_blit.hpp` | `FullscreenBlit` : copie plein écran par triangle (le RHI n'a pas de commande de blit) ; `draw` respecte le viewport et le ciseau actifs ; `compose` pose une source prémultipliée sur la cible |
| `upscaling.hpp` | extensions de fin d'image : `IUpscaleMode` (avec `initialize`/`shutdown`), `IAdvancedUpscaleMode`, `IPostProcessPass`, `IFrameGenerationMode`, `BilinearBlitUpscaleMode` |
| `scene_renderer.hpp` | `SceneRenderer` : cible de scène, groupe 0 (caméra, lumières, `FrameData`), `beginFrame` / `setCamera` / `drawMesh` (enregistre) / `endFrame` (construit et exécute le graphe de l'image, voir plus bas), plugins (`addFeature`), échelle de rendu, jitter de Halton (seulement pour un mode exécuté qui le demande), historique (les consommateurs lisent l'image PRÉCÉDENTE, `resetHistory` à la première image et après un redimensionnement), vecteurs de mouvement, profondeur lisible. Un mode de mise à l'échelle n'est exécuté que si `upscalingEnabled()` (sinon copie 1:1), il est initialisé avant d'être activé et arrêté quand il est remplacé ; un mode de génération refusé laisse l'actif en place ; `generatedFrame()` expose son image. Remplace `Renderer` + `Renderer3D` |
| `debug_draw.hpp` | `DebugDraw` (REN-34) : lignes, boîtes, sphères, repères en coordonnées monde ; désactivé par défaut hors debug (chaque appel revient au premier test) ; dessiné par `SceneRenderer::drawDebug` dans la passe de scène, test de profondeur au choix (shaders `debug_line.*` chargés au premier appel) |
| `shaders/rhi/` | `mesh.vert` / `mesh.frag` (éclairage identique à l'ancien), `blit.vert` / `blit.frag` : GLSL 450 (`set`/`binding`, push constants) |

### Graphe de passes, plugins et données par image (M5 : REN-01, REN-03)

| En-tête | Rôle |
| --- | --- |
| `render_graph.hpp` | `RenderGraph` : ressources nommées (textures et tampons, importés ou transitoires), passes `kRaster` (le graphe ouvre la passe de rendu sur les attachements déclarés) ou `kGeneric` (la passe enregistre ce qu'elle veut), ordre déduit, passes inutiles écartées, cycle refusé (`compile` : kInvalidArgument avec le chemin des passes), réserve de transitoires (réutilisées d'une image à l'autre, partagées entre ressources de vies disjointes et de même description, libérées après `kTransientKeepFrames` images sans usage), une mesure du `Profiler` par passe exécutée (`RenderGraph/<passe>`, minuterie GPU si un `ProfilerGpuTimerFactory` est installé avec l'encodeur de l'image), groupe de débogage par passe (RenderDoc). Les barrières restent au RHI |
| `render_feature.hpp` | `RenderFeature` (plugin : `supports`, `prepare(FrameData)`, `declarePasses(RenderGraphBuilder&)`) et `RenderGraphBuilder` (graphe de l'image + ressources connues : `sceneColor`, `sceneDepth`, `motionVectors`, `output`, tailles ; `addSceneInput`) |
| `frame_data.hpp` | `FrameData` (docs/03 §10, std140, 640 octets, `static_assert` sur tailles et offsets), `FrameEnvironment`, `splitDouble`, `frameNoiseSeed` ; côté GLSL : `shaders/rhi/frame_data.glsl` (`#include "frame_data.glsl"`, groupe 0 binding 2) |

**Règles de dépendance** (par ressource ; l'ordre de déclaration ne compte qu'entre les accès à une même ressource) :

- les écrivains d'une ressource (`write`, `writeColor`, `writeDepth` ; une passe qui lit et écrit est un écrivain) s'exécutent dans
  leur ordre de déclaration ;
- `read` voit la version laissée par le dernier écrivain déclaré **avant** la passe, et l'écrivain suivant attend cette passe
  (écriture après lecture) ; sans écrivain déclaré avant elle, la passe voit le contenu **final** ;
- `readFinal` voit toujours le contenu final (après tous les écrivains de l'image, même déclarés plus tard) ;
- une ressource importée sans écrivain est lue telle quelle (contenu d'avant l'image).

Sont gardées : les passes à effet de bord (`sideEffect`), celles qui écrivent une ressource importée, et tout ce dont elles lisent
les données (une écriture après lecture ordonne sans garder le lecteur). À égalité, l'ordre de déclaration est conservé. Un cycle est
refusé par `compile` avec le chemin des passes.

**API en bref** (entrées et sorties d'une passe) :

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
    .writeColor(bright);         // sortie : attachement (le graphe ouvre et referme la passe de rendu)
graph.addPass("bloom.apply", PassType::kGeneric, applyFn).read(bright).write(scene); // réécrit la scène
```

Une passe `kGeneric` enregistre ce qu'elle veut (ses propres passes de rendu, compute, copies) et déclare ses accès par `read` /
`write` ; `ctx.texture(h)` / `ctx.buffer(h)` donnent la ressource GPU, `ctx.desc(h)` sa description. Les usages d'une texture
transitoire sont déduits des accès (attachement : `kRenderTarget`, lecture : `kSampled`) ; un accès en stockage ou en copie doit être
déclaré dans `RgTextureDesc::usage`. `findTexture(nom)` retrouve une ressource déclarée par une autre passe (ex. un AOV, REN-21).

**Image de `SceneRenderer`.** `endFrame` déclare, dans cet ordre : `scene` (raster : couleur, vecteurs de mouvement, profondeur ; joue
les dessins enregistrés), `post.<nom>` (un par `IPostProcessPass`), `upscale` (mode actif, ou copie 1:1), `frame-generation`,
`history` (historique en double tampon : lecture de l'image précédente dans l'un, copie de la courante dans l'autre), puis les passes
des plugins, dans l'ordre d'enregistrement. Groupe 0 de toute passe : `ctx.frame()->group` (caméra 0, lumières 1, `FrameData` 2).
Comme les dessins sont joués à `endFrame`, les tampons d'un maillage dessiné doivent rester valides jusque-là ; les constantes, le
pipeline et la transformation sont résolus à `drawMesh`, les segments de debug copiés à `drawDebug`.

**Ajouter une passe d'ombre (REN-06 à 08).** Un plugin dont `declarePasses` crée la carte (`graph.createTexture("shadow.atlas",
{.format = kD32Float, .size = ..., .usage = kRenderTarget | kSampled})`), déclare une passe `kRaster` qui l'écrit (`writeDepth`) et
dessine les casters, puis appelle `builder.addSceneInput(atlas)` : la passe d'ombre s'exécute avant `scene`, n'est jamais écartée, et
la texture (`ctx.texture(atlas)`) est liée au groupe 1 des matériaux qui la lisent. Ni le renderer ni les backends ne changent.

**Ajouter un post-traitement ou un effet HDR (REN-17 à 19, REN-21).** Un plugin qui lit `resources().sceneColor` avec `read` (et
`sceneDepth`, `motionVectors` au besoin), écrit une texture transitoire, puis une passe qui la réécrit dans `sceneColor` (`write`) :
elle s'exécute après `scene` et avant `upscale`, `frame-generation` et `history`, qui lisent la scène avec `readFinal` bien qu'elles
soient déclarées avant les plugins. Pour écrire **après** la mise à l'échelle (interface, superpositions), une passe qui écrit
`resources().output` : les écrivains de la sortie suivent leur ordre de déclaration, et les plugins sont déclarés après `upscale`.
C'est ainsi que la démo branche l'interface (`CanvasRenderer`) et la présentation (`apps/demo/src/Game/Game.cpp`,
`Game::OverlayFeature`, passes `ui` et `present`).

### Dessin 2D (B5)

| En-tête | Rôle |
| --- | --- |
| `draw_list.hpp` | `DrawList` : liste de commandes **sans dépendance graphique** (rectangle, effacement partiel, série de glyphes MSDF, image de couche, début/fin de couche, découpage), coordonnées logiques en pixels, origine en haut. C'est ce que l'interface produit (`orosmo::ui::Ui::draw`) |
| `canvas_renderer.hpp` | `CanvasRenderer` : exécute une `DrawList` sur une texture du RHI : mélange alpha (comme l'ancien) pour les rectangles et glyphes, couches à couleur **prémultipliée** composées comme telles (pas de double alpha ; fond du conteneur derrière le contenu, alpha compris), découpages imbriqués par ciseau (calculés en flottants, bornés à la surface), atlas de glyphes mis en cache par identifiant, couches hors écran (défilement, mise à jour partielle ; une couche est gardée tant qu'elle est référencée, même masquée par le découpage, ou dessinée dans une couche gardée ; libérée sinon), tampon de sommets de glyphes envoyé une fois par liste. Erreurs : taille de couche non finie ou trop grande, erreur de l'encodeur pendant le rendu, couche manquante (kNotFound : l'appelant doit redemander une liste complète) |
| `shaders/rhi/ui_*.vert/frag` | rectangle, glyphes MSDF, image de couche : GLSL 450, push constants, quad généré par `gl_VertexIndex` |

Un rectangle = un dessin : le batch par instances (R2D-01) vient en M5 (`docs/QUESTIONS.md` A21). La taille logique de la liste est indépendante de
la taille de la cible (échelle d'interface, HiDPI). (La parité avec `ui.png` de l'ancien rendu était vérifiée par `parity_tests.cpp`, supprimé avec l'interface héritée.)

La scène est **toujours** dessinée dans sa propre cible, puis copiée vers la destination (1:1 sans échelle) : l'ancien « rendu direct à
l'écran » n'existe plus, ce qui supprime la profondeur du framebuffer par défaut. Parité vérifiée en M2 par `parity_tests.cpp` (supprimé avec l'ancien terrain) ; la capture de la démo est comparée à `docs/BASELINE/reference/` par la CI.

## Règles

- **Le périphérique survit aux objets** qui le référencent (pointeur non possédé) ; la destruction des handles est différée par le RHI.
- **Erreurs** : `Result<T>`, jamais d'exception. Une recréation qui échoue (`resize`, `reconfigure`, rechargement de shader) laisse
  l'objet dans son état précédent.
- **Repère** du RHI (A26) : les octets envoyés ou lus commencent par le **haut** de l'image, comme les fichiers ; `Image` et `readPixel*` utilisent
  l'origine en **haut**, avec retournement explicite.
- **Pas d'état global, pas de singleton** : ni `GraphicsRenderState`, ni `ShaderLibrary::Instance()`.

## Limites connues

- Texture 2D seulement (pas de tableau, cube, 3D côté façade ; le RHI les offre). Un échantillonneur par texture (pas de partage).
- `GpuTimer` : une mesure à la fois ; branché sur le `Profiler` par `ProfilerGpuTimerFactory` (A25), utilisé par le graphe (une mesure par passe).
- Graphe : textures transitoires 2D seulement (pas de tableau, cube, 3D ni MSAA) ; partage de mémoire seulement entre ressources de même
  description (pas d'alias d'une texture sur la mémoire d'une autre de format différent) ; les passes s'exécutent sur un seul encodeur
  (pas de files asynchrones). `FrameData` : la position de la caméra est un `float` côté `Camera` (la scission en double ne gagne rien
  tant que la caméra ne porte pas de `double`) ; pas encore d'AOV déclarés par les plugins (`declareAov`, REN-21).
- Pas d'optimisation des shaders (SPIR-V stable) ni de cache de pipelines natifs.
