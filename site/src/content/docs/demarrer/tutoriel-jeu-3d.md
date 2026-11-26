---
title: "Tutoriel : un petit jeu 3D"
description: "De orosmo init à un personnage glTF animé, éclairé par un soleil qui projette des ombres, avec une caméra au clavier et un menu .cui."
sidebar:
  order: 4
  label: Tutoriel jeu 3D
---

Ce tutoriel part d'un projet créé par `orosmo init` et aboutit à un programme complet : un personnage glTF animé sur un
sol, un soleil dont l'ombre suit l'animation, un rendu HDR avec tonemapping, une caméra pilotée au clavier et à la souris,
et un menu `.cui` qu'Échap ouvre et ferme, avec un bouton Quitter.

Le programme tient en un fichier, `src/main.cpp`, plus l'état du menu et sa vue. Les blocs de code de cette page, mis bout
à bout, forment ce fichier. Il a été compilé avec les options du projet (`-Wall -Wextra -Wpedantic -Werror`) et lancé
sous Linux avec OpenGL et le modèle CesiumMan ci-dessous : personnage animé, ombre portée sur le sol, menu, aucune erreur
de validation. Cette vérification a été faite à la main (captures comparées) ; aucun test automatique ne couvre ce
programme ni l'ombre d'un modèle animé.

## Préparer le projet

```bash
orosmo init monjeu && cd monjeu          # voir « Créer un projet avec orosmo init »
v=$(sed -n 's/^OROSMO_VERSION *[:?]*= *//p' config.mk)   # version choisie par le projet (OROSMO_VERSION, config.mk)
cp -r "$(orosmo path "$v")/res" res      # police et thèmes de ce moteur dans res/
mkdir -p res/models                      # le personnage ira dans res/models/hero.glb (voir plus bas)
rm src/HudView.cui src/hud_state.hpp     # le panneau du projet de démarrage est remplacé par le menu
```

