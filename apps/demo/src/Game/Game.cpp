#include "Game.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <utility>

#include "UI/Views/DemoView.gen.h"
#include "UI/Views/PerformanceView.gen.h"
#include <orosmo/core/log.hpp>
#include <orosmo/core/paths.hpp>
#include <orosmo/renderer/draw_list.hpp>
#include <orosmo/renderer/forward_plus.hpp>
#include <orosmo/renderer/image.hpp>
#include <orosmo/ui/window_input.hpp>

namespace platform = orosmo::platform;
namespace rd = orosmo::renderer;
namespace rhi = orosmo::rhi;
namespace ui = orosmo::ui;
using orosmo::core::Profiler;

namespace
{
constexpr const char *kShaderDirectory = GET_RESOURCE_PATH("shader/rhi");
constexpr float kFixedStep = 1.0f / 60.0f;
constexpr float kDefaultOrthoHeight = 18.0f;
} // namespace

// Plugin de rendu de la démo (REN-01) : l'interface (CanvasRenderer, inchangé) puis la présentation sont deux passes du graphe de
// l'image, après la mise à l'échelle de la scène. Le contenu des passes reste celui d'avant M5 (`Game::renderUi`, copie étirée).
class Game::OverlayFeature final : public rd::RenderFeature
{
  public:
    explicit OverlayFeature(Game &game) : game_(game) {}
    std::string name() const override { return "demo.overlay"; }
    orosmo::core::Result<> declarePasses(rd::RenderGraphBuilder &builder) override
    {
        rd::RenderGraph &graph = builder.graph();
        const rd::RgTexture output = builder.resources().output;
        // Une interface refusée (liste ou cible invalide) ne casse pas l'image : elle n'est ni comptée ni capturée (uiFailed).
        graph
            .addPass("ui", rd::PassType::kGeneric,
                     [this](rd::RenderPassContext &ctx) -> orosmo::core::Result<>
                     {
                         game_.uiFailed = !game_.renderUi(ctx.encoder(), game_.composed.color(0));
                         uiDone = true;
                         return {};
                     })
            .read(output)
            .write(output);
        // Taille sans importance ici (ce n'est pas un attachement déclaré) : la copie étire l'image sur tout le backbuffer.
        const rd::RgTexture backbuffer = graph.importTexture("backbuffer", game_.backbuffer, {.format = rhi::Format::kRGBA8Unorm, .size = {1, 1}});
        graph
            .addPass("present", rd::PassType::kGeneric,
                     [this, output](rd::RenderPassContext &ctx) -> orosmo::core::Result<>
                     {
                         game_.present(ctx.encoder(), ctx.texture(output));
                         presented = true;
                         return {};
                     })
            .read(output)
            .write(backbuffer);
        return {};
    }

    bool uiDone = false;
    bool presented = false;

  private:
    Game &game_;
};

Game::Game(bool headlessMode, int width, int height, rhi::Backend backend)
    : window("Orosmo", width, height, platform::WindowState::kWindowed), requestedWidth(width), requestedHeight(height), selectedBackend(backend),
      headless(headlessMode)
{
}

orosmo::core::Result<std::unique_ptr<Game>> Game::create(bool headless, int width, int height, rhi::Backend backend)
{
    std::unique_ptr<Game> game(new Game(headless, width, height, backend));
    if (auto r = game->createGraphics(); !r)
    {
        return std::unexpected(r.error());
    }
    return game;
}

orosmo::core::Result<> Game::createGraphics()
{
    using orosmo::core::ErrorCode;
    using orosmo::core::makeError;
    LOG_TRACE("Initializing window");
    rhi::DeviceDesc desc;
    desc.backend = this->selectedBackend;
#ifdef DEBUG
    desc.validation = true;
#else
    desc.validation = false;
#endif
    if (this->headless)
    {
        desc.surface.size = {static_cast<std::uint32_t>(this->requestedWidth), static_cast<std::uint32_t>(this->requestedHeight)};
    }
    else
    {
        if (this->window.init([&]() { (void)rhi::prepareWindowHints(desc); }) != 0)
        {
            return makeError(ErrorCode::kUnsupported, "Failed to initialize the window (no display?)");
        }
        desc.surface.nativeWindow = this->window.getWindow();
    }
    auto created = rhi::createDevice(desc);
    if (!created)
    {
        this->window.close();
        return makeError(created.error().code, "Failed to create the graphics device: " + created.error().message);
    }
    this->device = std::move(*created);
    const auto &caps = this->device->capabilities();
    LOG_INFO("Graphics device: ", caps.deviceName, " (", caps.driverVersion, ")");
    if (!caps.deviceName.empty() && !caps.driverVersion.empty())
    {
        this->uiState.deviceInfo = caps.deviceName + " (" + caps.driverVersion + ")";
    }
    else if (!caps.deviceName.empty())
    {
        this->uiState.deviceInfo = caps.deviceName;
    }
    else
    {
        this->uiState.deviceInfo = std::string(demo::backendDisplayName(this->selectedBackend));
    }

    this->shaders = std::make_unique<rd::ShaderLibrary>(*this->device);
    this->pipelines = std::make_unique<rd::PipelineCache>(*this->device);
    auto createdSceneRenderer = rd::SceneRenderer::create(*this->device, *this->shaders, *this->pipelines, {.shaderDirectory = kShaderDirectory});
    if (!createdSceneRenderer)
    {
        return makeError(createdSceneRenderer.error().code, "Failed to create the scene renderer: " + createdSceneRenderer.error().message);
    }
    this->sceneRenderer = std::move(*createdSceneRenderer);
    auto canvasCreated = rd::CanvasRenderer::create(*this->device, *this->shaders, *this->pipelines, kShaderDirectory);
    if (!canvasCreated)
    {
        return makeError(canvasCreated.error().code, "Failed to create the 2D renderer: " + canvasCreated.error().message);
    }
    this->canvas = std::move(*canvasCreated);

    auto overlay = this->sceneRenderer->addFeature(std::make_unique<OverlayFeature>(*this));
    if (!overlay)
    {
        return makeError(overlay.error().code, "Failed to register the overlay pass: " + overlay.error().message);
    }
    this->overlay = static_cast<OverlayFeature *>(*overlay);

    this->gpuTimers = std::make_unique<rd::ProfilerGpuTimerFactory>(*this->device);
    Profiler::SetGpuTimerFactory(this->gpuTimers.get());
    return {};
}

