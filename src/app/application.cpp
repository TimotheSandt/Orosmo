// Façade d'application (A75.1, reste d'A75.9 ; A79 lot L13) : voir include/orosmo/app/application.hpp.
#include <orosmo/app/application.hpp>

#include <orosmo/anim/animated_model.hpp>
#include <orosmo/assets/model.hpp>
#include <orosmo/assets/model_loader.hpp>
#include <orosmo/core/log.hpp>
#include <orosmo/core/paths.hpp>
#include <orosmo/platform/window.hpp>
#include <orosmo/renderer/canvas_renderer.hpp>
#include <orosmo/renderer/draw_list.hpp>
#include <orosmo/renderer/post_effects.hpp>
#include <orosmo/renderer/render_target.hpp>
#include <orosmo/renderer/scene_renderer.hpp>
#include <orosmo/renderer/shadow_feature.hpp>
#include <orosmo/ui/font.hpp>
#include <orosmo/ui/text_renderer.hpp>
#include <orosmo/ui/ui.hpp>
#include <orosmo/ui/window_input.hpp>

#include <charconv>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace orosmo::app
{

namespace
{

using core::ErrorCode;
using core::makeError;

const char *backendName(rhi::Backend backend) noexcept
{
    switch (backend)
    {
    case rhi::Backend::kVulkan:
        return "Vulkan";
    case rhi::Backend::kOpenGL:
        return "OpenGL";
    case rhi::Backend::kMetal:
        return "Metal";
    }
    return "unknown";
}

std::optional<std::uint32_t> parseUint(std::string_view text)
{
    std::uint32_t value = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc() || end != text.data() + text.size())
        return std::nullopt;
    return value;
}

fs::path defaultResourceDirectory()
{
    std::error_code ec;
    return fs::is_directory(fs::path("res") / "shader" / "rhi", ec) ? fs::path("res") : fs::path(".");
}

} // namespace

// ── Ligne de commande ───────────────────────────────────────────────────────────

core::Result<bool> parseArguments(ApplicationSettings &settings, int argc, const char *const *argv)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        const auto value = [&]() -> core::Result<std::string_view>
        {
            if (i + 1 >= argc)
                return makeError(ErrorCode::kInvalidArgument, "missing value for " + std::string(arg));
            return std::string_view(argv[++i]);
        };
        const auto positive = [&]() -> core::Result<std::uint32_t>
        {
            auto text = value();
            if (!text)
                return std::unexpected(text.error());
            const auto parsed = parseUint(*text);
            if (!parsed || *parsed == 0)
                return makeError(ErrorCode::kInvalidArgument, "invalid value for " + std::string(arg) + " (expected a positive integer): " + std::string(*text));
            return *parsed;
        };
        if (arg == "--help" || arg == "-h")
            return false;
        if (arg == "--headless")
            settings.headless = true;
        else if (arg == "--strict-backend")
            settings.backendFallback = false;
        else if (arg == "--api")
        {
            auto text = value();
            if (!text)
                return std::unexpected(text.error());
            if (*text == "vulkan")
                settings.backend = rhi::Backend::kVulkan;
            else if (*text == "opengl")
                settings.backend = rhi::Backend::kOpenGL;
            else if (*text == "metal")
                settings.backend = rhi::Backend::kMetal;
            else
                return makeError(ErrorCode::kInvalidArgument, "invalid value for --api (vulkan, opengl or metal): " + std::string(*text));
        }
        else if (arg == "--size")
        {
            auto text = value();
            if (!text)
                return std::unexpected(text.error());
            const std::size_t x = text->find('x');
            const auto w = x == std::string_view::npos ? std::nullopt : parseUint(text->substr(0, x));
            const auto h = x == std::string_view::npos ? std::nullopt : parseUint(text->substr(x + 1));
            if (!w || !h || *w == 0 || *h == 0 || *w > 16384 || *h > 16384)
                return makeError(ErrorCode::kInvalidArgument, "invalid value for --size (expected WxH): " + std::string(*text));
            settings.width = static_cast<int>(*w);
            settings.height = static_cast<int>(*h);
        }
        else if (arg == "--frames")
        {
            auto n = positive();
            if (!n)
                return std::unexpected(n.error());
            settings.frames = *n;
        }
        else if (arg == "--capture-frames")
        {
            auto n = positive();
            if (!n)
                return std::unexpected(n.error());
            settings.captureFrames = *n;
        }
        else if (arg == "--capture")
        {
            auto text = value();
            if (!text)
                return std::unexpected(text.error());
            std::error_code ec;
            const fs::path absolute = fs::absolute(fs::path(*text), ec);
            settings.captureDirectory = ec ? fs::path(*text) : absolute;
        }
        else
            return makeError(ErrorCode::kInvalidArgument, "unknown option: " + std::string(arg));
    }
    return true;
}

