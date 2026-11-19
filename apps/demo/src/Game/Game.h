#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "CameraController.h"
#include <engine/platform/input_manager.hpp>
#include "Profiler.h"
#include "UIManager.h"
#include <engine/platform/window.hpp>
#include "World.h"

#include "engine/renderer/camera.hpp"
#include "engine/renderer/canvas_renderer.hpp"
#include "engine/renderer/gpu_timer.hpp"
#include "engine/renderer/pipeline_cache.hpp"
#include "engine/renderer/render_target.hpp"
#include "engine/renderer/scene_renderer.hpp"
#include "engine/renderer/shader_library.hpp"
#include "engine/rhi/device.hpp"

class Game
{
  public:
    // `headless` (PLAT-04) : aucune fenêtre ; périphérique hors écran 1280x720, pas de temps fixe, pas d'entrées. Les échecs
    // normaux (pas d'affichage, pas d'OpenGL 4.3, shaders absents) reviennent en `Result`, pas en exception (CLAUDE.md règle 10).
    static engine::core::Result<std::unique_ptr<Game>> create(bool headless = false);
    ~Game();
    Game(const Game &) = delete;
    Game &operator=(const Game &) = delete;

    engine::core::Result<> init();
    void stop();

    void run();

    // TOOL-03: after `frames` rendered frames, save <directory>/scene.png (3D) and ui.png, then close.
    void enableCapture(std::string directory, std::uint32_t frames);
    void setSeed(std::uint32_t seed) { terrainSeed = seed; }
    void setVSync(bool enabled) { if (device) { device->setVSync(enabled); } }
    // Faux si une capture était demandée et n'a pas été faite, ou si une image de la capture a été refusée par le périphérique.
    bool captureSucceeded() const noexcept { return !captureFailed && (captureDirectory.empty() || captureDone); }

  private:
    explicit Game(bool headless);
    engine::core::Result<> createGraphics();
    void processInput();
    void update();
    bool beginFrame();
    void render();
    void endFrame();
    void refreshViewSize();
    bool renderUi(engine::rhi::CommandEncoder &encoder, const engine::rhi::TextureHandle &composed);

  private:
    Window window;
    // Graphique (RHI) : le périphérique d'abord (détruit en dernier) ; tout le reste le référence.
    std::unique_ptr<engine::rhi::Device> device;
    std::unique_ptr<engine::renderer::ShaderLibrary> shaders;
    std::unique_ptr<engine::renderer::PipelineCache> pipelines;
    std::unique_ptr<engine::renderer::SceneRenderer> sceneRenderer;
    std::unique_ptr<engine::renderer::CanvasRenderer> canvas;
    std::unique_ptr<engine::renderer::ProfilerGpuTimerFactory> gpuTimers;
    engine::renderer::RenderTarget composed;   // l'image finale (scène puis interface), à la taille de la vue
    engine::renderer::RenderTarget uiTarget;   // interface à échelle réduite (touches de debug 5 à 8)
    std::unique_ptr<engine::rhi::CommandEncoder> frameEncoder;
    engine::rhi::TextureHandle backbuffer;
    float uiRenderScale = 1.0f;
    bool uiUpscaling = false;

    engine::renderer::Camera camera;
    CameraController cameraController;
    std::unique_ptr<World> world = nullptr;
    bool stopped = false;

    std::uint32_t terrainSeed = 1u; // set by main(): never derived from the clock
    int viewWidth = 0;
    int viewHeight = 0;
    int pixelWidth = 0;  // = viewWidth sauf écran à échelle (DPI) : taille réelle du tampon d'image
    int pixelHeight = 0;
    std::string captureDirectory;
    std::uint32_t captureFrames = 0;
    std::uint32_t renderedFrames = 0;
    bool captureDone = false;
    bool captureFailed = false; // une image de la capture a été refusée (submit) : la capture ne serait pas fiable
    bool headless = false;
};
