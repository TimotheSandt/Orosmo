#include "Game.h"

#include <algorithm>
#include <functional>

#include <utility>

#include <engine/core/paths.hpp>
#include "UI/Views/PerformanceView.gen.h"
#include "engine/renderer/draw_list.hpp"
#include "engine/renderer/image.hpp"


namespace rd = engine::renderer;
using engine::core::Profiler;
namespace rhi = engine::rhi;

namespace
{
constexpr const char *kShaderDirectory = GET_RESOURCE_PATH("shader/rhi");
} // namespace

namespace
{
constexpr int kHeadlessWidth = 1280;
constexpr int kHeadlessHeight = 720;
constexpr float kHeadlessStep = 1.0f / 60.0f;
} // namespace

Game::Game(bool headlessMode) : headless(headlessMode) {}

engine::core::Result<std::unique_ptr<Game>> Game::create(bool headless)
{
    std::unique_ptr<Game> game(new Game(headless));
    if (auto r = game->createGraphics(); !r)
    {
        return std::unexpected(r.error());
    }
    return game;
}

engine::core::Result<> Game::createGraphics()
{
    using engine::core::ErrorCode;
    using engine::core::makeError;
    LOG_TRACE("Initializing window");
    rhi::DeviceDesc desc;
    desc.backend = rhi::Backend::kOpenGL;
#ifdef DEBUG
    desc.validation = true;
#else
    desc.validation = false;
#endif
    if (headless)
    {
        desc.surface.size = {kHeadlessWidth, kHeadlessHeight}; // backbuffer hors écran, pas de fenêtre
    }
    else
    {
        if (window.Init([&]() { (void)rhi::prepareWindowHints(desc); }) != 0)
        {
            return makeError(ErrorCode::kUnsupported, "Failed to initialize the window (no display?)");
        }
        desc.surface.nativeWindow = window.GetWindow();
    }
    auto created = rhi::createDevice(desc);
    if (!created)
    {
        window.Close();
        return makeError(created.error().code, "Failed to create the graphics device: " + created.error().message);
    }
    device = std::move(*created);
    LOG_INFO("Graphics device: ", device->capabilities().deviceName, " (", device->capabilities().driverVersion, ")");

    shaders = std::make_unique<rd::ShaderLibrary>(*device);
    pipelines = std::make_unique<rd::PipelineCache>(*device);
    auto scene = rd::SceneRenderer::create(*device, *shaders, *pipelines, {.shaderDirectory = kShaderDirectory});
    if (!scene)
    {
        return makeError(scene.error().code, "Failed to create the scene renderer: " + scene.error().message);
    }
    sceneRenderer = std::move(*scene);
    auto canvasCreated = rd::CanvasRenderer::create(*device, *shaders, *pipelines, kShaderDirectory);
    if (!canvasCreated)
    {
        return makeError(canvasCreated.error().code, "Failed to create the 2D renderer: " + canvasCreated.error().message);
    }
    canvas = std::move(*canvasCreated);

    gpuTimers = std::make_unique<rd::ProfilerGpuTimerFactory>(*device);
    Profiler::SetGpuTimerFactory(gpuTimers.get());
    return {};
}

Game::~Game() { this->stop(); }

engine::core::Result<> Game::init()
{
    this->refreshViewSize();

    this->camera.setPosition(glm::vec3(0.0f, 1.0f, 0.0f));
    this->camera.setFovDegrees(75.0f);
    this->camera.setNearPlane(0.1f);
    this->camera.setFarPlane(1000.0f);
    this->camera.setAspect(static_cast<unsigned>(this->viewWidth), static_cast<unsigned>(this->viewHeight));

    this->world = std::make_unique<World>();
    if (auto ready = this->world->Init(*device, *shaders, kShaderDirectory, this->terrainSeed); !ready)
    {
        return engine::core::makeError(ready.error().code, "Failed to initialize the world: " + ready.error().message);
    }

    if (auto created = this->createUi(); !created)
    {
        return created;
    }

    LOG_INFO("Game initialized");
    return {};
}

void Game::stop()
{
    if (this->stopped)
    {
        return;
    }
    this->stopped = true;

    if (device)
    {
        device->waitIdle();
    }
    Profiler::SetGpuTimerFactory(nullptr);
    Profiler::ClearGpuQueries(); // les minuteurs en attente référencent le périphérique
    if (this->world)
    {
        this->world->Destroy();
        this->world.reset();
    }
    // L'interface référence la police, qui référence le rendu du texte.
    ui.reset();
    font.reset();
    text.reset();
    // Ordre inverse de la création : tout ce qui référence le périphérique avant lui, la fenêtre en dernier.
    composed.reset();
    uiTarget.reset();
    canvas.reset();
    sceneRenderer.reset();
    pipelines.reset();
    shaders.reset();
    gpuTimers.reset();
    device.reset();
    this->window.Close();
}

