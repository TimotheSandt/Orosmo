// TOOL-01 : visualiseur de terrain. Caméra libre, tuiles générées en tâche de fond (TileStreamer) et dessinées par le
// TerrainRenderer, panneau de réglage écrit dans le langage d'interface (TerrainPanel.cui) avec thème changeable à chaud,
// préréglages, export. Mode sans fenêtre et capture d'image pour la CI.
#pragma once

#include "viewer_state.hpp"

#include <orosmo/core/file_watcher.hpp>
#include <orosmo/core/tasks.hpp>
#include <orosmo/platform/window.hpp>
#include <orosmo/renderer/camera.hpp>
#include <orosmo/renderer/canvas_renderer.hpp>
#include <orosmo/renderer/light.hpp>
#include <orosmo/renderer/pipeline_cache.hpp>
#include <orosmo/renderer/render_target.hpp>
#include <orosmo/renderer/scene_renderer.hpp>
#include <orosmo/renderer/shader_library.hpp>
#include <orosmo/renderer/terrain_renderer.hpp>
#include <orosmo/rhi/device.hpp>
#include <orosmo/ui/font.hpp>
#include <orosmo/ui/ui.hpp>
#include <orosmo/world/streaming.hpp>

#include <orosmo/ui/text_renderer.hpp>

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
    static orosmo::core::Result<std::unique_ptr<Viewer>> create(const ViewerOptions &options);
    ~Viewer();
    // Boucle principale ; renvoie le code de sortie (0, ou 2 si la capture a échoué).
    int run();

  private:
    explicit Viewer(ViewerOptions options);
    orosmo::core::Result<> init();
    void frame(float deltaTime);
    orosmo::ui::InputFrame readInput(float deltaTime);
    void handleRequests();
    void restartStreaming();
    bool syncTiles();
    void moveCamera(float deltaTime);
    bool render();
    void exportHeightmap();
    void loadTheme();

    ViewerOptions options_;
    orosmo::platform::Window window_;
    std::unique_ptr<orosmo::rhi::Device> device_;
    std::unique_ptr<orosmo::renderer::ShaderLibrary> shaders_;
    std::unique_ptr<orosmo::renderer::PipelineCache> pipelines_;
    std::unique_ptr<orosmo::renderer::SceneRenderer> scene_;
    std::unique_ptr<orosmo::renderer::CanvasRenderer> canvas_;
    std::unique_ptr<orosmo::renderer::TerrainRenderer> terrain_;
    orosmo::renderer::LightManager lights_;
    orosmo::renderer::RenderTarget composed_;
    orosmo::renderer::Camera camera_;

    orosmo::core::TaskSystem tasks_;
    std::unique_ptr<orosmo::world::TileStreamer> streamer_;
    std::unordered_map<std::uint64_t, const orosmo::world::ResidentTile *> shown_; // tuiles envoyées au renderer

    orosmo::ui::TextRenderer text_;
    std::unique_ptr<orosmo::ui::TextRendererFont> font_;
    std::unique_ptr<orosmo::ui::Ui> ui_;
    orosmo::core::FileWatcher watcher_;
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
