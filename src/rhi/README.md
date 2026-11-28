# rhi

Couche 2 de `docs/03` : l'interface graphique abstraite (RHI) et ses backends. Archive : `librhi.a`.
API publique : `include/orosmo/rhi/` (aucun type d'API graphique). Backends privés : `src/rhi/<backend>/`, **seuls endroits** où
`gl*`, `Vk*`, `MTL*` ont le droit d'exister (`tools/check_api_isolation.py`).

| Fichier | Rôle |
| --- | --- |
| `include/orosmo/rhi/handle.hpp` | `Handle<Tag>` (indice + génération) et `HandlePool<Tag, T>` : un handle périmé est refusé, jamais déréférencé |
| `include/orosmo/rhi/types.hpp` | formats, usages, descriptions (buffer, texture, échantillonneur, shader, pipeline, groupe de liaison, passe) |
| `include/orosmo/rhi/capabilities.hpp` | `Capabilities` : limites, fonctions, capacités optionnelles réservées (toutes `false`, RHI-18) |
| `include/orosmo/rhi/device.hpp` | `Device`, `CommandEncoder` (`id()` : identifiant unique dans le processus, jamais réutilisé, pour reconnaître un nouvel encodeur), `DeviceStats`, `createDevice`, `fallbackBackend` (OpenGL après Vulkan ; rien sous macOS, où OpenGL s'arrête à 4.1) |
| `src/rhi/format.cpp`, `src/rhi/device.cpp` | `formatInfo`, choix du backend |
| `include/orosmo/rhi/shader_compiler.hpp`, `src/rhi/shader_compiler.cpp` | pipeline de shaders (RHI-12) : `compileGlsl`, `translateToGlsl430`, `ShaderCache`, `createShaderFromGlsl`, `ShaderHotReloader` |
| `src/rhi/opengl/` | backend OpenGL 4.3 (`gl_device.cpp` ressources, `gl_encoder.cpp` commandes, `gl_translate.hpp` tables) |

## Utilisation

```cpp
using namespace orosmo::rhi;
DeviceDesc desc;                       // backend OpenGL, surface hors écran 1x1 par défaut
desc.surface.size = {1280, 720};
auto device = createDevice(desc);      // Result<unique_ptr<Device>> ; kUnsupported avec la raison si GL 4.3 est absent
auto color = (*device)->createTexture({.format = Format::kRGBA8Unorm, .size = {1280, 720, 1},
                                       .usage = TextureUsage::kRenderTarget | TextureUsage::kCopySrc});
auto encoder = (*device)->createCommandEncoder();
encoder->beginRenderPass({.colors = {{.texture = *color, .clearColor = {0, 0, 0, 1}}}});
encoder->setPipeline(pipeline);  encoder->draw(3);
encoder->endRenderPass();
auto done = (*device)->submit(std::move(encoder));   // renvoie la première erreur d'usage, sinon succès
```

## Règles appliquées (docs/03 §3)

- **Images en vol (A61 G, M8)** : `DeviceDesc::framesInFlight`, de 1 à `kMaxFramesInFlight` (3), défaut `kDefaultFramesInFlight`
  (2) ; une autre valeur est refusée par `createDevice` (kInvalidArgument) ; `Device::framesInFlight()` la rend. Contrat commun
  aux deux backends : quand `beginFrame` de l'image k rend la main, l'image k − N est terminée par le GPU, et une ressource
  détruite pendant l'image k est libérée après N présentations. Tests : `tests/rhi/frames_in_flight_tests.cpp` (suites
  `rhi/frames-in-flight` sans GPU, `rhi/backend/frames-in-flight` sur chaque backend).
- **Handles opaques**, destruction **différée** : `destroy` rend le handle périmé tout de suite ; l'objet GL n'est libéré qu'une
  fois `framesInFlight()` images présentées après celle de sa destruction **et** la clôture GPU de cette image franchie (`glFenceSync` posée par
  `present()`, attendue au besoin). `DeviceStats::pendingDestruction` compte ces libérations en attente.
- **État implicite contenu** : les opérations du périphérique (création, envoi, mips) lient leur texture sur une unité réservée
  (24, `kScratchTextureUnit`) et n'écrasent jamais une texture liée par `setBindGroup` ; les liaisons de tampons de sommets et
  d'indices sont retirées du VAO au changement de pipeline et en fin de passe (un tampon détruit n'est plus retenu) ; la plage de
  profondeur est remise à [0, 1] à chaque passe ; une lecture synchrone rend la liaison de framebuffer trouvée.
