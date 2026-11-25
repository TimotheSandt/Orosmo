---
title: Lire la boucle d'une application
description: Les pièces du projet de démarrage d'orosmo init, dans l'ordre : fenêtre, périphérique, scène, interface, boucle d'images.
sidebar:
  order: 3
---

Le projet créé par `orosmo init` affiche un cube coloré, une lumière et un petit panneau `.cui`. Son `main.cpp` fait
environ 400 lignes, dont 210 de mise en place et de boucle. Cette page le lit pièce par pièce. Source :
[`tools/engine-sdk/template/src/main.cpp`](https://github.com/TimotheSandt/Orosmo/blob/dev/tools/engine-sdk/template/src/main.cpp).

Il n'y a pas de couche « application » qui fait tout cela pour vous : chaque objet est créé à la main, et leur ordre compte.

## Ce qui est créé, dans l'ordre

| Ordre | Objet | Rôle |
|---|---|---|
| 1 | `platform::Window` | fenêtre et entrées |
| 2 | `rhi::Device` (`rhi::createDevice`) | le GPU |
| 3 | `renderer::ShaderLibrary`, `PipelineCache` | partagés par tous les renderers, à garder vivants jusqu'à la fin |
| 4 | `SceneRenderer`, `CanvasRenderer` | la scène 3D, le dessin 2D de l'interface |
| 5 | `LightManager`, `Mesh` | lumières et objet dessiné |
| 6 | `ui::TextRenderer`, `TextRendererFont`, `ui::Ui` | police et interface |

Les objets de rang supérieur gardent des références vers ceux de rang inférieur : détruisez-les dans l'ordre inverse. Les
poignées du RHI (`TextureHandle`…) ne sont pas possédées, et un `Mesh` ne possède pas son programme de shader ni son
groupe de matériau. Pour l'état lié à l'interface, déclarez-le avant `ui::Ui` : il est détruit après l'arbre de vues.

## Fenêtre et périphérique

```cpp
platform::Window window("monjeu", 1280, 720);
rhi::DeviceDesc desc;
desc.backend = rhi::Backend::kOpenGL;
desc.validation = true;                 // le starter le met à true en debug seulement

if (window.init([&] { (void)rhi::prepareWindowHints(desc); }) != 0) { /* pas d'affichage ? */ }
desc.surface.nativeWindow = window.getWindow();

auto device = rhi::createDevice(desc);  // Result<std::unique_ptr<Device>>
if (!device) { std::cerr << device.error().message << '\n'; return EXIT_FAILURE; }
```

`prepareWindowHints` pose les attributs de contexte du backend juste avant la création de la fenêtre : il faut donc
l'appeler dans le rappel de `init`. `Window::init` renvoie `0` en cas de succès, pas un `Result`. Voir
[Choisir le backend graphique](/guides/application/peripherique/).

## Shaders, scène et lumières

```cpp
const fs::path shaderDir = GET_RESOURCE_PATH("shader/rhi");
rd::ShaderLibrary shaders(**device);
rd::PipelineCache pipelines(**device);

auto scene  = rd::SceneRenderer::create(**device, shaders, pipelines, {.shaderDirectory = shaderDir});
auto canvas = rd::CanvasRenderer::create(**device, shaders, pipelines, shaderDir);
auto lights = rd::LightManager::create(**device);
auto meshProgram = shaders.loadGraphics(shaderDir / "mesh.vert", shaderDir / "mesh.frag");
auto cube = rd::Mesh::create(**device, makeColoredCube());   // MeshData : sommets, indices, attributeSizes
cube->setProgram(*meshProgram);                              // un Mesh sans programme n'est pas dessiné

lights->setAmbientLight({1.0f, 1.0f, 1.0f}, 0.35f);
lights->addLight(rd::directionalLight({0.6f, 0.8f, 0.5f}, {1.0f, 0.96f, 0.90f}, 0.85f));
if (auto uploaded = lights->upload(); !uploaded) { /* ... */ }
(*scene)->setLights(&*lights);       // pointeur gardé : `lights` doit vivre aussi longtemps que `scene`
```

Chaque fabrique renvoie un `Result` : d'où les `**device` (un `Result` de `unique_ptr`) et les `(*scene)->`. Les shaders sont
lus dans `shaders/rhi/`, que le `Makefile` copie dans `res/shader/rhi/` à côté de l'exécutable ; `GET_RESOURCE_PATH`
ajoute `res/` en debug.

## Interface

```cpp
ui::TextRenderer text;
if (!text.init(1280, 720) || !text.loadFont(GET_RESOURCE_PATH("fonts/Roboto-Regular.ttf"), "default", 48)) { /* ... */ }
text.setActiveFont("default");
ui::TextRendererFont font(text);
HudState state;                       // avant `interface`
ui::Ui interface(font);
interface.themes().load(GET_RESOURCE_PATH("themes/dark.json"));
interface.setRoot(cui::buildHudView(interface, state));
```

`HudView.gen.h` est produit à la compilation par le compilateur `.cui` à partir de `HudView.cui` ; `cui::buildHudView`
relie la vue à `HudState`. Voir [Écrire une vue en .cui](/guides/interface/langage-cui/).

## La boucle d'une image

```cpp
while (!window.shouldClose()) {
    if (!window.newFrame()) continue;                   // événements, entrées, compteur de FPS

    ui::InputFrame input = ui::readWindowInput(window, dt);
    interface.update(input, {viewW, viewH});

    auto backbuffer = (*device)->beginFrame();          // image de la fenêtre ; erreur si la fenêtre est réduite
    if (!backbuffer) continue;                          // sauter l'image, pas quitter

    auto encoder = (*device)->createCommandEncoder();   // un encodeur par image
    auto ok = (*scene)->beginFrame(*encoder, {.clearColor = {0.10f, 0.13f, 0.18f, 1.0f}});
    if (ok) ok = (*scene)->setCamera(camera);
    if (ok) ok = (*scene)->drawMesh(*cube, camera);     // enregistré, pas encore dessiné
    if (ok) ok = (*scene)->endFrame({composed.color(0), rhi::Format::kRGBA8Unorm, size});   // le graphe est exécuté ici

    rd::DrawList drawList;
    interface.draw(drawList);
    if (ok) ok = (*canvas)->render(*encoder, drawList, {composed.color(0), rhi::Format::kRGBA8Unorm, size}, true);
    if (ok) ok = (*scene)->blit().copy(*encoder, composed.color(0), (*scene)->blit().linearSampler(), *backbuffer,
                                       rhi::Format::kRGBA8Unorm, "present");
    auto submitted = (*device)->submit(std::move(encoder));
    (void)(*device)->present();                         // toujours, même après une erreur
}
(*device)->waitIdle();
```

(Extrait abrégé : le starter ajoute la gestion d'erreur, la capture et la taille de la cible `composed`.)

Il y a deux `beginFrame` : celui du `Device` rend l'image de la fenêtre, celui du `SceneRenderer` prend l'encodeur. `drawMesh`
**enregistre** un dessin ; `endFrame` construit le graphe de passes et l'exécute sur l'encodeur. L'interface est dessinée
après `endFrame`, sur la même cible (le dernier argument de `render`, `true`, garde ce qui est déjà dessiné). Tout
travail compute (comme le skinning) doit être enregistré sur le même encodeur avant `SceneRenderer::beginFrame`.
À la fin, `Window::terminate()` s'appelle après la destruction des fenêtres et du périphérique.

## Erreurs

Les fabriques renvoient `core::Result<T>` (`std::expected<T, Error>`) ; `error().message` est prêt à afficher. Deux
exceptions viennent de l'ancien code : `Window::init` renvoie un `int`, `TextRenderer::init` et `loadFont` un `bool`.

## Le projet sans GPU

`tools/engine-sdk/examples/minimal/` n'utilise que le noyau (tâches, réflexion, JSON) et se compile par `make -C examples/minimal`
dans le paquet. Il écrit `{"data":{"name":"minimal","sum":524800,"tasksCompleted":1024},"version":1}`.

## Limites

- Pas de scène à entités (ECS-01 à ECS-03 Absents), pas de boucle de simulation (SIM-01 Absent), pas de logique de jeu
  rechargeable (GAME-01 à GAME-07 Absents) : l'application tient elle-même ses objets.
- Le starter ne gère qu'OpenGL (G209).
