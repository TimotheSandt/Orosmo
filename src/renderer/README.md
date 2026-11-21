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
| `scene_renderer.hpp` | `SceneRenderer` : cible de scène, groupe de liaison (caméra + lumières, mis à jour avant chaque dessin), `beginFrame` / `setCamera` / `drawMesh` / `endFrame`, échelle de rendu, jitter de Halton (seulement pour un mode exécuté qui le demande), historique (les consommateurs lisent l'image PRÉCÉDENTE, `resetHistory` à la première image et après un redimensionnement), vecteurs de mouvement, profondeur lisible. Un mode de mise à l'échelle n'est exécuté que si `upscalingEnabled()` (sinon copie 1:1), il est initialisé avant d'être activé et arrêté quand il est remplacé ; un mode de génération refusé laisse l'actif en place ; `generatedFrame()` expose son image. Remplace `Renderer` + `Renderer3D` |
| `debug_draw.hpp` | `DebugDraw` (REN-34) : lignes, boîtes, sphères, repères en coordonnées monde ; désactivé par défaut hors debug (chaque appel revient au premier test) ; dessiné par `SceneRenderer::drawDebug` dans la passe de scène, test de profondeur au choix (shaders `debug_line.*` chargés au premier appel) |
| `shaders/rhi/` | `mesh.vert` / `mesh.frag` (éclairage identique à l'ancien), `blit.vert` / `blit.frag` : GLSL 450 (`set`/`binding`, push constants) |

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

Texte : il passe par le chemin existant (`ui::TextRenderer` -> `DrawList` -> `CanvasRenderer::render` sur la même cible, après `Renderer2D::render`). Limites : à `z` égal les matériaux peuvent s'intercaler (donner des `z` différents pour un ordre précis) ; atlas = une texture par matériau (pas de tableau de textures) ; contours de formes sans anticrénelage ; les modes de mélange 2D/3D (R2D-05) et plusieurs vues (R2D-06) ne sont pas faits.

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
| `instance_renderer.hpp` | `InstanceRenderer` : maillages et matériaux enregistrés par identifiant ; `draw(scene, batches)` envoie toutes les instances de l'image dans **un** tampon de sommets à pas d'instance et fait **un `drawIndexed` par lot** (`firstInstance` = début du lot) dans la passe ouverte de `SceneRenderer` (`passContext()`) ; rendu relatif à la caméra (translations retranchées à l'envoi), fil de fer, `InstanceDrawStats` |
| `shaders/rhi/mesh_instanced.vert` | instance aux emplacements 8 à 15 ; à associer à `mesh.frag` compilé avec `OROSMO_INSTANCED` (tramage de Bayer 4×4 du LOD ; même éclairage que `mesh.frag`) |

`SceneRenderer::passContext()` (encodeur, groupe 0, formats de cible, origine de la caméra) est le seul ajout à `SceneRenderer`.

Démo : `apps/demo/bin/debug/main --instances 10000` (défaut 0 : scène et captures inchangées) ; les statistiques sont journalisées.
Mesure (1280×720, vue par défaut) : 10 000 objets → 3 717 visibles (6 283 hors champ), 125 en fondu de LOD, **3 842 instances en
4 draw calls** (2 LOD × 2 matériaux) ; sans culling : 10 000 instances. Test BVH (10 000 boîtes) : 515 nœuds + 279 tests d'objets au lieu
de 10 000 tests linéaires.

Limites : un seul `InstanceRenderer::draw` par image et par instance de la classe (le tampon d'instances est réécrit à chaque appel) ;
imposteurs : n'importe quel maillage (un quad) peut servir de dernier niveau, mais pas de génération automatique (atlas, orientation
vers la caméra) ; un fondu croisé qui chevauche la bande de rejet n'est pas combiné (seul le dernier niveau s'efface) ; le tramage
désactive l'early-Z des fragments concernés ; culling CPU seulement (le culling GPU et l'occlusion Hi-Z sont REN-29).

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
  (tri, passes, réfraction) n'est pas encore là.

## Limites connues

- Texture 2D seulement (pas de tableau, cube, 3D côté façade ; le RHI les offre). Un échantillonneur par texture (pas de partage).
- `GpuTimer` : une mesure à la fois ; le branchement sur le `Profiler` (`IGpuTimerFactory`, A25) arrive avec le renderer (B4).
- Pas d'optimisation des shaders (SPIR-V stable) ni de cache de pipelines natifs.
