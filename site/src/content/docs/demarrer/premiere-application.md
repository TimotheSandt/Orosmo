---
title: Lire la boucle d'une application
description: "Le projet de démarrage d'orosmo init, écrit avec app::Application, puis les pièces que la façade crée dans l'ordre : fenêtre, périphérique, scène, interface, boucle d'images."
sidebar:
  order: 3
---

Le projet créé par `orosmo init` affiche un cube coloré qui tourne, deux lumières et un petit panneau `.cui`. Son
`main.cpp` tient en une trentaine de lignes : il décrit la scène et le panneau, et la façade `app::Application` fait le
reste (fenêtre, périphérique, renderers, interface, boucle). Source :
[`tools/engine-sdk/template/src/main.cpp`](https://github.com/TimotheSandt/Orosmo/blob/dev/tools/engine-sdk/template/src/main.cpp).

Cette page lit d'abord ce programme, puis les pièces que la façade crée et relie pour lui, dans l'ordre : l'API bas niveau,
toujours disponible, qu'on écrit à la main pour ce que la façade ne couvre pas, et où l'ordre des objets compte. Lisez
la section [Conventions des exemples](#conventions-des-exemples) : elle fixe les noms, les types et les chemins de
ressources utilisés dans tout le wiki. Pour un programme complet écrit avec la façade, voir le
[tutoriel du jeu 3D](/demarrer/tutoriel-jeu-3d/).

## Le programme de départ

```cpp
int main(int argc, char **argv)
{
    HudState state; // lié par le panneau : déclaré avant l'application, détruit après elle
    return app::Application::main(argc, argv, app::defaultSettings("mygame"), [&state](app::Application &a) -> orosmo::core::Result<> {
        auto mesh = a.addMesh(rd::MeshBuilder(rd::MeshLayout::kColored).addBox({1, 1, 1}, faces));   // une couleur par face
        if (!mesh)
            return std::unexpected(mesh.error());
        const orosmo::scene::Entity cube = a.spawn(app::MeshRenderer{.mesh = *mesh});
        if (auto spin = a.world().componentType<game::Spin>("Spin"))   // enregistré par la logique de jeu (src/game/)
            a.world().add(cube, *spin, game::Spin{});
        a.spawn(app::LightComponent{.type = rd::LightType::kAmbient, .intensity = 0.35f});
        a.spawn(app::LightComponent{.color = {1.0f, 0.96f, 0.90f}, .intensity = 0.85f, .direction = {-0.6f, -0.8f, -0.5f}});
        a.onUpdate([&state](app::Application &a, const app::FrameTime &) { /* compteur, case « Rotate cube », Échap */ });
        return a.setView(cui::HudView{.state = &state});
    });
}
```

(Extrait abrégé : le fichier écrit les noms en entier et ajoute les couleurs des faces et le corps de `onUpdate`.)

- `Application::main` lit la ligne de commande (`--api vulkan|opengl`, `--headless`, `--size`, `--frames`, `--capture`,
  `--capture-frames`, `--help`), fait du dossier de l'exécutable le dossier courant, ouvre le journal, crée l'application,
  appelle la fonction de préparation, puis tourne jusqu'à la fermeture de la fenêtre. Le périphérique est Vulkan, avec
  repli sur OpenGL s'il ne démarre pas.
- `defaultSettings` (`orosmo/app/default_game.hpp`, inclus par ce seul fichier) : la logique de jeu de `src/game/`,
  chargée depuis `bin/debug/game.so` et rechargée à chaud en développement, liée dans l'exécutable par `make release` ; le
  rechargement du panneau quand un `.cui` est enregistré ; les couches de validation et le journal `logs/log.log` en debug
  seulement.
- La scène est faite d'entités : `spawn` crée une entité avec un composant de rendu (`MeshRenderer`, `LightComponent`…)
  et une transformation. Le cube reçoit aussi le composant `Spin` de la logique de jeu, dont le système le fait tourner
  dans la simulation à pas fixe.
- `onUpdate` est appelé à chaque image ; `setView` installe le panneau comme racine de l'interface.

Le rechargement à chaud (logique de jeu et vues) est décrit dans
[Compiler avec Make ou CMake](/reference/construction/#logique-de-jeu-rechargeable).

## Ce que la façade crée, dans l'ordre

| Ordre | Objet | Rôle |
|---|---|---|
| 1 | `platform::Window` | fenêtre et entrées |
| 2 | `rhi::Device` (`rhi::createDevice`) | le GPU |
| 3 | `renderer::ShaderLibrary`, `PipelineCache` | partagés par tous les renderers, à garder vivants jusqu'à la fin |
| 4 | `SceneRenderer`, `CanvasRenderer` | la scène 3D, le dessin 2D de l'interface |
| 5 | `LightManager`, `Mesh` | lumières et objet dessiné |
| 6 | `ui::TextRenderer`, `TextRendererFont`, `ui::Ui` | police et interface |

`Application` y ajoute la simulation (`sim::Simulation`, avec le monde d'entités), le système qui dessine les composants de
rendu (`app::SceneRenderSystem`), le module de jeu et l'hôte de la vue `.cui` (`app::ViewHost`). Les objets de rang
supérieur gardent des références vers ceux de rang inférieur : détruisez-les dans l'ordre inverse. Les
poignées du RHI (`TextureHandle`…) ne sont pas possédées, et un `Mesh` ne possède pas son programme de shader ni son
groupe de matériau. Pour l'état lié à l'interface, déclarez-le avant `ui::Ui` : il est détruit après l'arbre de vues.

## Conventions des exemples

Tous les exemples du wiki suivent les mêmes noms et les mêmes règles, pour pouvoir être assemblés.

```cpp
namespace core     = orosmo::core;
namespace platform = orosmo::platform;
namespace rhi      = orosmo::rhi;
namespace rd       = orosmo::renderer;
namespace ui       = orosmo::ui;
```

| Nom | Type exact | Origine |
|---|---|---|
| `window` | `platform::Window` | créée en premier, détruite en dernier |
| `device` | `rhi::Device &` | `auto created = rhi::createDevice(desc); rhi::Device &device = **created;` (`created` est un `core::Result<std::unique_ptr<rhi::Device>>` qui doit rester vivant) |
| `shaders`, `pipelines` | `rd::ShaderLibrary`, `rd::PipelineCache` | `rd::ShaderLibrary shaders(device);` ; sans configuration, cela suffit pour charger les shaders du moteur ; une `ShaderLibraryConfig` n'est utile que pour un cache de shaders sur disque ou des dossiers d'inclusion (voir [Écrire et recharger des shaders](/guides/avance/shaders/)) |
| `renderer` | `rd::SceneRenderer &` | `auto sceneResult = rd::SceneRenderer::create(device, shaders, pipelines, {.shaderDirectory = shaderDir}); rd::SceneRenderer &renderer = **sceneResult;` |
| `canvas` | `rd::CanvasRenderer &` | même principe avec `rd::CanvasRenderer::create` |
| `r2d` | `rd::Renderer2D &` | même principe avec `rd::Renderer2D::create` |
| `gui` | `ui::Ui` | `ui::Ui gui(font);` (`font` est un `ui::TextRendererFont`) |
| `camera` | `rd::Camera` | objet local de l'application |
| `lights` | `rd::LightManager` | `auto lightsResult = rd::LightManager::create(device); rd::LightManager &lights = *lightsResult;` |
| `cube` | `rd::Mesh &` | `auto cubeResult = rd::Mesh::create(device, data); rd::Mesh &cube = *cubeResult;` |
| `encoder` | `std::unique_ptr<rhi::CommandEncoder>` | `auto encoder = device.createCommandEncoder();` ; les fonctions le prennent par `*encoder` |
| `shaderDir` | `std::filesystem::path` | `GET_RESOURCE_PATH("shader/rhi")` |

Un code qui garde le `Result` écrit `**device` et `(*scene)->` : c'est le même objet, sans la référence intermédiaire. Les
exemples du wiki utilisent les références ci-dessus, plus lisibles.

**Déballer un `Result`.** Les fabriques renvoient `core::Result<T>` (`std::expected<T, core::Error>`). Dans un exemple,
`if (!r) return …;` veut dire : écrire `r.error().message` et sortir, ou propager avec `return std::unexpected(r.error());`.
Un commentaire `// …` marque du code omis. Pour un `Result<T>` par valeur (`Mesh`, `LightManager`), on déréférence une fois
(`*lightsResult`) ; pour un `Result<std::unique_ptr<T>>` (`SceneRenderer`, `CanvasRenderer`, `Device`), deux fois.

### Chemins des ressources

Le moteur lit ses shaders, une police et les thèmes à l'exécution, **relativement au dossier courant**, qui est celui de
l'exécutable après `orosmo::core::setWorkingDirectoryToExecutable()` (`orosmo/core/paths.hpp`). Pour les écrire, utilisez
la macro `GET_RESOURCE_PATH("dossier/fichier")` : elle vaut `"res/dossier/fichier"` (sauf si le symbole `RELEASE` est défini,
ce que les `Makefile` du moteur ne font pas : elle vaut donc `res/…` en debug comme en release). Elle n'accepte qu'un
littéral de chaîne.

```cpp
#include <orosmo/core/paths.hpp>

const std::filesystem::path shaderDir = GET_RESOURCE_PATH("shader/rhi");
const char *font   = GET_RESOURCE_PATH("fonts/Roboto-Regular.ttf");
const char *theme  = GET_RESOURCE_PATH("themes/dark.json");
const char *model  = GET_RESOURCE_PATH("models/casque.glb");   // un asset de votre jeu
```

Arborescence d'un projet créé par `orosmo init`, puis du dossier de l'exécutable après `make debug` :

```text
~/.orosmo/engines/0.7.0/  le moteur installé (orosmo path 0.7.0) ; ne le modifiez pas, il sert à tous vos projets
  res/fonts, res/themes     police et thèmes par défaut
  shaders/                  shaders du RHI (shaders/rhi/…)
mygame/
  config.mk               OROSMO_VERSION := 0.7.0
  src/                    votre code et vos .cui
  res/                    (facultatif) vos ressources : voir ci-dessous
  Generated/              code C++ produit à partir des .cui
  bin/debug/
    main
    res/fonts, themes     copiés depuis res/ si ce dossier existe, sinon depuis le res/ du moteur
    res/shader/rhi        copiés depuis les shaders/ du moteur
    res/models/…          vos assets, s'ils sont dans res/
```

Un projet créé avec `orosmo init --vendor`, ou avant la `v0.6.2`, a le moteur dans `mygame/orosmo/` au lieu du dossier
installé ; le reste est identique.

:::caution
Le `Makefile` copie **un seul** dossier vers `bin/<type>/res/` : `res/` du projet s'il existe, sinon le `res/` du moteur
(variable `RES_DIR`). Si vous créez `res/` pour y mettre un modèle, la police et les thèmes du paquet ne sont plus copiés :
le texte ne s'affiche plus et un thème qui étend `dark.json` ne trouve plus son parent. Copiez d'abord ceux du moteur
que `make` utilise réellement (`make info` l'affiche : `ENGINE_DIR` donné, `orosmo/` d'un projet `--vendor`, ou
`orosmo path $(OROSMO_VERSION)`), dans le dossier du projet :

```bash
eng=$(ORS_NO_PROGRESS=1 make -s info | sed -n '/^Engine:/{s/^Engine: *//;s/ (OROSMO_VERSION.*//;p;}')
[ -n "$eng" ] && test -d "$eng/res" && mkdir -p res && cp -R "$eng/res/." res/   # contenu de res/ du moteur, même dans un res/ existant ; rien si eng est vide
```

puis ajoutez vos fichiers (`res/models/`, `res/textures/`…). Les shaders, eux, sont toujours copiés depuis le moteur. Conséquence : `orosmo update` ne met plus à jour
la police et les thèmes de votre `res/`.
:::

## Fenêtre et périphérique

```cpp
platform::Window window("monjeu", 1280, 720, platform::WindowState::kWindowed);
rhi::DeviceDesc desc;
desc.backend = rhi::Backend::kOpenGL;
desc.validation = true;                 // defaultSettings : en debug seulement

if (!window.init([&] { (void)rhi::prepareWindowHints(desc); })) { /* pas d'affichage ? */ }
desc.surface.nativeWindow = window.getWindow();

auto created = rhi::createDevice(desc);  // core::Result<std::unique_ptr<rhi::Device>>
if (!created) { std::cerr << created.error().message << '\n'; return EXIT_FAILURE; }
rhi::Device &device = **created;
```

`prepareWindowHints` pose les attributs de contexte du backend juste avant la création de la fenêtre : il faut donc
l'appeler dans le rappel de `init`. `Window::init` rend un `core::Result<>` depuis M8 (avant : un `int`, `0` en cas de
succès ; un ancien `window.init() != 0` ne compile plus, G270). Le quatrième argument du
constructeur (`kWindowed`) compte : sans lui, la fenêtre s'ouvre en plein écran dans un programme compilé sans `DEBUG`
(avec la façade : `ApplicationSettings::windowed` ; voir [Fenêtre, clavier et souris](/guides/application/fenetre-entrees/)).
Pour Vulkan avec repli sur OpenGL, comme le fait la façade, remplacez ce bloc par celui de
[Choisir le backend graphique](/guides/application/peripherique/).

## Shaders, scène et lumières

```cpp
const std::filesystem::path shaderDir = GET_RESOURCE_PATH("shader/rhi");
rd::ShaderLibrary shaders(device);
rd::PipelineCache pipelines(device);

auto lightsResult = rd::LightManager::create(device);   // avant les renderers : détruit après eux (ils gardent un pointeur)
auto sceneResult  = rd::SceneRenderer::create(device, shaders, pipelines, {.shaderDirectory = shaderDir});
auto canvasResult = rd::CanvasRenderer::create(device, shaders, pipelines, shaderDir);
auto meshProgram  = shaders.loadGraphics(shaderDir / "mesh.vert", shaderDir / "mesh.frag");
auto cubeResult   = rd::Mesh::create(device, makeColoredCube());   // MeshData : sommets, indices, attributeSizes
if (!sceneResult || !canvasResult || !lightsResult || !meshProgram || !cubeResult) { /* afficher l'erreur, sortir */ }
rd::SceneRenderer  &renderer = **sceneResult;
rd::CanvasRenderer &canvas   = **canvasResult;
rd::LightManager   &lights   = *lightsResult;
rd::Mesh           &cube     = *cubeResult;
cube.setProgram(*meshProgram);                          // un Mesh sans programme n'est pas dessiné

lights.setAmbientLight({1.0f, 1.0f, 1.0f}, 0.35f);
lights.addLight(rd::directionalLight({0.6f, 0.8f, 0.5f}, {1.0f, 0.96f, 0.90f}, 0.85f));
if (auto uploaded = lights.upload(); !uploaded) { /* ... */ }
renderer.setLights(&lights);         // pointeur gardé : `lights` doit vivre aussi longtemps que `renderer`
```

Les shaders sont lus dans `res/shader/rhi/`, que le `Makefile` copie à côté de l'exécutable (voir
[Chemins des ressources](#chemins-des-ressources)).

## Interface

```cpp
ui::TextRenderer text;
if (!text.init(1280, 720) || !text.loadFont(GET_RESOURCE_PATH("fonts/Roboto-Regular.ttf"), "default", 48)) { /* ... */ }
text.setActiveFont("default");
ui::TextRendererFont font(text);
HudState state;                       // avant `gui`
ui::Ui gui(font);
gui.themes().load(GET_RESOURCE_PATH("themes/dark.json"));
gui.setRoot(cui::buildHudView(gui, state));
```

`HudView.gen.h` est produit à la compilation par le compilateur `.cui` à partir de `HudView.cui` ; `cui::buildHudView`
relie la vue à `HudState`. Voir [Écrire une vue en .cui](/guides/interface/langage-cui/).

## La boucle d'une image

```cpp
rd::RenderTarget composed;     // cible couleur de la taille de la fenêtre, créée à la première image (voir plus bas)

while (!window.shouldClose()) {
    if (!window.newFrame()) continue;                   // événements, entrées, compteur de FPS

    ui::InputFrame input = ui::readWindowInput(window, dt);   // dt : durée de l'image précédente, en secondes
    gui.update(input, {viewW, viewH});

    auto backbuffer = device.beginFrame();              // image de la fenêtre ; erreur si la fenêtre est réduite
    if (!backbuffer) continue;                          // sauter l'image, pas quitter

    auto encoder = device.createCommandEncoder();       // un encodeur par image
    renderer.setOutputResolution(static_cast<int>(size.width), static_cast<int>(size.height));   // sinon beginFrame refuse 0 × 0
    auto ok = renderer.beginFrame(*encoder, {.clearColor = {0.10f, 0.13f, 0.18f, 1.0f}});
    if (ok) ok = renderer.setCamera(camera);
    if (ok) ok = renderer.drawMesh(cube, camera);       // enregistré, pas encore dessiné
    if (ok) ok = renderer.endFrame({composed.color(0), rhi::Format::kRGBA8Unorm, size});   // le graphe est exécuté ici

    rd::DrawList drawList;
    gui.draw(drawList);
    if (ok) ok = canvas.render(*encoder, drawList, {composed.color(0), rhi::Format::kRGBA8Unorm, size}, true);
    if (ok) ok = renderer.blit().copy(*encoder, composed.color(0), renderer.blit().linearSampler(), *backbuffer,
                                      rhi::Format::kRGBA8Unorm, "present");
    auto submitted = device.submit(std::move(encoder));
    (void)device.present();                             // toujours, même après une erreur
}
device.waitIdle();
```

(Extrait abrégé : `size` est un `rhi::Extent2D` en pixels du framebuffer. `composed` se crée par
`rd::RenderTarget::create(device, {.size = size, .depth = false})` et se redimensionne par `composed.resize(w, h)` quand la
fenêtre change de taille ; `Application::frame` ajoute aussi la gestion d'erreur et la capture.)

Il y a deux `beginFrame` : celui du `Device` rend l'image de la fenêtre, celui du `SceneRenderer` prend l'encodeur. `drawMesh`
**enregistre** un dessin ; `endFrame` construit le graphe de passes et l'exécute sur l'encodeur. L'interface est dessinée
après `endFrame`, sur la même cible (le dernier argument de `render`, `true`, garde ce qui est déjà dessiné). Tout
travail compute (comme le skinning) doit être enregistré sur le même encodeur avant `SceneRenderer::beginFrame`.
À la fin, `Window::terminate()` s'appelle après la destruction des fenêtres et du périphérique.

## Erreurs

Les règles pour déballer un `Result` sont dans les [conventions](#conventions-des-exemples). 
`error().message` est prêt à afficher. Deux
exceptions viennent de l'ancien code : `TextRenderer::init` et `loadFont` rendent un `bool` (`Window::init` rend un
`Result<>` depuis M8).

## Le projet sans GPU

`tools/engine-sdk/examples/minimal/` n'utilise que le noyau (tâches, réflexion, JSON) et se compile par `make -C examples/minimal`
dans le paquet. Il écrit `{"data":{"name":"minimal","sum":524800,"tasksCompleted":1024},"version":1}`.

## Limites

- La façade a un seul ensemble de renderers (une fenêtre, une scène, une interface) ; plusieurs vues ou fenêtres demandent
  l'API bas niveau.
- Une poignée de ressource (`core::Handle`) n'a de sens que dans l'exécution qui l'a donnée : une scène sauvegardée garde
  ses composants de rendu, pas leurs maillages.

Détails : [État du moteur](/projet/etat/).