Game::~Game() { this->stop(); }

orosmo::core::Result<> Game::createPostEffects()
{
    const bool aaOn = this->antiAliasingMode != rd::AntiAliasingMode::kNone;
    if (this->postMode == demo::PostMode::kNone && !aaOn)
    {
        return {}; // rien d'enregistré : graphe et image identiques à ceux d'avant les post-traitements et l'anticrénelage
    }
    // Ordre de docs/03 §11 : 3D (scene, transparents, 2D monde de --scene mixed) -> post-traitements -> interface et présentation.
    // Les effets sont enregistrés après les plugins de la scène, et l'interface est réenregistrée après eux.
    this->sceneRenderer->removeFeature("demo.overlay");
    this->overlay = nullptr;
    // REN-19 : TAA avant les effets (scène HDR), FXAA / SMAA après (valeurs affichées).
    std::shared_ptr<rd::PostContext> aaContext;
    if (aaOn)
    {
        auto context = rd::PostContext::create(*this->device, *this->shaders, *this->pipelines, kShaderDirectory);
        if (!context)
        {
            return orosmo::core::makeError(context.error().code, "Failed to create the anti-aliasing: " + context.error().message);
        }
        aaContext = *context;
    }
    if (this->antiAliasingMode == rd::AntiAliasingMode::kTaa)
    {
        auto taa = rd::addTemporalAntiAliasing(*this->sceneRenderer, aaContext);
        if (!taa)
        {
            return orosmo::core::makeError(taa.error().code, "Failed to create the TAA: " + taa.error().message);
        }
        this->antiAliasing.taa = *taa;
    }
    if (this->postMode != demo::PostMode::kNone)
    {
        auto added = rd::addPostEffects(*this->sceneRenderer, *this->device, *this->shaders, *this->pipelines, kShaderDirectory);
        if (!added)
        {
            return orosmo::core::makeError(added.error().code, "Failed to create the post-processing effects: " + added.error().message);
        }
        this->post = *added;
    }
    if (this->antiAliasingMode == rd::AntiAliasingMode::kFxaa || this->antiAliasingMode == rd::AntiAliasingMode::kSmaa)
    {
        auto edge = rd::addEdgeAntiAliasing(*this->sceneRenderer, aaContext);
        if (!edge)
        {
            return orosmo::core::makeError(edge.error().code, "Failed to create the anti-aliasing: " + edge.error().message);
        }
        this->antiAliasing = *edge;
    }
    auto overlayAgain = this->sceneRenderer->addFeature(std::make_unique<OverlayFeature>(*this));
    if (!overlayAgain)
    {
        return orosmo::core::makeError(overlayAgain.error().code, "Failed to register the overlay pass: " + overlayAgain.error().message);
    }
    this->overlay = static_cast<OverlayFeature *>(*overlayAgain);
    if (aaOn)
    {
        if (auto selected = this->antiAliasing.select(this->antiAliasingMode); !selected)
        {
            return selected;
        }
        LOG_INFO("Anti-aliasing: ", rd::toString(this->antiAliasingMode));
    }
    if (this->postMode == demo::PostMode::kNone)
    {
        return {};
    }
    // Les couleurs de la démo sont écrites en sRGB et lues comme linéaires (pas de linéarisation des matériaux) : l'image HDR,
    // encodée en sRGB en sortie, est plus claire que l'image LDR. Exposition neutre.
    this->post.tonemap->setEnabled(true);
    this->post.tonemap->settings().tonemapper = rd::Tonemapper::kAces;
    this->post.tonemap->settings().exposureEv = 0.0f;
    if (this->postMode == demo::PostMode::kBloom || this->postMode == demo::PostMode::kFull)
    {
        this->post.bloom->setEnabled(true);
        this->post.bloom->settings().threshold = 0.8f;
        this->post.bloom->settings().intensity = 0.15f;
    }
    if (this->postMode == demo::PostMode::kFull)
    {
        if (this->post.autoExposure != nullptr)
        {
            this->post.autoExposure->setEnabled(true);
            this->post.autoExposure->settings().key = 0.18f;
        }
        // Étalonnage chaud et un peu plus contrasté, calculé (aucun fichier) : rouge relevé, bleu baissé, courbe en S douce.
        rd::ColorLut lut = rd::identityLut(17);
        for (glm::vec3 &c : lut.values)
        {
            const glm::vec3 s = c * c * (3.0f - 2.0f * c);
            const glm::vec3 contrast = glm::mix(c, s, 0.35f);
            c = glm::clamp(contrast * glm::vec3(1.06f, 1.0f, 0.9f) + glm::vec3(0.015f, 0.005f, 0.0f), 0.0f, 1.0f);
        }
        if (auto r = this->post.colorGrading->setLut(lut); !r)
        {
            return r;
        }
        this->post.colorGrading->setEnabled(true);
        this->post.vignette->setEnabled(true);
    }
    LOG_INFO("Post-processing: HDR scene, ", rd::toString(this->post.tonemap->settings().tonemapper), " tonemapping",
             this->post.bloom->enabled() ? ", bloom" : "", this->post.autoExposure != nullptr && this->post.autoExposure->enabled() ? ", auto exposure" : "",
             this->post.colorGrading->enabled() ? ", LUT, vignette" : "");
    return this->post.sync(*this->sceneRenderer);
}