std::string usage(std::string_view program)
{
    std::ostringstream out;
    out << "Usage: " << program
        << " [--api vulkan|opengl] [--strict-backend] [--headless] [--size WxH] [--frames N] [--capture DIR] [--capture-frames N]"
           " [--help]\n"
           "  --api             graphics API (default: Vulkan, falling back to OpenGL if it cannot start; no fallback on macOS)\n"
           "  --strict-backend  no fallback: fail if the requested API cannot start\n"
           "  --headless        no window (off-screen rendering; a GPU is still needed)\n"
           "  --frames N        stop after N frames\n"
           "  --capture DIR     after --capture-frames frames (default 3), write DIR/scene.png and DIR/ui.png, then stop\n";
    return out.str();
}

// ── Application ────────────────────────────────────────────────────────────────

struct Application::Impl
{
    ApplicationSettings settings;
    fs::path resourceDirectory;

    std::unique_ptr<platform::Window> window;
    std::unique_ptr<rhi::Device> device;
    rhi::Backend backend = rhi::Backend::kOpenGL;
    bool fellBack = false;

    std::unique_ptr<renderer::RenderContext> context;
    std::unique_ptr<renderer::SceneRenderer> scene;
    std::unique_ptr<renderer::CanvasRenderer> canvas;
    renderer::ShadowFeature *shadows = nullptr;
    std::unique_ptr<assets::ModelLoader> loader;
    SceneResources resources;
    std::unique_ptr<sim::Simulation> simulation;
    std::unique_ptr<SceneRenderSystem> system;

    std::unique_ptr<ui::TextRenderer> text;
    std::unique_ptr<ui::TextRendererFont> font;
    std::unique_ptr<ui::Ui> ui;
    std::optional<GameModule> game;
    std::unique_ptr<ViewHost> views; // après le jeu et l'interface : détruit avant eux

    renderer::RenderTarget composed;
    renderer::RenderTarget resized; // image en cours à une nouvelle taille : devient `composed` si elle réussit (revue PR #37)
    renderer::Camera camera;
    core::SteadyClock steadyClock;
    std::optional<core::FrameTimer> timer;

    UpdateCallback onUpdate;
    UiCallback onUi;
    RenderCallback onRender;

    bool quit = false;
    bool paused = false;
    bool animationsPaused = false;
    bool captureOk = true;
    bool captureAttempted = false; // la capture demandée a été tentée (réussie ou non)
    std::uint64_t frame = 0;
    rhi::Extent2D outputSize{};

    ~Impl()
    {
        // Ordre explicite : rien ne doit survivre au périphérique ni à ce qu'il référence.
        if (device)
            device->waitIdle();
        // Les rappels d'abord : leurs captures peuvent tenir des ressources du périphérique.
        onRender = nullptr;
        onUi = nullptr;
        onUpdate = nullptr;
        views.reset(); // l'arbre de la vue part avant la bibliothèque de jeu qui a pu le construire
        game.reset();  // retire ses actions de l'interface et ses systèmes de la simulation
        ui.reset();
        font.reset();
        text.reset();
        system.reset(); // retire ses casters du plugin d'ombres
        resources.animatedModels.clear();
        resources.meshes.clear();
        loader.reset();
        resized.reset();
        composed.reset();
        canvas.reset();
        scene.reset();
        simulation.reset();
        context.reset();
        device.reset();
        window.reset();
    }

