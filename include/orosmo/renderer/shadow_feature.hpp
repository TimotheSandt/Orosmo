// REN-06 à REN-08 : plugin d'ombres (RenderFeature). Ajoute au graphe de l'image, AVANT la passe « scene » (addSceneInput), les
// passes qui tiennent à jour un atlas d'ombres persistant : cascades de la première lumière directionnelle ombrée (REN-07) et une
// vue par projecteur ombré (REN-08), tuiles allouées par priorité dans l'atlas, budget de mises à jour par image, mises à jour des
// cascades lointaines amorties, fondu à distance, et CACHE : en mode Cached, la géométrie fixe d'une vue est dessinée une fois dans
// un atlas de cache, puis recopiée sous les casters mobiles seulement quand quelque chose a bougé dans la vue ; rien n'est redessiné
// quand rien ne bouge. Les casters sont des maillages (Mesh) ou les objets instanciés d'un ObjectSet (culling par vue d'ombre,
// un drawIndexed instancié par lot). Les matériaux récepteurs lisent l'atlas par le groupe 1 (bindings 6 et 7) avec
// `shaders/rhi/shadow_receiver.glsl` (mesh.frag compilé avec OROSMO_SHADOWS). Ni le graphe, ni SceneRenderer, ni les backends ne
// changent pour ce plugin.
#pragma once