orosmo::core::Result<> Game::init()
{
    this->refreshViewSize();

    this->resetCamera();
    this->camera.setFovDegrees(this->uiState.scene.fovDegrees);
    this->camera.setNearPlane(0.1f);
    this->camera.setFarPlane(1000.0f);
    this->camera.setAspect(static_cast<unsigned>(this->viewWidth), static_cast<unsigned>(this->viewHeight));

    // REN-35 : le mode de debug demande ses AOV AVANT le chargement du programme de maillage, compilé avec leurs macros (sans mode,
    // aucune macro : le programme et la cible de scène sont ceux d'avant).
    if (auto set = this->sceneRenderer->setDebugView(this->debugView); !set)
    {
        return orosmo::core::makeError(set.error().code, "Failed to set the debug view: " + set.error().message);
    }
    // REN-19 : le TAA lit le mouvement et la profondeur linéaire ; demandés ici pour que le programme de maillage les écrive (le
    // plugin, enregistré plus tard après les plugins de la scène, les exige aussi).
    if (this->antiAliasingMode == rd::AntiAliasingMode::kTaa)
    {
        for (const rd::Aov aov : rd::AovSet{rd::Aov::kMotion, rd::Aov::kLinearDepth}.list())
        {
            if (auto declared = this->sceneRenderer->declareAov(aov); !declared)
            {
                return orosmo::core::makeError(declared.error().code, "Failed to request the TAA inputs: " + declared.error().message);
            }
        }
    }
    // REN-04 : forward+ (pré-passe de profondeur + clusters en compute) si demandé et possible ; sinon forward (règle 11).
    bool forwardPlus = false;
    if (this->lightingMode == demo::LightingMode::kForwardPlus)
    {
        auto enabled = rd::enableForwardPlus(*this->sceneRenderer, *this->device, *this->shaders, *this->pipelines, kShaderDirectory);
        if (enabled)
        {
            forwardPlus = true;
            LOG_INFO("Lighting: clustered forward+ (", (*enabled)->settings().gridX, "x", (*enabled)->settings().gridY, "x",
                     (*enabled)->settings().gridZ, " clusters, ", (*enabled)->settings().maxLightsPerCluster, " lights per cluster)");
        }
        else
        {
            LOG_WARNING("Lighting: forward+ unavailable, using forward: ", enabled.error().message);
        }
    }
    this->scene = std::make_unique<demo::DemoScene>();
    rd::ShadowFeature *shadowFeature = nullptr;
    if (this->shadowsEnabled)
    {
        auto shadows = rd::ShadowFeature::create(*this->device, *this->shaders, *this->pipelines, kShaderDirectory);
        if (!shadows)
        {
            return orosmo::core::makeError(shadows.error().code, "Failed to create the shadows: " + shadows.error().message);
        }
        auto added = this->sceneRenderer->addFeature(std::move(*shadows));
        if (!added)
        {
            return std::unexpected(added.error());
        }
        shadowFeature = static_cast<rd::ShadowFeature *>(*added);
        this->scene->setShadows(shadowFeature);
    }
    this->scene->setDebugOptions(this->debugView, this->freezeFrustum);
    if (auto ready = this->scene->init(*this->device, *this->shaders, kShaderDirectory, forwardPlus, this->pointLightCount,
                                       this->sceneRenderer->aovShaderDefines()); !ready)
    {
        return orosmo::core::makeError(ready.error().code, "Failed to initialize the demo scene: " + ready.error().message);
    }
    if (auto ready = this->scene->initInstances(*this->device, *this->shaders, *this->pipelines, kShaderDirectory, this->instanceCount); !ready)
    {
        return orosmo::core::makeError(ready.error().code, "Failed to create the instanced objects: " + ready.error().message);
    }

    if (this->mixedScene)
    {
        // Les plugins du mélange passent AVANT l'interface et la présentation : on réenregistre ces dernières après eux.
        this->sceneRenderer->removeFeature("demo.overlay");
        this->mixed = std::make_unique<demo::MixedScene>();
        if (auto ready = this->mixed->init(*this->device, *this->shaders, *this->pipelines, *this->sceneRenderer, kShaderDirectory); !ready)
        {
            return orosmo::core::makeError(ready.error().code, "Failed to initialize the mixed scene: " + ready.error().message);
        }
        if (shadowFeature != nullptr && this->transparentShadow)
        {
            if (auto casters = this->mixed->addShadowCasters(*shadowFeature, *this->transparentShadow); !casters)
            {
                return orosmo::core::makeError(casters.error().code, "Failed to register the pane shadows: " + casters.error().message);
            }
        }
        auto overlayAgain = this->sceneRenderer->addFeature(std::make_unique<OverlayFeature>(*this));
        if (!overlayAgain)
        {
            return orosmo::core::makeError(overlayAgain.error().code, "Failed to register the overlay pass: " + overlayAgain.error().message);
        }
        this->overlay = static_cast<OverlayFeature *>(*overlayAgain);
    }

    if (auto created = this->createPostEffects(); !created)
    {
        return created;
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

    if (this->device)
    {
        this->device->waitIdle();
    }
    Profiler::SetGpuTimerFactory(nullptr);
    Profiler::ClearGpuQueries(); // les minuteurs en attente référencent le périphérique
    if (this->mixed)
    {
        this->mixed->destroy();
        this->mixed.reset();
    }
    if (this->scene)
    {
        this->scene->destroy();
        this->scene.reset();
    }
    // Ordre critique : `fullUi` et `hudUi` référencent `uiState`, `performance`, `font` (qui référence `text`) et `watcher`.
    this->fullUi.reset();
    this->uiState.ui = nullptr;
    this->hudUi.reset();
    this->font.reset();
    this->text.reset();
    this->watcher.reset();
    // Ordre inverse de la création : tout ce qui référence le périphérique avant lui, la fenêtre en dernier.
    this->frameEncoder.reset();
    this->composed.reset();
    this->uiTarget.reset();
    this->canvas.reset();
    this->overlay = nullptr; // possédé par le renderer
    this->sceneRenderer.reset();
    this->pipelines.reset();
    this->shaders.reset();
    this->gpuTimers.reset();
    this->device.reset();
    this->window.close();
}

// Logical size of the picture: the window's, except while capturing or headless (fixed size, reproducible images).
void Game::refreshViewSize()
{
    const bool fixedSize = !this->captureDirectory.empty() || this->headless;
    this->viewWidth = fixedSize ? this->requestedWidth : this->window.getWidth();
    this->viewHeight = fixedSize ? this->requestedHeight : this->window.getHeight();
    // Pixels réels : sur un écran à échelle (DPI), le tampon d'image est plus grand que la fenêtre. L'interface reste en
    // coordonnées logiques (la taille de la vue) ; le canevas la projette sur les pixels.
    this->pixelWidth = this->viewWidth;
    this->pixelHeight = this->viewHeight;
    if (!fixedSize)
    {
        this->window.getFramebufferSize(this->pixelWidth, this->pixelHeight);
    }
}

void Game::resetCamera()
{
    this->camera.setPosition(demo::DemoScene::defaultCameraPosition());
    this->camera.setOrientation(demo::DemoScene::defaultCameraOrientation());
    const glm::vec3 forward = this->camera.orientation();
    this->uiState.cameraYawRadians = std::atan2(forward.x, -forward.z);
}

void Game::applySceneParams()
{
    const demo::DemoSceneParams &params = this->uiState.scene;
    this->camera.setFovDegrees(std::clamp(params.fovDegrees, 15.0f, 120.0f));
    if (params.projection == demo::ProjectionMode::kOrthographic)
    {
        this->camera.setOrthographic(kDefaultOrthoHeight);
    }
    else
    {
        this->camera.setPerspective();
    }
    this->camera.setWireframe(params.wireframe || this->cameraController.wireframeRequested());
    this->sceneRenderer->setCameraRelative(params.cameraRelative);
    const float clampedScale = std::clamp(params.renderScale, 0.25f, 1.0f);
    this->sceneRenderer->setRenderScale(clampedScale);
    this->sceneRenderer->setUpscalingEnabled(clampedScale < 0.999f);
}

void Game::enableCapture(std::string directory, std::uint32_t frames)
{
    this->captureDirectory = std::move(directory);
    this->captureFrames = frames;
}

void Game::run()
{
    if (this->headless)
    {
        // Sans fenêtre : captureFrames images à pas de temps fixe (déterministe), puis la capture éventuelle.
        const std::uint32_t totalFrames = std::max(this->captureFrames, 1u);
        for (std::uint32_t frame = 0; frame < totalFrames; ++frame)
        {
            this->update();
            if (this->headlessFrameFailed || this->captureFailed)
            {
                return;
            }
            if (!this->beginFrame())
            {
                LOG_ERROR(1, "Headless: the frame could not be started");
                this->headlessFrameFailed = true;
                return;
            }
            Profiler::ProfileGPU("Render", &Game::render, this);
            this->endFrame();
            if (this->headlessFrameFailed || this->captureFailed)
            {
                return;
            }
        }
        return;
    }
    while (!this->window.shouldClose())
    {
        if (!this->window.newFrame())
        {
            continue;
        }

        this->update();
        if (this->window.shouldClose())
        {
            break;
        }
        if (!this->beginFrame())
        {
            continue;
        }
        Profiler::ProfileGPU("Render", &Game::render, this);
        this->endFrame();
    }
}

void Game::processInput(bool toggleUiJustPressed)
{
    platform::InputManager &inputManager = platform::InputManager::getInstance(this->window.getWindow());
    // Échap n'arrête JAMAIS l'application : hors de l'interface complète, il l'ouvre (la fermeture est gérée après fullUi->update).
    if (toggleUiJustPressed && this->uiMode != demo::UiMode::kFull)
    {
        this->uiMode = demo::UiMode::kFull;
        this->window.setCursorMode(platform::CursorMode::kNormal);
    }

    const bool uiWantsKeyboard = this->fullUi && this->fullUi->wantsKeyboard();
    if (!uiWantsKeyboard && inputManager.isKeyJustPressed(platform::KeyButton::kF11))
    {
        this->window.toggleBorderless();
    }
#ifdef DEBUG
    if (!uiWantsKeyboard && inputManager.isKeyJustPressed(platform::KeyButton::kF12))
    {
        this->window.toggleFullscreen();
    }
    if (this->uiMode != demo::UiMode::kFull)
    {
        if (inputManager.isKeyJustPressed(platform::KeyButton::kNum1))
        {
            this->uiState.scene.renderScale = 0.25f;
        }
        if (inputManager.isKeyJustPressed(platform::KeyButton::kNum2))
        {
            this->uiState.scene.renderScale = 0.5f;
        }
        if (inputManager.isKeyJustPressed(platform::KeyButton::kNum3))
        {
            this->uiState.scene.renderScale = 0.75f;
        }
        if (inputManager.isKeyJustPressed(platform::KeyButton::kNum4))
        {
            this->uiState.scene.renderScale = this->uiState.scene.renderScale < 0.999f ? 1.0f : 0.5f;
        }
        if (inputManager.isKeyJustPressed(platform::KeyButton::kNum5))
        {
            this->uiRenderScale = 0.25f;
            this->uiUpscaling = true;
        }
        if (inputManager.isKeyJustPressed(platform::KeyButton::kNum6))
        {
            this->uiRenderScale = 0.5f;
            this->uiUpscaling = true;
        }
        if (inputManager.isKeyJustPressed(platform::KeyButton::kNum7))
        {
            this->uiRenderScale = 0.75f;
            this->uiUpscaling = true;
        }
        if (inputManager.isKeyJustPressed(platform::KeyButton::kNum8))
        {
            this->uiUpscaling = !this->uiUpscaling;
        }
    }
#endif
}

void Game::update()
{
    const bool capturing = !this->captureDirectory.empty();
    float deltaTime = kFixedStep;
    this->refreshViewSize();

    if (this->watcher)
    {
        this->watchClock += static_cast<double>(deltaTime);
        if (this->watchClock > 0.5)
        {
            this->watchClock = 0.0;
            this->watcher->poll();
        }
    }

    const bool wasFullUi = this->uiMode == demo::UiMode::kFull;
    bool escapeJustPressed = false;
    bool f1JustPressed = false;
    if (!this->headless)
    {
        if (!capturing)
        {
            const double fps = this->window.getFPS();
            deltaTime = fps > 0.0 ? static_cast<float>(1.0 / fps) : kFixedStep;
            platform::InputManager &inputManager = platform::InputManager::getInstance(this->window.getWindow());
            escapeJustPressed = inputManager.isKeyJustPressed(platform::KeyButton::kEscape);
            f1JustPressed = inputManager.isKeyJustPressed(platform::KeyButton::kF1);
            this->processInput(escapeJustPressed || f1JustPressed);
            if (this->uiMode != demo::UiMode::kFull)
            {
                this->cameraController.update(this->window, this->camera, deltaTime, this->viewWidth, this->viewHeight);
            }
        }
    }

    this->scene->update(deltaTime, this->uiState.scene);
    this->performance.tick(deltaTime, [&]() { return samplePerformance(this->window.getAverageFPS()); });

    const glm::vec3 forward = this->camera.orientation();
    this->uiState.cameraYawRadians = std::atan2(forward.x, -forward.z);
    this->uiState.tick(deltaTime, deltaTime * 1000.0f);

    const glm::vec2 viewport{static_cast<float>(this->viewWidth), static_cast<float>(this->viewHeight)};

    if (this->uiMode == demo::UiMode::kFull && this->fullUi)
    {
        const bool uiTakesEscape = this->fullUi->wantsKeyboard() || this->fullUi->popup() != nullptr || this->fullUi->modalOpen();
        ui::InputFrame input;
        input.deltaTime = deltaTime;

        if (capturing)
        {
            const std::string &targetId = !this->clickId.empty() ? this->clickId : this->hoverId;
            if (!targetId.empty() && this->renderedFrames >= 1)
            {
                ui::Widget *target = this->fullUi->find(targetId);
                if (target == nullptr)
                {
                    LOG_ERROR(1, "Capture: no widget with id '", targetId, "' (--hover / --click)");
                    std::cerr << "Orosmo: no widget with id '" << targetId << "' (--hover / --click)\n";
                    this->captureFailed = true;
                    if (this->headless)
                    {
                        this->headlessFrameFailed = true;
                    }
                    this->window.requestClose();
                    return;
                }
                input.mouse = target->rect().position + target->rect().size * 0.5f;
                input.mouseDown = !this->clickId.empty() && this->renderedFrames == 1;
            }
        }
        else if (!this->headless && wasFullUi)
        {
            input = ui::readWindowInput(this->window, deltaTime);
        }

        this->fullUi->update(input, viewport);

        if (this->uiState.themeRequested)
        {
            this->uiState.themeRequested = false;
            (void)this->loadFullUiTheme(false);
            this->fullUi->layout(viewport);
        }
        if (this->uiState.resetCameraRequested)
        {
            this->uiState.resetCameraRequested = false;
            this->resetCamera();
        }
        if (this->uiState.captureRequested)
        {
            this->uiState.captureRequested = false;
            this->manualCapturePending = true;
        }
        if (this->uiState.quitRequested)
        {
            this->uiState.quitRequested = false;
            this->window.requestClose();
        }
        if (this->uiState.closeUiRequested)
        {
            this->uiState.closeUiRequested = false;
            this->uiMode = demo::UiMode::kHud;
        }

        // Échap referme l'interface complète vers le HUD uniquement si aucun champ, popup ou dialogue ne l'a consommé.
        if (!this->headless && !capturing && wasFullUi && this->uiMode == demo::UiMode::kFull && !uiTakesEscape &&
            !this->fullUi->wantsKeyboard() && this->fullUi->popup() == nullptr && !this->fullUi->modalOpen() && (escapeJustPressed || f1JustPressed))
        {
            this->uiMode = demo::UiMode::kHud;
        }
    }

    if (this->uiMode == demo::UiMode::kHud && this->hudUi)
    {
        ui::InputFrame hudInput;
        hudInput.deltaTime = deltaTime;
        this->hudUi->update(hudInput, viewport);
    }
}

bool Game::loadFullUiTheme(bool reportAsError)
{
    const auto themes = this->uiState.themes();
    const std::size_t index =
        (this->uiState.theme >= 0 && static_cast<std::size_t>(this->uiState.theme) < themes.size()) ? static_cast<std::size_t>(this->uiState.theme) : 0u;
    const std::string &themeName = themes[index];
    const std::filesystem::path themePath = std::filesystem::path(GET_RESOURCE_PATH("themes")) / (themeName + ".json");
    bool ok = true;
    if (auto loaded = this->fullUi->themes().load(themePath); !loaded)
    {
        ok = false;
        this->uiState.record("theme error: " + loaded.error().message);
        if (reportAsError)
        {
            LOG_ERROR(1, "UI theme error (", themePath.generic_string(), "): ", loaded.error().message);
        }
    }
    else
    {
        this->uiState.record("theme " + themeName);
    }
    if (this->watcher)
    {
        this->fullUi->themes().watch(*this->watcher);
    }
    return ok;
}

// Interface : police (Roboto, taille de base 48 px), HUD compact (hud.json + PerformanceView.cui) et interface complète
// (ThemeHost + FileWatcher + DemoView.cui).
orosmo::core::Result<> Game::createUi()
{
    using orosmo::core::ErrorCode;
    using orosmo::core::makeError;
    this->text = std::make_unique<ui::TextRenderer>();
    if (!this->text->init(static_cast<unsigned>(this->viewWidth), static_cast<unsigned>(this->viewHeight)) ||
        !this->text->loadFont(GET_RESOURCE_PATH("fonts/Roboto-Regular.ttf"), "default", 48))
    {
        return makeError(ErrorCode::kNotFound, "UI: cannot load the font " GET_RESOURCE_PATH("fonts/Roboto-Regular.ttf"));
    }
    this->text->setActiveFont("default");
    this->font = std::make_unique<ui::TextRendererFont>(*this->text);

    // Compact HUD
    this->hudUi = std::make_unique<ui::Ui>(*this->font);
    if (auto theme = this->hudUi->themes().load(GET_RESOURCE_PATH("themes/hud.json")); !theme)
    {
        return makeError(theme.error().code, "UI (HUD): " + theme.error().message);
    }
    this->hudUi->setRoot(cui::buildPerformanceView(*this->hudUi, this->performance));

    // Full UI showcase
    this->watcher = std::make_unique<orosmo::core::FileWatcher>();
    this->fullUi = std::make_unique<ui::Ui>(*this->font);
    if (!this->headless)
    {
        ui::useSystemClipboard(*this->fullUi, this->window);
    }
    this->uiState.ui = this->fullUi.get();
    if (!this->uiState.selectThemeByName(this->initialTheme))
    {
        return makeError(ErrorCode::kInvalidArgument, "UI: unknown theme '" + this->initialTheme + "'");
    }
    if (!this->uiState.selectTabByName(this->initialTab))
    {
        return makeError(ErrorCode::kInvalidArgument, "UI: unknown tab '" + this->initialTab + "'");
    }
    if (!this->loadFullUiTheme(true))
    {
        const auto &err = this->fullUi->themes().lastError();
        return makeError(err ? err->code : ErrorCode::kNotFound, "UI: " + (err ? err->message : std::string("failed to load theme")));
    }
    this->fullUi->actions().add("demoAction", [this](const ui::Event &) { this->uiState.record("action  demoAction (ActionRegistry)"); });
    this->fullUi->setRoot(cui::buildDemoView(*this->fullUi, this->uiState, this->performance));
    for (const std::string &missing : this->fullUi->actions().missing())
    {
        LOG_ERROR(1, "UI: unregistered action ", missing);
    }
    return {};
}

// L'interface active se pose sur l'image composée (preserveTarget = true), directement ou via cible réduite (touches 5 à 8).
bool Game::renderUi(rhi::CommandEncoder &encoder, const rhi::TextureHandle &composedTexture)
{
    ui::Ui *activeUi = nullptr;
    if (this->uiMode == demo::UiMode::kHud)
    {
        activeUi = this->hudUi.get();
    }
    else if (this->uiMode == demo::UiMode::kFull)
    {
        activeUi = this->fullUi.get();
    }
    if (activeUi == nullptr)
    {
        return true;
    }

    rd::DrawList list;
    activeUi->draw(list);
    auto failed = [&](const std::string &what, const orosmo::core::Error &error)
    {
        LOG_ERROR(1, what, error.message);
        return false;
    };

    const bool scaled = this->uiUpscaling && this->uiRenderScale < 1.0f;
    if (!scaled)
    {
        auto drawn = this->canvas->render(
            encoder, list, {composedTexture, rhi::Format::kRGBA8Unorm, {static_cast<std::uint32_t>(this->pixelWidth), static_cast<std::uint32_t>(this->pixelHeight)}},
            true);
        return drawn ? true : failed("UI: ", drawn.error());
    }

    const auto w = static_cast<std::uint32_t>(std::max(1.0f, static_cast<float>(this->pixelWidth) * this->uiRenderScale));
    const auto h = static_cast<std::uint32_t>(std::max(1.0f, static_cast<float>(this->pixelHeight) * this->uiRenderScale));
    if (!this->uiTarget.valid())
    {
        auto target = rd::RenderTarget::create(*this->device, {.size = {w, h}, .depth = false, .debugName = "ui"});
        if (!target)
        {
            return failed("UI target: ", target.error());
        }
        this->uiTarget = std::move(*target);
    }
    else if (auto resized = this->uiTarget.resize(w, h); !resized)
    {
        return failed("UI target: ", resized.error());
    }
    rd::FullscreenBlit &blit = this->sceneRenderer->blit();
    if (auto drawn = this->canvas->render(encoder, list, {this->uiTarget.color(0), rhi::Format::kRGBA8Unorm, {w, h}}, false); !drawn)
    {
        return failed("UI: ", drawn.error());
    }
    if (auto composedUi = blit.compose(encoder, this->uiTarget.color(0), blit.linearSampler(), composedTexture, rhi::Format::kRGBA8Unorm, "ui.upscale");
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
    auto frame = this->device->beginFrame();
    if (!frame)
    {
        return false; // fenêtre réduite
    }
    this->backbuffer = *frame;
    const auto viewW = static_cast<std::uint32_t>(this->pixelWidth);
    const auto viewH = static_cast<std::uint32_t>(this->pixelHeight);
    if (viewW == 0 || viewH == 0)
    {
        return false;
    }
    if (!this->composed.valid())
    {
        auto target = rd::RenderTarget::create(*this->device, {.size = {viewW, viewH}, .depth = false, .debugName = "composed"});
        if (!target)
        {
            LOG_ERROR(1, "Composed target: ", target.error().message);
            return false;
        }
        this->composed = std::move(*target);
    }
    else if (auto resized = this->composed.resize(viewW, viewH); !resized)
    {
        LOG_ERROR(1, "Composed target: ", resized.error().message);
        return false;
    }
    this->frameEncoder = this->device->createCommandEncoder();
    this->gpuTimers->setEncoder(this->frameEncoder.get()); // les minuteries GPU du Profiler écrivent dans cet encodeur
    return true;
}

void Game::render()
{
    rhi::CommandEncoder &encoder = *this->frameEncoder;
    const auto viewW = static_cast<std::uint32_t>(this->pixelWidth);
    const auto viewH = static_cast<std::uint32_t>(this->pixelHeight);

    this->applySceneParams();
    this->camera.setAspect(viewW, viewH);
    this->sceneRenderer->setOutputResolution(this->pixelWidth, this->pixelHeight);
    const glm::vec4 clear = this->window.getClearColor();
    // Un échec de la scène (pipeline qui ne se lie pas, cible refusée...) rend l'image fausse : elle n'est ni comptée ni capturée.
    auto keep = [&](orosmo::core::Result<> r, const char *step)
    {
        if (!r && !this->sceneError)
        {
            LOG_ERROR(1, "Scene (", step, "): ", r.error().message);
            this->sceneError = r.error();
        }
    };
    // Le graphe de l'image (scène, mise à l'échelle, puis l'interface et la présentation du plugin de la démo) est exécuté par
    // endFrame ; chaque passe est mesurée par le Profiler sous « RenderGraph/<passe> ».
    this->overlay->uiDone = false;
    this->overlay->presented = false;
    keep(this->sceneRenderer->beginFrame(encoder, {.clearColor = {clear.r, clear.g, clear.b, clear.a}}), "beginFrame");
    keep(this->sceneRenderer->setCamera(this->camera), "setCamera");
    Profiler::Profile("RecordScene", [&]() { keep(this->scene->render(*this->sceneRenderer, this->camera, this->uiState.scene), "scene"); });
    if (this->mixed)
    {
        keep(this->mixed->render(*this->sceneRenderer, this->camera, static_cast<float>(this->renderedFrames) / 60.0f), "mixed scene");
    }
    keep(this->sceneRenderer->endFrame({this->composed.color(0), rhi::Format::kRGBA8Unorm, {viewW, viewH}}), "endFrame");

    // Graphe interrompu (scène refusée) : l'interface et la présentation sont encodées quand même, comme avant le graphe.
    if (!this->overlay->uiDone)
    {
        this->uiFailed = !this->renderUi(encoder, this->composed.color(0));
    }
    if (!this->overlay->presented)
    {
        this->present(encoder, this->composed.color(0));
    }
}

// Présentation : l'image composée est copiée (étirée si la vue n'a pas la taille de la fenêtre) sur le backbuffer.
void Game::present(rhi::CommandEncoder &encoder, const rhi::TextureHandle &composedTexture)
{
    (void)this->sceneRenderer->blit().copy(encoder, composedTexture, this->sceneRenderer->blit().linearSampler(), this->backbuffer,
                                           rhi::Format::kRGBA8Unorm, "present");
}

void Game::endFrame()
{
    this->gpuTimers->setEncoder(nullptr);
    const auto result = this->device->submit(std::move(this->frameEncoder));
    if (!result)
    {
        LOG_ERROR(1, "Frame: ", result.error().message);
    }
    // Image valide : soumise ET scène dessinée sans erreur (sinon la capture lirait une cible incomplète).
    const bool submitted = result.has_value() && !this->sceneError && !this->uiFailed;
    this->sceneError.reset();
    this->uiFailed = false;

    if (!submitted)
    {
        if (this->headless)
        {
            this->headlessFrameFailed = true;
        }
        if (!this->captureDirectory.empty())
        {
            this->captureFailed = true;
            this->window.requestClose();
        }
        Profiler::Profile("SwapBuffers", [&]() { (void)this->device->present(); });
        return;
    }

    ++this->renderedFrames;

    if (this->manualCapturePending)
    {
        this->manualCapturePending = false;
        std::error_code ec;
        const std::filesystem::path outDir("capture");
        std::filesystem::create_directories(outDir, ec);
        const auto sceneSaved = this->sceneRenderer->saveColorPng(outDir / "scene.png");
        const auto uiSaved = rd::savePng(*this->device, this->composed.color(0), outDir / "ui.png");
        if (sceneSaved && uiSaved)
        {
            const std::string message = "Saved capture/scene.png and capture/ui.png";
            LOG_INFO(message);
            this->uiState.record(message);
            if (this->fullUi)
            {
                this->fullUi->notify(message, ui::NotificationKind::kSuccess);
            }
        }
        else
        {
            const std::string detail = !sceneSaved ? sceneSaved.error().message : uiSaved.error().message;
            const std::string message = "Capture failed: " + detail;
            LOG_ERROR(1, message);
            this->uiState.record(message);
            if (this->fullUi)
            {
                this->fullUi->notify(message, ui::NotificationKind::kError);
            }
        }
    }

    if (!this->captureDirectory.empty() && !this->captureDone && this->renderedFrames >= this->captureFrames)
    {
        std::error_code ec;
        const std::filesystem::path outDir(this->captureDirectory);
        std::filesystem::create_directories(outDir, ec);
        const auto sceneSaved = this->sceneRenderer->saveColorPng(outDir / "scene.png");
        const auto uiSaved = rd::savePng(*this->device, this->composed.color(0), outDir / "ui.png");
        if (!sceneSaved)
        {
            LOG_ERROR(1, "Capture scene.png: ", sceneSaved.error().message);
        }
        if (!uiSaved)
        {
            LOG_ERROR(1, "Capture ui.png: ", uiSaved.error().message);
        }
        this->captureDone = sceneSaved.has_value() && uiSaved.has_value();
        if (!this->captureDone)
        {
            this->captureFailed = true;
        }
        this->window.requestClose();
    }

    Profiler::Profile("SwapBuffers", [&]() { (void)this->device->present(); });
}
