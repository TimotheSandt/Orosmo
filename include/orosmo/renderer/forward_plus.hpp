// REN-04 : éclairage forward+ clusterisé, en plugin du graphe de passes (RenderFeature, REN-01).
//
// Image : pré-passe de profondeur (`SceneRenderer::setDepthPrepass`, passe « depth-prepass ») -> passe compute
// « forwardplus.clusters » (shaders/rhi/light_clusters.comp : un fil par cluster, listes d'indices de lumières dans un SSBO) -> passe
// « scene », où les matériaux compilés avec OROSMO_FORWARD_PLUS (mesh.frag) ne parcourent que les lumières du cluster du fragment.
// Les deux tampons sont liés au groupe 0 de l'image (liaisons kClusterGridBinding et kClusterLightsBinding, RenderGraphBuilder::
// addFrameBinding). Le forward et le forward+ utilisent la même fonction d'éclairage dans le même ordre : même image (une lumière
// écartée d'un cluster n'apporte exactement rien, lights.glsl).
//
// Budgets réglables : `maxLights` (lumières examinées, dans l'ordre du tampon ; les suivantes sont ignorées) et
// `maxLightsPerCluster` (lumières listées par cluster ; au-delà, le cluster est « en débordement » et les suivantes sont ignorées).
// Capacités (règle 11) : compute et cinq emplacements de tampons de stockage au moins ; sinon `supports` est faux et
// `enableForwardPlus` refuse (kUnsupported) : l'application garde le forward.
#pragma once