    core::Result<> createDevice(rhi::Backend wanted)
    {
        // `--api opengl` sur macOS : l'échec (fenêtre ou périphérique) vient de la version d'OpenGL, pas d'un affichage manquant.
        const std::string macHint = rhi::kHostIsMacOS && wanted == rhi::Backend::kOpenGL
                                        ? " (macOS stops at OpenGL 4.1, the engine needs 4.3: use --api vulkan)"
                                        : "";
        rhi::DeviceDesc desc;
        desc.backend = wanted;
        desc.validation = settings.validation;
        desc.vsync = settings.vsync;
        desc.appName = settings.title;
        if (settings.headless)
        {
            desc.surface.size = {static_cast<std::uint32_t>(settings.width), static_cast<std::uint32_t>(settings.height)};
        }
        else
        {
            window = settings.windowed ? std::make_unique<platform::Window>(settings.title, settings.width, settings.height,
                                                                            platform::WindowState::kWindowed)
                                       : std::make_unique<platform::Window>(settings.title, settings.width, settings.height);
            if (auto opened = window->init([&] { (void)rhi::prepareWindowHints(desc); }); !opened)
            {
                window.reset();
                return makeError(ErrorCode::kUnsupported, std::string("window: ") + opened.error().message +
                                                              " (no display? --headless renders off-screen; on Linux without X: xvfb-run)" + macHint);
            }
            desc.surface.nativeWindow = window->getWindow();
        }
        auto created = settings.createDevice ? settings.createDevice(desc) : rhi::createDevice(desc);
        if (!created)
        {
            if (window)
                window->close();
            window.reset();
            return makeError(created.error().code, std::string(backendName(wanted)) + " device: " + created.error().message + macHint);
        }
        device = std::move(*created);
        backend = wanted;
        return {};
    }

    core::Result<> createGraphics()
    {
        auto first = createDevice(settings.backend);
        if (!first)
        {
            if (settings.backend != rhi::Backend::kVulkan || !settings.backendFallback)
                return first;
            const auto fallback = rhi::fallbackBackend(settings.backend);
            if (!fallback)
                return makeError(first.error().code, first.error().message + "; " + rhi::kNoFallbackOnMacOS);
            LOG_WARNING("Vulkan cannot start (", first.error().message, "); falling back to OpenGL");
            if (auto second = createDevice(*fallback); !second)
                return makeError(second.error().code, first.error().message + "; fallback: " + second.error().message);
            fellBack = true;
        }
        LOG_INFO("Graphics device: ", backendName(backend), " ", device->capabilities().deviceName);

        context = renderer::RenderContext::create(*device, settings.shaderDirectory.empty() ? resourceDirectory / "shader" / "rhi" : settings.shaderDirectory);
        auto createdScene = renderer::SceneRenderer::create(*context);
        if (!createdScene)
            return makeError(createdScene.error().code, "scene renderer: " + createdScene.error().message);
        scene = std::move(*createdScene);
        auto createdCanvas = renderer::CanvasRenderer::create(*context);
        if (!createdCanvas)
            return makeError(createdCanvas.error().code, "2D renderer: " + createdCanvas.error().message);
        canvas = std::move(*createdCanvas);

        if (settings.shadows)
        {
            auto feature = renderer::ShadowFeature::create(*context);
            if (!feature)
                return makeError(feature.error().code, "shadows: " + feature.error().message);
            auto added = scene->addFeature(std::move(*feature));
            if (!added)
                return makeError(added.error().code, "shadows: " + added.error().message);
            shadows = static_cast<renderer::ShadowFeature *>(*added);
        }
        if (settings.hdr)
        {
            if (auto format = scene->setSceneColorFormat(rhi::Format::kRGBA16Float); !format)
                return makeError(format.error().code, "HDR: " + format.error().message);
            auto post = renderer::PostContext::create(*context);
            if (!post)
                return makeError(post.error().code, "tonemapping: " + post.error().message);
            auto tonemap = renderer::TonemapFeature::create(*post);
            if (!tonemap)
                return makeError(tonemap.error().code, "tonemapping: " + tonemap.error().message);
            if (auto added = scene->addFeature(std::move(*tonemap)); !added)
                return makeError(added.error().code, "tonemapping: " + added.error().message);
        }
        return {};
    }

    core::Result<> createUi()
    {
        if (!settings.ui)
            return {};
        text = std::make_unique<ui::TextRenderer>();
        const fs::path fontPath = resourceDirectory / settings.font;
        if (!text->init(static_cast<unsigned>(settings.width), static_cast<unsigned>(settings.height)) ||
            !text->loadFont(fontPath.string(), "default", settings.fontSize))
            return makeError(ErrorCode::kNotFound, "UI font cannot be loaded: " + fontPath.string());
        text->setActiveFont("default");
        font = std::make_unique<ui::TextRendererFont>(*text, static_cast<float>(settings.fontSize));
        ui = std::make_unique<ui::Ui>(*font);
        if (!settings.theme.empty())
        {
            const fs::path themePath = resourceDirectory / settings.theme;
            if (auto theme = ui->themes().load(themePath); !theme)
                return makeError(theme.error().code, "UI theme " + themePath.string() + ": " + theme.error().message);
        }
        return {};
    }

