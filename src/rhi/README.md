# rhi

Couche 2 de `docs/03` : l'interface graphique abstraite (RHI) et ses backends. Archive : `librhi.a`.
API publique : `include/engine/rhi/` (aucun type d'API graphique). Backends privés : `src/rhi/<backend>/`, **seuls endroits** où
`gl*`, `Vk*`, `MTL*` ont le droit d'exister (`tools/check_api_isolation.py`).

| Fichier | Rôle |
| --- | --- |
| `include/engine/rhi/handle.hpp` | `Handle<Tag>` (indice + génération) et `HandlePool<Tag, T>` : un handle périmé est refusé, jamais déréférencé |
| `include/engine/rhi/types.hpp` | formats, usages, descriptions (buffer, texture, échantillonneur, shader, pipeline, groupe de liaison, passe) |
| `include/engine/rhi/capabilities.hpp` | `Capabilities` : limites, fonctions, capacités optionnelles réservées (toutes `false`, RHI-18) |
| `include/engine/rhi/device.hpp` | `Device`, `CommandEncoder`, `DeviceStats`, `createDevice` |
| `src/rhi/format.cpp`, `src/rhi/device.cpp` | `formatInfo`, choix du backend |
| `include/engine/rhi/shader_compiler.hpp`, `src/rhi/shader_compiler.cpp` | pipeline de shaders (RHI-12) : `compileGlsl`, `translateToGlsl430`, `ShaderCache`, `createShaderFromGlsl`, `ShaderHotReloader` |
| `src/rhi/opengl/` | backend OpenGL 4.3 (`gl_device.cpp` ressources, `gl_encoder.cpp` commandes, `gl_translate.hpp` tables) |

## Utilisation

```cpp
using namespace engine::rhi;
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

- **Handles opaques**, destruction **différée** de 2 images (`present()` libère) : `destroy` rend le handle périmé tout de suite.
- **Erreurs** : `Result<T>`. Un encodeur ne lance rien : une commande invalide est ignorée, journalisée, et la **première** erreur
  est renvoyée par `Device::submit`.
- **Barrières** : le RHI les calcule. Le backend OpenGL est conservateur : `glMemoryBarrier(GL_ALL_BARRIER_BITS)` après chaque
  `dispatch` et à la fin d'une passe qui a lié un tampon de stockage ou une image.
- **Noms de debug** : `glObjectLabel` sur chaque objet, `glPushDebugGroup` par passe nommée et par `beginDebugGroup`.
- **Validation** : contexte de debug (`GL_KHR_debug`) ; `Device::validationErrorCount()` compte les messages d'erreur, les tests
  exigent 0.

## Modèle de liaison (docs/03 §4, RHI-07)

Trois groupes (0 image, 1 matériau/passe, 2 objet), `kBindingsPerGroup` = 8 liaisons chacun. OpenGL les aplatit :
**emplacement = groupe × 8 + binding**, pour les UBO, SSBO, unités de texture et unités d'image. Les shaders OpenGL écrits à la main
utilisent ces numéros (`layout(binding = 8) uniform sampler2D tex;` = groupe 1, binding 0) ; le pipeline de shaders du lot B
convertira `set`/`binding` du GLSL 450 commun vers cet emplacement. L'**UBO 24** (`kGroupCount * kBindingsPerGroup`) porte les push
constants (128 octets, émulés). Les images de stockage exigent un emplacement `< GL_MAX_IMAGE_UNITS` (8 sur certains pilotes) :
utiliser le groupe 0 pour elles ; `setBindGroup` refuse sinon, avec ce message.

## Pipeline de shaders (RHI-12)

Source unique en **GLSL 450** « style Vulkan » : `layout(set = N, binding = M)`, `layout(push_constant)`, `gl_VertexIndex`,
`#include` (`#extension GL_GOOGLE_include_directive : enable`), textures **combinées** (`sampler2D`). Chaîne :
shaderc → SPIR-V (sans optimisation : stable, noms conservés) → réflexion SPIRV-Cross (liaisons par `(set, binding)`, taille des push
constants, taille de groupe de calcul, sorties couleur ; refus d'une liaison hors des 3 groupes × 8, de textures et échantillonneurs
séparés, de deux ressources sur la même liaison) → pour OpenGL, GLSL 430 avec **liaison = set × 8 + binding** et push constants en UBO 24.
Pas de correction d'espace de clipping (A26). Le choix « SPIRV-Cross plutôt que `GL_ARB_gl_spirv` » vient du minimum OpenGL 4.3 ;
`GL_ARB_gl_spirv` (4.6) pourra être ajouté derrière `Capabilities::spirvShaders`.

- **Cache** (`ShaderCache`) : clé = hachage du source *prétraité* (includes et macros développés) + étage + version du cache ; mémoire
  puis disque (`<dossier>/<hash>.spv`, écriture atomique, fichier corrompu ignoré). Cache de **pipelines** natifs du pilote : non fait.
- **Rechargement à chaud** (`ShaderHotReloader`) : surveille le fichier et ses `#include`, recompile, appelle le rappel (résultat ou
  erreur) ; l'appelant recrée le pipeline et garde l'ancien si la compilation échoue.
- Dépendance : `spirv-cross` (vcpkg, toutes plateformes), `shaderc` (apt sous Linux).

## Conventions (choix du lot A, à confirmer : `docs/QUESTIONS.md` A26)

Repère **OpenGL** : origine du viewport et du ciseau en bas à gauche, Y vers le haut, première ligne lue par `readTexture` = bas
de l'image, profondeur NDC −1..1 (`glDepthRange` 0..1). Vulkan et Metal ont l'origine en haut ; l'harmonisation (viseur négatif,
`GL_ARB_clip_control` pour la profondeur 0..1 et le reverse-Z de REN-02) est décidée avec le premier shader commun (lot B) et
M4. Les textures d'image sont stockées sans inversion ici.

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
- Pas de cache de pipelines sur disque (RHI-06, lot B), pas de multi-draw avec compteur (`GL_ARB_indirect_parameters`), pas de
  formats compressés (`bc7Textures = false`), pas de textures multi-échantillons.
- Une seule file : copies et rendu sont synchrones côté pilote (RHI-19 non traité).
- Un seul fil : le périphérique et ses encodeurs s'utilisent depuis le fil qui l'a créé (contexte GL).
- Le backend Vulkan précédent (ancienne abstraction `IGraphicsDevice`) sera retiré du build pendant M2-M3, au lot B (décision de l'auteur,
  `docs/QUESTIONS.md` A1) ; il est réécrit en M4 sur ce RHI.

## Tests

`tests/rhi/handle_tests.cpp` (CPU), `tests/rhi/opengl_device_tests.cpp` (GPU, contexte hors écran ; sans pilote OpenGL 4.3 chaque
test s'arrête avec un avertissement). Sous Linux sans affichage : `xvfb-run -a make demo-test` (Mesa llvmpipe fournit GL 4.5).
