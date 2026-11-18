# Le RHI et la couche de rendu

Guide d'ensemble (état de M2 B6). Détails : [`src/rhi/README.md`](../src/rhi/README.md), [`src/renderer/README.md`](../src/renderer/README.md), architecture : [`03-ARCHITECTURE_ET_PORTABILITE.md`](03-ARCHITECTURE_ET_PORTABILITE.md).

## Les couches

```
 apps/demo       Game, World, CameraController                    (application)
 ui              UI::Manager::Collect -> DrawList                  (l'interface ne dessine pas)
 renderer        SceneRenderer, CanvasRenderer, Mesh, Camera, LightManager, ShaderLibrary, PipelineCache,
                 RenderTarget, Texture, GpuBuffer, GpuTimer, Image    -> include/engine/renderer/
 rhi             Device, CommandEncoder, handles, shader_compiler     -> include/engine/rhi/
   rhi/opengl    le seul endroit où gl*, GL_*, glad existent          -> src/rhi/opengl/
 platform        Window (GLFW), InputManager
 core            Logger, Result<T>, tâches, VFS, configuration
```

## Une image, de bout en bout

```cpp
auto device   = rhi::createDevice({.surface = {.nativeWindow = window}});        // la fenêtre a été créée après rhi::prepareWindowHints
rd::ShaderLibrary shaders(*device);  rd::PipelineCache pipelines(*device);
auto scene    = rd::SceneRenderer::create(*device, shaders, pipelines, {.shaderDirectory = "res/shader/rhi"});
auto canvas   = rd::CanvasRenderer::create(*device, shaders, pipelines, "res/shader/rhi");

auto frame    = (*device)->beginFrame();                       // backbuffer (kBusy si la fenêtre est réduite : sauter l'image)
auto encoder  = (*device)->createCommandEncoder();
scene->beginFrame(*encoder, {.clearColor = {0.07f, 0.13f, 0.17f, 1}});    // passe de scène (cible interne, taille de rendu)
scene->setCamera(camera);  scene->drawMesh(mesh, camera);
scene->endFrame({composed, rhi::Format::kRGBA8Unorm, {w, h}});           // post-traitements, mise à l'échelle -> image composée
ui.Collect(list, w, h);  canvas->render(*encoder, list, {composed, ...}); // l'interface se pose sur la scène
scene->blit().copy(*encoder, composed, ..., *frame, rhi::Format::kRGBA8Unorm);   // présentation
(*device)->submit(std::move(encoder));  (*device)->present();
```

## Règles à retenir

- **Handles opaques**, destruction différée ; un handle périmé est refusé, jamais déréférencé. **Erreurs** : `Result<T>`, jamais d'exception.
- **Pas d'état implicite** : chaque passe déclare ses attachements, chaque pipeline porte tout son état fixe (`PipelineCache` les construit).
- **Liaison** : trois groupes (0 image, 1 matériau, 2 objet) × 8 liaisons ; OpenGL les aplatit en `groupe × 8 + binding`. Push constants : jusqu'à 128 octets.
- **Shaders** : GLSL 450 « style Vulkan » dans `shaders/rhi/`, compilés (shaderc) puis traduits (SPIRV-Cross) en GLSL 430 pour OpenGL ; rechargement à chaud : `ShaderLibrary::pollHotReload`.
- **Repère** : celui d'OpenGL (QUESTIONS A26). Les octets envoyés ou lus commencent par le bas de l'image ; `Image` et `readPixel*` retournent explicitement.
- **Capacités optionnelles** : toujours lues dans `Capabilities`, jamais supposées (`rayQuery`, `bindless`, `meshShader`... toutes `false`).

## Preuves et images de référence

| Test | Ce qu'il fixe |
| --- | --- |
| `tests/renderer/parity_tests.cpp` | scène de terrain (graine 1) et interface (`ui.png`, texte figé) identiques aux captures de l'ancien rendu : `docs/BASELINE/reference/` |
| capture de la démo (`--capture`) | `scene.png` comparé à la référence par `tools/compare_png.py` (CI, tolérance pour llvmpipe) |
| `tests/rhi/*`, `tests/renderer/*` | périphérique, ressources, shaders, fenêtre, dessinateur 2D, minuteries GPU |

`make check` : couches, en-têtes autonomes, isolation des API (`--graphics-only --strict` : aucun `gl*`, `Vk*`, `MTL*` hors de `src/rhi/` ; GLFW en baseline jusqu'au lot C).

## Ajouter un backend

Voir `docs/03` §9 et §14 (guide `ADD_A_BACKEND.md` prévu en M4) : implémenter `Device` et `CommandEncoder`, la table de formats, le modèle de liaison, passer les tests `tests/rhi/` et les images de référence.
