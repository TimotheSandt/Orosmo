// Rendu d'une scène 3D : cible de rendu de la scène, caméra, lumières et données par image (REN-03) liées par le groupe 0, dessin de
// maillages, puis fin d'image (post-traitements, historique temporel, mise à l'échelle vers la destination, génération d'images,
// plugins). Depuis M5 (REN-01), tout passe par un graphe de passes (`render_graph.hpp`) construit et exécuté par `endFrame` :
// `drawMesh` / `drawDebug` ENREGISTRENT des dessins, joués par la passe « scene » ; les plugins (`RenderFeature`) y ajoutent leurs
// passes. Remplace l'ancien couple `Renderer` / `Renderer3D` avec les mêmes options, sans état global.
#pragma once

#include "orosmo/core/error.hpp"
#include "orosmo/renderer/aov.hpp"
#include "orosmo/renderer/camera.hpp"
#include "orosmo/renderer/debug_view.hpp"
#include "orosmo/renderer/debug_draw.hpp"
#include "orosmo/renderer/frame_data.hpp"
#include "orosmo/renderer/fullscreen_blit.hpp"
#include "orosmo/renderer/gpu_buffer.hpp"
#include "orosmo/renderer/light.hpp"
#include "orosmo/renderer/mesh.hpp"
#include "orosmo/renderer/pipeline_cache.hpp"
#include "orosmo/renderer/render_feature.hpp"
#include "orosmo/renderer/render_graph.hpp"
#include "orosmo/renderer/render_target.hpp"
#include "orosmo/renderer/shader_library.hpp"
#include "orosmo/renderer/upscaling.hpp"
#include "orosmo/rhi/device.hpp"