    core::Result<> startGame()
    {
        if (!settings.game || !settings.game->start)
            return {};
        const GameTargets targets{.world = &simulation->world(), .simulation = simulation.get(), .actions = ui ? &ui->actions() : nullptr};
        auto started = settings.game->start(targets);
        if (!started)
        {
            if (settings.game->required)
                return makeError(started.error().code, "game logic: " + started.error().message);
            LOG_WARNING("Game logic not started: ", started.error().describe());
            return {};
        }
        game.emplace(std::move(*started));
        return {};
    }

    core::Result<> capture()
    {
        std::error_code ec;
        fs::create_directories(settings.captureDirectory, ec);
        if (ec || !fs::is_directory(settings.captureDirectory))
            return makeError(ErrorCode::kIo, "capture: cannot create " + settings.captureDirectory.string() + (ec ? ": " + ec.message() : std::string()));
        const fs::path scenePath = settings.captureDirectory / "scene.png";
        const fs::path uiPath = settings.captureDirectory / "ui.png";
        if (auto saved = scene->saveColorPng(scenePath); !saved)
            return makeError(saved.error().code, "capture " + scenePath.string() + ": " + saved.error().message);
        if (auto saved = renderer::savePng(*device, composed.color(0), uiPath); !saved)
            return makeError(saved.error().code, "capture " + uiPath.string() + ": " + saved.error().message);
        return {};
    }

    // Cible de l'image : `composed` à la même taille ; à une nouvelle taille, `resized`, publiée par `publishComposed` après une
    // image réussie : si elle échoue, `readOutput` lit encore la dernière image composée, à la taille de `outputSize`.
    core::Result<renderer::RenderTarget *> ensureComposed(rhi::Extent2D size)
    {
        if (composed.valid() && composed.width() == size.width && composed.height() == size.height)
            return &composed;
        if (!resized.valid())
        {
            auto target = renderer::RenderTarget::create(*device, {.size = size, .depth = false, .debugName = "app.composed"});
            if (!target)
                return std::unexpected(target.error());
            resized = std::move(*target);
        }
        else if (auto r = resized.resize(size.width, size.height); !r)
            return std::unexpected(r.error());
        return &resized;
    }

    void publishComposed(const renderer::RenderTarget *target)
    {
        if (target != &resized)
            return;
        composed = std::move(resized);
        resized.reset();
    }
};

