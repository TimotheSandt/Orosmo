#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "CameraController.h"
#include "Scene/demo_scene.hpp"
#include "Scene/mixed_scene.hpp"
#include "UI/demo_ui_state.hpp"
#include "UI/performance_stats.hpp"
#include "launch_options.hpp"

#include <orosmo/core/error.hpp>
#include <orosmo/core/file_watcher.hpp>
#include <orosmo/core/profiler.hpp>
#include <orosmo/platform/input_manager.hpp>
#include <orosmo/platform/window.hpp>
#include <orosmo/renderer/camera.hpp>
#include <orosmo/renderer/canvas_renderer.hpp>
#include <orosmo/renderer/gpu_timer.hpp>
#include <orosmo/renderer/pipeline_cache.hpp>
#include <orosmo/renderer/post_effects.hpp>
#include <orosmo/renderer/render_target.hpp>
#include <orosmo/renderer/scene_renderer.hpp>
#include <orosmo/renderer/shader_library.hpp>
#include <orosmo/rhi/device.hpp>
#include <orosmo/ui/font.hpp>
#include <orosmo/ui/text_renderer.hpp>
#include <orosmo/ui/ui.hpp>

class Game
{
  public:
    // `headless` (PLAT-04) : aucune fenêtre ; périphérique hors écran, pas de temps fixe, pas d'entrées. Les échecs normaux
    // (pas d'affichage, pas d'OpenGL 4.3, shaders absents) reviennent en `Result`, pas en exception.
    static orosmo::core::Result<std::unique_ptr<Game>> create(bool headless = false, int width = 1280, int height = 720,
                                                              orosmo::rhi::Backend backend = orosmo::rhi::Backend::kOpenGL);
    ~Game();
    Game(const Game &) = delete;
    Game &operator=(const Game &) = delete;

    orosmo::core::Result<> init();
    void stop();

    void run();

    // TOOL-03: after `frames` rendered frames, save <directory>/scene.png (3D) and ui.png, then close.
    void enableCapture(std::string directory, std::uint32_t frames);
    // --instances N (REN-28) : N objets instanciés ajoutés à la scène par init() ; 0 (défaut) : aucun.
    void setInstanceCount(std::uint32_t count) noexcept { this->instanceCount = count; }
    void setGpuDriven(bool enabled) noexcept { this->gpuDriven = enabled; }
    // --shadows on (REN-06 à 08) : ombres en cascade avec cache (plugin ShadowFeature) ; désactivées par défaut.
    void setShadowsEnabled(bool enabled) noexcept { this->shadowsEnabled = enabled; }
    // --post (REN-17/18) : effets activés par init() ; kNone (défaut) : aucun effet enregistré, scène LDR inchangée.
    void setPostMode(demo::PostMode mode) noexcept { postMode = mode; }
    // --lighting / --lights (REN-04) : mode d'éclairage demandé (forward+ : repli sur forward sans compute) et lumières ponctuelles.
    void setLighting(demo::LightingMode mode, std::uint32_t pointLights) noexcept
    {
        this->lightingMode = mode;
        this->pointLightCount = pointLights;
    }
    // --scene mixed (R2D-05, REN-32) : transparents triés et 2D dans le monde en plus de la scène ; défaut : inchangée.
    void setMixedScene(bool enabled) noexcept { mixedScene = enabled; }
    void setUiMode(demo::UiMode mode) noexcept { uiMode = mode; }
    void setInitialTheme(std::string theme) { initialTheme = std::move(theme); }
    void setInitialTab(std::string tab) { initialTab = std::move(tab); }
    void setCaptureInteraction(std::string hover, std::string click)
    {
        hoverId = std::move(hover);
        clickId = std::move(click);
    }
    void setVSync(bool enabled)
    {
        if (device)
        {
            device->setVSync(enabled);
        }
    }
    // Faux si une capture était demandée et n'a pas été faite, ou si une image de la capture a été refusée par le périphérique.
    bool captureSucceeded() const noexcept { return !captureFailed && (captureDirectory.empty() || captureDone); }
    // Sans fenêtre : faux si une image n'a pas pu être commencée ou a été refusée (submit ou scène), avec ou sans capture.
    bool headlessSucceeded() const noexcept { return !headlessFrameFailed; }