#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace orosmo::renderer
{

struct SceneRendererConfig
{
    // Dossier des shaders du moteur : `mesh.vert`, `mesh.frag`, `blit.vert`, `blit.frag` (shaders/rhi/).
    std::filesystem::path shaderDirectory;
};

// Dessin externe joué dans la passe « scene » (REN-28, `SceneRenderer::drawCustom`) : la cible et l'origine du rendu relatif à la
// caméra (à retrancher des positions monde ; 0 sinon). Le rappel reçoit l'encodeur de la passe ouverte et le groupe 0 (caméra,
// lumières, FrameData), à lier après son pipeline.
struct SceneDrawInfo
{
    std::vector<rhi::Format> colorFormats;
    rhi::Format depthFormat = rhi::Format::kD32Float;
    glm::vec3 cameraOrigin{0.0f};
};
using SceneDrawCallback = std::function<core::Result<>(rhi::CommandEncoder &encoder, rhi::BindGroupHandle frameGroup)>;

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
    // Mise à l'échelle désactivée : le mode actif n'est pas exécuté, la scène est copiée 1:1 sur la destination.
    void setUpscalingEnabled(bool enabled) noexcept { upscalingEnabled_ = enabled; }
    bool upscalingEnabled() const noexcept { return upscalingEnabled_; }

    void registerUpscaleMode(std::unique_ptr<IUpscaleMode> mode); // les modes enregistrés vivent autant que le renderer
    // Active un mode enregistré : il doit être supporté (`supports`) et s'initialiser (`initialize`), sinon faux et l'ancien mode
    // reste actif. L'ancien mode est arrêté (`shutdown`). « disabled » : aucun mode ; à échelle réduite la destination n'est alors
    // pas touchée, à l'échelle 1 la scène y est copiée 1:1.
    bool setActiveUpscaleMode(const std::string &name);
    std::string activeUpscaleMode() const;
    std::vector<std::string> registeredUpscaleModes() const;
    UpscaleRequirements activeUpscaleModeRequirements() const noexcept;
    void addPostProcessPass(std::unique_ptr<IPostProcessPass> pass);
    void removePostProcessPass(const std::string &name);
    // Remplace le mode de génération d'images (nul : aucun). Un mode non supporté ou qui ne s'initialise pas est refusé
    // (kUnsupported) et le mode actif est conservé.
    core::Result<> setFrameGenerationMode(std::unique_ptr<IFrameGenerationMode> mode);
    // Image produite par le mode de génération pendant le dernier endFrame (nulle sinon), valable jusqu'au prochain endFrame.
    rhi::TextureHandle generatedFrame() const noexcept { return generatedFrame_; }

    void setMotionVectorsEnabled(bool enabled) noexcept { motionVectorsEnabled_ = enabled; }
    bool motionVectorsEnabled() const noexcept { return motionVectorsEnabled_; }
    void setDepthAsTextureEnabled(bool enabled) noexcept { depthAsTextureEnabled_ = enabled; }
    // REN-17 : format de la couleur de la scène, appliqué au prochain `beginFrame`. kRGBA8Unorm (défaut, LDR) ; kRGBA16Float ou
    // kRGBA32Float pour une scène HDR (post-traitements HDR, `post_effects.hpp`). Un autre format est refusé (kInvalidArgument).
    core::Result<> setSceneColorFormat(rhi::Format format);
    rhi::Format sceneColorFormat() const noexcept { return sceneColorFormat_; }
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

    // REN-02 : rendu relatif à la caméra. La caméra est à l'origine de la matrice envoyée et chaque maillage est placé par rapport
    // à elle (translation - position de la caméra) : la précision ne dépend plus de la distance au centre du monde.
    void setCameraRelative(bool enabled) noexcept { cameraRelative_ = enabled; }
    bool cameraRelative() const noexcept { return cameraRelative_; }

    // Lumières lues par les shaders ; sans appel, une lumière ambiante nulle. Peut être appelé après beginFrame : chaque dessin
    // enregistré utilise les lumières choisies au moment de son `drawMesh`.
    void setLights(LightManager *lights) noexcept { lights_ = lights; }

    // REN-04 : pré-passe de profondeur (docs/03 §9 : « pré-passe de profondeur -> clusters -> passe principale »). Activée, la passe
    // « depth-prepass » joue les maillages (`drawMesh`, hors fil de fer) sans écrire la couleur, puis la passe « scene » garde cette
    // profondeur (test « plus grand ou égal », sans écriture) : chaque pixel n'est éclairé qu'une fois. Les dessins externes
    // (`drawCustom`) et les lignes de debug ne sont pas dans la pré-passe (ils testent et écrivent la profondeur comme avant). Prise
    // en compte au `beginFrame` suivant. Désactivée par défaut (image et coût inchangés).
    void setDepthPrepass(bool enabled) noexcept { depthPrepass_ = enabled; }
    bool depthPrepass() const noexcept { return depthPrepass_; }

    // ── Plugins de rendu (REN-01) ───────────────────────────────────────────────────────────────────────────────────
    // Enregistre un plugin : ses passes sont ajoutées au graphe à chaque `endFrame`, dans l'ordre d'enregistrement. Refusé
    // (kUnsupported) si `supports` est faux, (kAlreadyExists) si un plugin du même nom existe. Renvoie le plugin (non possédé).
    core::Result<RenderFeature *> addFeature(std::unique_ptr<RenderFeature> feature);
    void removeFeature(const std::string &name);
    std::vector<std::string> featureNames() const;
    // Graphe de la dernière image (ordre d'exécution, passes écartées, ressources transitoires).
    const RenderGraph &renderGraph() const noexcept { return graph_; }

    // ── Sorties auxiliaires (REN-21, REN-41) ────────────────────────────────────────────────────────────────────────
    // Demande un AOV : la cible de scène en reçoit une cible de plus à la prochaine image (kUnsupported si le périphérique n'a pas
    // assez d'attachements couleur). Sans demande (ni plugin qui en exige via `RenderFeature::requiredAovs`, ni vecteurs de
    // mouvement, ni mode de debug), la cible de scène n'a que sa couleur : aucune cible, aucun coût de plus.
    core::Result<> declareAov(Aov aov);
    void releaseAov(Aov aov) noexcept { aovDeclared_.remove(aov); }
    // Ensemble effectif : déclarés, exigés par les plugins, mouvement si `setMotionVectorsEnabled`.
    AovSet activeAovs() const;
    // Macros à donner à ShaderLibrary::loadGraphics pour un programme de maillage qui écrit les AOV actifs (à recharger si
    // l'ensemble change) ; vide sans AOV.
    std::vector<std::pair<std::string, std::string>> aovShaderDefines() const { return orosmo::renderer::aovShaderDefines(activeAovs()); }
    // Attachements de la cible de scène de la dernière image commencée.
    const AovLayout &aovLayout() const noexcept { return aovLayout_; }
    // Mode d'affichage de debug (REN-35) ; kNone (défaut) : aucun effet, aucune allocation. kNormals et kMotion demandent leur AOV
    // (le programme de maillage doit être compilé avec `aovShaderDefines()` APRÈS cet appel) ; kAlbedo, kOverdraw et kWireframe
    // remplacent le programme des maillages par un programme de debug ; kLod n'a d'effet que via `VisibilityOptions::lodColors`.
    core::Result<> setDebugView(DebugView view);
    DebugView debugView() const noexcept { return debugView_; }

    // ── Données par image (REN-03) ──────────────────────────────────────────────────────────────────────────────────
    void setFrameEnvironment(const FrameEnvironment &environment) noexcept { environment_ = environment; }
    const FrameEnvironment &frameEnvironment() const noexcept { return environment_; }
    // Contenu envoyé au GPU par le dernier `endFrame` (groupe 0, binding kFrameDataBinding).
    const FrameData &frameData() const noexcept { return frameData_; }
    std::uint64_t frameIndex() const noexcept { return frameIndex_; }
    // Nombre de `beginFrame` réussis : identifie l'image ouverte, même quand l'image précédente a échoué dans `endFrame` (avant que
    // frameIndex n'avance).
    std::uint64_t frameSerial() const noexcept { return frameSerial_; }

    // ── Image ───────────────────────────────────────────────────────────────────────────────────────────────────────
    // Prépare l'image : cible de la scène (taille de rendu = sortie × échelle), jitter. `encoder` reçoit toutes les commandes de
    // l'image, enregistrées par `endFrame` ; il doit vivre jusque-là.
    core::Result<> beginFrame(rhi::CommandEncoder &encoder, const FrameOptions &options = {});
    // Applique le jitter courant à la caméra, envoie ses données au GPU.
    core::Result<> setCamera(Camera &camera);
    // Enregistre le dessin (pipeline, constantes et transformation sont résolus ici ; les tampons du maillage doivent rester
    // valides jusqu'à `endFrame`).
    core::Result<> drawMesh(Mesh &mesh, const Camera &camera);
    // REN-34 : dessine les segments de `debug` dans la passe de scène (après setCamera). Sans segment ou désactivé : rien (aucun
    // coût). Les shaders `debug_line.vert/frag` sont chargés au premier appel. Les segments sont copiés à l'appel.
    core::Result<> drawDebug(const DebugDraw &debug);
    // REN-28 : dessin externe (InstanceRenderer), enregistré à sa place parmi les dessins et joué dans la passe « scene ». Après
    // setCamera ; `info` donne les formats de la cible (pipelines) et l'origine du rendu relatif à la caméra.
    core::Result<SceneDrawInfo> sceneDrawInfo() const;
    core::Result<> drawCustom(SceneDrawCallback draw);
    // Construit et exécute le graphe de l'image : passe de scène (dessins enregistrés), post-traitements, mise à l'échelle vers
    // `output`, génération d'images, historique, puis les passes des plugins. Chaque passe est mesurée par le Profiler sous
    // « RenderGraph/<passe> » (scene, post.<nom>, upscale, frame-generation, history).
    core::Result<> endFrame(const UpscaleTarget &output);

    // Outil de copie plein écran partagé (présentation, mises à l'échelle de l'application).
    FullscreenBlit &blit() noexcept { return *blit_; }
    int frameWidth() const noexcept { return frameWidth_; }
    int frameHeight() const noexcept { return frameHeight_; }
    const RenderTarget &sceneTarget() const noexcept { return sceneTarget_; }
    // Enregistre la couleur 0 de la cible de scène (résolution de rendu, avant mise à l'échelle) ; valable après endFrame.
    core::Result<> saveColorPng(const std::filesystem::path &path) const;

  private:
    // Dessin enregistré par drawMesh / drawDebug, joué par la passe « scene ».
    struct SceneDraw
    {
        rhi::PipelineHandle pipeline;
        rhi::PipelineHandle prepassPipeline; // pré-passe de profondeur (nul : absent de la pré-passe)
        std::vector<std::byte> pushConstants;
        LightManager *lights = nullptr;
        rhi::BindGroupHandle textureGroup;
        rhi::BindGroupHandle modelGroup;
        rhi::BufferHandle vertexBuffer;
        rhi::BufferHandle instanceBuffer;
        rhi::BufferHandle indexBuffer;
        std::uint32_t count = 0; // index (maillage) ou sommets (lignes de debug)
        std::uint32_t instanceCount = 1;
        std::uint64_t vertexOffset = 0; // octets, dans le tampon des lignes de debug
        bool debugLines = false;
        SceneDrawCallback custom; // dessin externe (drawCustom) : tout le reste est ignoré
    };

    SceneRenderer(rhi::Device &device, ShaderLibrary &library, PipelineCache &pipelines)
        : device_(device), library_(library), pipelines_(pipelines), graph_(device)
    {
    }
    bool scaled() const noexcept { return upscalingEnabled_ && renderScale_ < 1.0f && outputWidth_ > 0 && outputHeight_ > 0; }
    bool modeRuns() const noexcept { return upscalingEnabled_ && activeUpscaleMode_ != nullptr; }
    bool needsJitter() const noexcept;
    bool needsHistory() const noexcept;
    LightManager &activeLights() noexcept { return lights_ != nullptr ? *lights_ : defaultLights_; }
    core::Result<> rebuildFrameGroup(LightManager &lights);
    core::Result<> record(Mesh &mesh, bool lines);
    core::Result<> playDraws(rhi::CommandEncoder &encoder, bool prepass = false);
    core::Result<> uploadDebugLines();
    core::Result<> updateFrameData();
    core::Result<> ensureHistory(RenderTarget &target, bool &fresh);
    core::Result<> buildGraph(const UpscaleTarget &output);
    // Mélange et profondeur du matériau (REN-41) puis effets du mode de debug, sur les cibles déjà préparées.
    void configureMaterialState(GraphicsState &state, BlendMode blend, bool lines) const;
    bool replacesMeshProgram() const noexcept
    {
        return debugView_ == DebugView::kAlbedo || debugView_ == DebugView::kOverdraw || debugView_ == DebugView::kWireframe;
    }

    rhi::Device &device_;
    ShaderLibrary &library_;
    PipelineCache &pipelines_;
    std::unique_ptr<FullscreenBlit> blit_;
    std::shared_ptr<ShaderProgram> meshProgram_;
    std::shared_ptr<ShaderProgram> debugProgram_;
    GpuBuffer debugBuffer_;
    std::vector<float> debugVertices_; // segments de debug de l'image (envoyés une fois, à endFrame)
    std::filesystem::path shaderDirectory_;
    GpuBuffer cameraBuffer_;
    GpuBuffer frameBuffer_; // FrameData (REN-03)
    LightManager defaultLights_;
    LightManager *lights_ = nullptr;
    rhi::BindGroupHandle frameGroup_;
    rhi::BufferHandle groupCamera_;
    rhi::BufferHandle groupLights_;
    // Liaisons du groupe 0 ajoutées par les plugins (RenderGraphBuilder::addFrameBinding) pour cette image, et celles du groupe actuel.
    std::vector<rhi::BindGroupEntry> featureFrameBindings_;
    std::vector<rhi::BindGroupEntry> groupFeatureBindings_;
    FrameResources frameResources_;

    RenderGraph graph_;
    std::vector<std::unique_ptr<RenderFeature>> features_;
    std::vector<SceneDraw> draws_;
    FrameOptions frameOptions_;

    RenderTarget sceneTarget_;
    // Historique en double tampon : on lit l'image précédente dans l'un pendant que l'image courante est copiée dans l'autre (le
    // graphe n'a ainsi aucune écriture à ordonner après les lecteurs).
    std::array<RenderTarget, 2> history_;
    std::uint32_t lastHistory_ = 0; // tampon écrit par la dernière image qui a réussi sa copie (si historyValid_)
    rhi::CommandEncoder *encoder_ = nullptr;
    bool frameOpen_ = false;

    FrameData frameData_;
    FrameEnvironment environment_;
    glm::mat4 cameraView_{1.0f};
    glm::mat4 cameraProjection_{1.0f};
    glm::dvec3 cameraWorld_{0.0};
    glm::mat4 previousViewProj_{1.0f};
    glm::vec2 previousJitter_{0.0f};
    bool hasPreviousFrame_ = false;
    double timeSeconds_ = 0.0;
    std::uint64_t frameIndex_ = 0;
    std::uint64_t frameSerial_ = 0;

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
    rhi::TextureHandle generatedFrame_;
    bool historyValid_ = false; // history_[lastHistory_] contient l'image précédente (écrite avec succès à l'image précédente)

    // AOV (REN-21)
    AovSet aovDeclared_;
    AovSet aovActive_;     // ensemble de la cible de scène courante (fixé par beginFrame)
    AovLayout aovLayout_;
    DebugView debugView_ = DebugView::kNone;
    std::shared_ptr<ShaderProgram> debugViewProgram_;
    glm::vec3 previousCameraOrigin_{0.0f};

    bool motionVectorsEnabled_ = false;
    bool depthPrepass_ = false;
    bool prepassThisFrame_ = false; // depthPrepass_ lu au beginFrame
    bool cameraRelative_ = false;
    glm::vec3 cameraOrigin_{0.0f};
    bool depthAsTextureEnabled_ = false;
    rhi::Format sceneColorFormat_ = rhi::Format::kRGBA8Unorm;
    float deltaTime_ = 0.0f;
    glm::vec2 currentJitter_{0.0f};
    bool resetHistoryNextFrame_ = false;
    JitterMode jitterMode_ = JitterMode::kAuto;
    std::uint32_t jitterSequenceLength_ = 16;
    std::uint32_t jitterIndex_ = 0;
};

} // namespace orosmo::renderer