// Logical size of the picture: the window's, except while capturing (fixed size, reproducible images).
void Game::refreshViewSize()
{
    constexpr int kCaptureWidth = 1280;
    constexpr int kCaptureHeight = 720;
    const bool fixedSize = !this->captureDirectory.empty() || this->headless;
    this->viewWidth = fixedSize ? kCaptureWidth : *window.GetWidthptr();
    this->viewHeight = fixedSize ? kCaptureHeight : *window.GetHeightptr();
    // Pixels réels : sur un écran à échelle (DPI), le tampon d'image est plus grand que la fenêtre. L'interface reste en
    // coordonnées logiques (la taille de la vue) ; le canevas la projette sur les pixels.
    this->pixelWidth = this->viewWidth;
    this->pixelHeight = this->viewHeight;
    if (!fixedSize)
    {
        window.GetFramebufferSize(this->pixelWidth, this->pixelHeight);
    }
}

void Game::enableCapture(std::string directory, std::uint32_t frames)
{
    this->captureDirectory = std::move(directory);
    this->captureFrames = frames;
}

void Game::run()
{
    if (headless)
    {
        // Sans fenêtre : captureFrames images à pas de temps fixe (déterministe), puis la capture éventuelle.
        for (std::uint32_t frame = 0; frame < std::max(captureFrames, 1u); ++frame)
        {
            this->update();
            if (!this->beginFrame())
            {
                LOG_ERROR(1, "Headless: the frame could not be started");
                return;
            }
            Profiler::ProfileGPU("Render", &Game::render, this);
            this->endFrame();
            if (captureFailed)
            {
                return;
            }
        }
        return;
    }
    while (!window.ShouldClose())
    {
        if (!this->window.NewFrame())
        {
            continue;
        }

        this->update();
        if (!this->beginFrame())
        {
            continue;
        }
        Profiler::ProfileGPU("Render", &Game::render, this);
        this->endFrame();
    }
}

void Game::processInput()
{
    InputManager &inputManager = InputManager::GetInstance(this->window.GetWindow());
    if (inputManager.IsKeyJustPressed(KeyButton::ESCAPE))
    {
        this->window.RequestClose();
    }
    if (inputManager.IsKeyJustPressed(KeyButton::F11))
    {
        this->window.ToggleBorderless();
    }
#ifdef DEBUG
    if (inputManager.IsKeyJustPressed(KeyButton::F12))
    {
        this->window.ToggleFullscreen();
    }
    if (inputManager.IsKeyJustPressed(KeyButton::NUM_1))
    {
        sceneRenderer->setRenderScale(0.25f);
        sceneRenderer->setUpscalingEnabled(true);
    }
    if (inputManager.IsKeyJustPressed(KeyButton::NUM_2))
    {
        sceneRenderer->setRenderScale(0.5f);
        sceneRenderer->setUpscalingEnabled(true);
    }
    if (inputManager.IsKeyJustPressed(KeyButton::NUM_3))
    {
        sceneRenderer->setRenderScale(0.75f);
        sceneRenderer->setUpscalingEnabled(true);
    }
    if (inputManager.IsKeyJustPressed(KeyButton::NUM_4))
    {
        sceneRenderer->setUpscalingEnabled(!sceneRenderer->upscalingEnabled());
    }
    if (inputManager.IsKeyJustPressed(KeyButton::NUM_5))
    {
        uiRenderScale = 0.25f;
        uiUpscaling = true;
    }
    if (inputManager.IsKeyJustPressed(KeyButton::NUM_6))
    {
        uiRenderScale = 0.5f;
        uiUpscaling = true;
    }
    if (inputManager.IsKeyJustPressed(KeyButton::NUM_7))
    {
        uiRenderScale = 0.75f;
        uiUpscaling = true;
    }
    if (inputManager.IsKeyJustPressed(KeyButton::NUM_8))
    {
        uiUpscaling = !uiUpscaling;
    }
#endif
}