  private:
    Game(bool headless, int width, int height, orosmo::rhi::Backend backend);
    orosmo::core::Result<> createGraphics();
    void processInput(bool toggleUiJustPressed);
    void update();
    bool beginFrame();
    void render();
    void endFrame();
    void refreshViewSize();
    void applySceneParams();
    void resetCamera();
    orosmo::core::Result<> createUi();
    orosmo::core::Result<> createPostEffects();
    bool loadFullUiTheme(bool reportAsError);
    bool renderUi(orosmo::rhi::CommandEncoder &encoder, const orosmo::rhi::TextureHandle &composedTexture);
    void present(orosmo::rhi::CommandEncoder &encoder, const orosmo::rhi::TextureHandle &composedTexture);
    class OverlayFeature; // plugin de rendu : passes « ui » et « present » du graphe de l'image (Game.cpp)

  private:
    orosmo::platform::Window window;
    // Graphique (RHI) : le périphérique d'abord (détruit en dernier) ; tout le reste le référence.
    std::unique_ptr<orosmo::rhi::Device> device;
    std::unique_ptr<orosmo::renderer::ShaderLibrary> shaders;
    std::unique_ptr<orosmo::renderer::PipelineCache> pipelines;
    std::unique_ptr<orosmo::renderer::SceneRenderer> sceneRenderer;
    std::unique_ptr<orosmo::renderer::CanvasRenderer> canvas;
    OverlayFeature *overlay = nullptr; // possédé par sceneRenderer
    std::unique_ptr<orosmo::renderer::ProfilerGpuTimerFactory> gpuTimers;
    orosmo::renderer::RenderTarget composed; // image finale (scène puis interface), à la taille de la vue
    orosmo::renderer::RenderTarget uiTarget; // interface à échelle réduite (touches de debug 5 à 8)
    std::unique_ptr<orosmo::rhi::CommandEncoder> frameEncoder;
    orosmo::rhi::TextureHandle backbuffer;
    float uiRenderScale = 1.0f;
    bool uiUpscaling = false;

    // Interface (orosmo::ui) : ordre de déclaration critique !
    // `performance`, `uiState`, `watcher`, `text` et `font` DOIVENT survivre à `hudUi` et `fullUi` (destruction inverse).
    PerformanceStats performance;
    DemoUiState uiState;
    std::unique_ptr<orosmo::core::FileWatcher> watcher;
    std::unique_ptr<orosmo::ui::TextRenderer> text;
    std::unique_ptr<orosmo::ui::TextRendererFont> font;
    std::unique_ptr<orosmo::ui::Ui> hudUi;
    std::unique_ptr<orosmo::ui::Ui> fullUi;

    orosmo::renderer::Camera camera;
    CameraController cameraController;
    std::unique_ptr<demo::DemoScene> scene;
    std::unique_ptr<demo::MixedScene> mixed;
    bool mixedScene = false;
    bool stopped = false;

    int requestedWidth = 1280;
    int requestedHeight = 720;
    int viewWidth = 0;
    int viewHeight = 0;
    int pixelWidth = 0; // = viewWidth sauf écran à échelle (DPI) : taille réelle du tampon d'image
    int pixelHeight = 0;
    orosmo::rhi::Backend selectedBackend = orosmo::rhi::Backend::kOpenGL;
    demo::UiMode uiMode = demo::UiMode::kHud;
    std::string initialTheme = "dark";
    std::string initialTab = "scene";
    std::uint32_t instanceCount = 0;
    bool gpuDriven = false;
    bool shadowsEnabled = false;
    demo::PostMode postMode = demo::PostMode::kNone;
    orosmo::renderer::PostEffects post; // effets possédés par sceneRenderer
    demo::LightingMode lightingMode = demo::LightingMode::kForward;
    std::uint32_t pointLightCount = 0;
    std::string hoverId;
    std::string clickId;
    std::string captureDirectory;
    std::uint32_t captureFrames = 0;
    std::uint32_t renderedFrames = 0;
    double watchClock = 0.0;
    bool captureDone = false;
    bool manualCapturePending = false;
    bool headlessFrameFailed = false;
    bool captureFailed = false;
    std::optional<orosmo::core::Error> sceneError;
    bool uiFailed = false;
    bool headless = false;
};
