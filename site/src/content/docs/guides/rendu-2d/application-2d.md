---
title: "Application 2D complète"
description: "Un programme 2D de bout en bout, sans scène 3D : fenêtre, périphérique, sprites, formes et texte à l'écran avec Renderer2D, CanvasRenderer et TextRenderer."
sidebar:
  order: 1
  label: Application 2D
---

Un jeu 100 % 2D n'a pas besoin de `SceneRenderer`, de caméra 3D ni de lumières. Il utilise `Renderer2D` pour les sprites et
les formes, `ui::TextRenderer` avec `CanvasRenderer` pour le texte, et une copie vers l'écran. Cette page donne un programme
complet, puis explique chaque étape. Les conventions des noms (`device`, `shaders`, `GET_RESOURCE_PATH`) sont celles de
[Lire la boucle d'une application](/demarrer/premiere-application/) (section « Conventions des exemples ») ; un projet créé
par `orosmo init` a déjà les dossiers `shader/rhi` et `fonts` de ressources.

## Le programme

Le programme choisit OpenGL (`rhi::Backend::kOpenGL`) pour rester court. Pour Vulkan ou le choix à l'exécution, voir
[Choisir le backend graphique](/guides/application/peripherique/). Pour l'essayer dans un projet créé par `orosmo init`,
remplacez `src/main.cpp` par ce fichier et supprimez les fichiers du panneau d'exemple, comme dans le
[tutoriel du jeu 3D](/demarrer/tutoriel-jeu-3d/) : `rm src/HudView.cui src/hud_state.hpp`.

```cpp title="src/main.cpp"
#include <orosmo/core/paths.hpp>
#include <orosmo/platform/input_manager.hpp>
#include <orosmo/platform/window.hpp>
#include <orosmo/renderer/camera2d.hpp>
#include <orosmo/renderer/canvas_renderer.hpp>
#include <orosmo/renderer/draw_list.hpp>
#include <orosmo/renderer/fullscreen_blit.hpp>
#include <orosmo/renderer/pipeline_cache.hpp>
#include <orosmo/renderer/render_target.hpp>
#include <orosmo/renderer/renderer2d.hpp>
#include <orosmo/renderer/shader_library.hpp>
#include <orosmo/renderer/texture.hpp>
#include <orosmo/rhi/device.hpp>
#include <orosmo/ui/text_renderer.hpp>

#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <span>

namespace fs = std::filesystem;
namespace platform = orosmo::platform;
namespace rd = orosmo::renderer;
namespace rhi = orosmo::rhi;
namespace ui = orosmo::ui;

int main()
{
    // 1. Fenêtre et périphérique.
    platform::Window window("jeu2d", 1280, 720, platform::WindowState::kWindowed);
    rhi::DeviceDesc desc;
    desc.backend = rhi::Backend::kOpenGL;
    if (window.init([&] { (void)rhi::prepareWindowHints(desc); }) != 0)
    {
        std::cerr << "Pas de fenêtre (pas d'affichage ?)\n";
        return EXIT_FAILURE;
    }
    desc.surface.nativeWindow = window.getWindow();
    auto created = rhi::createDevice(desc);
    if (!created)
    {
        std::cerr << created.error().message << '\n';
        return EXIT_FAILURE;
    }
    rhi::Device &device = **created;

    // 2. Shaders, dessinateurs 2D, texte.
    const fs::path shaderDir = GET_RESOURCE_PATH("shader/rhi");
    rd::ShaderLibrary shaders(device);
    rd::PipelineCache pipelines(device);
    auto r2dResult = rd::Renderer2D::create(device, shaders, pipelines, shaderDir);
    auto canvasResult = rd::CanvasRenderer::create(device, shaders, pipelines, shaderDir);
    auto blitResult = rd::FullscreenBlit::create(device, shaders, pipelines, shaderDir);
    if (!r2dResult || !canvasResult || !blitResult)
    {
        std::cerr << "Échec de l'initialisation du rendu 2D\n";
        return EXIT_FAILURE;
    }
    rd::Renderer2D &r2d = **r2dResult;
    rd::CanvasRenderer &canvas = **canvasResult;
    rd::FullscreenBlit &blit = **blitResult;
    ui::TextRenderer text;
    if (!text.init(1280, 720) || !text.loadFont(GET_RESOURCE_PATH("fonts/Roboto-Regular.ttf"), "default", 48))
    {
        std::cerr << "Police introuvable\n";
        return EXIT_FAILURE;
    }

    // 3. Une texture de sprite : damier 8 x 8, filtrage au plus proche.
    std::array<std::uint8_t, 8 * 8 * 4> texels{};
    for (int i = 0; i < 64; ++i)
    {
        const std::uint8_t v = (((i % 8) + (i / 8)) & 1) != 0 ? 255 : 90;
        texels[static_cast<std::size_t>(i) * 4 + 0] = v;
        texels[static_cast<std::size_t>(i) * 4 + 1] = v;
        texels[static_cast<std::size_t>(i) * 4 + 2] = 255;
        texels[static_cast<std::size_t>(i) * 4 + 3] = 255;
    }
    rd::TextureInfo info;
    info.width = info.height = 8;
    info.filter = rd::FilterMode::kNearest;
    auto checker = rd::Texture::create(device, info, std::as_bytes(std::span(texels)));
    auto material = checker ? r2d.material(*checker, true) : orosmo::core::Result<rd::SpriteMaterial>(std::unexpected(checker.error()));
    if (!material)
    {
        std::cerr << "Texture : " << material.error().message << '\n';
        return EXIT_FAILURE;
    }

    // 4. Boucle : image 2D dans une cible, texte par-dessus, copie vers l'écran.
    rd::RenderTarget composed;
    float seconds = 0.0f;
    while (!window.shouldClose())
    {
        if (!window.newFrame())
        {
            continue;
        }
        if (platform::InputManager::getInstance(window.getWindow()).isKeyJustPressed(platform::KeyButton::kEscape))
        {
            window.requestClose();
        }
        int pixelW = 0;
        int pixelH = 0;
        window.getFramebufferSize(pixelW, pixelH);
        auto backbuffer = device.beginFrame();
        if (!backbuffer || pixelW <= 0 || pixelH <= 0)
        {
            continue;
        }
        const auto w = static_cast<std::uint32_t>(pixelW);
        const auto h = static_cast<std::uint32_t>(pixelH);
        if (!composed.valid())
        {
            auto target = rd::RenderTarget::create(device, {.size = {w, h}, .depth = false, .debugName = "2d"});
            if (!target)
            {
                std::cerr << target.error().message << '\n';
                return EXIT_FAILURE;
            }
            composed = std::move(*target);
        }
        else if (auto resized = composed.resize(w, h); !resized)
        {
            std::cerr << resized.error().message << '\n';
            return EXIT_FAILURE;
        }
        seconds += 1.0f / 60.0f;

        // Caméra : une unité de monde = un pixel, origine au coin bas-gauche de la fenêtre.
        rd::Camera2D camera;
        camera.setViewport(w, h);
        camera.setCenter({static_cast<float>(w) * 0.5f, static_cast<float>(h) * 0.5f});
        r2d.begin(camera);
        (void)r2d.draw({.material = *material, .position = {200.0f, 300.0f + 40.0f * std::sin(seconds * 2.0f)}, .size = {128.0f, 128.0f},
                            .rotation = seconds, .z = 1.0f});
        r2d.rect({100.0f, 100.0f}, {400.0f, 24.0f}, {0.2f, 0.7f, 0.3f, 1.0f}, 0.0f, 0.0f);
        r2d.circle({700.0f, 300.0f}, 80.0f, {0.9f, 0.4f, 0.1f, 1.0f});
        r2d.line({100.0f, 600.0f}, {900.0f, 560.0f}, 4.0f, {1.0f, 1.0f, 1.0f, 1.0f});

        // Texte : liste de dessin en pixels, origine en haut à gauche.
        rd::DrawList list;
        list.logicalSize = {static_cast<float>(w), static_cast<float>(h)};
        text.setDrawListSink(&list);
        text.renderText("Bonjour en 2D", 20.0f, 20.0f, 0.75f, glm::vec3(1.0f));
        text.setDrawListSink(nullptr);

        auto encoder = device.createCommandEncoder();
        const rhi::Format format = rhi::Format::kRGBA8Unorm;
        auto ok = r2d.render(*encoder, composed.color(0), format, {w, h}, false, {0.08f, 0.10f, 0.14f, 1.0f});
        if (ok)
        {
            ok = canvas.render(*encoder, list, {composed.color(0), format, {w, h}}, true);
        }
        if (ok)
        {
            ok = blit.copy(*encoder, composed.color(0), blit.linearSampler(), *backbuffer, format, "present");
        }
        auto submitted = device.submit(std::move(encoder));
        if (!ok || !submitted)
        {
            std::cerr << (!ok ? ok.error().message : submitted.error().message) << '\n';
            return EXIT_FAILURE;
        }
        (void)device.present();
    }
    device.waitIdle();
    return EXIT_SUCCESS;
}
```

Il lie les modules `platform`, `rhi`, `renderer` et `ui`, comme le projet de démarrage. Le bloc ci-dessus, assemblé tel quel,
passe `g++ -std=c++23 -fsyntax-only` ; il n'a pas été lancé. `Window` est créée en `kWindowed` : sans cet argument, elle s'ouvre
en plein écran dans un build qui n'est pas en debug.

## Étapes

1. Fenêtre et périphérique. `prepareWindowHints` est appelé dans le rappel de `Window::init`, avant la création de la
   fenêtre. Voir [Choisir le backend graphique](/guides/application/peripherique/).
2. Dessinateurs. `Renderer2D` (sprites, formes), `CanvasRenderer` (liste de dessin du texte) et `FullscreenBlit` (copie vers
   l'écran) prennent tous `device`, `shaders`, `pipelines` et le dossier des shaders.
3. Texture de sprite. `Texture::create` avec des texels RGBA8 ; `Renderer2D::material(texture, true)` l'enregistre avec un
   filtrage au plus proche. La texture doit vivre aussi longtemps que son usage.
4. Cible intermédiaire. Une `RenderTarget` sans profondeur, de la taille du framebuffer (`getFramebufferSize`, en pixels
   réels, pas la taille logique de la fenêtre). Elle est redimensionnée à chaque image si la fenêtre change.
5. Image. `Camera2D` règle la vue : ici une unité de monde par pixel, avec le centre au milieu de la cible. On dépose les
   sprites et les formes entre `begin` et `render`.
6. Texte. `TextRenderer` écrit des glyphes dans une `DrawList` ; les coordonnées sont en pixels avec l'origine en haut à gauche
   et y vers le bas, alors que le monde de `Camera2D` a y vers le haut.
7. Ordre des passes. `Renderer2D::render` efface la cible (`loadExisting = false`), puis `CanvasRenderer::render` pose le
   texte par-dessus (`loadExisting = true`), puis `FullscreenBlit::copy` copie la cible vers le backbuffer. Tout passe par le
   même encodeur, puis `submit` et `present`.

## Pièges

- `Renderer2D::render` demande un encodeur sans passe ouverte : ne le mélangez pas avec une passe que vous ouvrez vous-même.
- La texture du backbuffer ne s'échantillonne pas et ne peut pas servir de source : c'est pourquoi on dessine d'abord dans une
  cible puis on copie.
- Texte et sprites n'ont pas le même repère vertical (voir étape 6).
- Sans `setDrawListSink`, `renderText` ne produit rien.

## Aller plus loin

- Les options des sprites, formes et de la caméra : [Sprites, formes et caméra 2D](/guides/rendu-2d/sprites-formes/).
- Mise en page et mesure du texte : [Texte et polices](/guides/rendu-2d/texte/).
- 2D et 3D dans la même image, ou plusieurs vues : [Mélanger 2D et 3D](/guides/rendu-2d/melange-2d-3d/). Il passe par un
  `SceneRenderer` et `Layer2DFeature` en mode `kOverlay`.
- Pas de cartes de tuiles ni d'éclairage 2D pour l'instant : [État du moteur](/projet/etat/).
