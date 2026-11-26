// REN-29 : rendu piloté par le GPU, en plugin du graphe de passes (RenderFeature « gpu-driven »).
//
// Scène GPU persistante (GpuScene, miroir d'un ObjectSet : seuls les objets modifiés sont renvoyés) -> passe compute
// « gpudriven.cull » (shaders/rhi/gpu_cull.comp : un fil par objet, pyramide de vue, distance, taille à l'écran, PUIS occlusion contre
// la Hi-Z, puis LOD ; chaque instance visible est ajoutée à la commande indirecte de sa clé (maillage, matériau)) -> passe « scene » :
// un `drawIndexedIndirect` à plusieurs commandes (multi-draw indirect) par (matériau, pool de géométrie) -> passe compute
// « gpudriven.hiz » (shaders/rhi/hiz_build.comp : pyramide de profondeur construite depuis la profondeur FINALE de la scène, lue par
// le culling de l'image SUIVANTE). Aucun retour au CPU entre le culling et le dessin.
//
// Géométrie : les maillages d'InstanceRenderer de même disposition de sommets sont copiés (copie GPU) dans un pool commun (un tampon
// de sommets, un d'index) ; une commande = (maillage, matériau), rangée par (matériau, pool) pour que chaque groupe soit UN appel.
// Mêmes matériaux, programmes, push constants et format d'instance (GpuInstance) qu'InstanceRenderer : même image.
//
// Capacités (règle 11) : compute, dessin indirect (avec firstInstance non nul : `Capabilities::drawIndirectFirstInstance`) et 7
// emplacements de tampons de stockage ; sinon `supports` est faux et
// `enableGpuDriven` refuse (kUnsupported) : l'application garde le culling CPU (ObjectSet::prepare + InstanceRenderer::draw).
// Sans `Capabilities::multiDrawIndirect`, le RHI joue les commandes une par une (même résultat).
#pragma once