Pourquoi copier le `res/` du moteur : dès que le projet a son propre dossier `res/`, c'est le seul copié à côté de l'exécutable
(voir [Chemins des ressources](/demarrer/premiere-application/#chemins-des-ressources)).

Il faut un modèle glTF avec une peau (skin) et au moins une animation. Le chemin est la constante `kHeroModel`, en haut
de `src/main.cpp` (section 1) : `res/models/hero.glb` par défaut.

Le modèle essayé est CesiumMan, du dépôt d'exemples de Khronos
([glTF-Sample-Assets](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/CesiumMan)) : un personnage
d'environ 1,5 m, debout selon +Y, avec une animation de marche. Il s'affiche sans réglage d'échelle ni de rotation.

```bash
curl -L -o res/models/hero.glb \
  https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models/CesiumMan/glTF-Binary/CesiumMan.glb
```

Licence : © 2017 Cesium, [Creative Commons Attribution 4.0](https://creativecommons.org/licenses/by/4.0/) (CC-BY-4.0).
Si vous distribuez le modèle, citez « Cesium Man, © 2017 Cesium, CC-BY 4.0 » ; le logo Cesium de la texture est une
marque, exclue de cette licence.

Autre source, si vous avez compilé le dépôt du moteur (voir [Compiler le moteur](/demarrer/compiler-le-moteur/)) :
`apps/demo/bin/debug/main --scene anim` écrit un personnage généré par le code, `figure.gltf`, `figure.bin` et
`figure_albedo.png`, dans le dossier `orosmo-demo-anim` du dossier temporaire du système (`/tmp/orosmo-demo-anim/` sous
Linux, vidé au redémarrage). Copiez les trois fichiers dans `res/models/` et donnez à `kHeroModel` la valeur
`GET_RESOURCE_PATH("models/figure.gltf")`. Ce modèle a aussi été essayé avec le programme.

## L'état du menu et sa vue

Ces deux fichiers sont ceux de l'exemple [Un menu qui s'ouvre avec Échap](/guides/interface/langage-cui/#exemple--un-menu-qui-souvre-avec-échap).
La vue ne décide de rien : elle affiche `state.open` et ses boutons changent des champs que la boucle C++ lit.

```cpp title="src/menu_state.hpp"
#pragma once

#include <array>
#include <string>

struct MenuState
{
    bool open = false;        // menu visible ; le jeu se met en pause tant qu'il l'est
    bool quit = false;        // mis à vrai par le bouton Quitter ; la boucle principale le lit
    int theme = 0;            // index dans kThemes
    bool themeChanged = true; // la boucle recharge le thème quand c'est vrai

    static constexpr std::array<const char *, 3> kThemes{"dark", "light", "framed"};

    void toggle() { open = !open; }
    void resume() { open = false; }
    void requestQuit() { quit = true; }
    void nextTheme()
    {
        theme = (theme + 1) % static_cast<int>(kThemes.size());
        themeChanged = true;
    }
    std::string themeName() const { return kThemes[static_cast<std::size_t>(theme)]; }
};
```

```text title="src/MenuView.cui"
import "orosmo/ui"
import "menu_state.hpp"

view MenuView {
    @Mutable var state: MenuState

    Overlay {
        Panel {
            Label("Menu").variant("title")
            Button("Reprendre").onClick(state.resume).id("resume")
            Button("Thème : \(state.themeName())").onClick(state.nextTheme).id("theme")
            Button("Quitter").onClick(state.requestQuit).id("quit")
        }
        .width(260)
        .height(.fit)
        .align(.center)
        .visible(state.open)
    }
}
```

Le `Makefile` du projet compile chaque `.cui` de `src/` en `MenuView.gen.h` et `MenuView.gen.cpp`, qui définissent
`cui::buildMenuView(ui, state)`.

## 1. En-têtes et noms

Le fichier commence par les en-têtes du moteur. Les alias suivent les
[conventions des exemples](/demarrer/premiere-application/#conventions-des-exemples), avec `as` (contenus) et `an`
(animation) en plus. `MenuView.gen.h` est produit à la compilation à partir de `MenuView.cui`.

```cpp title="src/main.cpp"
// main.cpp du tutoriel « Un petit jeu 3D » : un personnage glTF animé sur un sol, éclairé par un soleil qui projette des ombres, une caméra au clavier et à la
// souris, un menu .cui ouvert par Échap.
#include "MenuView.gen.h"
#include "menu_state.hpp"

#include <orosmo/anim/clip.hpp>
#include <orosmo/anim/pose.hpp>
#include <orosmo/anim/skinning.hpp>
#include <orosmo/anim/state_machine.hpp>
#include <orosmo/assets/model.hpp>
#include <orosmo/assets/render_model.hpp>
#include <orosmo/assets/texture_cache.hpp>
#include <orosmo/core/log.hpp>
#include <orosmo/core/paths.hpp>
#include <orosmo/platform/input_manager.hpp>
#include <orosmo/platform/window.hpp>
#include <orosmo/renderer/camera.hpp>
#include <orosmo/renderer/canvas_renderer.hpp>
#include <orosmo/renderer/draw_list.hpp>
#include <orosmo/renderer/light.hpp>
#include <orosmo/renderer/material.hpp>
#include <orosmo/renderer/pipeline_cache.hpp>
#include <orosmo/renderer/post_effects.hpp>
#include <orosmo/renderer/render_target.hpp>
#include <orosmo/renderer/scene_renderer.hpp>
#include <orosmo/renderer/shader_library.hpp>
#include <orosmo/renderer/shadow_feature.hpp>
#include <orosmo/rhi/device.hpp>
#include <orosmo/ui/font.hpp>
#include <orosmo/ui/text_renderer.hpp>
#include <orosmo/ui/ui.hpp>
#include <orosmo/ui/window_input.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace core = orosmo::core;
namespace platform = orosmo::platform;
namespace rhi = orosmo::rhi;
namespace rd = orosmo::renderer;
namespace ui = orosmo::ui;
namespace as = orosmo::assets;
namespace an = orosmo::anim;

namespace
{
constexpr int kWidth = 1280;
constexpr int kHeight = 720;
// Modèle du personnage (glTF avec peau et animations), relatif au dossier de l'exécutable.
constexpr const char *kHeroModel = GET_RESOURCE_PATH("models/hero.glb");
```

## 2. Le personnage animé

`loadHero` suit le chemin de [Animer un personnage](/guides/animation/squelettes/) :

- `as::loadGltf` lit le fichier sur le CPU ;
- `as::RenderModel::create` crée un maillage et un matériau PBR par primitive. `options.skinnable` donne aux maillages un
  tampon de sommets que le compute de skinning peut réécrire ; `options.shadows` rend les matériaux récepteurs d'ombres
  (aucune macro de shader à ajouter à la main) ;
- `an::SkinnedModel` déforme les sommets selon une pose. Il demande le compute : on le lit dans les capacités
  (`supportedBy`) au lieu de le supposer. Sans lui, le personnage reste en pose de repos ;
- un `an::Clip` par animation du fichier, un état par clip dans une `an::StateMachine`, et le premier clip joué en boucle.

L'ordre des membres de `Hero` compte : `skinned` garde un pointeur vers `*render` (d'où le `unique_ptr`, qui ne bouge pas)
et doit être détruit avant lui ; les états de la machine pointent dans `clips`, qui ne doit plus grandir après.

```cpp title="src/main.cpp (suite)"
// ── 2. Le personnage animé
// Ordre des membres = ordre de destruction inverse : `skinned` référence `render`, les états de `machine` pointent dans `clips`.
struct Hero
{
    as::Model model;
    std::vector<an::Clip> clips;
    std::optional<an::StateMachine> machine;
    std::unique_ptr<as::RenderModel> render; // adresse fixe : SkinnedModel garde un pointeur vers lui
    std::optional<an::SkinnedModel> skinned; // détruit avant `render`
};

core::Result<> loadHero(Hero &hero, rhi::Device &device, rd::ShaderLibrary &shaders, rd::PipelineCache &pipelines,
                        const std::filesystem::path &shaderDir, as::TextureCache &textures, rd::PbrShaderCache &pbr,
                        const std::shared_ptr<rd::PbrDefaults> &defaults, const rd::ShadowFeature *shadows)
{
    auto loaded = as::loadGltf(kHeroModel);
    if (!loaded)
        return std::unexpected(loaded.error());
    hero.model = std::move(*loaded);

    const bool canSkin = an::SkinnedModel::supportedBy(device.capabilities());
    as::RenderModelOptions options;
    options.sourceKey = "hero";
    options.skinnable = canSkin; // tampon de sommets réécrit par le compute de skinning
    options.shadows = shadows;   // matériaux récepteurs d'ombres
    auto render = as::RenderModel::create(device, hero.model, textures, pbr, defaults, options);
    if (!render)
        return std::unexpected(render.error());
    hero.render = std::make_unique<as::RenderModel>(std::move(*render));

    if (canSkin)
    {
        auto skinned = an::SkinnedModel::create(device, shaders, pipelines, shaderDir, hero.model, *hero.render);
        if (!skinned)
            return std::unexpected(skinned.error());
        hero.skinned.emplace(std::move(*skinned));
    }

    // Un état par animation du fichier ; on joue la première en boucle.
    for (std::size_t i = 0; i < hero.model.animations.size(); ++i)
    {
        auto clip = an::Clip::fromModel(hero.model, i);
        if (!clip)
            return std::unexpected(clip.error());
        hero.clips.push_back(std::move(*clip));
    }
    hero.machine.emplace(an::restPose(hero.model));
    for (const an::Clip &clip : hero.clips) // `clips` ne change plus : les pointeurs restent valides
    {
        if (auto r = hero.machine->addState({clip.name(), &clip, an::LoopMode::kLoop, 1.0f}); !r)
            return r;
    }
    if (!hero.clips.empty())
    {
        if (auto r = hero.machine->start(hero.clips.front().name()); !r)
            return r;
    }
    return {};
}
```

## 3. Le sol, un modèle construit par le code

Le sol est un `as::Model` écrit à la main : un carré, un matériau. Il passe ainsi par le même chemin PBR que le personnage
et reçoit les ombres de la même façon. Son matériau est non métallique (`metallicFactor = 0`) : un métal pur ne renvoie
rien sous la lumière ambiante (voir [Métal sans environnement](/guides/rendu-3d/materiaux-pbr/#métal-sans-environnement)).

```cpp title="src/main.cpp (suite)"
// ── 3. Le sol, un modèle construit par le code
as::Model makeGround(float half)
{
    as::MeshPrimitive quad;
    quad.positions = {{-half, 0.0f, half}, {half, 0.0f, half}, {half, 0.0f, -half}, {-half, 0.0f, -half}};
    quad.normals.assign(4, core::Vec3(0.0f, 1.0f, 0.0f));
    quad.uv0 = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
    quad.indices = {0, 1, 2, 0, 2, 3}; // sens direct (anti-horaire) vu d'en haut
    quad.material = 0;

    as::MaterialData stone;
    stone.name = "sol";
    stone.baseColorFactor = {0.55f, 0.55f, 0.5f, 1.0f};
    stone.metallicFactor = 0.0f; // diélectrique : éclairé aussi par l'ambiante
    stone.roughnessFactor = 0.9f;

    as::Model model;
    model.meshes.push_back({"sol", {quad}});
    model.materials.push_back(stone);
    model.nodes.resize(1);
    model.nodes[0].mesh = 0;
    model.scenes.push_back({"scene", {0}});
    model.defaultScene = 0;
    return model;
}
```

## 4. Le contrôleur de caméra

Repris de [Fenêtre, clavier et souris](/guides/application/fenetre-entrees/). Les actions nommées sont liées une fois ;
la vitesse est multipliée par la durée de l'image `dt`. Le clic droit maintenu capture le curseur pour tourner la vue.
`gui.wantsKeyboard()` et `gui.wantsMouse()` évitent de bouger la caméra quand l'interface utilise le clavier ou la souris.

Un `KeyButton` désigne la touche qui porte la lettre dans la disposition active : `kW` est la touche marquée W, sur un
clavier QWERTY comme AZERTY. Pour des touches à la même place que W A S D sur un clavier AZERTY, liez `kZ`, `kQ`, `kS`,
`kD` ; `InputManager::getKeyLayout()` donne la disposition détectée.

```cpp title="src/main.cpp (suite)"
// ── 4. Le contrôleur de caméra
struct CameraController
{
    bool looking = false;
    glm::dvec2 last{0.0};

    static void bindKeys()
    {
        using platform::InputManager;
        using platform::KeyButton;
        InputManager::bindActionToInput("forward", KeyButton::kW); // la touche qui porte la lettre W
        InputManager::bindActionToInput("back", KeyButton::kS);
        InputManager::bindActionToInput("left", KeyButton::kA);
        InputManager::bindActionToInput("right", KeyButton::kD);
        InputManager::bindActionToInput("up", KeyButton::kSpace);
        InputManager::bindActionToInput("down", KeyButton::kLeftShift);
        InputManager::bindActionToInput("look", platform::MouseButton::kRight);
    }

    void update(platform::Window &window, const ui::Ui &gui, rd::Camera &camera, float dt)
    {
        platform::InputManager &input = platform::InputManager::getInstance(window.getWindow());
        const float distance = 4.0f * dt; // 4 m/s
        if (!gui.wantsKeyboard())
        {
            if (input.isActionActive("forward")) camera.moveForward(distance);
            if (input.isActionActive("back")) camera.moveForward(-distance);
            if (input.isActionActive("right")) camera.moveRight(distance);
            if (input.isActionActive("left")) camera.moveRight(-distance);
            if (input.isActionActive("up")) camera.moveUp(distance);
            if (input.isActionActive("down")) camera.moveUp(-distance);
        }
        if (input.isActionActive("look") && !gui.wantsMouse())
        {
            window.setCursorMode(platform::CursorMode::kDisabled); // curseur capturé
            const glm::dvec2 mouse = window.getCursorPosition();
            if (looking)
            {
                camera.rotateYaw(-0.1f * static_cast<float>(mouse.x - last.x)); // 0,1 degré par pixel
                camera.rotatePitch(-0.1f * static_cast<float>(mouse.y - last.y));
            }
            last = mouse;
            looking = true;
        }
        else
        {
            release(window);
        }
    }

    // Rend le curseur : appelé aussi quand le menu s'ouvre en pause, où update n'est plus appelé.
    void release(platform::Window &window)
    {
        window.setCursorMode(platform::CursorMode::kNormal);
        looking = false;
    }
};

int run()
{
```

## 5. Fenêtre et périphérique

La fenêtre s'ouvre en mode fenêtré (`kWindowed` : sans cet argument, un programme compilé sans `DEBUG` s'ouvre en plein
écran). Le périphérique est OpenGL, comme dans le projet de démarrage ; pour Vulkan avec repli sur OpenGL, remplacez ce
bloc par celui de [Choisir le backend graphique](/guides/application/peripherique/). Les couches de validation ne sont
actives qu'en debug.

```cpp title="src/main.cpp (suite)"
    // ── 5. Fenêtre et périphérique
    platform::Window window("Tutoriel", kWidth, kHeight, platform::WindowState::kWindowed);
    rhi::DeviceDesc desc;
    desc.backend = rhi::Backend::kOpenGL;
#ifdef DEBUG
    desc.validation = true;
#endif
    if (window.init([&] { (void)rhi::prepareWindowHints(desc); }) != 0)
    {
        std::cerr << "Window: no display?\n";
        return EXIT_FAILURE;
    }
    desc.surface.nativeWindow = window.getWindow();
    auto created = rhi::createDevice(desc);
    if (!created)
    {
        std::cerr << "Device: " << created.error().message << '\n';
        return EXIT_FAILURE;
    }
    rhi::Device &device = **created;
```

## 6. Renderer, soleil, ombres, tonemapping

Trois choix à comprendre :

- le plugin d'ombres est créé et enregistré avant les modèles : leurs matériaux reçoivent les liaisons de l'atlas
  d'ombres à leur création ;
- le soleil est en `ShadowMode::kDynamic`. En `kCached`, l'ombre d'un personnage qui s'anime sur place ne serait pas
  redessinée (voir [Ajouter des ombres](/guides/rendu-3d/ombres/#un-modèle-animé-skinning)). Sa `direction` va de la
  surface vers le soleil ;
- la scène est en HDR (`kRGBA16Float`) et `TonemapFeature` expose, compresse et encode la sortie en sRGB. Sans lui, les
  matériaux PBR s'affichent trop sombres et trop contrastés (voir [HDR et post-traitement](/guides/rendu-3d/post-traitement/)).

`LightManager::upload()` envoie les lumières au GPU ; le renderer et le plugin d'ombres gardent tous deux un pointeur vers
`lights`, qui doit vivre plus longtemps qu'eux.

```cpp title="src/main.cpp (suite)"
    // ── 6. Renderer, soleil, ombres, tonemapping
    const std::filesystem::path shaderDir = GET_RESOURCE_PATH("shader/rhi");
    rd::ShaderLibrary shaders(device);
    rd::PipelineCache pipelines(device);
    auto lightsResult = rd::LightManager::create(device);   // déclaré AVANT les renderers : détruit après eux
    auto sceneResult = rd::SceneRenderer::create(device, shaders, pipelines, {.shaderDirectory = shaderDir});
    auto canvasResult = rd::CanvasRenderer::create(device, shaders, pipelines, shaderDir);
    if (!sceneResult || !canvasResult || !lightsResult)
    {
        std::cerr << "Renderer: initialization failed\n";
        return EXIT_FAILURE;
    }
    rd::SceneRenderer &renderer = **sceneResult;
    rd::CanvasRenderer &canvas = **canvasResult;
    rd::LightManager &lights = *lightsResult;

    // Ombres : le plugin existe AVANT les modèles, dont les matériaux reçoivent ses liaisons à la création.
    auto shadowResult = rd::ShadowFeature::create(device, shaders, pipelines, shaderDir);
    if (!shadowResult)
    {
        std::cerr << "Shadows: " << shadowResult.error().message << '\n';
        return EXIT_FAILURE;
    }
    auto addedShadows = renderer.addFeature(std::move(*shadowResult));
    if (!addedShadows)
    {
        std::cerr << "Shadows: " << addedShadows.error().message << '\n';
        return EXIT_FAILURE;
    }
    auto *shadows = static_cast<rd::ShadowFeature *>(*addedShadows);

    lights.setAmbientLight({0.75f, 0.85f, 1.0f}, 0.3f);
    rd::Light sun = rd::directionalLight({0.4f, 0.8f, 0.3f} /* vers le soleil */, {1.0f, 0.95f, 0.85f}, 2.5f);
    sun.shadowMode = rd::ShadowMode::kDynamic; // redessinée à chaque image : suit la pose animée
    lights.addLight(sun);
    if (auto r = lights.upload(); !r)
    {
        std::cerr << "Lights: " << r.error().message << '\n';
        return EXIT_FAILURE;
    }
    renderer.setLights(&lights);
    shadows->setLights(&lights);

    // HDR + tonemapping : exposition, courbe ACES, encodage sRGB de la sortie.
    if (auto r = renderer.setSceneColorFormat(rhi::Format::kRGBA16Float); !r)
    {
        std::cerr << "HDR: " << r.error().message << '\n';
        return EXIT_FAILURE;
    }
    auto post = rd::PostContext::create(device, shaders, pipelines, shaderDir);
    if (!post)
    {
        std::cerr << "Post: " << post.error().message << '\n';
        return EXIT_FAILURE;
    }
    auto tonemap = rd::TonemapFeature::create(*post);
    if (!tonemap)
    {
        std::cerr << "Tonemap: " << tonemap.error().message << '\n';
        return EXIT_FAILURE;
    }
    if (auto r = renderer.addFeature(std::move(*tonemap)); !r)
    {
        std::cerr << "Tonemap: " << r.error().message << '\n';
        return EXIT_FAILURE;
    }
```

## 7. Charger le personnage et le sol

`textures`, `pbr` et `defaults` sont partagés par tous les modèles. Chaque maillage du personnage devient un caster
mobile : le plugin d'ombres lit son tampon de sommets, celui que le skinning réécrit, et l'ombre suit donc la pose.
`rd::Aabb::empty()` désactive le culling par vue d'ombre pour ce caster (une boîte au repos serait fausse pendant
l'animation). Le sol reçoit sans projeter : pas de caster pour lui.

```cpp title="src/main.cpp (suite)"
    // ── 7. Charger le personnage et le sol
    as::TextureCache textures(device);
    rd::PbrShaderCache pbr(shaders, shaderDir);
    auto defaults = rd::PbrDefaults::create(device);
    if (!defaults)
    {
        std::cerr << "PBR: " << defaults.error().message << '\n';
        return EXIT_FAILURE;
    }

    Hero hero;
    if (auto r = loadHero(hero, device, shaders, pipelines, shaderDir, textures, pbr, *defaults, shadows); !r)
    {
        std::cerr << "Hero: " << r.error().message << '\n';
        return EXIT_FAILURE;
    }
    // Le personnage projette : chaque maillage est un caster mobile (boîte vide : dessiné dans toutes les vues).
    std::vector<rd::ShadowFeature::CasterId> casters;
    for (std::size_t i = 0; i < hero.render->meshCount(); ++i)
    {
        auto id = shadows->addMeshCaster(hero.render->mesh(i), rd::Aabb::empty(), rd::CasterMobility::kMovable);
        if (!id)
        {
            std::cerr << "Caster: " << id.error().message << '\n';
            return EXIT_FAILURE;
        }
        casters.push_back(*id);
    }

    const as::Model groundModel = makeGround(10.0f);
    as::RenderModelOptions groundOptions;
    groundOptions.sourceKey = "ground";
    groundOptions.shadows = shadows; // reçoit seulement : pas de caster pour le sol
    auto ground = as::RenderModel::create(device, groundModel, textures, pbr, *defaults, groundOptions);
    if (!ground)
    {
        std::cerr << "Ground: " << ground.error().message << '\n';
        return EXIT_FAILURE;
    }
```

## 8. L'interface et le menu

Trois objets pour le texte : `ui::TextRenderer` (atlas de glyphes), `ui::TextRendererFont` (son adaptateur), puis
`ui::Ui`. `MenuState` est déclaré avant `gui`, car l'arbre de vues garde des liaisons vers lui. Le thème n'est pas chargé
ici : la boucle le charge à la première image, parce que `menu.themeChanged` vaut vrai au départ. Le menu lui-même est
l'exemple de [Écrire une vue en .cui](/guides/interface/langage-cui/#exemple--un-menu-qui-souvre-avec-échap).

```cpp title="src/main.cpp (suite)"
    // ── 8. L'interface et le menu
    ui::TextRenderer text;
    if (!text.init(kWidth, kHeight) || !text.loadFont(GET_RESOURCE_PATH("fonts/Roboto-Regular.ttf"), "default", 48))
    {
        std::cerr << "UI font: load failed\n";
        return EXIT_FAILURE;
    }
    text.setActiveFont("default");
    ui::TextRendererFont font(text);
    MenuState menu; // avant `gui` : détruit après l'arbre de vues, qui garde des liaisons vers lui
    ui::Ui gui(font);
    gui.setRoot(cui::buildMenuView(gui, menu));
```

## 9. La caméra

Position de départ un peu au-dessus du sol, regard vers -Z (la direction est un vecteur, pas des angles).

```cpp title="src/main.cpp (suite)"
    // ── 9. La caméra
    rd::Camera camera;
    camera.setPosition({0.0f, 1.6f, 5.0f});
    camera.setOrientation(glm::normalize(glm::vec3(0.0f, -0.2f, -1.0f)));
    camera.setFovDegrees(60.0f);
    camera.setFarPlane(200.0f);
    CameraController controller;
    CameraController::bindKeys();
```

## 10. La boucle

L'ordre d'une image est celui de [Dessiner une scène](/guides/rendu-3d/scene/#le-cycle-dune-image) :

1. entrées : Échap ouvre ou ferme le menu (sauf si un champ de texte a le clavier), le thème est rechargé s'il a changé,
   l'interface est mise à jour, et `menu.quit` ferme la fenêtre ;
2. simulation, seulement hors pause : caméra, machine à états, nouvelle pose ;
3. `device.beginFrame()` ; si la fenêtre est réduite, l'image est sautée ;
4. un encodeur, puis le compute du skinning, avant `renderer.beginFrame` ;
5. `beginFrame`, `setCamera`, les `draw` des deux modèles, puis `endFrame` : c'est là que s'exécutent les passes
   d'ombres, la scène et le tonemapping ;
6. l'interface est dessinée par-dessus, puis le tout est copié dans l'image de la fenêtre ;
7. `submit`, puis `present`, appelé même après une erreur.

À la sortie, `waitIdle` attend la fin du GPU, puis les casters sont retirés du plugin avant la destruction du personnage.

```cpp title="src/main.cpp (suite)"
    // ── 10. La boucle
    rd::RenderTarget composed;
    while (!window.shouldClose())
    {
        if (!window.newFrame())
            continue;
        const float dt = std::min(static_cast<float>(window.getElapseTimeSecond()), 0.1f);

        // Entrées : Échap ouvre ou ferme le menu ; le jeu est en pause tant qu'il est ouvert.
        auto &input = platform::InputManager::getInstance(window.getWindow());
        if (!gui.wantsKeyboard() && input.isKeyJustPressed(platform::KeyButton::kEscape))
            menu.toggle();
        if (menu.themeChanged)
        {
            menu.themeChanged = false;
            const auto path = std::filesystem::path(GET_RESOURCE_PATH("themes")) / (menu.themeName() + ".json");
            if (auto r = gui.themes().load(path); !r)
                std::cerr << "Theme: " << r.error().message << '\n';
        }
        const glm::vec2 view{static_cast<float>(window.getWidth()), static_cast<float>(window.getHeight())};
        gui.update(ui::readWindowInput(window, dt), view);
        if (menu.quit)
            window.requestClose();
        const bool paused = menu.open;
        if (!paused)
        {
            controller.update(window, gui, camera, dt);
            if (hero.machine)
            {
                (void)hero.machine->update(dt);
                if (hero.skinned)
                    (void)hero.skinned->setPose(hero.machine->pose());
            }
        }
        else
        {
            controller.release(window);   // le menu s'ouvre pendant un clic droit : le curseur redevient libre
        }

        // Image de la fenêtre ; échoue si la fenêtre est réduite : on saute l'image.
        int pixelW = 0;
        int pixelH = 0;
        window.getFramebufferSize(pixelW, pixelH);
        auto backbuffer = device.beginFrame();
        if (!backbuffer)
            continue;
        if (pixelW <= 0 || pixelH <= 0)
        {
            (void)device.present();
            continue;
        }
        const rhi::Extent2D size{static_cast<std::uint32_t>(pixelW), static_cast<std::uint32_t>(pixelH)};
        if (!composed.valid())
        {
            auto target = rd::RenderTarget::create(device, {.size = size, .depth = false, .debugName = "composed"});
            if (!target)
            {
                std::cerr << "Target: " << target.error().message << '\n';
                (void)device.present();
                break;
            }
            composed = std::move(*target);
        }
        else if (auto r = composed.resize(size.width, size.height); !r)
        {
            std::cerr << "Target: " << r.error().message << '\n';
            (void)device.present();
            break;
        }

        camera.setAspect(size.width, size.height);
        renderer.setOutputResolution(pixelW, pixelH);
        renderer.setDeltaTime(dt);

        auto encoder = device.createCommandEncoder();
        core::Result<> ok;
        if (hero.skinned)
            ok = hero.skinned->dispatch(*encoder); // compute du skinning : AVANT renderer.beginFrame
        if (ok)
            ok = renderer.beginFrame(*encoder, {.clearColor = {0.45f, 0.6f, 0.8f, 1.0f}});
        if (ok)
            ok = renderer.setCamera(camera);
        if (ok)
            ok = ground->draw(renderer, camera);
        if (ok)
            ok = hero.render->draw(renderer, camera);
        if (ok)
            ok = renderer.endFrame({composed.color(0), rhi::Format::kRGBA8Unorm, size}); // ombres, scène, tonemapping
        if (ok)
        {
            rd::DrawList drawList;
            gui.draw(drawList);
            ok = canvas.render(*encoder, drawList, {composed.color(0), rhi::Format::kRGBA8Unorm, size}, true);
        }
        if (ok)
            ok = renderer.blit().copy(*encoder, composed.color(0), renderer.blit().linearSampler(), *backbuffer,
                                      rhi::Format::kRGBA8Unorm, "present");
        auto submitted = device.submit(std::move(encoder));
        (void)device.present(); // toujours, même après une erreur
        if (!ok || !submitted)
        {
            std::cerr << "Frame: " << (!ok ? ok.error().message : submitted.error().message) << '\n';
            break;
        }
    }

    device.waitIdle();
    for (const auto id : casters)
        shadows->removeCaster(id); // le plugin vit dans le renderer : il ne doit plus voir ces maillages
    return EXIT_SUCCESS;
}
} // namespace
```

## 11. main

`setWorkingDirectoryToExecutable` rend les chemins de `GET_RESOURCE_PATH` relatifs au dossier de l'exécutable.
`Window::terminate()` vient en dernier, après la destruction de la fenêtre et du périphérique (à la fin de `run`).

```cpp title="src/main.cpp (suite)"
int main()
{
    orosmo::core::setWorkingDirectoryToExecutable(); // GET_RESOURCE_PATH est relatif au dossier de l'exécutable
    const int code = run();
    platform::Window::terminate(); // après la destruction de la fenêtre et du périphérique
    return code;
}
```

## Compiler et lancer

```bash
make debug       # compile src/main.cpp et MenuView.cui -> bin/debug/main
make run
```

| Touche | Effet |
|---|---|
| W, S | avancer, reculer |
| A, D | gauche, droite |
| Espace, Maj gauche | monter, descendre |
| clic droit maintenu + souris | tourner la vue |
| Échap | ouvrir ou fermer le menu (le jeu se met en pause) |

## Limites

- Un personnage animé ne projette une ombre juste qu'avec une lumière en `ShadowMode::kDynamic`, qui redessine ses vues
  d'ombre à chaque image (plus coûteux que `kCached`). Ce comportement a été vérifié à la main avec ce programme
  (l'ombre suit la pose en `kDynamic`, se fige en `kCached`) ; il n'a pas encore de test automatique.
- Sans compute (certains pilotes logiciels, macOS), `SkinnedModel` n'est pas créé : le personnage reste en pose de repos,
  et son ombre aussi. Le moteur n'a pas de repli de skinning sur le CPU pour le dessin.
- Le programme n'ouvre que OpenGL ; il n'y a pas encore de boucle de simulation à pas fixe ni de scène à entités : chaque
  objet est tenu par le code de l'application.
- Le thème est rechargé tel quel depuis `res/themes/` ; les couleurs de l'interface ne passent pas par le tonemapping.

Détail et suivi : [État du projet](/projet/etat/).

## Voir aussi

- [Lire la boucle d'une application](/demarrer/premiere-application/)
- [Animer un personnage](/guides/animation/squelettes/)
- [Ajouter des ombres](/guides/rendu-3d/ombres/)
- [Écrire une vue en .cui](/guides/interface/langage-cui/)