#include "orosmo/core/error.hpp"
#include "orosmo/renderer/render_context.hpp"
#include "orosmo/renderer/culling.hpp"
#include "orosmo/renderer/gpu_buffer.hpp"
#include "orosmo/renderer/instance_batch.hpp"
#include "orosmo/renderer/light.hpp"
#include "orosmo/renderer/pipeline_cache.hpp"
#include "orosmo/renderer/render_feature.hpp"
#include "orosmo/renderer/shader_library.hpp"
#include "orosmo/renderer/shadows.hpp"
#include "orosmo/renderer/transparent_shadow.hpp"
#include "orosmo/rhi/device.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace orosmo::renderer
{

class Mesh;
class ObjectSet;
class InstanceRenderer;

inline constexpr std::uint32_t kMaxShadowCascades = 4;
inline constexpr std::uint32_t kMaxSpotShadows = 8;
// Liaisons du groupe 1 (matériau) lues par shadow_receiver.glsl.
inline constexpr std::uint32_t kShadowAtlasBinding = 6;
inline constexpr std::uint32_t kShadowDataBinding = 7;

// Bloc std140 lu par shaders/rhi/shadow_receiver.glsl (même ordre, mêmes tailles).
struct alignas(16) ShadowGpuData
{
    glm::mat4 cascadeMatrix[kMaxShadowCascades]; // repère des shaders -> (u, v, profondeur) de l'atlas
    glm::vec4 cascadeRect[kMaxShadowCascades];   // u0, v0, u1, v1
    glm::vec4 cascadeParams[kMaxShadowCascades]; // texel (m), 1 / profondeur couverte (m), dessinée (0/1), 0
    glm::vec4 directional{-1.0f, 0.0f, 0.0f, 0.0f}; // index de la lumière (-1 : aucune), cascades, début et fin du fondu (m)
    glm::vec4 filter{0.0f};                       // rayon PCF, biais constant, biais de pente, décalage normal (texels)
    glm::vec4 atlas{0.0f};                        // 1 / taille, taille, projecteurs, 0
    glm::mat4 spotMatrix[kMaxSpotShadows];        // repère des shaders -> clip (u, v après division par w)
    glm::vec4 spotRect[kMaxSpotShadows];
    glm::vec4 spotParams[kMaxSpotShadows];        // index de la lumière, proche, lointain, texel à 1 m
    glm::vec4 spotPosition[kMaxSpotShadows];      // position (repère des shaders), début du fondu
    glm::vec4 spotDirection[kMaxSpotShadows];     // direction, fin du fondu
};
static_assert(std::is_standard_layout_v<ShadowGpuData> && std::is_trivially_copyable_v<ShadowGpuData>);
static_assert(offsetof(ShadowGpuData, cascadeRect) == 256 && offsetof(ShadowGpuData, cascadeParams) == 320);
static_assert(offsetof(ShadowGpuData, directional) == 384 && offsetof(ShadowGpuData, filter) == 400 && offsetof(ShadowGpuData, atlas) == 416);
static_assert(offsetof(ShadowGpuData, spotMatrix) == 432 && offsetof(ShadowGpuData, spotRect) == 944 && offsetof(ShadowGpuData, spotParams) == 1072);
static_assert(offsetof(ShadowGpuData, spotPosition) == 1200 && offsetof(ShadowGpuData, spotDirection) == 1328 && sizeof(ShadowGpuData) == 1456);

struct ShadowSettings
{
    std::uint32_t atlasSize = 4096;       // côté de l'atlas (puissance de deux) ; le cache a la même taille
    std::uint32_t minTileSize = 64;
    std::uint32_t cascadeCount = 4;       // 1 à kMaxShadowCascades
    std::uint32_t cascadeResolution = 1024;
    float maxDistance = 80.0f;            // portée des ombres directionnelles depuis la caméra (m)
    float splitLambda = 0.75f;            // cascadeSplits
    float fadeFraction = 0.1f;            // fondu sur la dernière fraction de maxDistance
    float depthExtension = 60.0f;         // casters vers la lumière hors de la sphère d'une cascade (m)
    // Amortissement : une cascade n'est mise à jour qu'une image sur N (N = 1 : à chaque image où elle doit l'être).
    std::array<std::uint32_t, kMaxShadowCascades> cascadeUpdateInterval{1, 1, 2, 4};
    std::uint32_t maxViewUpdatesPerFrame = 8; // budget : vues (tuiles) redessinées au plus par image, par priorité
    int pcfRadius = 1;                    // PCF (2r + 1)^2
    float constantBias = 1.0f;            // en texels de la vue
    float slopeBias = 1.5f;               // en texels, × tan(angle)
    float normalOffset = 1.0f;            // décalage du récepteur le long de la normale, en texels
    std::uint32_t spotResolution = 1024;  // résolution d'un projecteur proche (divisée par deux à chaque doublement de distance)
    std::uint32_t maxSpotShadows = kMaxSpotShadows;
};

// Réglages d'une lumière (par index dans le LightManager).
struct LightShadowParams
{
    int priority = 0;               // plus grand : servi d'abord (atlas et budget)
    std::uint32_t maxResolution = 0; // 0 : résolution par défaut (cascadeResolution ou spotResolution)
    float spotNear = 0.1f;          // projecteur : plans proche et lointain (m)
    float spotRange = 30.0f;
    float fadeStart = 40.0f;        // projecteur : fondu de l'ombre selon la distance caméra-lumière (m)
    float fadeEnd = 50.0f;          // au-delà : aucune vue (statut kFaded)
};

// Compteurs de la dernière image (outils de debug, mesures du cache).
struct ShadowStats
{
    std::uint32_t views = 0;          // vues voulues (tuiles demandées)
    std::uint32_t viewsUpdated = 0;   // vues redessinées cette image (toute action de dessin)
    std::uint32_t staticRedraws = 0;  // vues dont la géométrie fixe a été redessinée dans le cache
    std::uint32_t movableRedraws = 0; // vues dont les casters mobiles ont été redessinés par-dessus le cache
    std::uint32_t fullRedraws = 0;    // vues en mode Dynamic redessinées entièrement
    std::uint32_t cacheCopies = 0;    // tuiles recopiées depuis le cache
    std::uint32_t cacheHits = 0;      // vues valides gardées telles quelles (rien n'a bougé)
    std::uint32_t deferred = 0;       // vues à mettre à jour reportées (budget ou amortissement)
    std::uint32_t dropped = 0;        // vues sans tuile (atlas plein)
    std::uint32_t renderPasses = 0;   // passes de rendu GPU enregistrées (0 à 2)
    std::uint32_t drawCalls = 0;
    std::uint32_t casterInstances = 0;
    std::uint32_t uploadedInstances = 0;  // instances envoyées au GPU cette image (seulement celles des dessins retenus)
    std::uint32_t mobilityViolations = 0; // lumières Static/Stationary déplacées (cumul depuis la création)
};

class ShadowFeature : public RenderFeature
{
  public:
    using CasterId = std::uint32_t;

    // `shaderDirectory` : shaders/rhi (shadow_*.vert/frag, mesh.vert/frag pour les variantes réceptrices).
    static core::Result<std::unique_ptr<ShadowFeature>> create(rhi::Device &device, ShaderLibrary &library, PipelineCache &pipelines,
                                                               const std::filesystem::path &shaderDirectory, const ShadowSettings &settings = {});
    // A75.4 : même fabrique depuis un `RenderContext`.
    static core::Result<std::unique_ptr<ShadowFeature>> create(const RenderContext &context, const ShadowSettings &settings = {})
    {
        return create(context.device(), context.library(), context.pipelines(), context.shaderDirectory(), settings);
    }
    ~ShadowFeature() override;
    ShadowFeature(const ShadowFeature &) = delete;
    ShadowFeature &operator=(const ShadowFeature &) = delete;

    std::string name() const override { return "shadows"; }
    core::Result<> prepare(const FrameData &frame) override;
    core::Result<> declarePasses(RenderGraphBuilder &builder) override;
    // A61 C : atlas, cache et passes que le plan de l'image demande (cache statique, copies, atlas) ; un cache d'ombres stable
    // garde le graphe, une vue à redessiner ou à recopier le reconstruit.
    std::optional<std::uint64_t> passSignature() const override;

    // Lumières lues à chaque image (mobilité, mode, position, direction) ; non possédé.
    void setLights(const LightManager *lights) noexcept { lights_ = lights; }
    void setLightParams(std::size_t lightIndex, const LightShadowParams &params);
    // Change les réglages ; un changement de taille d'atlas recrée les atlas (le groupe récepteur change : relire receiverGroup).
    core::Result<> setSettings(const ShadowSettings &settings);
    const ShadowSettings &settings() const noexcept { return settings_; }

    // Casters (non possédés : ils doivent vivre jusqu'à leur retrait ou au dernier endFrame). `localBounds` : boîte du maillage
    // (culling par vue) ; invalide : toujours dessiné. Un maillage avec ses propres données d'instance est refusé (kUnsupported).
    core::Result<CasterId> addMeshCaster(const Mesh &mesh, const Aabb &localBounds, CasterMobility mobility);
    // Tous les objets de `objects` (culling et LOD par vue d'ombre ; LOD choisi depuis la caméra), maillages de `meshes`.
    CasterId addObjectCasters(ObjectSet &objects, const InstanceRenderer &meshes, CasterMobility mobility);
    void removeCaster(CasterId id);
    // REN-39 : mode d'ombre d'un caster maillage transparent (défaut : kFull, ombre pleine). kNone : plus dessiné ; kMasked / kDithered :
    // `alpha` et `cutoff` sont lus à chaque image, et leur changement invalide la vue (cache compris) de ce caster. Casters d'un ObjectSet :
    // kUnsupported (ombre pleine).
    core::Result<> setCasterTransparency(CasterId id, const TransparentShadow &shadow);
    // Oublie tout le contenu (cache compris) : tout sera redessiné.
    void invalidate() noexcept;

    // Groupe 1 des matériaux récepteurs qui n'ont pas d'autre texture (atlas + bloc) ; changé par setSettings.
    rhi::BindGroupHandle receiverGroup() const noexcept { return receiverGroup_; }
    // Ajoute les deux liaisons des ombres (6 et 7) au groupe 1 d'un matériau qui a ses propres textures.
    void appendReceiverBindings(rhi::BindGroupDesc &desc) const;
    // Programmes récepteurs : mesh.vert + mesh.frag (ou mesh_instanced.vert + mesh.frag instancié) avec OROSMO_SHADOWS.
    const std::shared_ptr<ShaderProgram> &receiverProgram() const noexcept { return receiverProgram_; }
    const std::shared_ptr<ShaderProgram> &instancedReceiverProgram() const noexcept { return instancedReceiverProgram_; }
    // Variante réceptrice avec des définitions en plus (ex. forwardPlusDefines() : forward+ et ombres ensemble).
    core::Result<std::shared_ptr<ShaderProgram>> receiverProgramWith(const std::vector<Define> &extraDefines, bool instanced = false) const;

    const ShadowStats &stats() const noexcept { return stats_; }
    ShadowStatus lightStatus(std::size_t lightIndex) const noexcept;
    const ShadowGpuData &gpuData() const noexcept { return gpu_; }
    rhi::TextureHandle atlasTexture() const noexcept { return atlas_; }
    rhi::TextureHandle cacheTexture() const noexcept { return cache_; }
    std::uint64_t frameCounter() const noexcept { return frame_; }

  private:
    bool staticWorkPlanned() const noexcept;
    bool copiesPlanned() const noexcept;
    struct MeshCaster
    {
        const Mesh *mesh = nullptr;
        Aabb localBounds;
        CasterMobility mobility = CasterMobility::kStatic;
        TransparentShadow transparency; // REN-39
    };
    struct ObjectCasters
    {
        ObjectSet *objects = nullptr;
        const InstanceRenderer *meshes = nullptr;
        CasterMobility mobility = CasterMobility::kStatic;
    };
    struct Caster
    {
        CasterId id = 0;
        bool object = false;
        MeshCaster mesh;
        ObjectCasters objects;
    };
    // Dessin d'une vue : un maillage (matrice complète en push constants) ou un lot instancié (tampon de l'image).
    struct CasterDraw
    {
        const Mesh *mesh = nullptr;
        bool instanced = false;
        glm::mat4 matrix{1.0f}; // maillage : clip × modèle ; instancié : clip
        std::uint32_t firstInstance = 0;
        std::uint32_t instanceCount = 0;
        TransparentShadow transparency; // maillage : mode (kMasked / kDithered : programme à rejet) ; instancié : toujours kFull
    };
    enum class Action : std::uint8_t
    {
        kNone,
        kFull,       // Dynamic : effacer + tout dessiner
        kStaticCopy, // Cached : cache redessiné, recopié, casters mobiles par-dessus
        kCopy,       // Cached : recopie du cache + casters mobiles
    };
    struct ViewKey
    {
        std::size_t light = 0;
        std::uint32_t cascade = 0;
        auto operator<=>(const ViewKey &) const = default;
    };
    struct ViewState
    {
        std::optional<AtlasTile> tile;
        bool rendered = false;       // la tuile de l'atlas contient la vue `renderedFit`
        bool cacheValid = false;     // la tuile du cache contient la géométrie fixe de `renderedFit`
        ShadowViewFit renderedFit;
        std::uint64_t staticHash = 0;
        std::uint64_t movableHash = 0;
        std::uint64_t lastUpdate = 0;
        std::uint32_t requestedSize = 0;
        bool requested = false;
        bool spot = false;
        LightShadowParams params; // projecteur : plans et fondu de la vue dessinée
    };
    struct Request
    {
        ViewKey key;
        std::uint32_t size = 0;
        int priority = 0;
        bool spot = false;
        float distance = 0.0f;
        ShadowViewFit fit;
        ShadowBehavior behavior;
        std::uint32_t interval = 1;
        // De quoi recalculer `fit` à la résolution réellement allouée (la tuile peut être plus petite que `size`).
        CameraShape camera;
        Light light;
        float sliceNear = 0.0f;
        float sliceFar = 0.0f;
        float halfAngle = 0.0f;
        LightShadowParams lightParams;
    };
    struct PlannedView
    {
        ViewKey key;
        AtlasTile tile;
        Action action = Action::kNone;
        std::vector<CasterDraw> staticDraws;  // dans le cache (kStaticCopy) ou dans l'atlas (kFull : statiques + mobiles)
        std::vector<CasterDraw> movableDraws;
    };

    ShadowFeature(rhi::Device &device, ShaderLibrary &library, PipelineCache &pipelines) : device_(device), library_(library), pipelines_(pipelines) {}
    core::Result<> createAtlases(const ShadowSettings &settings);
    void appendReceiverBindings(rhi::BindGroupDesc &desc, rhi::TextureHandle atlas) const;
    void destroyAtlases() noexcept;
    LightShadowParams paramsFor(std::size_t light) const;
    void collectRequests(const FrameData &frame, std::vector<Request> &requests);
    void allocateTiles(std::vector<Request> &requests);
    std::uint64_t gatherCasters(const ShadowViewFit &fit, const glm::vec3 &eye, CasterMobility mobility, std::vector<CasterDraw> &draws);
    ShadowViewFit fitFor(const Request &request, std::uint32_t resolution) const;
    void invalidatePending() noexcept;
    core::Result<> uploadFrame();
    core::Result<> recordStatic(RenderPassContext &ctx);
    core::Result<> recordAtlas(RenderPassContext &ctx);
    core::Result<> drawCasters(rhi::CommandEncoder &e, const std::vector<CasterDraw> &draws);
    core::Result<rhi::PipelineHandle> casterPipeline(const Mesh &mesh, bool instanced, bool alphaTest);
    void writeGpuData(const glm::vec3 &origin);

    rhi::Device &device_;
    ShaderLibrary &library_;
    PipelineCache &pipelines_;
    std::filesystem::path shaderDirectory_;
    ShadowSettings settings_;
    const LightManager *lights_ = nullptr;
    std::map<std::size_t, LightShadowParams> params_;
    std::vector<Light> previousLights_;
    std::vector<ShadowStatus> statuses_;

    std::shared_ptr<ShaderProgram> casterProgram_;
    std::shared_ptr<ShaderProgram> instancedCasterProgram_;
    std::shared_ptr<ShaderProgram> alphaCasterProgram_; // REN-39 : OROSMO_SHADOW_ALPHA (rejet masqué / tramé)
    std::shared_ptr<ShaderProgram> clearProgram_;
    std::shared_ptr<ShaderProgram> copyProgram_;
    std::shared_ptr<ShaderProgram> receiverProgram_;
    std::shared_ptr<ShaderProgram> instancedReceiverProgram_;

    rhi::TextureHandle atlas_;
    rhi::TextureHandle cache_;
    rhi::SamplerHandle compareSampler_;
    rhi::SamplerHandle pointSampler_;
    rhi::BindGroupHandle receiverGroup_;
    rhi::BindGroupHandle copyGroup_;
    GpuBuffer dataBuffer_;
    GpuBuffer instanceBuffer_;
    bool atlasFresh_ = true; // contenu indéfini : effacer l'atlas et le cache à la première passe
    bool cacheFresh_ = true;

    ShadowAtlasAllocator allocator_;
    std::map<ViewKey, ViewState> views_;
    std::vector<ViewKey> pending_; // vues mises à jour cette image, pas encore validées (enregistrement de la passe de l'atlas réussi)
    std::vector<Caster> casters_;
    CasterId nextCaster_ = 1;
    std::vector<PlannedView> planned_;
    std::vector<GpuInstance> frameInstances_;
    InstanceBatchBuilder scratchBatches_;
    ShadowGpuData gpu_{};
    ShadowStats stats_;
    std::uint32_t mobilityViolations_ = 0;
    std::uint64_t frame_ = 0;
};

} // namespace orosmo::renderer
