// Rendu d'une scène 3D : cible de rendu de la scène, caméra et lumières liées par un groupe de liaison par image, dessin de maillages,
// puis fin d'image (post-traitements, historique temporel, mise à l'échelle vers la destination, génération d'images). Remplace
// l'ancien couple `Renderer` / `Renderer3D` avec les mêmes options, sans état global : tout passe par l'encodeur de l'image.
#pragma once

#include "engine/core/error.hpp"
#include "engine/renderer/camera.hpp"
#include "engine/renderer/fullscreen_blit.hpp"
#include "engine/renderer/gpu_buffer.hpp"
#include "engine/renderer/light.hpp"
#include "engine/renderer/mesh.hpp"
#include "engine/renderer/pipeline_cache.hpp"
#include "engine/renderer/render_target.hpp"
#include "engine/renderer/shader_library.hpp"
#include "engine/renderer/upscaling.hpp"
#include "engine/rhi/device.hpp"

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace engine::renderer
{

struct SceneRendererConfig
{
    // Dossier des shaders du moteur : `mesh.vert`, `mesh.frag`, `blit.vert`, `blit.frag` (shaders/rhi/).
    std::filesystem::path shaderDirectory;
};

struct FrameOptions
{
    std::array<float, 4> clearColor{0.0f, 0.0f, 0.0f, 1.0f};
    bool clearDepth = true;
};

class SceneRenderer
{
  public:
    enum class JitterMode : std::uint8_t
    {
        kAuto,   // suite de Halton(2,3) tant qu'un mode temporel est actif
        kManual, // la valeur donnée à setCameraJitter
    };

    static core::Result<std::unique_ptr<SceneRenderer>> create(rhi::Device &device, ShaderLibrary &library, PipelineCache &pipelines,
                                                               const SceneRendererConfig &config);
    ~SceneRenderer();
    SceneRenderer(const SceneRenderer &) = delete;
    SceneRenderer &operator=(const SceneRenderer &) = delete;

    // ── Configuration (mêmes options que l'ancien `Renderer`) ───────────────────────────────────────────────────────
    void setOutputResolution(int width, int height) noexcept;
    int outputWidth() const noexcept { return outputWidth_; }
    int outputHeight() const noexcept { return outputHeight_; }
    void setRenderScale(float scale) noexcept; // dans (0, 1] ; < 1 active la mise à l'échelle
    float renderScale() const noexcept { return renderScale_; }
    void setUpscalingEnabled(bool enabled) noexcept { upscalingEnabled_ = enabled; }
    bool upscalingEnabled() const noexcept { return upscalingEnabled_; }

    void registerUpscaleMode(std::unique_ptr<IUpscaleMode> mode);
    bool setActiveUpscaleMode(const std::string &name); // « disabled » : aucune copie vers la destination
    std::string activeUpscaleMode() const;
    std::vector<std::string> registeredUpscaleModes() const;
    UpscaleRequirements activeUpscaleModeRequirements() const noexcept;
    void addPostProcessPass(std::unique_ptr<IPostProcessPass> pass);
    void removePostProcessPass(const std::string &name);
    void setFrameGenerationMode(std::unique_ptr<IFrameGenerationMode> mode);

    void setMotionVectorsEnabled(bool enabled) noexcept { motionVectorsEnabled_ = enabled; }
    bool motionVectorsEnabled() const noexcept { return motionVectorsEnabled_; }
    void setDepthAsTextureEnabled(bool enabled) noexcept { depthAsTextureEnabled_ = enabled; }
    bool depthAsTextureEnabled() const noexcept { return depthAsTextureEnabled_; }
    void setDeltaTime(float seconds) noexcept { deltaTime_ = seconds; }
    void setJitterMode(JitterMode mode) noexcept { jitterMode_ = mode; }
    JitterMode jitterMode() const noexcept { return jitterMode_; }
    void setCameraJitter(const glm::vec2 &jitter) noexcept
    {
        currentJitter_ = jitter;
        jitterMode_ = JitterMode::kManual;
    }
    const glm::vec2 &cameraJitter() const noexcept { return currentJitter_; }
    void setJitterSequenceLength(std::uint32_t length) noexcept;
    void resetTemporalHistory() noexcept { resetHistoryNextFrame_ = true; }

    // Lumières lues par les shaders ; sans appel, une lumière ambiante nulle.
    void setLights(LightManager *lights) noexcept { lights_ = lights; }

    // ── Image ───────────────────────────────────────────────────────────────────────────────────────────────────────
    // Ouvre la passe de la scène sur la cible interne (taille de rendu = sortie × échelle), vide couleur (et profondeur).
    core::Result<> beginFrame(rhi::CommandEncoder &encoder, const FrameOptions &options = {});
    // Applique le jitter courant à la caméra, envoie ses données au GPU.
    core::Result<> setCamera(Camera &camera);
    core::Result<> drawMesh(Mesh &mesh, const Camera &camera);
    // Referme la passe, exécute post-traitements, historique, mise à l'échelle vers `output`, génération d'images.
    core::Result<> endFrame(const UpscaleTarget &output);

    // Outil de copie plein écran partagé (présentation, mises à l'échelle de l'application).
    FullscreenBlit &blit() noexcept { return *blit_; }
    int frameWidth() const noexcept { return frameWidth_; }
    int frameHeight() const noexcept { return frameHeight_; }
    const RenderTarget &sceneTarget() const noexcept { return sceneTarget_; }
    // Enregistre la couleur 0 de la cible de scène (résolution de rendu, avant mise à l'échelle) ; valable après endFrame.
    core::Result<> saveColorPng(const std::filesystem::path &path) const;

  private:
    SceneRenderer(rhi::Device &device, ShaderLibrary &library, PipelineCache &pipelines) : device_(device), library_(library), pipelines_(pipelines) {}
    bool scaled() const noexcept { return upscalingEnabled_ && renderScale_ < 1.0f && outputWidth_ > 0 && outputHeight_ > 0; }
    bool needsTemporalResources() const noexcept;
    core::Result<> rebuildFrameGroup();
    core::Result<> draw(Mesh &mesh, const Camera &camera, bool lines);

    rhi::Device &device_;
    ShaderLibrary &library_;
    PipelineCache &pipelines_;
    std::unique_ptr<FullscreenBlit> blit_;
    std::shared_ptr<ShaderProgram> meshProgram_;
    GpuBuffer cameraBuffer_;
    LightManager defaultLights_;
    LightManager *lights_ = nullptr;
    rhi::BindGroupHandle frameGroup_;
    rhi::BufferHandle groupCamera_;
    rhi::BufferHandle groupLights_;

    RenderTarget sceneTarget_;
    RenderTarget historyTarget_;
    rhi::CommandEncoder *encoder_ = nullptr;
    bool passOpen_ = false;

    int outputWidth_ = 0;
    int outputHeight_ = 0;
    float renderScale_ = 1.0f;
    bool upscalingEnabled_ = false;
    int frameWidth_ = 0;
    int frameHeight_ = 0;

    std::vector<std::unique_ptr<IUpscaleMode>> upscaleModes_;
    IUpscaleMode *activeUpscaleMode_ = nullptr;
    std::vector<std::unique_ptr<IPostProcessPass>> postProcessPasses_;
    std::unique_ptr<IFrameGenerationMode> frameGenMode_;

    bool motionVectorsEnabled_ = false;
    bool depthAsTextureEnabled_ = false;
    float deltaTime_ = 0.0f;
    glm::vec2 currentJitter_{0.0f};
    bool resetHistoryNextFrame_ = false;
    JitterMode jitterMode_ = JitterMode::kAuto;
    std::uint32_t jitterSequenceLength_ = 16;
    std::uint32_t jitterIndex_ = 0;
};

} // namespace engine::renderer