Application::Application(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
Application::~Application() = default;

core::Result<std::unique_ptr<Application>> Application::create(ApplicationSettings settings)
{
    if (settings.width <= 0 || settings.height <= 0)
        return makeError(ErrorCode::kInvalidArgument, "Application: width and height must be positive");
    auto impl = std::make_unique<Impl>();
    impl->settings = std::move(settings);
    ApplicationSettings &s = impl->settings;
    impl->resourceDirectory = s.resourceDirectory.empty() ? defaultResourceDirectory() : s.resourceDirectory;

    if (auto graphics = impl->createGraphics(); !graphics)
        return std::unexpected(graphics.error());

    impl->simulation = std::make_unique<sim::Simulation>(s.simulation);
    auto system = SceneRenderSystem::create(*impl->context, impl->simulation->world(), impl->resources, impl->shadows);
    if (!system)
        return makeError(system.error().code, "scene render system: " + system.error().message);
    impl->system = std::move(*system);
    impl->scene->setLights(&impl->system->lights());
    if (impl->shadows != nullptr)
        impl->shadows->setLights(&impl->system->lights());

    impl->camera.setPosition({0.0f, 2.0f, 5.0f});
    impl->camera.lookAt({0.0f, 0.0f, 0.0f});
    impl->camera.setFovDegrees(60.0f);
    impl->camera.setNearPlane(0.1f);
    impl->camera.setFarPlane(1000.0f);

    if (auto created = impl->createUi(); !created)
        return std::unexpected(created.error());
    if (auto started = impl->startGame(); !started)
        return std::unexpected(started.error());

    if (impl->ui)
    {
        ViewHostSettings views = s.views;
        views.game = impl->game ? &*impl->game : nullptr;
        impl->views = std::make_unique<ViewHost>(*impl->ui, std::move(views));
    }

    impl->timer.emplace(s.clock != nullptr ? *s.clock : impl->steadyClock, s.maxFrameDelta);
    return std::unique_ptr<Application>(new Application(std::move(impl)));
}

int Application::main(int argc, char **argv, ApplicationSettings settings, const SetupCallback &setup)
{
    const std::string program = argc > 0 && argv[0] != nullptr ? fs::path(argv[0]).filename().string() : std::string("app");
    auto parsed = parseArguments(settings, argc, argv);
    if (!parsed)
    {
        std::cerr << parsed.error().message << '\n' << usage(program);
        return EXIT_FAILURE;
    }
    if (!*parsed)
    {
        std::cout << usage(program);
        return EXIT_SUCCESS;
    }
    core::setWorkingDirectoryToExecutable();
    if (settings.logFile.empty())
        SET_LOG_FILE_DEFAULT;
    else
        SET_LOG_FILE(settings.logFile.string());

    int code = EXIT_FAILURE;
    {
        auto app = create(std::move(settings));
        if (!app)
        {
            LOG_ERROR(1, app.error().message);
            std::cerr << app.error().message << '\n';
        }
        else if (auto ready = setup ? setup(**app) : core::Result<>(); !ready)
        {
            LOG_ERROR(1, "setup: ", ready.error().message);
            std::cerr << "setup: " << ready.error().message << '\n';
        }
        else
        {
            code = (*app)->run() ? EXIT_SUCCESS : EXIT_FAILURE;
        }
    }
    platform::Window::terminate();
    FLUSH_LOG_TO_FILE;
    return code;
}

bool Application::run()
{
    bool ok = true;
    while (!impl_->quit)
    {
        auto continued = frame();
        if (!continued)
        {
            LOG_ERROR(1, "Frame failed: ", continued.error().message);
            std::cerr << "Frame failed: " << continued.error().message << '\n';
            ok = false;
            break;
        }
        if (!*continued)
            break;
    }
    impl_->device->waitIdle();
    // Boucle arrêtée (fenêtre fermée) avant l'image de la capture demandée : échec, comme une limite d'images trop basse.
    if (ok && impl_->captureOk && !impl_->settings.captureDirectory.empty() && !impl_->captureAttempted)
    {
        const std::string message = "capture not written: the loop stopped after " + std::to_string(impl_->frame) +
                                    " frames, before captureFrames (" + std::to_string(impl_->settings.captureFrames) + ")";
        LOG_ERROR(1, message);
        std::cerr << message << '\n';
        impl_->captureOk = false;
    }
    return ok && impl_->captureOk;
}

core::Result<bool> Application::frame()
{
    Impl &s = *impl_;
    const ApplicationSettings &cfg = s.settings;
    const bool capturing = !cfg.captureDirectory.empty();
    const bool fixedSize = cfg.headless || capturing;
    const bool fixed = fixedSize && cfg.clock == nullptr; // une horloge fournie (test) donne le delta, même sans fenêtre
    if (s.window)
    {
        if (s.window->shouldClose() || !s.window->newFrame())
            return false; // fermée, ou inutilisable (A75.16)
    }

    // 1. Temps : delta réel borné, ou un pas fixe (sans fenêtre, capture : même image à chaque exécution).
    core::Nanoseconds delta = s.timer->tick();
    if (fixed)
        delta = cfg.simulation.step;
    FrameTime time;
    time.dt = std::chrono::duration<double>(delta).count();
    time.fixedStep = s.simulation->stepSeconds();
    time.frame = s.frame;

    // 2. Jeu et simulation.
    if (s.game)
        s.game->update(time.dt);
    if (!s.paused)
        time.steps = s.simulation->advance(delta);
    time.alpha = s.simulation->alpha();
    time.simulationStep = s.simulation->stepIndex();

    // 3. Logique de l'application.
    if (s.onUpdate)
        s.onUpdate(*this, time);

    // Taille de l'image : fenêtre (tampon d'image), ou fixe.
    int viewW = cfg.width;
    int viewH = cfg.height;
    int pixelW = cfg.width;
    int pixelH = cfg.height;
    if (s.window && !fixedSize)
    {
        viewW = s.window->getWidth();
        viewH = s.window->getHeight();
        s.window->getFramebufferSize(pixelW, pixelH);
    }

    // 4. Interface.
    const float dt = static_cast<float>(time.dt);
    if (s.views)
        s.views->update(); // développement : vue reconstruite après un .cui enregistré (hors de Ui::update)
    if (s.ui)
    {
        ui::InputFrame input = s.window ? ui::readWindowInput(*s.window, dt) : ui::InputFrame{};
        if (s.window && fixedSize && s.window->getWidth() > 0 && s.window->getHeight() > 0)
        {
            // Capture avec fenêtre : l'interface est mise en page à width × height puis étirée sur la fenêtre.
            input.mouse *= glm::vec2(static_cast<float>(viewW) / static_cast<float>(s.window->getWidth()),
                                     static_cast<float>(viewH) / static_cast<float>(s.window->getHeight()));
        }
        input.deltaTime = dt;
        s.ui->update(input, {static_cast<float>(viewW), static_cast<float>(viewH)});
    }
    if (s.onUi)
        s.onUi(*this, time);

    // 5. Rendu.
    auto backbuffer = s.device->beginFrame();
    if (!backbuffer)
    {
        if (backbuffer.error().code == ErrorCode::kBusy)
            return true; // image sautée (fenêtre réduite)
        return std::unexpected(backbuffer.error());
    }
    if (pixelW <= 0 || pixelH <= 0)
    {
        if (auto presented = s.device->present(); !presented && presented.error().code != ErrorCode::kBusy)
            return std::unexpected(presented.error());
        return true;
    }
    const rhi::Extent2D size{static_cast<std::uint32_t>(pixelW), static_cast<std::uint32_t>(pixelH)};
    auto target = s.ensureComposed(size);
    if (!target)
        return makeError(target.error().code, "output target: " + target.error().message);
    renderer::RenderTarget &composed = **target;

    s.camera.setAspect(size.width, size.height);
    s.scene->setOutputResolution(pixelW, pixelH);
    s.scene->setDeltaTime(dt);
    if (auto updated = s.system->update(s.animationsPaused ? 0.0f : dt); !updated)
        return std::unexpected(updated.error());
    (void)s.system->applyCamera(s.camera); // sans caméra dans la scène : celle de l'application

    auto encoder = s.device->createCommandEncoder();
    core::Result<> ok = s.system->dispatch(*encoder); // skinning en compute : hors passe, avant beginFrame (A75.2)
    if (ok)
        ok = s.scene->beginFrame(*encoder, {.clearColor = cfg.clearColor});
    if (ok)
        ok = s.scene->setCamera(s.camera);
    if (ok)
    {
        if (auto drawn = s.system->draw(*s.scene, s.camera); !drawn)
            ok = std::unexpected(drawn.error());
    }
    if (ok && s.onRender)
    {
        RenderFrame render{*s.scene, s.camera, *encoder, size};
        ok = s.onRender(*this, render);
    }
    const renderer::UpscaleTarget output{composed.color(0), rhi::Format::kRGBA8Unorm, size};
    if (ok)
        ok = s.scene->endFrame(output);
    else
        s.scene->abortFrame(); // une étape a échoué après beginFrame : l'image est fermée, le prochain frame() peut l'ouvrir
    if (ok && s.ui)
    {
        renderer::DrawList list;
        s.ui->draw(list);
        ok = s.canvas->render(*encoder, list, {composed.color(0), rhi::Format::kRGBA8Unorm, size}, true);
    }
    if (ok)
        ok = s.scene->blit().copy(*encoder, composed.color(0), s.scene->blit().linearSampler(), *backbuffer, rhi::Format::kRGBA8Unorm,
                                  "app.present");
    auto submitted = s.device->submit(std::move(encoder));
    if (!ok || !submitted)
    {
        if (auto presented = s.device->present(); !presented)
            LOG_WARNING("present after a failed frame: ", presented.error().message);
        return std::unexpected(!ok ? ok.error() : submitted.error());
    }
    s.publishComposed(&composed);
    s.outputSize = size;
    ++s.frame;

    if (capturing && s.frame >= cfg.captureFrames)
    {
        s.captureAttempted = true;
        if (auto captured = s.capture(); !captured)
        {
            LOG_ERROR(1, captured.error().message);
            std::cerr << captured.error().message << '\n';
            s.captureOk = false;
        }
        s.quit = true;
    }
    // La limite implicite sans fenêtre ne coupe pas une capture ; une limite demandée avant elle est un échec de la capture.
    const std::uint32_t limit = cfg.frames != 0 ? cfg.frames : (s.window || capturing ? 0u : ApplicationSettings::kDefaultHeadlessFrames);
    if (limit != 0 && s.frame >= limit)
    {
        if (capturing && s.frame < cfg.captureFrames)
        {
            const std::string message = "capture not written: stopped after " + std::to_string(s.frame) + " frames, before captureFrames (" +
                                        std::to_string(cfg.captureFrames) + ")";
            LOG_ERROR(1, message);
            std::cerr << message << '\n';
            s.captureOk = false;
        }
        s.quit = true;
    }

    if (auto presented = s.device->present(); !presented && presented.error().code != ErrorCode::kBusy)
        return std::unexpected(presented.error());
    return !s.quit;
}

void Application::quit() noexcept { impl_->quit = true; }
bool Application::quitting() const noexcept { return impl_->quit; }
void Application::onUpdate(UpdateCallback callback) { impl_->onUpdate = std::move(callback); }
void Application::onUi(UiCallback callback) { impl_->onUi = std::move(callback); }
void Application::onRender(RenderCallback callback) { impl_->onRender = std::move(callback); }
void Application::pauseSimulation(bool paused) noexcept { impl_->paused = paused; }
bool Application::simulationPaused() const noexcept { return impl_->paused; }
void Application::pauseAnimations(bool paused) noexcept { impl_->animationsPaused = paused; }
bool Application::animationsPaused() const noexcept { return impl_->animationsPaused; }

scene::World &Application::world() noexcept { return impl_->simulation->world(); }
sim::Simulation &Application::simulation() noexcept { return *impl_->simulation; }
const RenderComponentTypes &Application::components() const noexcept { return impl_->system->types(); }
renderer::Camera &Application::camera() noexcept { return impl_->camera; }

namespace
{
template <typename T>
scene::Entity spawnWith(scene::World &world, scene::ComponentType<T> type, const T &component, const scene::Transform &transform)
{
    const scene::Entity entity = world.create();
    if (auto placed = world.setTransform(entity, transform); !placed || world.add(entity, type, component) == nullptr)
    {
        (void)world.destroy(entity);
        return scene::kNullEntity;
    }
    return entity;
}
} // namespace

scene::Entity Application::spawn(const MeshRenderer &component, const scene::Transform &transform)
{
    return spawnWith(world(), components().mesh, component, transform);
}
scene::Entity Application::spawn(const LightComponent &component, const scene::Transform &transform)
{
    return spawnWith(world(), components().light, component, transform);
}
scene::Entity Application::spawn(const CameraComponent &component, const scene::Transform &transform)
{
    return spawnWith(world(), components().camera, component, transform);
}
scene::Entity Application::spawn(const AnimatedModelComponent &component, const scene::Transform &transform)
{
    return spawnWith(world(), components().animatedModel, component, transform);
}

core::Handle<renderer::Mesh> Application::addMesh(renderer::Mesh mesh)
{
    return impl_->resources.meshes.add(std::make_unique<renderer::Mesh>(std::move(mesh)));
}

core::Result<core::Handle<renderer::Mesh>> Application::addMesh(const renderer::MeshData &data)
{
    auto mesh = renderer::Mesh::create(*impl_->device, data);
    if (!mesh)
        return std::unexpected(mesh.error());
    return addMesh(std::move(*mesh));
}

core::Result<core::Handle<renderer::Mesh>> Application::addMesh(const renderer::MeshBuilder &builder)
{
    auto mesh = builder.create(*impl_->device);
    if (!mesh)
        return std::unexpected(mesh.error());
    return addMesh(std::move(*mesh));
}

core::Result<std::shared_ptr<renderer::PbrMaterial>> Application::createMaterial(const renderer::PbrMaterialDesc &desc)
{
    auto loader = modelLoader();
    if (!loader)
        return std::unexpected(loader.error());
    return renderer::PbrMaterial::create(*impl_->device, desc, (*loader)->defaults(), impl_->shadows);
}

core::Result<core::Handle<renderer::Mesh>> Application::addMesh(const renderer::MeshBuilder &builder, const renderer::PbrMaterialDesc &material)
{
    if (builder.layout() != renderer::MeshLayout::kPbr)
        return makeError(ErrorCode::kInvalidArgument, "Application::addMesh: a material needs the PBR layout (MeshLayout::kPbr)");
    auto created = createMaterial(material);
    if (!created)
        return std::unexpected(created.error());
    auto mesh = builder.create(*impl_->device);
    if (!mesh)
        return std::unexpected(mesh.error());
    mesh->setMaterial(std::move(*created));
    return addMesh(std::move(*mesh));
}

renderer::Mesh *Application::mesh(core::Handle<renderer::Mesh> handle) noexcept { return impl_->resources.meshes.get(handle); }

core::Result<assets::ModelLoader *> Application::modelLoader()
{
    if (!impl_->loader)
    {
        auto loader = assets::ModelLoader::create(*impl_->context);
        if (!loader)
            return std::unexpected(loader.error());
        impl_->loader = std::make_unique<assets::ModelLoader>(std::move(*loader));
    }
    return impl_->loader.get();
}

core::Result<core::Handle<anim::AnimatedModel>> Application::loadAnimatedModel(const fs::path &path)
{
    return loadAnimatedModel(path, anim::AnimatedModelOptions{});
}

core::Result<core::Handle<anim::AnimatedModel>> Application::loadAnimatedModel(const fs::path &path, const anim::AnimatedModelOptions &options)
{
    auto loader = modelLoader();
    if (!loader)
        return std::unexpected(loader.error());
    anim::AnimatedModelOptions withShadows = options;
    if (withShadows.render.shadows == nullptr)
        withShadows.render.shadows = impl_->shadows;
    auto model = anim::AnimatedModel::create(*impl_->context, **loader, path, withShadows);
    if (!model)
        return std::unexpected(model.error());
    return impl_->resources.animatedModels.add(std::move(*model));
}

core::Result<core::Handle<anim::AnimatedModel>> Application::addAnimatedModel(assets::Model model)
{
    auto loader = modelLoader();
    if (!loader)
        return std::unexpected(loader.error());
    anim::AnimatedModelOptions options;
    options.render.shadows = impl_->shadows;
    auto created = anim::AnimatedModel::create(*impl_->context, **loader, std::move(model), options);
    if (!created)
        return std::unexpected(created.error());
    return impl_->resources.animatedModels.add(std::move(*created));
}

anim::AnimatedModel *Application::animatedModel(core::Handle<anim::AnimatedModel> handle) noexcept
{
    return impl_->resources.animatedModels.get(handle);
}

SceneResources &Application::resources() noexcept { return impl_->resources; }
fs::path Application::resourcePath(const fs::path &relative) const { return impl_->resourceDirectory / relative; }

rhi::Device &Application::device() noexcept { return *impl_->device; }
rhi::Backend Application::backend() const noexcept { return impl_->backend; }
bool Application::fellBack() const noexcept { return impl_->fellBack; }
const renderer::RenderContext &Application::renderContext() const noexcept { return *impl_->context; }
renderer::SceneRenderer &Application::scene() noexcept { return *impl_->scene; }
renderer::ShadowFeature *Application::shadows() noexcept { return impl_->shadows; }
renderer::LightManager &Application::lights() noexcept { return impl_->system->lights(); }
SceneRenderSystem &Application::sceneSystem() noexcept { return *impl_->system; }
bool Application::hasUi() const noexcept { return impl_->ui != nullptr; }
ui::Ui &Application::ui() noexcept
{
    ENGINE_ASSERT(impl_->ui != nullptr, "Application::ui: created with settings.ui = false");
    return *impl_->ui;
}
ViewHost &Application::views() noexcept
{
    ENGINE_ASSERT(impl_->views != nullptr, "Application::views: created with settings.ui = false");
    return *impl_->views;
}
platform::Window *Application::window() noexcept { return impl_->window.get(); }
GameModule *Application::game() noexcept { return impl_->game ? &*impl_->game : nullptr; }
const ApplicationSettings &Application::settings() const noexcept { return impl_->settings; }
rhi::Extent2D Application::outputSize() const noexcept { return impl_->outputSize; }
std::uint64_t Application::frameIndex() const noexcept { return impl_->frame; }

core::Result<renderer::Image> Application::readOutput()
{
    if (!impl_->composed.valid() || impl_->frame == 0)
        return makeError(ErrorCode::kNotFound, "Application::readOutput: no frame rendered yet");
    return renderer::readImage(*impl_->device, impl_->composed.color(0));
}

} // namespace orosmo::app