void Game::update()
{
    float deltaTime = kHeadlessStep;
    this->refreshViewSize();
    if (!headless)
    {
        this->processInput();
        const double fps = this->window.GetFPS();
        deltaTime = fps > 0.0 ? static_cast<float>(1.0 / fps) : kHeadlessStep;
        this->cameraController.update(this->window, this->camera, deltaTime, this->viewWidth, this->viewHeight);
    }

    this->world->Update();
    this->performance.tick(deltaTime, [&]() { return samplePerformance(this->window.GetAverageFPS()); });
    engine::ui::InputFrame input; // le panneau n'est pas interactif : pas d'entrées, seulement le temps écoulé
    input.deltaTime = deltaTime;
    this->ui->update(input, {static_cast<float>(this->viewWidth), static_cast<float>(this->viewHeight)});
}

// Interface : police de l'application (Roboto, taille de base 48 px), thème du panneau (res/themes/hud.json), vue générée par cui.
engine::core::Result<> Game::createUi()
{
    using engine::core::ErrorCode;
    using engine::core::makeError;
    this->text = std::make_unique<engine::ui::TextRenderer>();
    if (!this->text->init(static_cast<unsigned>(this->viewWidth), static_cast<unsigned>(this->viewHeight)) ||
        !this->text->loadFont(GET_RESOURCE_PATH("fonts/Roboto-Regular.ttf"), "default", 48))
    {
        return makeError(ErrorCode::kNotFound, "UI: cannot load the font " GET_RESOURCE_PATH("fonts/Roboto-Regular.ttf"));
    }
    this->text->setActiveFont("default");
    this->font = std::make_unique<engine::ui::TextRendererFont>(*this->text);
    this->ui = std::make_unique<engine::ui::Ui>(*this->font);
    if (auto theme = this->ui->themes().load(GET_RESOURCE_PATH("themes/hud.json")); !theme)
    {
        return makeError(theme.error().code, "UI: " + theme.error().message);
    }
    this->ui->setRoot(cui::buildPerformanceView(*this->ui, this->performance));
    return {};
}

// L'interface se pose sur l'image composée : directement, ou rendue à échelle réduite sur une cible TRANSPARENTE puis composée
// par-dessus la scène (touches de debug 5 à 8, ancien `Renderer2D::SetRenderScale`) : seule l'interface perd en définition.
// L'interface (engine::ui) produit une liste complète à chaque image : un échec ne laisse rien à invalider.
bool Game::renderUi(rhi::CommandEncoder &encoder, const rhi::TextureHandle &composedTexture)
{
    rd::DrawList list;
    this->ui->draw(list);
    auto failed = [&](const std::string &what, const engine::core::Error &error)
    {
        LOG_ERROR(1, what, error.message);
        return false;
    };

    const bool scaled = uiUpscaling && uiRenderScale < 1.0f;
    if (!scaled)
    {
        auto drawn = canvas->render(encoder, list, {composedTexture, rhi::Format::kRGBA8Unorm, {static_cast<std::uint32_t>(pixelWidth), static_cast<std::uint32_t>(pixelHeight)}}, true);
        return drawn ? true : failed("UI: ", drawn.error());
    }

    const auto w = static_cast<std::uint32_t>(std::max(1.0f, static_cast<float>(pixelWidth) * uiRenderScale));
    const auto h = static_cast<std::uint32_t>(std::max(1.0f, static_cast<float>(pixelHeight) * uiRenderScale));
    if (!uiTarget.valid())
    {
        auto target = rd::RenderTarget::create(*device, {.size = {w, h}, .depth = false, .debugName = "ui"});
        if (!target)
        {
            return failed("UI target: ", target.error());
        }
        uiTarget = std::move(*target);
    }
    else if (auto resized = uiTarget.resize(w, h); !resized)
    {
        return failed("UI target: ", resized.error());
    }
    rd::FullscreenBlit &blit = sceneRenderer->blit();
    // Cible effacée en transparent : l'interface y accumule une couleur prémultipliée, composée ensuite sur la scène.
    if (auto drawn = canvas->render(encoder, list, {uiTarget.color(0), rhi::Format::kRGBA8Unorm, {w, h}}, false); !drawn)
    {
        return failed("UI: ", drawn.error());
    }
    if (auto composedUi = blit.compose(encoder, uiTarget.color(0), blit.linearSampler(), composedTexture, rhi::Format::kRGBA8Unorm, "ui.upscale");
        !composedUi)
    {
        return failed("UI compose: ", composedUi.error());
    }
    return true;
}