#include "orosmo/core/error.hpp"
#include "orosmo/renderer/gpu_buffer.hpp"
#include "orosmo/renderer/light.hpp"
#include "orosmo/renderer/pipeline_cache.hpp"
#include "orosmo/renderer/render_feature.hpp"
#include "orosmo/renderer/shader_library.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace orosmo::renderer
{

class SceneRenderer;

inline constexpr std::uint32_t kClusterGridBinding = 3;   // groupe 0 : en-tête + nombre de lumières par cluster
inline constexpr std::uint32_t kClusterLightsBinding = 4; // groupe 0 : indices de lumières, `maxLightsPerCluster` par cluster
inline constexpr std::uint32_t kClusterWorkgroupSize = 64; // local_size_x de light_clusters.comp

// Mêmes constantes que shaders/include/orosmo/lights.glsl.
inline constexpr float kLightCutoff = 1.0f / 256.0f;
inline constexpr float kLightMaxRange = 1.0e6f;

// Portée d'une lumière ponctuelle ou d'un projecteur : distance où couleur × force × 1,5 / (a d² + b d + 1) tombe à kLightCutoff ;
// 0 si elle est déjà sous ce seuil ; kLightMaxRange sans atténuation. Référence CPU de `lightRange` (lights.glsl).
float lightRange(const Light &light) noexcept;
// Atténuation fenêtrée (1 - (d / portée)⁴)² / (a d² + b d + 1), nulle à partir de la portée. Référence de lights.glsl.
float lightAttenuation(float distance, float a, float b, float range) noexcept;

struct ForwardPlusSettings
{
    std::uint32_t gridX = 16; // cases de l'écran (en NDC, indépendantes de la résolution)
    std::uint32_t gridY = 9;
    std::uint32_t gridZ = 24; // tranches de profondeur de vue exponentielles
    std::uint32_t maxLightsPerCluster = 128;
    std::uint32_t maxLights = 65536;
    float nearDepth = 0.1f;   // début de la tranche 1 (la tranche 0 va de l'œil à near × (far / near)^(1 / Z))
    float farDepth = 500.0f;  // fin de l'avant-dernière tranche ; la dernière va à l'infini

    std::uint32_t clusterCount() const noexcept { return gridX * gridY * gridZ; }
};

// En-tête du tampon de la grille (std430), suivi d'un `uint` par cluster. Même disposition que `ClusterGrid` (clusters.glsl).
struct GpuClusterHeader
{
    std::uint32_t grid[4];   // x, y, z, maxLightsPerCluster
    float depth[4];          // near, far, Z / log(far / near), réservé
    std::uint32_t limits[4]; // maxLights, réservés
};
static_assert(sizeof(GpuClusterHeader) == 48, "ClusterGrid std430 : clusterCounts[] commence à l'offset 48");
static_assert(offsetof(GpuClusterHeader, depth) == 16 && offsetof(GpuClusterHeader, limits) == 32, "ClusterGrid std430 : trois uvec4/vec4");

// Tranche de profondeur (même formule que `clusterSlice`, clusters.glsl) : 0 jusqu'à near, puis exponentielle, bornée à gridZ - 1.
std::uint32_t clusterSlice(const ForwardPlusSettings &settings, float viewDepth) noexcept;
// Valide les réglages (grille non vide, budgets > 0, 0 < near < far, taille des tampons raisonnable) : kInvalidArgument sinon.
core::Result<> validateSettings(const ForwardPlusSettings &settings);

// Lecture du mode de debug « lumières par cluster » (et des statistiques des budgets).
struct ClusterStats
{
    std::uint32_t clusters = 0;
    std::uint32_t emptyClusters = 0;
    std::uint32_t maxLightsInCluster = 0;   // avant le budget
    std::uint32_t overflowingClusters = 0;  // plus de lumières que `maxLightsPerCluster`
    std::uint64_t lightReferences = 0;      // somme des listes (après le budget)
    double averageLightsPerCluster = 0.0;   // après le budget
};
ClusterStats summarizeClusterCounts(std::span<const std::uint32_t> counts, std::uint32_t maxLightsPerCluster) noexcept;

class ForwardPlusLighting final : public RenderFeature
{
  public:
    static constexpr const char *kName = "forward-plus";

    // `library` et `pipelines` doivent survivre au plugin. Charge `light_clusters.comp` depuis `shaderDirectory` (shaders/rhi/).
    // Refusé (kUnsupported) si le périphérique n'a pas les capacités (voir `supportedBy`).
    static core::Result<std::unique_ptr<ForwardPlusLighting>> create(rhi::Device &device, ShaderLibrary &library, PipelineCache &pipelines,
                                                                     const std::filesystem::path &shaderDirectory,
                                                                     const ForwardPlusSettings &settings = {});
    static bool supportedBy(const rhi::Capabilities &capabilities) noexcept;

    std::string name() const override { return kName; }
    bool supports(const rhi::Capabilities &capabilities) const noexcept override { return supportedBy(capabilities); }
    core::Result<> declarePasses(RenderGraphBuilder &builder) override;

    // Nouveaux réglages (tampons recréés à la prochaine image) ; refusés (kInvalidArgument) s'ils ne sont pas valides.
    core::Result<> setSettings(const ForwardPlusSettings &settings);
    const ForwardPlusSettings &settings() const noexcept { return settings_; }

    // Debug « lumières par cluster » (lecture synchrone, chemin lent : outils, tests) : pour chaque cluster c = x + X (y + Y z), le
    // nombre de lumières qui le touchent à la dernière image exécutée (avant le budget). Vide avant la première image.
    core::Result<std::vector<std::uint32_t>> readClusterLightCounts() const;
    // Liste (indices dans le tampon des lumières) du cluster `cluster`, après le budget.
    core::Result<std::vector<std::uint32_t>> readClusterLights(std::uint32_t cluster) const;
    core::Result<ClusterStats> readClusterStats() const;
    rhi::BufferHandle gridBuffer() const noexcept { return grid_.handle(); }
    rhi::BufferHandle lightListBuffer() const noexcept { return lightLists_.handle(); }

  private:
    ForwardPlusLighting(rhi::Device &device, PipelineCache &pipelines) : device_(device), pipelines_(pipelines) {}
    core::Result<> ensureBuffers();

    rhi::Device &device_;
    PipelineCache &pipelines_;
    std::shared_ptr<ShaderProgram> program_;
    ForwardPlusSettings settings_;       // réglages demandés (appliqués à la prochaine création des tampons)
    ForwardPlusSettings bufferSettings_; // réglages avec lesquels grid_ et lightLists_ ont été créés (lectures de debug)
    GpuBuffer grid_;
    GpuBuffer lightLists_;
    bool buffersDirty_ = true;
};

// Installe le forward+ sur `renderer` : pré-passe de profondeur activée et plugin enregistré (renvoyé, possédé par le renderer).
// Les maillages doivent ensuite utiliser la variante OROSMO_FORWARD_PLUS de mesh.frag (`forwardPlusDefines`). kUnsupported sans
// les capacités : le renderer reste en forward, inchangé.
core::Result<ForwardPlusLighting *> enableForwardPlus(SceneRenderer &renderer, rhi::Device &device, ShaderLibrary &library,
                                                      PipelineCache &pipelines, const std::filesystem::path &shaderDirectory,
                                                      const ForwardPlusSettings &settings = {});
// Retire le plugin et la pré-passe (retour au forward ; les maillages doivent reprendre la variante sans OROSMO_FORWARD_PLUS).
void disableForwardPlus(SceneRenderer &renderer);
// Définitions de la variante forward+ de mesh.frag ; `clusterDebug` : couleur = lumières par cluster (OROSMO_CLUSTER_DEBUG).
std::vector<Define> forwardPlusDefines(bool clusterDebug = false);

} // namespace orosmo::renderer