- **Erreurs** : `Result<T>`, jamais d'exception. Sur OpenGL, l'encodeur **exécute chaque commande à l'appel** : une commande
  valide (un `draw`) a déjà agi quand `submit` est appelé, et abandonner l'encodeur ou constater l'échec de `submit` ne l'annule
  pas. Une commande invalide, elle, est refusée avant tout appel GL, journalisée, et la **première** erreur est renvoyée par
  `Device::submit` ; une passe ou un groupe de debug laissé ouvert est aussi une erreur (refermé quand même). Une passe dont une
  valeur d'effacement est refusée ne touche à aucune de ses cibles (toutes validées avant le premier effacement).
  Les plages (copies, commandes indirectes) sont vérifiées sans débordement arithmétique ; les décalages et pas indirects doivent
  être multiples de 4. Une copie tampon <-> texture n'exige que les octets réellement touchés (pas de remplissage après la
  dernière ligne) ; `updateTexture` lit des lignes serrées (`bufferRowLength` ne vaut que pour les copies tampon <-> texture).
- **Barrières** : le RHI les calcule. Le backend OpenGL est conservateur : `glMemoryBarrier(GL_ALL_BARRIER_BITS)` après chaque
  `dispatch` et à la fin d'une passe qui a lié ou utilisé du stockage ; dans une passe, une barrière de stockage précède chaque
  dessin dont le programme accède au stockage (SSBO ou image, lu à l'édition de liens) quand un dessin précédent de la passe y
  accédait aussi — que la liaison ait été faite avant ou pendant la passe. Les passes sans stockage (interface) n'en paient rien.
- **Mémoire `kUpload`** (perf M5) : avec `glBufferStorage` (cœur 4.4, ou `GL_ARB_buffer_storage` sur un pilote 4.3 ; lu à la création
  du périphérique, `ENGINE_GL_BUFFER_STORAGE=0` force le repli), stockage immuable mappé en permanence (persistant + cohérent).
  `updateBuffer` y est une **copie mémoire** quand plus aucun lot non terminé ne lit le tampon ; sinon (lié par l'encodeur en cours,
  ou par un lot soumis dont la clôture n'est pas franchie) c'est `glBufferSubData`, ordonné dans le flux GL comme avant : le résultat ne
  change jamais, seul le coût. Chaque `submit` qui a utilisé un tampon mappé pose une clôture (`glFenceSync`), interrogée sans attente.
  Le chemin rapide suppose donc des tampons distincts par image en vol (anneaux du renderer : `FrameRing`, `BatchArena`). Sans
  `glBufferStorage` : `glBufferData` + `glBufferSubData` (comportement d'avant). Les autres mémoires ne changent pas.
- **Noms de debug** : `glObjectLabel` sur chaque objet, `glPushDebugGroup` par passe nommée et par `beginDebugGroup`.
- **Validation** : contexte de debug (`GL_KHR_debug`) ; `Device::validationErrorCount()` compte les messages d'erreur, les tests
  exigent 0.

## Modèle de liaison (docs/03 §4, RHI-07)

Trois groupes (0 image, 1 matériau/passe, 2 objet), `kBindingsPerGroup` = 8 liaisons chacun. OpenGL les aplatit :
**emplacement = groupe × 8 + binding**, pour les UBO, SSBO, unités de texture et unités d'image. Les shaders OpenGL écrits à la main
utilisent ces numéros (`layout(binding = 8) uniform sampler2D tex;` = groupe 1, binding 0) ; le pipeline de shaders du lot B
convertira `set`/`binding` du GLSL 450 commun vers cet emplacement. L'**UBO 24** (`kGroupCount * kBindingsPerGroup`) porte les push
constants (128 octets, émulés). Une image de stockage est refusée par `setBindGroup` si son emplacement plat (groupe × 8 + binding)
atteint `GL_MAX_IMAGE_UNITS`. Ce minimum vaut 8 sur certains pilotes : le groupe 0 est donc le choix **portable** pour les images
(il marche partout), pas une obligation là où le pilote en offre plus. Même règle pour les tampons de stockage :
OpenGL 4.3 n'en garantit que 8 emplacements, la limite réelle est `Capabilities::maxStorageBufferBindings` (le périphérique se
crée même si elle est inférieure à 24).

## Pipeline de shaders (RHI-12)

Source unique en **GLSL 450** « style Vulkan » : `layout(set = N, binding = M)`, `layout(push_constant)`, `gl_VertexIndex`,
`#include` (`#extension GL_GOOGLE_include_directive : enable`), textures **combinées** (`sampler2D`). Chaîne :
shaderc → SPIR-V (sans optimisation : stable, noms conservés) → réflexion SPIRV-Cross (liaisons par `(set, binding)`, taille des push
constants, taille de groupe de calcul, sorties couleur ; refus d'une liaison hors des 3 groupes × 8, de textures et échantillonneurs
séparés, de ressources qui se chevauchent : un tableau `sampler2D t[N]` occupe N liaisons consécutives, qui doivent tenir dans
son groupe ; `ReflectedBinding::count` donne N) → pour OpenGL, GLSL 430 avec **liaison = set × 8 + binding** et push constants en UBO 24.
Pas de correction d'espace de clipping : le repère du RHI (A26) est le même pour tous les backends. Le choix « SPIRV-Cross plutôt que `GL_ARB_gl_spirv` » vient du minimum OpenGL 4.3 ;
`GL_ARB_gl_spirv` (4.6) pourra être ajouté derrière `Capabilities::spirvShaders`.

- **Cache** (`ShaderCache`) : clé = hachage du source *prétraité* (includes et macros développés) + étage + version du cache ; mémoire
  puis disque (`<dossier>/<hash>.spv`, écriture dans un fichier temporaire propre au processus puis renommage atomique ; fichier
corrompu ou rejeté par la réflexion : recompilé). Cache de **pipelines** natifs du pilote : fait pour Vulkan (`VkPipelineCache` écrit sur disque à l'arrêt et relu, RHI-06) ; pas pour OpenGL.
- **Rechargement à chaud** (`ShaderHotReloader`) : surveille le fichier et ses `#include` — y compris un include **absent** ou
  ajouté par une édition qui ne compile pas encore —, recompile, appelle le rappel (résultat ou erreur) ; l'appelant recrée le
  pipeline et garde l'ancien si la compilation échoue. Les surveillances des fichiers qui restent des dépendances sont
  conservées (une modification enregistrée pendant une compilation n'est pas perdue). Ni copiable ni déplaçable.
- Dépendance : `spirv-cross` (vcpkg, toutes plateformes), `shaderc` (apt sous Linux).

## Conventions (repère du RHI, décidé par l'auteur : `docs/QUESTIONS.md` A26, appliqué en M3)

- **Origine en haut à gauche** pour toutes les cibles : la ligne 0 d'une texture (envoi, lecture, rendu) est son **haut** ; les
  rectangles de viewport et de ciseau partent du haut ; une image se lit et s'enregistre sans retournement.
- **NDC** : x vers la droite, **y = +1 en haut**, profondeur dans **[0, 1]**. Les projections de la caméra sont en reverse-Z (1 au
  plan proche, 0 au loin) : effacement de profondeur à 0, test « plus grand » (REN-02).
- **Faces** : un triangle anti-horaire en NDC est la face avant (`FrontFace::kCounterClockwise`).
- **OpenGL** : `glClipControl(GL_UPPER_LEFT, GL_ZERO_TO_ONE)` (cœur 4.5 ou `GL_ARB_clip_control` sur un pilote 4.3 : sinon
  `createDevice` refuse avec un message clair). Le framebuffer de la **fenêtre** est affiché ligne 0 en bas de l'écran : ses passes
  utilisent l'origine basse, ses rectangles et ses lectures sont retournés par le backend ; l'utilisateur ne voit que la convention.
- **Vulkan (M4)** : hauteur de viewport négative (cœur 1.1) pour garder y = +1 en haut ; profondeur [0, 1] native.
- Preuves : `tests/rhi/opengl_state_tests.cpp` (« A26: a counter-clockwise triangle... »), tous les tests de lecture de pixels, et
  les images de référence, identiques avant et après le changement (capture de la démo ; `parity_tests.cpp` à l'époque, supprimé
  le 2026-11-20 avec l'interface héritée).

## Surface : hors écran ou fenêtre (B2, RHI-08)

- **Hors écran** (`SurfaceDesc::nativeWindow` nul) : contexte porté par une fenêtre GLFW invisible 1×1, backbuffer = texture RGBA8 de
  `surface.size` (tests, mode sans fenêtre PLAT-04).
- **Fenêtre** : la couche fenêtre appelle `prepareWindowHints(desc)` **avant** `glfwCreateWindow` (4.3 core, contexte de debug si
  `desc.validation`, double tampon, sRGB non souhaité), pose ses propres attributs, puis passe le `GLFWwindow*` dans
  `SurfaceDesc::nativeWindow`. Le RHI **adopte** la fenêtre (il ne la détruit pas ; à la destruction du périphérique le contexte est
  libéré du fil, la fenêtre reste valide) et la rend courante sur le fil appelant.
  - Le backbuffer est le framebuffer 0 : RGBA8 linéaire (le backend coupe `GL_FRAMEBUFFER_SRGB` pour lui, le visuel système pouvant être
    sRGB), usages `RenderTarget` et `CopySrc` seulement. Il doit être **l'unique attachement** de sa passe (ni profondeur, ni MRT :
    rendre dans une cible puis copier/dessiner dans le backbuffer) ; il ne s'échantillonne pas, ne reçoit pas d'envoi de texels, n'a pas de mips.
  - `beginFrame` relit la taille du framebuffer (HiDPI compris) ; une fenêtre réduite renvoie `kBusy` (sauter l'image). `resize`
    relit aussi cette taille. `present` = `glfwSwapBuffers` ; `setVSync` = `glfwSwapInterval`.

## Limites connues (lot A et B2)

- Le contexte hors écran est une fenêtre GLFW invisible 1×1 ; GLFW n'est jamais initialisé ni terminé par une fenêtre adoptée
  (c'est la couche fenêtre qui le fait).
- **Shaders GLSL 430 uniquement** (`ShaderLanguage::kGlsl`) ; SPIR-V refusé (`Capabilities::spirvShaders = false`). Le pipeline
  GLSL 450 → SPIR-V → GLSL 430 est le lot B (RHI-12).
- Pas de cache de pipelines natifs sur disque côté OpenGL (le backend Vulkan en a un, RHI-06), pas de multi-draw avec compteur (`GL_ARB_indirect_parameters`), pas de
  formats compressés (`bc7Textures = false`), pas de textures multi-échantillons.
- Une seule file : copies et rendu sont synchrones côté pilote (RHI-19 non traité).
- Un seul fil : le périphérique et ses encodeurs s'utilisent depuis le fil qui l'a créé (contexte GL).
- Le backend Vulkan précédent (ancienne abstraction `IGraphicsDevice`) sera retiré du build pendant M2-M3, au lot B (décision de l'auteur,
  `docs/QUESTIONS.md` A1) ; il est réécrit en M4 sur ce RHI.

## Tests

`tests/rhi/handle_tests.cpp` (CPU), `tests/rhi/opengl_device_tests.cpp` (GPU, contexte hors écran ; sans pilote OpenGL 4.3 chaque
test s'arrête avec un avertissement). Sous Linux sans affichage : `xvfb-run -a make demo-test` (Mesa llvmpipe fournit GL 4.5).