// Prépare l'image : backbuffer, image composée à la taille de la vue, encodeur. Faux : on saute l'image (fenêtre réduite, erreur).
bool Game::beginFrame()
{
    this->refreshViewSize();
    auto frame = device->beginFrame();
    if (!frame)
    {
        return false; // fenêtre réduite
    }
    backbuffer = *frame;
    const auto viewW = static_cast<std::uint32_t>(this->pixelWidth);
    const auto viewH = static_cast<std::uint32_t>(this->pixelHeight);
    if (viewW == 0 || viewH == 0)
    {
        return false;
    }
    if (!composed.valid())
    {
        auto target = rd::RenderTarget::create(*device, {.size = {viewW, viewH}, .depth = false, .debugName = "composed"});
        if (!target)
        {
            LOG_ERROR(1, "Composed target: ", target.error().message);
            return false;
        }
        composed = std::move(*target);
    }
    else if (auto resized = composed.resize(viewW, viewH); !resized)
    {
        LOG_ERROR(1, "Composed target: ", resized.error().message);
        return false;
    }
    frameEncoder = device->createCommandEncoder();
    gpuTimers->setEncoder(frameEncoder.get()); // les minuteries GPU du Profiler écrivent dans cet encodeur
    return true;
}

void Game::render()
{
    rhi::CommandEncoder &encoder = *frameEncoder;
    const auto viewW = static_cast<std::uint32_t>(this->pixelWidth);
    const auto viewH = static_cast<std::uint32_t>(this->pixelHeight);

    this->camera.setAspect(viewW, viewH);
    sceneRenderer->setOutputResolution(this->pixelWidth, this->pixelHeight);
    const glm::vec4 clear = window.GetClearColor();
    // Un échec de la scène (pipeline qui ne se lie pas, cible refusée...) rend l'image fausse : elle n'est ni comptée ni capturée.
    auto keep = [&](engine::core::Result<> r, const char *step)
    {
        if (!r && !this->sceneError)
        {
            LOG_ERROR(1, "Scene (", step, "): ", r.error().message);
            this->sceneError = r.error();
        }
    };
    Profiler::ProfileGPU("Clear", [&]() { keep(sceneRenderer->beginFrame(encoder, {.clearColor = {clear.r, clear.g, clear.b, clear.a}}), "beginFrame"); });
    keep(sceneRenderer->setCamera(this->camera), "setCamera");
    Profiler::ProfileGPU("RenderWorld", [&]() { keep(this->world->Render(*sceneRenderer, this->camera), "world"); });
    Profiler::ProfileGPU("Upscale", [&]() { keep(sceneRenderer->endFrame({composed.color(0), rhi::Format::kRGBA8Unorm, {viewW, viewH}}), "endFrame"); });

    Profiler::ProfileGPU("UIUpscale", [&]() { (void)renderUi(encoder, composed.color(0)); });

    // Présentation : l'image composée est copiée (étirée si la vue n'a pas la taille de la fenêtre) sur le backbuffer.
    (void)sceneRenderer->blit().copy(encoder, composed.color(0), sceneRenderer->blit().linearSampler(), backbuffer, rhi::Format::kRGBA8Unorm, "present");
}

void Game::endFrame()
{
    gpuTimers->setEncoder(nullptr);
    const auto result = device->submit(std::move(frameEncoder));
    if (!result)
    {
        LOG_ERROR(1, "Frame: ", result.error().message);
    }
    // Image valide : soumise ET scène dessinée sans erreur (sinon la capture lirait une cible incomplète).
    const bool submitted = result.has_value() && !this->sceneError;
    this->sceneError.reset();

    if (!submitted)
    {
        // Image refusée : pas comptée. Pendant une capture, la sortie ne serait pas fiable : on s'arrête en échec.
        if (!this->captureDirectory.empty())
        {
            this->captureFailed = true;
            this->window.RequestClose();
        }
        Profiler::Profile("SwapBuffers", [&]() { (void)device->present(); });
        return;
    }
    ++this->renderedFrames;
    if (!this->captureDirectory.empty() && !this->captureDone && this->renderedFrames >= this->captureFrames)
    {
        // scene.png : couleur de la scène à la résolution de rendu ; ui.png : image composée (scène + interface), comme avant.
        const bool sceneSaved = sceneRenderer->saveColorPng(this->captureDirectory + "/scene.png").has_value();
        const bool uiSaved = rd::savePng(*device, composed.color(0), this->captureDirectory + "/ui.png").has_value();
        this->captureDone = sceneSaved && uiSaved;
        this->window.RequestClose();
    }

    Profiler::Profile("SwapBuffers", [&]() { (void)device->present(); });
}
