# Le RHI et la couche de rendu

Guide d'ensemble (état de fin de M2). Détails : [`src/rhi/README.md`](../src/rhi/README.md), [`src/renderer/README.md`](../src/renderer/README.md), architecture : [`03-ARCHITECTURE_ET_PORTABILITE.md`](03-ARCHITECTURE_ET_PORTABILITE.md).

## Les couches

```
 apps/demo       Game, World, CameraController                    (application)
 ui              engine::ui::Ui::draw -> DrawList                  (l'interface ne dessine pas)
 renderer        SceneRenderer, CanvasRenderer, Mesh, Camera, LightManager, ShaderLibrary, PipelineCache,
                 RenderTarget, Texture, GpuBuffer, GpuTimer, Image    -> include/engine/renderer/
 rhi             Device, CommandEncoder, handles, shader_compiler     -> include/engine/rhi/
   rhi/opengl    le seul endroit où gl*, GL_*, glad existent          -> src/rhi/opengl/
 platform        Window, InputManager (GLFW caché : src/platform/)
 core            Logger, Result<T>, tâches, VFS, configuration
```

## Une image, de bout en bout

```cpp
// Chaque appel renvoie un Result<T> (std::expected) : on le vérifie, puis on prend la valeur. Pas d'exception.
auto created = rhi::createDevice({.surface = {.nativeWindow = window.GetWindow()}}); // fenêtre créée après rhi::prepareWindowHints
if (!created) { return fail(created.error()); }
std::unique_ptr<rhi::Device> device = std::move(*created);
rd::ShaderLibrary shaders(*device);
rd::PipelineCache pipelines(*device);
auto sceneMade  = rd::SceneRenderer::create(*device, shaders, pipelines, {.shaderDirectory = "res/shader/rhi"});
auto canvasMade = rd::CanvasRenderer::create(*device, shaders, pipelines, "res/shader/rhi");
if (!sceneMade || !canvasMade) { return fail(!sceneMade ? sceneMade.error() : canvasMade.error()); }
std::unique_ptr<rd::SceneRenderer> scene = std::move(*sceneMade);
std::unique_ptr<rd::CanvasRenderer> canvas = std::move(*canvasMade);
scene->setOutputResolution(w, h);                              // obligatoire avant beginFrame (sinon kInvalidArgument)

auto frame = device->beginFrame();                             // backbuffer ; kBusy si la fenêtre est réduite : sauter l'image
if (!frame) { return skipFrame(); }
auto encoder = device->createCommandEncoder();
if (auto r = scene->beginFrame(*encoder, {.clearColor = {0.07f, 0.13f, 0.17f, 1}}); !r) { return fail(r.error()); }
bool sceneOk = scene->setCamera(camera).has_value();
sceneOk = sceneOk && scene->drawMesh(mesh, camera).has_value();            // un pipeline qui ne se lie pas échoue ici
sceneOk = scene->endFrame({composed, rhi::Format::kRGBA8Unorm, {w, h}}).has_value() && sceneOk; // ferme la passe dans tous les cas
ui.Collect(list, w, h);
if (auto drawn = canvas->render(*encoder, list, {composed, rhi::Format::kRGBA8Unorm, {w, h}}); !drawn)
{
    log(drawn.error());       // kNotFound : une couche en cache a disparu ; toute autre erreur : liste ou cible invalide
    ui.InvalidateLayers();    // la prochaine liste redessine toutes les couches en entier
}
if (auto shown = scene->blit().copy(*encoder, composed, scene->blit().linearSampler(), *frame, rhi::Format::kRGBA8Unorm); !shown)
{
    log(shown.error());
}
if (auto submitted = device->submit(std::move(encoder)); !submitted || !sceneOk)
{
    // Image fausse (scène incomplète, commande refusée) : ne pas la compter ni la capturer (Game::endFrame).
}
device->present();
```

Exemple complet et compilé : `apps/demo/src/Game/Game.cpp` (`Game::createGraphics`, `beginFrame`, `render`, `endFrame`).

## Règles à retenir

- **Handles opaques**, destruction différée ; un handle périmé est refusé, jamais déréférencé. **Erreurs** : `Result<T>`, jamais d'exception.
- **Pas d'état implicite** : chaque passe déclare ses attachements, chaque pipeline porte tout son état fixe (`PipelineCache` les construit).
- **Liaison** : trois groupes (0 image, 1 matériau, 2 objet) × 8 liaisons ; OpenGL les aplatit en `groupe × 8 + binding`. Push constants : jusqu'à 128 octets.
- **Shaders** : GLSL 450 « style Vulkan » dans `shaders/rhi/`, compilés (shaderc) puis traduits (SPIRV-Cross) en GLSL 430 pour OpenGL ; rechargement à chaud : `ShaderLibrary::pollHotReload`.
- **Repère** (QUESTIONS A26, appliqué en M3) : origine en haut à gauche (ligne 0 = haut, viewport et ciseau depuis le haut), NDC y = +1 en haut, profondeur [0, 1], caméra en reverse-Z. OpenGL l'obtient par `glClipControl` (4.5 ou `GL_ARB_clip_control`).
- **Capacités optionnelles** : toujours lues dans `Capabilities`, jamais supposées (`rayQuery`, `bindless`, `meshShader`... toutes `false`).

## Preuves et images de référence

| Test | Ce qu'il fixe |
| --- | --- |
| capture de la démo (`--capture`) | `scene.png` comparé à la référence par `tools/compare_png.py` (CI, tolérance pour llvmpipe) |
| `tests/rhi/*`, `tests/renderer/*` | périphérique, ressources, shaders, fenêtre, dessinateur 2D, minuteries GPU |

`make check` (bloquant) : couches, en-têtes autonomes, isolation des API (aucun `gl*`, `Vk*`, `MTL*` hors de `src/rhi/`, aucun GLFW ni `<windows.h>` hors de `src/platform/` et `src/rhi/`).

## Ajouter un backend

Voir `docs/03` §9 et §14 (guide `ADD_A_BACKEND.md` prévu en M4) : implémenter `Device` et `CommandEncoder`, la table de formats, le modèle de liaison, passer les tests `tests/rhi/` et les images de référence.
