// TOOL-01 : visualiseur de terrain. Caméra libre, tuiles générées en tâche de fond (TileStreamer) et dessinées par le
// TerrainRenderer, panneau de réglage écrit dans le langage d'interface (TerrainPanel.cui) avec thème changeable à chaud,
// préréglages, export. Mode sans fenêtre et capture d'image pour la CI.
#pragma once

#include "viewer_state.hpp"

#include <engine/core/file_watcher.hpp>
#include <engine/core/tasks.hpp>
#include <engine/platform/window.hpp>
#include <engine/renderer/camera.hpp>
#include <engine/renderer/canvas_renderer.hpp>
#include <engine/renderer/light.hpp>
#include <engine/renderer/pipeline_cache.hpp>
#include <engine/renderer/render_target.hpp>
#include <engine/renderer/scene_renderer.hpp>
#include <engine/renderer/shader_library.hpp>
#include <engine/renderer/terrain_renderer.hpp>
#include <engine/rhi/device.hpp>
#include <engine/ui/font.hpp>
#include <engine/ui/ui.hpp>
#include <engine/world/streaming.hpp>

#include <engine/ui/text_renderer.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

struct ViewerOptions
{
    std::uint32_t seed = 1;
    std::string preset;               // nom d'un préréglage (vide : paramètres par défaut)
    std::string theme = "dark";
    std::filesystem::path resources;  // dossier contenant fonts/, themes/, shader/rhi/, presets/
    std::filesystem::path captureDirectory;
    std::uint32_t captureFrames = 30;
    bool headless = false;
    int width = 1280;
    int height = 720;
};

class Viewer
{
  public:
    static engine::core::Result<std::unique_ptr<Viewer>> create(const ViewerOptions &options);
    ~Viewer();
    // Boucle principale ; renvoie le code de sortie (0, ou 2 si la capture a échoué).
    int run();

  private:
    explicit Viewer(ViewerOptions options);
    engine::core::Result<> init();
    void frame(float deltaTime);
    engine::ui::InputFrame readInput(float deltaTime);
    void handleRequests();
    void restartStreaming();
    bool syncTiles();
    void moveCamera(float deltaTime);
    bool render();
    void exportHeightmap();
    void loadTheme();

    ViewerOptions options_;
    Window window_;
    std::unique_ptr<engine::rhi::Device> device_;
    std::unique_ptr<engine::renderer::ShaderLibrary> shaders_;
    std::unique_ptr<engine::renderer::PipelineCache> pipelines_;
    std::unique_ptr<engine::renderer::SceneRenderer> scene_;
    std::unique_ptr<engine::renderer::CanvasRenderer> canvas_;
    std::unique_ptr<engine::renderer::TerrainRenderer> terrain_;
    engine::renderer::LightManager lights_;
    engine::renderer::RenderTarget composed_;
    engine::renderer::Camera camera_;

    engine::core::TaskSystem tasks_;
    std::unique_ptr<engine::world::TileStreamer> streamer_;
    std::unordered_map<std::uint64_t, const engine::world::ResidentTile *> shown_; // tuiles envoyées au renderer

    engine::ui::TextRenderer text_;
    std::unique_ptr<engine::ui::TextRendererFont> font_;
    std::unique_ptr<engine::ui::Ui> ui_;
    engine::core::FileWatcher watcher_;
    double watchClock_ = 0.0;

    ViewerState state_;
    ViewerStats stats_;
    double time_ = 0.0;
    glm::ivec2 view_{0};  // taille logique (interface)
    glm::ivec2 pixels_{0}; // taille du tampon d'image
    bool rotating_ = false;
    glm::dvec2 lastCursor_{0.0};
    std::uint32_t renderedFrames_ = 0;
    bool captureFailed_ = false;
    bool captured_ = false;       // capture écrite
    bool selectionShown_ = false; // toute la sélection courante est sur le GPU
};
