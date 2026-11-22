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
| `light.hpp` | `Light`, `LightManager` : lumières ambiante, directionnelle, ponctuelle, projecteur (toutes rendues depuis M5, voir REN-05 plus bas) ; SSBO std430 dont la disposition est vérifiée par `static_assert` (`GpuLight` 96 octets, `GpuLightHeader` 48, offsets des champs) ; le tampon grandit au besoin. `light(i)` en lecture seule, `setLight` renvoie kOutOfRange pour un index périmé |
| `mesh.hpp` | `Mesh` : sommets, index (chacun vérifié contre le nombre de sommets), instances optionnelles, attributs décrits par leurs tailles en flottants, transformation (translation, rotations X/Y/Z, échelle) et matrice des normales (inverse transposée, calculée par objet), textures liées (groupe 1), constantes de shader (push constants) |
| `fullscreen_blit.hpp` | `FullscreenBlit` : copie plein écran par triangle (le RHI n'a pas de commande de blit) ; `draw` respecte le viewport et le ciseau actifs ; `compose` pose une source prémultipliée sur la cible |
| `upscaling.hpp` | extensions de fin d'image : `IUpscaleMode` (avec `initialize`/`shutdown`), `IAdvancedUpscaleMode`, `IPostProcessPass`, `IFrameGenerationMode`, `BilinearBlitUpscaleMode` |
| `scene_renderer.hpp` | `SceneRenderer` : cible de scène, groupe 0 (caméra, lumières, `FrameData`, liaisons 3 à 7 des plugins), pré-passe de profondeur optionnelle (`setDepthPrepass`, REN-04), `beginFrame` / `setCamera` / `drawMesh` (enregistre) / `endFrame` (construit et exécute le graphe de l'image, voir plus bas), plugins (`addFeature`), échelle de rendu, jitter de Halton (seulement pour un mode exécuté ou un plugin qui le demande : `RenderFeature::wantsJitter`, REN-19), historique (les consommateurs lisent l'image PRÉCÉDENTE, `resetHistory` à la première image et après un redimensionnement), vecteurs de mouvement, profondeur lisible. Un mode de mise à l'échelle n'est exécuté que si `upscalingEnabled()` (sinon copie 1:1), il est initialisé avant d'être activé et arrêté quand il est remplacé ; un mode de génération refusé laisse l'actif en place ; `generatedFrame()` expose son image. Remplace `Renderer` + `Renderer3D` |
| `debug_draw.hpp` | `DebugDraw` (REN-34) : lignes, boîtes, sphères, repères en coordonnées monde ; désactivé par défaut hors debug (chaque appel revient au premier test) ; dessiné par `SceneRenderer::drawDebug` dans la passe de scène, test de profondeur au choix (shaders `debug_line.*` chargés au premier appel) |
| `shaders/rhi/` | `mesh.vert` / `mesh.frag` (éclairage identique à l'ancien), `blit.vert` / `blit.frag` : GLSL 450 (`set`/`binding`, push constants) |

### Graphe de passes, plugins et données par image (M5 : REN-01, REN-03)

| En-tête | Rôle |
| --- | --- |
| `render_graph.hpp` | `RenderGraph` : ressources nommées (textures et tampons, importés ou transitoires), passes `kRaster` (le graphe ouvre la passe de rendu sur les attachements déclarés) ou `kGeneric` (la passe enregistre ce qu'elle veut), ordre déduit, passes inutiles écartées, cycle refusé (`compile` : kInvalidArgument avec le chemin des passes), réserve de transitoires (réutilisées d'une image à l'autre, partagées entre ressources de vies disjointes et de même description, libérées après `kTransientKeepFrames` images sans usage), une mesure du `Profiler` par passe exécutée (`RenderGraph/<passe>`, minuterie GPU si un `ProfilerGpuTimerFactory` est installé avec l'encodeur de l'image), groupe de débogage par passe (RenderDoc). Les barrières restent au RHI |
| `render_feature.hpp` | `RenderFeature` (plugin : `supports`, `prepare(FrameData)`, `declarePasses(RenderGraphBuilder&)`) et `RenderGraphBuilder` (graphe de l'image + ressources connues : `sceneColor`, `sceneDepth`, `motionVectors`, `output`, tailles ; `addSceneInput` pour une texture ou un tampon ; `addFrameBinding` : liaison 3 à 7 ajoutée au groupe 0 de l'image) |
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

**Image de `SceneRenderer`.** `endFrame` déclare, dans cet ordre : `depth-prepass` (seulement avec `setDepthPrepass(true)`, REN-04), `scene` (raster : couleur, vecteurs de mouvement, profondeur ; joue
les dessins enregistrés), `post.<nom>` (un par `IPostProcessPass`), `upscale` (mode actif, ou copie 1:1), `frame-generation`,
`history` (historique en double tampon : lecture de l'image précédente dans l'un, copie de la courante dans l'autre), puis les passes
des plugins, dans l'ordre d'enregistrement. Groupe 0 de toute passe : `ctx.frame()->group` (caméra 0, lumières 1, `FrameData` 2).
Comme les dessins sont joués à `endFrame`, les tampons d'un maillage dessiné doivent rester valides jusque-là ; les constantes, le
pipeline et la transformation sont résolus à `drawMesh`, les segments de debug copiés à `drawDebug`.

**Ajouter une passe d'ombre (REN-06 à 08).** C'est ce que fait `ShadowFeature` (voir « Ombres » plus bas) : atlas persistant importé
(`graph.importTexture`), passes `kGeneric` qui l'écrivent, puis `builder.addSceneInput(atlas)` : elles s'exécutent avant `scene` et ne
sont jamais écartées ; l'atlas est lié au groupe 1 des matériaux qui le lisent. Ni le graphe, ni le renderer, ni les backends n'ont changé.

**Ajouter un post-traitement ou un effet HDR (REN-17 à 19, REN-21 ; exemples réels : section « HDR et post-traitements »).** Un plugin qui lit `resources().sceneColor` avec `read` (et
`sceneDepth`, `motionVectors` au besoin), écrit une texture transitoire, puis une passe qui la réécrit dans `sceneColor` (`write`) :
elle s'exécute après `scene` et avant `upscale`, `frame-generation` et `history`, qui lisent la scène avec `readFinal` bien qu'elles
soient déclarées avant les plugins. Pour écrire **après** la mise à l'échelle (interface, superpositions), une passe qui écrit
`resources().output` : les écrivains de la sortie suivent leur ordre de déclaration, et les plugins sont déclarés après `upscale`.
C'est ainsi que la démo branche l'interface (`CanvasRenderer`) et la présentation (`apps/demo/src/Game/Game.cpp`,
`Game::OverlayFeature`, passes `ui` et `present`).

### Sorties auxiliaires et modes de debug (M5 : REN-21, REN-41, REN-35)

| En-tête | Rôle |
| --- | --- |
| `aov.hpp` | `Aov` (mouvement RG16F, normales octaédriques RG16F, rugosité/métal RG8, identifiant R32Uint, profondeur linéaire R32F, émission RGBA16F, albédo RGBA8), `AovSet`, `AovLayout` (couleur 0 puis un attachement par AOV actif, dans l'ordre de l'énumération), `aovShaderDefines`, `AovMaterial` / `aovWrites` (REN-41), `aovColorTargets` |
| `debug_view.hpp` | `DebugView` (none, normals, albedo, overdraw, lod, wireframe, motion), `lodDebugColor`, `FrustumFreeze` |
| `shaders/rhi/aov.glsl`, `debug_view*.frag` | sorties GLSL (macros `OROSMO_AOV_*`) ; visualisations |

**À la demande.** `SceneRenderer::declareAov(aov)`, `RenderFeature::requiredAovs()`, `setMotionVectorsEnabled` ou un mode de debug ajoutent des
cibles MRT à la passe de scène (`activeAovs()`, `aovLayout()`). Sans demande, la cible de scène n'a que sa couleur : aucune cible de plus,
aucun coût, l'image de la démo est identique au pixel près (test `renderer/aov`, « nothing extra is allocated »). Un plugin lit un AOV par
`RenderGraphBuilder::aov(Aov)` ; les AOV sont effacés à leur valeur « rien » (identifiant 0, normales -1). Le programme de maillage s'écrit pour les
AOV actifs en le chargeant avec `SceneRenderer::aovShaderDefines()` (`mesh.vert/frag` les gèrent ; un programme sans les sorties n'écrit que la couleur,
ses AOV gardent la valeur d'effacement). `Mesh::setObjectId` donne l'identifiant.

**Mouvement.** `(NDC courant - NDC précédent) / 2`, jitter retiré. Caméra : `FrameData::prevViewProj`. Objets : le maillage garde sa matrice de l'image précédente
(`ModelBlock::prevModel`, suivie seulement si l'AOV est actif), donc le mouvement des objets mobiles est exact ; le rendu relatif à la caméra est géré.

**Résolution MSAA** (la scène est aujourd'hui mono-échantillon ; règle fixée par `aovResolveRule`) : moyenne pour mouvement, matériau, émission, albédo ;
normales décodées, moyennées, renormalisées ; profondeur linéaire : échantillon le plus proche ; identifiants : échantillon le plus proche, JAMAIS moyennés.

**Transparents (REN-41).** `Mesh::setAovMaterial({BlendMode, AovSet})` : un mode transparent (`isTransparent`) n'écrit AUCUN AOV par défaut (masque d'écriture nul) ;
`request` liste ceux qu'il veut écrire. Le mélange et l'absence d'écriture de profondeur du mode sont appliqués au pipeline.

**Debug (REN-35).** `setDebugView` : `normals` / `motion` (AOV + passe `debug.view` sur la couleur), `albedo` / `overdraw` / `wireframe` (programme de remplacement
des maillages `drawMesh`), `lod` (`VisibilityOptions::lodColors` : couleur de l'instance = son niveau), gel du frustum (`FrustumFreeze`, `drawFrozen`).
Démo : `--debug-view none|normals|albedo|overdraw|lod|wireframe|motion` (défaut none), `--freeze-frustum` (avec `--instances N`).
Partiel : les modes dépendant des cascades d'ombre et des clusters forward+ (autres agents) ne sont pas là. Limites : les objets du `InstanceRenderer`
(programme propre) n'écrivent pas encore d'AOV et ignorent `albedo`/`overdraw`/`normals` (seul `lod` les concerne) ; la profondeur linéaire et la rugosité/métal/émission sont celles du matériau par défaut (pas de PBR avant REN-09).

### HDR et post-traitements (M5 : REN-17, REN-18)

Chaque effet est un **plugin du graphe** (`RenderFeature`), activable à l'exécution (`setEnabled` ; désactivé, il ne déclare aucune
passe). Chaque passe déclare ses entrées (`read`) et sa sortie (`writeColor` / `write`) ; l'ordre entre effets est leur ordre
d'enregistrement. Tout s'exécute à la résolution de rendu, entre `scene` et `upscale`.

| En-tête | Rôle |
| --- | --- |
| `tonemap.hpp` | références CPU (mêmes formules que `shaders/rhi/post/post_common.glsl`, comparées par les tests) : `Tonemapper` (`kClamp`, `kAces` ajustement de Hill, `kAgx` approximation de Wrensch, `kReinhardExtended` sur la luminance), `tonemap`, `linearToSrgb` / `srgbToLinear`, `histogramBin` (256 cases log2, case 0 = trop sombre), `histogramAverageLuminance`, `targetExposure`, `adaptationFactor`, `vignetteFactor`, `ColorLut` / `identityLut` / `sampleLut` / `parseCubeLut` (.cube) |
| `post_effects.hpp` | `PostContext` (shaders, copie plein écran, échantillonneurs partagés), `PostEffect` (base), les cinq effets, `PostEffects` et `addPostEffects` |

| Effet (nom du plugin) | Passes | Entrées → sorties |
| --- | --- | --- |
| `AutoExposureFeature` (`post.auto-exposure`, exige `Capabilities::compute`) | `post.auto-exposure` (générique : 3 dispatchs, remise à zéro, histogramme par tuiles 16×16 en mémoire partagée, moyenne géométrique + exposition visée `key / moyenne` bornée + adaptation `1 - exp(-dt × vitesse)`) | scène → tampons importés `post.histogram` (256 uint) et `post.exposure` (vec4 : exposition, moyenne, visée, valide), persistants (adaptation) ; `readHistogram` / `readExposure` / `resetAdaptation` |
| `BloomFeature` (`post.bloom`) | `post.bloom.down<i>` (13 échantillons ; seuil doux et borne anti-lucioles au premier), `post.bloom.up<i>` (tente 3×3, additif), `post.bloom.composite` (additif sur la scène, alpha intact) | scène → transitoires `post.bloom.<i>` (RGBA16F, 1/2 à 1/2^n) → scène |
| `TonemapFeature` (`post.tonemap`) | `post.tonemap`, `post.tonemap.copy` | scène (+ `post.exposure` s'il est dans le graphe) → transitoire `post.tonemap.out` → scène. Exposition = `FrameData::exposure.x` (`FrameEnvironment::exposure`) × 2^`exposureEv` × exposition automatique ; opérateur ; sRGB en sortie (`encodeSrgb`) |
| `ColorGradingFeature` (`post.color-grading`) | `post.color-grading`, `post.color-grading.copy` | scène + LUT 3D (RGBA16F, trilinéaire aux centres des texels, identité 16³ au départ, `setLut`, `setStrength`) → `post.color-grading.out` → scène |
| `VignetteFeature` (`post.vignette`) | `post.vignette` (mélange multiplicatif, alpha intact) | scène (`writeColor` en `kLoad`) |

**Cible HDR.** `SceneRenderer::setSceneColorFormat(kRGBA16Float)` (ou kRGBA32Float) ; défaut kRGBA8Unorm. `PostEffects::sync(renderer)`
choisit RGBA16F seulement si un effet HDR est actif (exposition automatique, bloom, tonemapping ; la LUT et le vignettage marchent en
LDR) : sans effet, la scène, le graphe et l'image restent ceux d'avant (vérifié au pixel près sur la démo). La capture de la scène
(`saveColorPng`) lit aussi RGBA16F (valeurs bornées à [0, 1]).

```cpp
auto fx = addPostEffects(renderer, device, library, pipelines, shaderDir); // cinq effets enregistrés, désactivés
fx->tonemap->setEnabled(true);                                             // ACES par défaut, sRGB en sortie
fx->tonemap->settings().tonemapper = Tonemapper::kAgx;
fx->bloom->setEnabled(true);
fx->sync(renderer);                                                        // scène RGBA16F au prochain beginFrame
```

Avec les plugins de la scène (transparents `TransparentFeature`, 2D monde `Layer2DFeature` en `kWorld`), enregistrer les effets
**après** eux (ils lisent la scène que ces plugins complètent) et **avant** les superpositions qui écrivent `output` (interface, 2D
`kOverlay`) : c'est l'ordre de docs/03 §11 (3D opaque → transparents → 2D monde → post-traitement → 2D en superposition).

Démo : `apps/demo/bin/debug/main --post none|hdr|bloom|full` (défaut `none`) ; `hdr` = tonemapping ACES, `bloom` = + bloom, `full`
= + exposition automatique, LUT chaude calculée, vignettage. Captures de référence : `docs/BASELINE/reference/scene-opengl-post-*.png`.
Tests : `tests/renderer/post_effects_tests.cpp` (suites `post/math` sans GPU et `renderer/post` sur chaque backend).

Limites : les couleurs des matériaux de la démo ne sont pas linéarisées (sRGB traités comme linéaires) : l'image HDR est donc plus
claire que l'image LDR ; chaque effet qui relit la scène y recopie sa sortie (une copie plein écran par effet, pas de ping-pong) ;
l'exposition automatique mesure toute l'image (pas de pondération centrale) ; pas de LUT d'étalonnage HDR (log) ni de sortie HDR
(écran PQ/scRGB) ; l'anticrénelage (REN-19) est décrit plus bas. Les AOV (REN-21) restent corrects avec la scène RGBA16F : seul l'attachement 0 change de format ; la pré-passe forward+ n'écrit aucun AOV.

### Anticrénelage (M5 : REN-19)

Trois plugins du graphe (`antialiasing.hpp`, `src/renderer/post/antialiasing.cpp` et `taa.cpp`, shaders `shaders/rhi/post/fxaa.frag`,
`smaa_*.frag`, `taa_resolve.frag`), dérivés de `PostEffect` (même `PostContext`, activables par `setEnabled`, aucune passe une fois
désactivés). `AntiAliasing::select(mode)` active l'un et désactive les autres.

| Plugin | Passes | Entrées → sorties |
| --- | --- | --- |
| `TaaFeature` (`aa.taa`) | `aa.taa.resolve` (raster, deux sorties), `aa.taa.copy` | scène + AOV `motion` et `linear-depth` (`requiredAovs`) + historique importé `aa.taa.history` / `aa.taa.info` (image précédente) → historique suivant (RGBA16F : couleur ; RGBA32F : profondeur, mouvement, images accumulées) → scène |
| `FxaaFeature` (`aa.fxaa`) | `aa.fxaa`, `aa.fxaa.copy` | scène (échantillonnage linéaire) → `aa.fxaa.out` → scène |
| `SmaaFeature` (`aa.smaa`) | `aa.smaa.edges`, `aa.smaa.weights`, `aa.smaa.blend`, `aa.smaa.copy` | scène → `aa.smaa.edges` (RG8) → `aa.smaa.weights` (RGBA8) → `aa.smaa.out` (+ scène) → scène |

**Ordre** (test « graph order ») : `scene` (+ transparents, 2D monde) → `aa.taa.*` → `post.auto-exposure` → `post.bloom.*` →
`post.tonemap` → `post.color-grading` → `aa.fxaa` / `aa.smaa.*` → `upscale` → superpositions. Choix retenu : le TAA travaille en
HDR **avant** l'exposition et le tonemapping (il accumule des valeurs de scène ; poids anti-lucioles `c / (1 + L)`, réversible) ;
FXAA et SMAA travaillent sur les valeurs affichées (contraste perceptuel). L'ordre entre plugins étant celui d'enregistrement :
`addTemporalAntiAliasing` avant `addPostEffects`, `addEdgeAntiAliasing` après (puis les superpositions). Tout s'exécute à la
résolution de rendu ; la mise à l'échelle suit.

```cpp
auto context = PostContext::create(device, library, pipelines, shaderDir);
if (!context) return std::unexpected(context.error());
auto taa = addTemporalAntiAliasing(renderer, *context);         // après les plugins de la scène
if (!taa) return std::unexpected(taa.error());
auto fx = addPostEffects(renderer, device, library, pipelines, shaderDir);
if (!fx) return std::unexpected(fx.error());
auto edge = addEdgeAntiAliasing(renderer, *context);
if (!edge) return std::unexpected(edge.error());
AntiAliasing aa = *edge;
aa.taa = *taa;
aa.select(AntiAliasingMode::kTaa);                               // ou kSmaa, kFxaa, kNone
// Programmes de maillage compilés avec renderer.aovShaderDefines() APRÈS l'enregistrement du TAA (mouvement, profondeur).
```

**TAA.** Jitter : la suite de Halton(2, 3) de `SceneRenderer` (16 positions, `setJitterSequenceLength`), demandée par
`TaaFeature::wantsJitter` (vrai seulement s'il est actif ; sinon, aucun jitter : image inchangée). Résolution : voisinage 3×3 en
YCoCg (couleurs compressées), mouvement du texel le plus proche (dilatation), reprojection `uv - mouvement`, rejet si aucune
profondeur de l'historique (3×3 autour du point reprojeté) n'est à moins de `depthTolerance` (10 %) de celle du pixel
(désocclusion, coupure de scène) ou si le mouvement diffère de plus de `motionTolerancePixels` (rejet progressif), écrêtage
par la variance (`clipGamma`, ∩ min/max), poids de l'image courante `1 / n` (n ≤ `maxAccumulatedFrames` = 12, remis à 1 au
rejet). Historique propre au plugin (RGBA16F, double tampon) ; il est ignoré à la première image, après `resetHistory()`, un
changement de taille ou une image sautée (plugin désactivé). Celui de `SceneRenderer` (RGBA8, après les post-traitements, pour
les modes de mise à l'échelle) n'est pas utilisé : il ne convient pas à une accumulation HDR.

**SMAA 1x (Partiel).** Détection des contours de luminance (seuil 0,1, adaptation au contraste local × 2), poids de mélange,
mélange du voisinage, comme la référence (Jimenez et al., 2012). Différences : la recherche le long des contours lit les texels un
par un (16 pas de chaque côté, pas de SearchTex) et l'aire couverte par la ligne de reconstruction (formes L, Z, U : hauteur ±1/2
aux extrémités, 0 au milieu) est calculée dans le shader, par la construction géométrique qui produit AreaTex : aucune table n'est
embarquée (ni provenance ni licence de données). Absents : diagonales, arrondi des coins, seuil prédiqué, T2x/S2x/4x.

**FXAA.** Algorithme « qualité » de FXAA 3.11 (T. Lottes), réécrit d'après sa description (aucun code de NVIDIA copié).

Démo : `apps/demo/bin/debug/main --aa none|fxaa|smaa|taa` (défaut `none` : aucun plugin, captures de référence inchangées) ;
combinable avec `--post`. Tests : `tests/renderer/antialiasing_tests.cpp` (suites `aa/names` et `renderer/aa`, chaque backend) :
arête en escalier lissée par FXAA et SMAA (horizontale et verticale, pixels intermédiaires comptés, zones unies intactes), ordre du
graphe, convergence du TAA sur une image fixe (écart moyen entre images successives divisé par plus de 2) et arêtes lissées, rejet
de l'historique sur une coupure (profondeur) et sur `resetHistory`, traînée derrière un objet en mouvement bornée (au plus 4 pixels
à plus de 0,1 du fond hors de l'objet dilaté de 2 pixels ; mesuré : voir le rapport du test).

Limites : **MSAA absent** (le RHI n'a ni nombre d'échantillons dans `TextureDesc` / `GraphicsPipelineDesc`, ni résolution de cible ;
la règle de résolution des AOV, `aovResolveRule`, est prête). Le TAA ne reçoit pas de mouvement des objets qui n'écrivent pas
d'AOV (`InstanceRenderer`, transparents : ils prennent le mouvement de ce qui est derrière eux, l'écrêtage limite la traînée),
l'historique est échantillonné en bilinéaire (léger flou en mouvement, pas de Catmull-Rom ni d'accentuation), et l'image courante
n'est pas « dé-jitterée ». La résolution dynamique n'est pas pilotée : `setRenderScale` + la mise à l'échelle bilinéaire existent,
le TAA s'exécute à la résolution de rendu, mais aucun contrôleur ne règle l'échelle d'après le temps GPU.

### Dessin 2D (B5)

| En-tête | Rôle |
| --- | --- |
| `draw_list.hpp` | `DrawList` : liste de commandes **sans dépendance graphique** (rectangle, effacement partiel, série de glyphes MSDF, image de couche, début/fin de couche, découpage), coordonnées logiques en pixels, origine en haut. C'est ce que l'interface produit (`orosmo::ui::Ui::draw`) |
| `canvas_renderer.hpp` | `CanvasRenderer` : exécute une `DrawList` sur une texture du RHI : mélange alpha (comme l'ancien) pour les rectangles et glyphes, couches à couleur **prémultipliée** composées comme telles (pas de double alpha ; fond du conteneur derrière le contenu, alpha compris), découpages imbriqués par ciseau (calculés en flottants, bornés à la surface), atlas de glyphes mis en cache par identifiant, couches hors écran (défilement, mise à jour partielle ; une couche est gardée tant qu'elle est référencée, même masquée par le découpage, ou dessinée dans une couche gardée ; libérée sinon), un seul tampon d'instances et de sommets envoyé par liste (voir « Lots »). Erreurs : taille de couche non finie ou trop grande, erreur de l'encodeur pendant le rendu, couche manquante (kNotFound : l'appelant doit redemander une liste complète) |
| `shaders/rhi/ui_*.vert/frag` | rectangle (**instancié** : quad par `gl_VertexIndex`, données en attributs par instance), glyphes MSDF et images (sommets x, y, u, v + couleur ou teinte par sommet), image de couche (quad par `gl_VertexIndex`) : GLSL 450 |

**Lots (R2D-01, UI-02).** `render` regroupe les dessins avant de les exécuter. Entre deux changements d'état (`PushClip`, `PopClip`,
`BeginLayer`, `EndLayer`, `DrawLayerImage`, qui coupent le segment), les rectangles (et les effacements partiels) deviennent **un dessin
instancié**, et les séries de glyphes (même atlas, mêmes réglages MSDF) ou d'images (même image) **un seul dessin**, couleur et teinte étant des attributs
de sommet. Un dessin peut rejoindre un lot compatible plus ancien du segment seulement s'il ne recouvre (boîte englobante, intérieurs stricts)
aucun dessin des lots qu'il dépasse : l'ordre de recouvrement, donc le résultat, est celui de la liste. Le tampon (16 flottants par rectangle,
8 par sommet) est envoyé une fois par liste. `CanvasRenderer::stats()` donne, pour le dernier rendu, `drawCommands`, `batchedCommands` (dessins SANS lots) et `drawCalls`.
Limites : une couche affichée (`DrawLayerImage`) reste un dessin chacune ; un tri par matériau à `z` égal n'est pas fait (le dépassement ci-dessus s'y substitue). La taille logique de la liste est indépendante de
la taille de la cible (échelle d'interface, HiDPI). (La parité avec `ui.png` de l'ancien rendu était vérifiée par `parity_tests.cpp`, supprimé avec l'interface héritée.)

### Dessin 2D du monde (R2D-01, R2D-02, R2D-03)

| En-tête | Rôle |
| --- | --- |
| `camera2d.hpp` | `Camera2D` : orthographique, monde Y vers le haut, écran Y vers le bas, `zoom` = pixels par unité, rotation, `worldToScreen` / `screenToWorld` / `visibleBounds`. **Pixel parfait** : échelle entière (au moins 1), centre aligné sur la grille, rotation ignorée |
| `renderer2d.hpp` | `Renderer2D` : `material(texture)`, `begin(camera)`, `draw(Sprite)`, `line` / `polyline` / `rect` / `rectOutline` / `circle` / `circleOutline` / `polygon` (découpage en oreilles, concave accepté), `render(encoder, cible, ...)`. Tri par `z` croissant, puis type, puis matériau : **un appel de dessin par (z, type, matériau)** ; sprites instanciés (un quad par instance), formes en triangles colorés. Pixel parfait : filtrage au plus proche et sprites non tournés alignés sur les pixels |
| `shaders/rhi/sprite2d.*`, `shape2d.*` | GLSL 450 des sprites et des formes |

Texte : il passe par le chemin existant (`ui::TextRenderer` -> `DrawList` -> `CanvasRenderer::render` sur la même cible, après `Renderer2D::render`). Limites : à `z` égal les matériaux peuvent s'intercaler (donner des `z` différents pour un ordre précis) ; atlas = une texture par matériau (pas de tableau de textures) ; contours de formes sans anticrénelage ; le mélange 2D/3D (R2D-05) et plusieurs vues (R2D-06) sont décrits plus bas.

La scène est **toujours** dessinée dans sa propre cible, puis copiée vers la destination (1:1 sans échelle) : l'ancien « rendu direct à
l'écran » n'existe plus, ce qui supprime la profondeur du framebuffer par défaut. Parité vérifiée en M2 par `parity_tests.cpp` (supprimé avec l'ancien terrain) ; la capture de la démo est comparée à `docs/BASELINE/reference/` par la CI.

### Instancing, culling et LOD (M5 : REN-28, REN-30, REN-31)

Objets **génériques** (aucune notion de terrain) : un objet = des niveaux de détail (identifiants de maillage), un matériau, une boîte
locale, une transformation, une couleur. Tout ce qui se décide sur le CPU est testable sans GPU (suites `visibility/*`).

| En-tête | Rôle |
| --- | --- |
| `culling.hpp` | REN-30 : `Aabb` (fusion, transformation d'Arvo), `Frustum::fromMatrix` (Gribb-Hartmann, profondeur [0, 1] ; le plan lointain d'une projection reverse-Z infinie est omis), `CullView` (pyramide, œil, échelle de projection, `minScreenRadius` en pixels pour les petits objets, `maxDistance`), `projectedRadiusPixels`, `Bvh` (coupure médiane déterministe, `refit` pour les objets mobiles, `query` : un nœud entièrement dedans ou dehors est réglé en bloc), `cullLinear` (référence) |
| `lod.hpp` | REN-31 : `LodSettings` (distances de bascule, largeur de la bande de fondu, distance de rejet, biais), `selectLod` : un niveau, ou deux niveaux voisins en fondu avec des seuils de tramage `f` et `-f` qui couvrent chaque pixel **exactement une fois** (pas de trou, pas de double couverture ; mode `Dithered` de `docs/03`). `ditherKeeps` reproduit la règle du shader |
| `instance_batch.hpp` | REN-28 : `GpuInstance` (128 octets : modèle, matrice des normales calculée une fois par instance, seuil de tramage, couleur), `InstanceBatchBuilder` : tri stable par clé (maillage, matériau), un lot = une clé |
| `object_set.hpp` | `ObjectSet` : objets (identifiants à génération), réglages de LOD partagés ; `prepare(view, out)` = culling BVH (reconstruite après ajout/retrait, réajustée après déplacement) + LOD + regroupement ; `VisibilityStats` |
| `instance_renderer.hpp` | `InstanceRenderer` : maillages et matériaux enregistrés par identifiant ; `draw(scene, batches)` envoie toutes les instances de l'image dans **un** tampon de sommets à pas d'instance et enregistre **un `drawIndexed` par lot** (`firstInstance` = début du lot) dans `SceneRenderer` (`drawCustom`), joué dans la passe « scene » du graphe à sa place parmi les dessins, donc avant les post-traitements et avec la profondeur de la scène ; rendu relatif à la caméra (translations retranchées à l'envoi), fil de fer, `InstanceDrawStats` |
| `shaders/rhi/mesh_instanced.vert` | instance aux emplacements 8 à 15 ; à associer à `mesh.frag` compilé avec `OROSMO_INSTANCED` (tramage de Bayer 4×4 du LOD ; même éclairage que `mesh.frag`) |

Seul ajout à `SceneRenderer` : `sceneDrawInfo()` (formats de la cible, origine du rendu relatif à la caméra) et `drawCustom(rappel)`, un dessin externe enregistré et joué dans la passe « scene » (le rappel reçoit l'encodeur et le groupe 0).

Démo : `apps/demo/bin/debug/main --instances 10000` (défaut 0 : scène et captures inchangées) ; les statistiques sont journalisées.
Mesure (1280×720, vue par défaut) : 10 000 objets → 3 717 visibles (6 283 hors champ), 125 en fondu de LOD, **3 842 instances en
4 draw calls** (2 LOD × 2 matériaux) ; sans culling : 10 000 instances. Test BVH (10 000 boîtes) : 515 nœuds + 279 tests d'objets au lieu
de 10 000 tests linéaires.

Limites : un seul `InstanceRenderer::draw` par image et par instance de la classe (le tampon d'instances est réécrit à chaque appel) ;
imposteurs : n'importe quel maillage (un quad) peut servir de dernier niveau, mais pas de génération automatique (atlas, orientation
vers la caméra) ; un fondu croisé qui chevauche la bande de rejet n'est pas combiné (seul le dernier niveau s'efface) ; le tramage
désactive l'early-Z des fragments concernés. Le culling GPU et l'occlusion Hi-Z sont dans « Rendu piloté par le GPU » (REN-29).

### Rendu piloté par le GPU (M5 : REN-29)

| En-tête | Rôle |
| --- | --- |
| `object_set.hpp` | ajout : suivi des changements par emplacement (`changeCounter`, `slotChange`, `objectAtSlot`, `slotCount`) pour les miroirs |
| `gpu_scene.hpp` | `GpuScene` : **scène GPU persistante**, miroir d'un `ObjectSet` (un `GpuSceneObject` de 208 octets par emplacement : modèle, matrice des normales, couleur, boîte monde, LOD, matériau). `collect` (CPU, testable sans GPU) ne relit que les emplacements modifiés depuis la synchronisation précédente et les regroupe en plages (écart ≤ `kMergeGap`) ; `sync` envoie ces plages seulement (tout au premier envoi, sur croissance du tampon ou autre ensemble). `keyReferences` : nombre maximal d'instances par clé (maillage, matériau) |
| `gpu_driven.hpp` | `GpuDrivenFeature` (plugin « gpu-driven ») : `draw(scene, objects, GpuDrivenView, options)` synchronise la scène GPU et enregistre les dessins indirects ; `setOcclusion` / `resetOcclusion` ; lectures `readStats` (`GpuCullStats`), `readVisibility` (drapeaux par emplacement), `readCommandCounts`, `readHiz`. `enableGpuDriven` / `disableGpuDriven` |
| `shaders/rhi/gpu_cull.comp` | un fil par emplacement : pyramide, distance, taille à l'écran (mêmes formules que `culling.cpp`), PUIS occlusion Hi-Z (variante `OROSMO_HIZ`), puis `selectLod` (même code que `lod.cpp`) ; ajout atomique à la commande de la clé, instance écrite au format `GpuInstance` dans la plage de la commande ; compteurs en mémoire partagée |
| `shaders/rhi/hiz_build.comp` | Hi-Z R32F, chaîne de mips complète : niveau 0 = profondeur de la scène (`OROSMO_HIZ_FROM_DEPTH`), niveau i = minimum (le plus loin en reverse-Z) des texels couverts du niveau i - 1, bords impairs compris |

**Image** : `gpudriven.cull` (compute : copie des commandes modèles, remise à zéro des compteurs, copies des pools de géométrie si
besoin, un dispatch ; `addSceneInput` des commandes et des instances) -> `scene` (par (matériau, pool de géométrie) non vide, UN
`drawIndexedIndirect` à plusieurs commandes : multi-draw indirect ; mêmes programmes, matériaux, push constants et format d'instance
qu'`InstanceRenderer`, donc même image) -> `gpudriven.hiz` (compute, lit la profondeur finale de la scène). Commande = (maillage,
matériau), indice `matériau × maillages + rang du maillage` ; les maillages de même disposition sont copiés (copie GPU) dans un pool
commun, rangés par pool. La plage de chaque commande vaut `keyReferences` : elle ne déborde jamais (`overflow` compté et annulé sinon).

**Capacités** (règle 11) : `Capabilities::compute`, `indirectDraw`, 7 emplacements de stockage ; sinon `enableGpuDriven` renvoie
`kUnsupported` et la démo garde le culling CPU (`ObjectSet::prepare` + `InstanceRenderer::draw`). Sans `multiDrawIndirect`, le RHI
rejoue les commandes une par une.

Tests (`renderer/gpu-driven`, OpenGL et Vulkan, validation à 0) : scène GPU (plages modifiées seulement), capacités, **ensemble visible
identique au culling CPU** (2 000 objets : pyramide, petits objets, fondus et rejets de LOD, instances par clé), **image identique**
à `InstanceRenderer`, occlusion (5 objets cachés derrière un mur rejetés à l'image 2, aucun visible rejeté ; `resetOcclusion`).

Démo : `apps/demo/bin/debug/main --instances 100000 --gpu-driven on` (défaut off : captures inchangées) ; le journal compare
« GPU-driven » et « CPU culling » (visibles, instances, dessins), le Profiler mesure `RenderGraph/gpudriven.cull`,
`RenderGraph/gpudriven.hiz` et `RenderGraph/scene` (journal « Perf: »).
Mesure (headless 1280×720, vue par défaut, Intel Arc MTL / Mesa, build debug, 30 images) : 100 000 objets -> **30 215 dans la vue
sur le GPU comme sur le CPU**, 2 rejetés par la Hi-Z, 30 338 instances en 4 commandes non vides (2 appels multi-draw) contre 30 340
instances en 4 draw calls pour le CPU (les 2 objets occultés). Temps GPU moyens : OpenGL cull 5,5 ms, hiz 0,17 ms, scene 5,2 ms (CPU :
scene 9,2 ms) ; Vulkan cull 3,0 ms, hiz 1,4 ms, scene 4,7 ms (CPU : scene 6,0 ms). Validation Vulkan : 0 message.

Limites : **occlusion de l'image précédente** (Hi-Z construite après la scène de l'image N, testée à l'image N + 1 avec la
vue-projection de N) : un objet qui se découvre (caméra qui contourne un mur, occulteur qui bouge) manque UNE image (pas de seconde
passe « two-phase ») ; pas d'occlusion à la première image, après un redimensionnement ou `resetOcclusion` ; un coin derrière la
caméra ou une boîte hors de l'image précédente n'est jamais rejeté. Les occulteurs sont tout ce qui écrit la profondeur de la scène (les
dessins `drawMesh` et les instances GPU elles-mêmes). Plus de `kGpuSceneMaxLevels` (8) LOD : objet ignoré par le GPU ; ≤ 65 535 × 64
emplacements par dispatch ; une seule `draw` par image ; les pools sont copiés à l'enregistrement (`Mesh::updateVertices` ensuite n'est
pas vu). Le RHI n'a pas de `drawIndexedIndirectCount` (compte lu sur le GPU) : toutes les commandes (même vides) sont jouées ; Vulkan
exige `drawIndirectFirstInstance` (G145), non exposé dans `Capabilities`. Les ombres (`ShadowFeature::addObjectCasters`) gardent le
culling CPU.
**AOV et debug (REN-21, REN-35)** : comme `InstanceRenderer`, les dessins GPU n'écrivent que la couleur (masque d'écriture nul
sur les autres attachements : les AOV gardent leur valeur d'effacement) ; `--debug-view lod` n'a pas de couleurs de LOD sur le
chemin GPU ; `--freeze-frustum` gèle aussi la vue du culling GPU (la Hi-Z garde la vue réelle de la caméra). Anticrénelage (REN-19) : le TAA lit
le mouvement et la profondeur ; les objets GPU écrivent la profondeur mais pas l'AOV de mouvement (valeur d'effacement : mouvement
nul, comme `InstanceRenderer`), donc leur reprojection suppose des objets fixes ; un objet déplacé par `setTransform` peut laisser une
traînée pendant le TAA.

### Ombres (M5 : REN-06, REN-07, REN-08)

| En-tête | Rôle |
| --- | --- |
| `shadows.hpp` | sans GPU : `LightMobility`, `ShadowMode`, `CasterMobility`, `resolveShadowBehavior` (tableau ci-dessous), `cascadeSplits` (schéma pratique), `cameraShapeFrom` (forme de la caméra depuis `FrameData`, jitter ignoré), `fitCascade` (sphère de rayon constant + ancrage sur la grille de texels), `fitSpot`, `atlasTileMatrix`, `ShadowAtlasAllocator` (tuiles carrées en puissances de deux, « buddy », déterministe), `ShadowHash` (FNV-1a) |
| `shadow_feature.hpp` | `ShadowFeature` (plugin `RenderFeature` « shadows ») : `setLights`, `setLightParams` (priorité, résolution, plans et fondu d'un projecteur), `ShadowSettings` (taille d'atlas, cascades, portée, amortissement, budget, PCF, biais), casters `addMeshCaster(mesh, boîte, mobilité)` et `addObjectCasters(ObjectSet, InstanceRenderer, mobilité)`, `receiverGroup` / `appendReceiverBindings`, `receiverProgram` / `instancedReceiverProgram`, `stats`, `lightStatus`, `gpuData` |
| `shaders/rhi/shadow_receiver.glsl` | `orosmoLightShadow(index, position, normale, vers la lumière, œil)` : choix de la cascade (la plus fine qui contient le point, PCF compris), décalage normal, biais de pente en mètres, `shadowPcf` (REN-22), fondu ; projecteurs : profondeur recalculée depuis la distance. Groupe 1, bindings 6 (atlas, échantillonneur de comparaison) et 7 (`ShadowGpuData`, std140, vérifié par `static_assert`) |
| `shaders/rhi/shadow_caster*.vert`, `shadow_caster.frag`, `shadow_tile.*` | casters (maillage : clip × modèle en push constants ; instancié : `GpuInstance` aux emplacements 8 à 11) ; effacement et recopie d'une tuile (le RHI n'a pas de copie de texture à texture : `gl_FragDepth`) |

`Light` porte `mobility` (défaut `kDynamic`) et `shadowMode` (défaut `kNone` : aucune lumière existante n'a d'ombre sans le demander).
`mesh.frag` multiplie la lumière directionnelle ET les projecteurs (`lightContribution`, donc en forward comme en forward+) par
`orosmoLightShadow` ; la pré-passe de profondeur (premier entier des push constants = 2) ne calcule aucun éclairage, donc ne lit pas les
ombres ; sans `OROSMO_SHADOWS` la fonction rend 1 (image identique au
pixel près, vérifié sur la capture de la démo).

**Comportement de chaque combinaison** (`resolveShadowBehavior`, testé sur CPU et sur GPU pour les 12 combinaisons) :

| Mobilité \ mode | `None` | `Baked` | `Dynamic` | `Cached` |
| --- | --- | --- | --- | --- |
| `Static` (rien ne change) | pas d'ombre | pas de cuisson (REN-16 absent) : **repli `Cached`**, statut `kBakedFallback` | toutes les casters redessinées à chaque mise à jour | fixe en cache, mobiles par-dessus |
| `Stationary` (couleur seule) | pas d'ombre | repli `Cached`, `kBakedFallback` | idem | idem |
| `Dynamic` (tout) | pas d'ombre | combinaison invalide : **repli `Dynamic`**, `kInvalidCombination` | idem | cache invalidé à chaque changement de la lumière |

Une lumière `Static` qui bouge ou change de couleur, ou `Stationary` qui bouge, est une **violation** : comptée
(`ShadowStats::mobilityViolations`), avertie dans le journal, et ses vues sont redessinées (l'ombre reste juste). Une lumière ponctuelle
est `kUnsupportedType` (cubemaps : REN-09), comme une directionnelle au-delà de la première ombrée.

**Cascades (REN-07).** Une seule lumière directionnelle ombrée (la première), 1 à 4 cascades sur `[proche, maxDistance]`. Stabilité :
le rayon de la sphère d'une tranche ne dépend que de la tranche et du champ (pas de l'orientation), le centre et la profondeur sont
ancrés sur la grille de texels dans le repère de la lumière : un déplacement de la caméra décale la projection d'un nombre entier de
texels, un déplacement sous-texel ne change pas la matrice (test `fitCascade`). Les matrices sont calculées dans le monde puis ramenées
au repère des shaders (rendu relatif à la caméra compris). Fondu sur la dernière fraction (`fadeFraction`) de `maxDistance`.

**Atlas, budget, cache (REN-08).** Un atlas de profondeur (`atlasSize`²) et un cache de même taille. Chaque vue (cascade ou projecteur)
demande une tuile : servies par priorité de la lumière, directionnelle d'abord, cascades proches d'abord ; une vue moins prioritaire est
évincée au besoin, puis la taille est divisée par deux jusqu'à `minTileSize`, sinon la vue est abandonnée (`dropped`, et `kAtlasFull` si la
lumière n'a plus aucune vue). Projecteurs : résolution divisée par deux à chaque doublement de la distance au-delà de la portée, au plus
`maxSpotShadows`, plus de vue au-delà de `fadeEnd` (`kFaded`), fondu entre `fadeStart` et `fadeEnd`. Mises à jour : au plus
`maxViewUpdatesPerFrame` vues par image (jamais dessinées d'abord, puis priorité, puis la plus ancienne : pas de famine), une cascade `i`
au plus une image sur `cascadeUpdateInterval[i]` ; une vue reportée garde son contenu ET sa matrice (le récepteur reste cohérent).
Cache (`Cached`) : la géométrie fixe d'une vue est dessinée dans le cache quand la vue (matrice, tuile) ou la liste des casters fixes
(hachage des matrices) change ; sinon la tuile n'est recopiée sous les casters mobiles que si leur liste a changé ; si rien n'a bougé,
**rien n'est dessiné** (aucune passe). Culling par vue d'ombre (pyramide de la vue ; `ObjectSet::prepare` avec le LOD choisi depuis la
caméra), un `drawIndexed` instancié par lot.

**Passes du graphe** : `shadow.cache` (cache, s'il y a de la géométrie fixe à redessiner) puis `shadow.atlas`, avant `scene` ; aucune
passe quand rien n'est à mettre à jour.

**Mesures** (`ShadowStats`). Test `ShadowFeature cache` (2 cascades, une caster fixe, une mobile) : 1re image 2 redessins du cache +
2 recopies (2 passes) ; **image statique : 0 vue redessinée, 0 passe, 0 dessin** ; caster mobile déplacée : 2 recopies + 2 dessins
mobiles, 1 passe, cache non redessiné. Démo (`--shadows on`, 1280×720, 4 cascades, structure fixe, cristal et prismes mobiles,
animation en cours) : 1re image 4 redessins du cache, 2 passes, 19 dessins ; ensuite **0 redessin de la géométrie fixe**, 2 à 4 vues
recopiées par image (amortissement 1, 1, 2, 4), 1 passe, 7 à 15 dessins. En mode `Dynamic`, chaque vue due est redessinée entièrement à
chaque image même immobile (test des combinaisons).

Démo : `apps/demo/bin/debug/main --shadows on` (défaut `off` : captures inchangées) ; les compteurs sont journalisés (12 premières
images, puis toutes les 600). Capture : `make demo-capture` (`build/demo-capture/shadows`), référence
`docs/BASELINE/reference/scene-opengl-demo-shadows-1280x720.png`.

Limites : pas d'ombres de contact (écran ; il faudrait une pré-passe de profondeur) ; pas de fondu entre cascades (bascule nette) ;
une seule directionnelle ombrée ; pas de ponctuelles (REN-09) ; `Baked` sans cuisson (REN-16) ;
le cache d'une cascade est redessiné dès que la caméra se déplace d'un texel (pas de défilement de la tuile) ; casters des deux faces
(pas de biais matériel : le RHI n'a pas de `depthBias`), biais dans le récepteur ; les casters `Mesh` à données d'instance propres sont
refusées ; LOD : les deux niveaux d'un fondu sont dessinés dans l'ombre (pas de tramage).

#### Ombres des transparents (M5 : REN-39)

Avant REN-39 un transparent n'était caster que s'il était enregistré par `addMeshCaster`, et alors il projetait une ombre **pleine**
(traité comme un opaque) ; sinon il ne projetait rien. Maintenant, `ShadowFeature::setCasterTransparency(id, {mode, alpha, cutoff})`
choisit, **par caster maillage** (donc par matériau), un `TransparentShadowMode` (`transparent_shadow.hpp`, sans GPU) :

| Mode | Texels écrits dans la carte d'ombre | Usage conseillé |
| --- | --- | --- |
| `kNone` | aucun (le caster n'est pas dessiné, ni dans l'empreinte du cache) | lueurs additives, particules |
| `kFull` (défaut) | tous : ombre pleine, `alpha` ignoré | verre très opaque, comportement d'avant REN-39 |
| `kMasked` | tous si `alpha >= cutoff`, sinon aucun (`discard`, même règle que `BlendMode::kMask`) | feuillage, grillages |
| `kDithered` | part ≈ `alpha`, trame de Bayer 4×4 (seuil `(m + 0,5) / 16`), `discard` | verre, fumée : ombre partielle |

`defaultTransparentShadowMode(BlendMode)` donne le conseil (opaque → pleine, masque → masquée, tramé / mélangé / prémultiplié /
transmission → tramée, additif → aucune) ; il n'est **jamais appliqué en silence** (un caster sans réglage reste en ombre pleine).
Le rejet est dans `shadow_caster.frag` (variante `OROSMO_SHADOW_ALPHA`, programme `shadow_caster.vert` + frag avec en push constants
clip × modèle puis `(alpha, seuil, mode)`, 80 octets) ; les modes `kNone` et `kFull` gardent le programme sans rejet.
La trame est calculée sur `gl_FragCoord` de l'atlas : **fixe dans l'espace de la carte**, donc stable dans le temps (aucune horloge,
aucun hasard), et le PCF du récepteur la lisse en une pénombre dont la part sombre vaut l'opacité. Précision : l'opacité est quantifiée
au seizième (`transparentShadowCoverage`), la part d'ombre mesurée sur l'image diffère d'au plus 0,12 de `alpha` (test, PCF 3×3, bords
exclus). Si la carte est très peu résolue (un texel de carte plus grand qu'un pixel et une trame visible), la trame se voit : c'est
la limite du tramage (pas de mélange temporel des ombres).

**Cache.** L'empreinte d'un caster (hachage) contient `mode`, `alpha` et `cutoff` pour `kMasked` / `kDithered` seulement : une opacité
animée d'un caster **fixe** redessine le cache de la vue, d'un caster **mobile** redessine les mobiles sur la recopie ; en `kFull`
ou `kNone` l'opacité est ignorée (rien n'est redessiné). Conseil : un caster dont l'opacité s'anime est `kMovable`, pour ne pas
redessiner toute la géométrie fixe à chaque image.
Limites : les casters d'un `ObjectSet` (instanciés) sont toujours en ombre pleine (`setCasterTransparency` répond `kUnsupported`) ;
une seule opacité par caster (pas de texture d'alpha par texel : le seuil et l'opacité sont par objet, comme dans `TransparentFeature`) ;
pas de couleur d'ombre (verre teinté : REN-38).
Démo : `--scene mixed --shadows on --transparent-shadow none|full|masked|dithered` (sans l'option : les vitres ne projettent rien,
captures de référence inchangées). Tests : `tests/renderer/transparent_shadow_tests.cpp` (suite `renderer/transparent-shadow`, CPU puis
OpenGL et Vulkan : aucune = éclairé, pleine = sombre, masquée au-dessus / au-dessous du seuil, tramée à 0,25 / 0,5 / 0,75, stabilité
image à image, invalidation du cache, validation à 0).

### Éclairage forward+ clusterisé et types de lumières (M5 : REN-04, REN-05)

| En-tête / fichier | Rôle |
| --- | --- |
| `forward_plus.hpp` | `ForwardPlusLighting` (plugin `RenderFeature` « forward-plus ») : passe compute `forwardplus.clusters` ; `ForwardPlusSettings` (grille 16×9×24 par défaut, budgets `maxLightsPerCluster` = 128 et `maxLights`, tranches exponentielles `nearDepth`/`farDepth`) ; `enableForwardPlus` / `disableForwardPlus` (plugin + pré-passe) ; `forwardPlusDefines` (variante de `mesh.frag`) ; références CPU `lightRange`, `lightAttenuation`, `clusterSlice` ; `GpuClusterHeader` (std430, `static_assert`) ; debug `readClusterLightCounts`, `readClusterLights`, `readClusterStats` / `summarizeClusterCounts` |
| `shaders/include/orosmo/lights.glsl` | REN-05 : portée (`lightRange` : là où l'intensité atténuée tombe à 1/256), atténuation `1 / (a d² + b d + 1)` fenêtrée par `(1 - (d / portée)⁴)²` (nulle au-delà), cône de projecteur (`spotCone`, cosinus `outerCone` / `innerCone`), `localLightRadiance` (diffus + spéculaire Phong) |
| `shaders/rhi/light_block.glsl` | bloc `LightBlock` (groupe 0, binding 1), même disposition que `GpuLightHeader` + `GpuLight` |
| `shaders/rhi/clusters.glsl` | bloc `ClusterGrid` (binding 3 : en-tête + nombre de lumières par cluster) et `ClusterLights` (binding 4 : indices) ; `clusterIndexAt`, `clusterLightCount`, `clusterLight`, `clusterHeatColor` |
| `shaders/rhi/light_clusters.comp` | un fil par cluster : boîte englobante en espace de vue (perspective ou orthographique), test sphère/boîte avec la portée de chaque lumière, dans l'ordre du tampon ; ambiantes et directionnelles dans tous les clusters |

**Types de lumières (REN-05).** Ambiante (en-tête et type 1), directionnelle (type 2, `direction` vers la lumière), ponctuelle
(type 3) et projecteur (type 4, `direction` = sens du faisceau, spéculaire en puissance 64 comme l'ancien shader) sont rendus par
`mesh.frag` (forward et forward+, aussi par la variante instanciée). Les ponctuelles et projecteurs étaient « réservés, non rendus »
avant M5 ; l'ancien shader (commenté) n'atténuait pas le spéculaire, qui l'est maintenant (sinon la portée serait infinie). Les
positions sont en coordonnées monde ; en rendu relatif à la caméra, les shaders les ramènent à l'origine de la caméra
(`FrameData::cameraWorldHigh/Low - cameraRelativeOrigin`). Dispositions std430 vérifiées par `static_assert` (`GpuLight`,
`GpuLightHeader`, `GpuClusterHeader`) et lues par le GPU dans un test. Lumières de surface (P2) : absentes.

**Image en forward+ (REN-04).** `enableForwardPlus(renderer, device, shaders, pipelines, dossier)` active la pré-passe de profondeur
de `SceneRenderer` et enregistre le plugin ; les maillages prennent la variante `forwardPlusDefines()` de `mesh.frag`. Ordre
(docs/03) : `depth-prepass` (raster : mêmes attachements que la scène, couleur masquée, profondeur écrite ; dessins de `drawMesh`
seulement, sans fil de fer ; premier entier des push constants à 2 : `mesh.frag` n'éclaire pas ces fragments) -> `forwardplus.clusters` (compute, groupe 0 de l'image) -> `scene` (profondeur chargée, test « plus
grand ou égal » sans écriture : chaque pixel n'est éclairé qu'une fois). Les tampons de clusters sont liés au groupe 0 par
`RenderGraphBuilder::addFrameBinding` (liaisons 3 et 4 ; 3 à 7 sont réservées aux plugins) et lus par la scène
(`addSceneInput(RgBuffer)`). Le forward et le forward+ évaluent chaque lumière par la même fonction et dans le même ordre ; une
lumière écartée d'un cluster n'apporte exactement rien (fenêtre de portée) : **même image** (test : tolérance 8, en pratique
identique). Compute absent ou moins de 5 emplacements de tampons de stockage : `supports` faux, `enableForwardPlus` renvoie
kUnsupported, l'application garde le forward (la démo le journalise).

**Budgets.** `maxLights` : seules les N premières lumières du tampon sont examinées. `maxLightsPerCluster` : un cluster liste au plus
N lumières (les premières dans l'ordre) ; `clusterCounts` garde le nombre réel, `readClusterStats().overflowingClusters` compte les
débordements (et la variante de debug les peint en magenta). Au-delà d'un budget, l'image du forward+ diffère du forward.

**Debug « lumières par cluster ».** Côté GPU : variante `forwardPlusDefines(true)` (`OROSMO_CLUSTER_DEBUG`) qui peint chaque fragment
selon le nombre de lumières de son cluster (bleu foncé : aucune ; bleu -> vert -> jaune -> rouge jusqu'au budget ; magenta :
débordement). Côté CPU (lecture synchrone) : `readClusterLightCounts()` (un nombre par cluster c = x + X (y + Y z)),
`readClusterLights(c)`, `readClusterStats()`.

Démo : `apps/demo/bin/release/main --headless --lighting forwardplus --lights 1000` (défaut `--lighting forward`, sans lumière
ponctuelle : captures de référence inchangées) ; `--lights N` place N ponctuelles déterministes (spirale de Fibonacci, hachage entier).
Le Profiler mesure `RenderGraph/depth-prepass`, `RenderGraph/forwardplus.clusters` et `RenderGraph/scene` (journal « Perf: »).

Limites : les dessins externes (`drawCustom`, donc `InstanceRenderer`) ne sont pas dans la pré-passe et la variante instanciée lit
toutes les lumières (même image, sans le gain du forward+) ; les clusters sont construits avec les lumières actives à `endFrame` (un
seul `LightManager` par image en forward+) ; pas de culling des clusters vides par la profondeur de la pré-passe (tous les clusters
sont remplis) ; test sphère/boîte conservateur pour les projecteurs (pas de test de cône) ; une case de grille est en NDC, donc sa
taille en pixels suit la résolution.

## Règles

- **Le périphérique survit aux objets** qui le référencent (pointeur non possédé) ; la destruction des handles est différée par le RHI.
- **Erreurs** : `Result<T>`, jamais d'exception. Une recréation qui échoue (`resize`, `reconfigure`, rechargement de shader) laisse
  l'objet dans son état précédent.
- **Repère** du RHI (A26) : les octets envoyés ou lus commencent par le **haut** de l'image, comme les fichiers ; `Image` et `readPixel*` utilisent
  l'origine en **haut**, avec retournement explicite.
- **Pas d'état global, pas de singleton** : ni `GraphicsRenderState`, ni `ShaderLibrary::Instance()`.

## Boîte à outils de shaders (REN-22) et modes de fusion (REN-37, données)

- `shaders/include/orosmo/` : `math.glsl` (constantes, `saturate`, `remap`, sRGB, base orthonormée), `noise.glsl` (PCG, bruit de valeur et
  de gradient, `fbm`), `coords.glsl` (uv/NDC, octaédrique, sphérique), `depth.glsl` (position depuis la profondeur, reverse-Z zéro-à-un,
  plan lointain fini ou infini), `pbr.glsl` (GGX, Smith corrélé, Schlick, `brdfGGX`), `shadow.glsl` (PCF sur `sampler2DShadow`, biais de pente).
  Pas d'atmosphère (A39). Inclusion : `#include "../include/orosmo/math.glsl"` relatif au shader (ex. `mesh.frag`), ou `"orosmo/math.glsl"`
  avec `shaders/include` dans `ShaderLibraryConfig::includeRoots`. Tests : `tests/renderer/shader_toolbox_tests.cpp` (compilation sur
  chaque backend, valeurs en compute GPU comparées à des références CPU).
- `blend_mode.hpp` : `BlendMode` (8 modes), `blendModeState`, `applyBlendMode(GraphicsPipelineDesc&, mode)`. Le rendu des transparents
  est dans `transparent_feature.hpp` (ci-dessous) ; la réfraction (REN-38) n'est pas là.

## Transparents triés (REN-32, REN-37 rendu)

`TransparentFeature` (`transparent_feature.hpp`) est un `RenderFeature` : sa passe `transparents` (raster, couleur et profondeur de la
scène en `kLoad`) s'exécute après `scene`. `submit(scene, mesh, TransparentMaterial)` dépose un objet (mode, opacité, seuil, priorité de
tri, décalage du centre) ; le pipeline vient de `blendModeState` (mélange, écriture de profondeur). Ordre fixe : (1) opaques, masqués,
tramés (profondeur écrite, ordre de dépôt) ; (2) `Blend` / `Premultiplied` / `Transmission` triés de l'arrière vers l'avant (distance du
centre à la caméra, priorité de matériau d'abord, égalité : ordre de dépôt) sans écriture de profondeur ; (3) `Additive` après les mélangés.
`sortTransparents` est pure (testée sans GPU). Shader : `mesh.frag` compilé avec `OROSMO_TRANSPARENT` (push constants : fil de fer,
opacité, seuil, mode ; `Mask`, `Dithered` par trame de Bayer 4x4, `Premultiplied` prémultiplie dans le shader). Tests :
`tests/renderer/transparency_tests.cpp` (une image de référence CPU par mode, tri, priorité, profondeur de la scène, OpenGL et Vulkan).
Limites : `MaskA2C` est **Partiel** (se comporte comme `Mask` : `rhi::GraphicsPipelineDesc` n'a pas de champ `alphaToCoverage` ni de MSAA ;
proposition : ajouter `bool alphaToCoverage` et un nombre d'échantillons au RHI) ; `Transmission` se compose comme `Blend` (pas de copie de
la scène, REN-38) ; l'opacité et le seuil sont par objet, pas par texel.

## Mélange 2D/3D et plusieurs vues (R2D-05, R2D-06)

Décision : `Renderer2D::render` reste (même API, plus une surcharge à `Render2DOptions` : profondeur et matrice de vue-projection), mais il
est enveloppé dans des `RenderFeature` (`layer2d_feature.hpp`, `Layer2DFeature`, un `Renderer2D` par plugin). L'ordre fixe 3D opaque (passe
`scene`) -> 3D transparent -> 2D monde -> post-traitement -> 2D en superposition découle de l'ordre d'enregistrement avec `addFeature` :

| Mode | Passe | Détail |
| --- | --- | --- |
| `kWorld` | lit `scene.depth`, écrit `scene.color` | le plan 2D est placé dans le monde par `setPlaneTransform` et vu par la caméra 3D ; test de profondeur (reverse-Z, lecture seule). Dessiné après les transparents, qui n'écrivent pas la profondeur : un sprite derrière une vitre la recouvre |
| `kOverlay` | écrit `output` | après la mise à l'échelle, en pixels de la sortie, sans profondeur |
| `kTexture` | avant `scene` | `setTextureSize`, `binding()` à donner à `Mesh::setTextures` ; shaders `mesh_textured.vert` + `mesh.frag` avec `OROSMO_TEXTURED` |

R2D-06 (`scene_view.hpp`) : `SceneView` = un `SceneRenderer` + une `Camera` + une texture de sortie (zone d'affichage) ; `render(encoder,
options, draw)` la rend. `ViewsFeature::place(vue, ViewRect)` compose des vues dans des zones de la sortie (écran partagé, incrustation,
passe `views`). Vue 3D dans l'interface : `CanvasRenderer::registerExternalImage(id, texture, sampler)` associe une `AtlasImage` sans pixels
(`newAtlasId`) à une texture du GPU, affichée par `ImageView` / `DrawImage` (le chemin de texture existant de l'UI). Les vues se rendent
avant l'`endFrame` de l'image principale. Tests : `tests/renderer/mixed_2d3d_tests.cpp`. Démo : `--scene mixed` (`apps/demo/src/Scene/mixed_scene.*`).
Limites : le pixel parfait est sans effet avec une matrice 3D ; la copie de la sortie d'une vue vers une autre fenêtre n'est pas faite.

## Limites connues

- Texture 2D seulement (pas de tableau, cube, 3D côté façade ; le RHI les offre). Un échantillonneur par texture (pas de partage).
- `GpuTimer` : une mesure à la fois ; branché sur le `Profiler` par `ProfilerGpuTimerFactory` (A25), utilisé par le graphe (une mesure par passe).
- Graphe : textures transitoires 2D seulement (pas de tableau, cube, 3D ni MSAA) ; partage de mémoire seulement entre ressources de même
  description (pas d'alias d'une texture sur la mémoire d'une autre de format différent) ; les passes s'exécutent sur un seul encodeur
  (pas de files asynchrones). `FrameData` : la position de la caméra est un `float` côté `Camera` (la scission en double ne gagne rien
  tant que la caméra ne porte pas de `double`) .
- Pas d'optimisation des shaders (SPIR-V stable) ni de cache de pipelines natifs.