#include "orosmo/core/error.hpp"
#include "orosmo/renderer/render_context.hpp"
#include "orosmo/renderer/culling.hpp"
#include "orosmo/renderer/gpu_buffer.hpp"
#include "orosmo/renderer/gpu_scene.hpp"
#include "orosmo/renderer/instance_renderer.hpp"
#include "orosmo/renderer/object_set.hpp"
#include "orosmo/renderer/pipeline_cache.hpp"
#include "orosmo/renderer/render_feature.hpp"
#include "orosmo/renderer/shader_library.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace orosmo::renderer
{

class SceneRenderer;

inline constexpr std::uint32_t kGpuCullWorkgroupSize = 64; // local_size_x de gpu_cull.comp
inline constexpr std::uint32_t kGpuCullStorageBindings = 7; // emplacements plats de stockage utilisés (groupe 0, liaisons 1 à 6)

// Vue du culling GPU : celle du culling CPU (mêmes règles) et la vue-projection en coordonnées MONDE (sauvée avec la Hi-Z construite
// à cette image, pour la reprojection de l'image suivante).
struct GpuDrivenView
{
    CullView cull;
    glm::mat4 viewProjection{1.0f};

    static GpuDrivenView fromMatrices(const glm::mat4 &view, const glm::mat4 &projection, float viewportHeight, bool orthographic = false);
};

struct GpuDrivenOptions
{
    bool frustumCulling = true; // faux : tout est visible (ni pyramide, ni distance, ni taille), comme VisibilityOptions
    bool lod = true;            // faux : toujours le niveau 0
    bool wireframe = false;     // second passage en fil de fer (comme InstanceRenderer::draw)
};

// Ce que le CPU sait au moment du dessin.
struct GpuDrawStats
{
    std::uint32_t commands = 0;      // commandes indirectes (une par (maillage, matériau) possible)
    std::uint32_t indirectCalls = 0; // appels drawIndexedIndirect enregistrés (un par (matériau, pool) non vide, × 2 en fil de fer)
    std::uint32_t pools = 0;
    GpuSceneSyncStats sync;
};

// Compteurs du culling GPU de la dernière image exécutée (readStats : lecture synchrone, chemin lent).
struct GpuCullStats
{
    std::uint32_t objects = 0;         // objets vivants examinés
    std::uint32_t frustumCulled = 0;
    std::uint32_t distanceCulled = 0;
    std::uint32_t smallCulled = 0;
    std::uint32_t frustumVisible = 0;  // après pyramide, distance et taille : comparable à VisibilityStats::cull.visible
    std::uint32_t occlusionCulled = 0; // rejetés par la Hi-Z (0 sans occlusion)
    std::uint32_t visible = 0;         // frustumVisible - occlusionCulled
    std::uint32_t lodCulled = 0;
    std::uint32_t crossFades = 0;
    std::uint32_t instances = 0;       // instances écrites (un objet en fondu en écrit deux)
    std::uint32_t invalid = 0;         // maillage ou matériau inconnu d'InstanceRenderer
    std::uint32_t overflow = 0;        // jamais attendu (plages dimensionnées par GpuScene::keyReferences)
    std::uint32_t drawsWithInstances = 0; // commandes dont le nombre d'instances n'est pas nul
};

// Drapeaux de visibilité par emplacement (readVisibility).
inline constexpr std::uint32_t kGpuVisibleAlive = 1u;
inline constexpr std::uint32_t kGpuVisibleFrustum = 2u;   // a passé pyramide, distance et taille
inline constexpr std::uint32_t kGpuVisibleOcclusion = 4u; // a aussi passé l'occlusion
inline constexpr std::uint32_t kGpuVisibleDrawn = 8u;     // au moins une instance écrite (LOD non rejeté, maillage et matériau connus)

struct GpuCommandCount
{
    InstanceKey key;
    std::uint32_t instances = 0;
};

class GpuDrivenFeature final : public RenderFeature
{
  public:
    static constexpr const char *kName = "gpu-driven";

    // `library`, `pipelines` et `meshes` (registre des maillages et matériaux) doivent survivre au plugin. Charge gpu_cull.comp et
    // hiz_build.comp depuis `shaderDirectory` (shaders/rhi/). kUnsupported sans les capacités (voir `supportedBy`).
    static core::Result<std::unique_ptr<GpuDrivenFeature>> create(rhi::Device &device, ShaderLibrary &library, PipelineCache &pipelines,
                                                                  const std::filesystem::path &shaderDirectory, const InstanceRenderer &meshes);
    // A75.4 : même fabrique depuis un `RenderContext`.
    static core::Result<std::unique_ptr<GpuDrivenFeature>> create(const RenderContext &context, const InstanceRenderer &meshes)
    {
        return create(context.device(), context.library(), context.pipelines(), context.shaderDirectory(), meshes);
    }
    static bool supportedBy(const rhi::Capabilities &capabilities) noexcept;

    ~GpuDrivenFeature() override;
    GpuDrivenFeature(const GpuDrivenFeature &) = delete;
    GpuDrivenFeature &operator=(const GpuDrivenFeature &) = delete;

    std::string name() const override { return kName; }
    bool supports(const rhi::Capabilities &capabilities) const noexcept override { return supportedBy(capabilities); }
    core::Result<> declarePasses(RenderGraphBuilder &builder) override;

    // Synchronise la scène GPU avec `objects` (objets modifiés seulement), prépare les commandes et enregistre les dessins indirects
    // dans `scene` (drawCustom : joués dans la passe « scene »). Entre beginFrame/setCamera et endFrame ; une fois par image.
    // Le culling lui-même s'exécute dans le graphe de l'image (passe « gpudriven.cull », avant « scene »).
    core::Result<GpuDrawStats> draw(SceneRenderer &scene, const ObjectSet &objects, const GpuDrivenView &view, const GpuDrivenOptions &options = {});

    // Occlusion Hi-Z (vraie par défaut). Désactivée : ni test ni construction de la Hi-Z.
    void setOcclusion(bool enabled) noexcept;
    bool occlusion() const noexcept { return occlusion_; }
    // Oublie la Hi-Z (coupure de caméra, téléportation) : l'image suivante n'a pas d'occlusion.
    void resetOcclusion() noexcept { hizValid_ = false; }
    // Vrai quand une Hi-Z construite à une image précédente sera utilisée par le prochain culling.
    bool hizValid() const noexcept { return hizValid_; }
    std::uint32_t hizLevels() const noexcept { return hizLevels_; }

    // Lectures synchrones (chemin lent : outils, tests, journal de la démo) des résultats de la dernière image exécutée.
    core::Result<GpuCullStats> readStats() const;
    core::Result<std::vector<std::uint32_t>> readVisibility() const; // un mot par emplacement (kGpuVisible*)
    core::Result<std::vector<GpuCommandCount>> readCommandCounts() const; // commandes non vides, dans l'ordre des commandes
    // Hi-Z (niveau `level`, lignes en partant du haut : repère A26) ; vide si elle n'existe pas.
    core::Result<std::vector<float>> readHiz(std::uint32_t level) const;

    const GpuScene &gpuScene() const noexcept { return scene_; }

  private:
    struct Pool
    {
        const Mesh *layout = nullptr; // maillage de référence (disposition des sommets)
        std::vector<std::uint32_t> meshes;
        GpuBuffer vertices;
        GpuBuffer indices;
    };
    struct MeshSlot
    {
        std::uint32_t pool = 0;
        std::uint32_t rank = 0; // colonne de la commande : rangée par pool
        std::uint32_t firstIndex = 0;
        std::int32_t vertexOffset = 0;
        rhi::BufferHandle vertexBuffer; // ceux copiés dans le pool (un changement reconstruit les pools)
        rhi::BufferHandle indexBuffer;
    };
    struct PendingCopy
    {
        rhi::BufferHandle source;
        rhi::BufferHandle destination;
        rhi::BufferCopy region;
    };

    GpuDrivenFeature(rhi::Device &device, PipelineCache &pipelines, const InstanceRenderer &meshes)
        : device_(device), pipelines_(pipelines), meshes_(meshes)
    {
    }
    core::Result<> ensurePools();
    core::Result<> ensureTables();
    core::Result<> ensureBuffer(GpuBuffer &buffer, std::uint64_t size, rhi::BufferUsage usage, const char *name, bool exact = false);
    core::Result<> ensureCullGroup();
    core::Result<> ensureHiz(rhi::Extent2D size);
    core::Result<> buildHiz(RenderPassContext &ctx, rhi::TextureHandle depth);
    void releaseHiz() noexcept;

    rhi::Device &device_;
    PipelineCache &pipelines_;
    const InstanceRenderer &meshes_;
    std::shared_ptr<ShaderProgram> cullProgram_;
    std::shared_ptr<ShaderProgram> cullHizProgram_;
    std::shared_ptr<ShaderProgram> hizDepthProgram_;
    std::shared_ptr<ShaderProgram> hizReduceProgram_;
    GpuScene scene_;

    // Géométrie et commandes.
    std::vector<Pool> pools_;
    std::vector<MeshSlot> meshSlots_;
    std::vector<PendingCopy> pendingCopies_;
    std::uint32_t meshCount_ = 0;
    std::uint32_t materialCount_ = 0;
    std::uint64_t tablesKeyVersion_ = ~std::uint64_t{0};
    bool tablesDirty_ = true;
    std::uint32_t commandCount_ = 0;
    std::vector<InstanceKey> commandKeys_;
    std::vector<std::uint32_t> commandCapacity_;

    GpuBuffer params_;
    GpuBuffer tables_;
    GpuBuffer template_;
    GpuBuffer args_;
    GpuBuffer instances_;
    GpuBuffer stats_;
    GpuBuffer zeros_;
    rhi::BindGroupHandle cullGroup_;
    std::vector<rhi::BufferHandle> cullGroupBuffers_; // handles liés dans cullGroup_ (reconstruit s'ils changent)

    // Hi-Z.
    bool occlusion_ = true;
    bool hizValid_ = false;
    glm::mat4 hizViewProj_{1.0f}; // vue-projection de l'image qui a construit la Hi-Z
    rhi::TextureHandle hiz_;
    rhi::Extent2D hizSize_{};
    std::uint32_t hizLevels_ = 0;
    rhi::SamplerHandle nearest_;
    rhi::BindGroupHandle hizSampleGroup_;
    std::vector<rhi::BindGroupHandle> hizLevelGroups_; // niveau 0 : profondeur -> niveau 0 ; i : niveau i - 1 -> niveau i
    rhi::TextureHandle hizDepth_;                      // profondeur liée au groupe du niveau 0

    // Image en cours (draw -> declarePasses).
    bool pending_ = false;
    bool cullWithHiz_ = false;
    std::uint32_t slotCount_ = 0;
    std::uint64_t lastDrawFrame_ = 0; // SceneRenderer::frameSerial de l'image ouverte qui a déposé les dessins
    glm::mat4 frameViewProj_{1.0f};
};

// Installe le plugin sur `renderer` (profondeur de la scène échantillonnable : la Hi-Z la lit) ; renvoyé, possédé par le renderer.
// kUnsupported sans les capacités : rien n'est changé, l'application garde le culling CPU.
core::Result<GpuDrivenFeature *> enableGpuDriven(SceneRenderer &renderer, rhi::Device &device, ShaderLibrary &library, PipelineCache &pipelines,
                                                 const std::filesystem::path &shaderDirectory, const InstanceRenderer &meshes);
// A75.4 : même fonction depuis un `RenderContext`.
inline core::Result<GpuDrivenFeature *> enableGpuDriven(SceneRenderer &renderer, const RenderContext &context, const InstanceRenderer &meshes)
{
    return enableGpuDriven(renderer, context.device(), context.library(), context.pipelines(), context.shaderDirectory(), meshes);
}
void disableGpuDriven(SceneRenderer &renderer);

} // namespace orosmo::renderer
