// REN-13 : éclairage par l'environnement (IBL) et sondes de lumière, en plugin du graphe de passes (RenderFeature « ibl »).
//
// Environnement : image HDR équirectangulaire (radiance linéaire). Trois produits :
//   - irradiance diffuse en harmoniques sphériques d'ordre 2 (9 coefficients RGB), calculée sur le CPU (`projectEquirect`) :
//     déterministe, identique sur toutes les plateformes ;
//   - spéculaire préfiltré : cube RGBA16F dont chaque niveau de mip correspond à une rugosité (niveau i : i / (niveaux - 1)),
//     convolué en compute par échantillonnage d'importance GGX (shaders/rhi/ibl_prefilter.comp), une fois par environnement ;
//   - table BRDF (split-sum) : (A, B) tels que spéculaire = f0 × A + B, calculée sur le CPU (`computeBrdfLut`), même BRDF que
//     pbr.glsl (GGX, Smith corrélé, Schlick).
// Sondes pour objets mobiles : jusqu'à kMaxLightProbes sondes SH placées dans le monde (position, rayon). Chaque fragment mélange
// les sondes proches selon sa position (`ProbeSet::sample`, même formule dans shaders/rhi/ibl.glsl) : un objet qui se déplace
// passe d'une sonde à l'autre sans saut ; hors de toute sonde, il reçoit l'irradiance de l'environnement.
//
// Unités : même convention que les lumières (une surface blanche mate éclairée par un environnement uniforme de radiance L
// renvoie L). Les coefficients SH stockés sont ceux de l'irradiance divisée par π.
//
// Liaisons du groupe 0 de l'image (RenderGraphBuilder::addFrameBinding) : kIblEnvironmentBinding (cube préfiltré),
// kIblBrdfLutBinding (table BRDF), kIblParamsBinding (bloc `IblBlock`, std140). Les matériaux PBR compilés avec `iblDefines()`
// les lisent (shaders/rhi/mesh_pbr.frag). Capacité (règle 11) : compute ; sinon `supports` est faux et `addFeature` refuse.
#pragma once

#include "orosmo/core/error.hpp"
#include "orosmo/renderer/gpu_buffer.hpp"
#include "orosmo/renderer/pipeline_cache.hpp"
#include "orosmo/renderer/render_feature.hpp"
#include "orosmo/renderer/shader_library.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace orosmo::renderer
{

inline constexpr std::uint32_t kIblEnvironmentBinding = 5;
inline constexpr std::uint32_t kIblBrdfLutBinding = 6;
inline constexpr std::uint32_t kIblParamsBinding = 7;
inline constexpr std::uint32_t kMaxLightProbes = 16;

// Image HDR équirectangulaire : radiance linéaire RGB, lignes serrées, première ligne = haut (+Y). Colonnes : u = 0 regarde vers
// +Z, u = 0,5 vers -Z, u = 0,75 vers +X (voir `equirectUv`).
struct EquirectImage
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<glm::vec3> texels; // width × height

    bool empty() const noexcept { return width == 0 || height == 0 || texels.size() != std::size_t{width} * height; }
};

// Correspondance direction <-> coordonnées équirectangulaires (u, v dans [0, 1], v = 0 en haut), la même que ibl.glsl.
glm::vec2 equirectUv(const glm::vec3 &direction) noexcept;
glm::vec3 equirectDirection(const glm::vec2 &uv) noexcept;
// Image uniforme (utile aux tests et comme environnement par défaut).
EquirectImage uniformEquirect(const glm::vec3 &radiance, std::uint32_t width = 8, std::uint32_t height = 4);

// Irradiance / π en harmoniques sphériques d'ordre 2 (bandes 0 à 2, ordre des coefficients : Y00, Y1-1, Y10, Y11, Y2-2, Y2-1,
// Y20, Y21, Y22).
struct ShIrradiance
{
    std::array<glm::vec3, 9> coefficients{};

    // Irradiance / π dans la direction `normal` (unitaire), bornée à 0.
    glm::vec3 evaluate(const glm::vec3 &normal) const noexcept;
    static ShIrradiance uniform(const glm::vec3 &radiance) noexcept;
};
// Projection d'un environnement, pondérée par l'angle solide de chaque texel. kInvalidArgument pour une image vide.
core::Result<ShIrradiance> projectEquirect(const EquirectImage &image);

// Table BRDF split-sum : `size` × `size` couples (A, B) ; colonne = n·v (de 0 à 1, centres de texels), ligne = rugosité
// perceptuelle. `samples` échantillons de Hammersley par texel (déterministe).
std::vector<glm::vec2> computeBrdfLut(std::uint32_t size, std::uint32_t samples = 256);

struct LightProbe
{
    glm::vec3 position{0.0f}; // monde
    float radius = 1.0f;      // influence : poids 1 au centre, 0 à `radius`
    ShIrradiance irradiance{};
};

// Sondes placées dans le monde (kMaxLightProbes au plus) et mélange par position.
class ProbeSet
{
  public:
    // kInvalidArgument au-delà de kMaxLightProbes ou pour un rayon <= 0.
    core::Result<> add(const LightProbe &probe);
    void clear() noexcept { probes_.clear(); }
    std::span<const LightProbe> probes() const noexcept { return probes_; }

    // Irradiance au point `position` : poids w = (1 - d / rayon)² par sonde ; si leur somme dépasse 1, moyenne pondérée des
    // sondes ; sinon complétée par `environment` avec le poids restant. Référence CPU de `iblIrradiance` (ibl.glsl).
    ShIrradiance sample(const glm::vec3 &position, const ShIrradiance &environment) const noexcept;

  private:
    std::vector<LightProbe> probes_;
};

struct IblSettings
{
    std::uint32_t cubeSize = 128;     // côté du niveau 0 du cube préfiltré
    std::uint32_t mipLevels = 6;      // niveaux de rugosité (bornés par la chaîne complète du cube)
    std::uint32_t samples = 64;       // échantillons GGX par texel du préfiltrage
    std::uint32_t brdfLutSize = 64;   // côté de la table BRDF
    float diffuseIntensity = 1.0f;
    float specularIntensity = 1.0f;
};

// Bloc std140 de la liaison kIblParamsBinding (ibl.glsl).
struct GpuIblBlock
{
    glm::vec4 params; // x : intensité diffuse, y : intensité spéculaire, z : dernier niveau de mip, w : nombre de sondes
    std::array<glm::vec4, 9> environmentSh;
    std::array<glm::vec4, kMaxLightProbes> probePositions; // xyz monde, w rayon
    std::array<glm::vec4, std::size_t{kMaxLightProbes} * 9> probeSh;
};
static_assert(sizeof(GpuIblBlock) == 16 * (1 + 9 + kMaxLightProbes + kMaxLightProbes * 9), "IblBlock std140 : vec4 seulement");

class IblFeature final : public RenderFeature
{
  public:
    static constexpr const char *kName = "ibl";

    // Crée les ressources (environnement noir par défaut, table BRDF). kUnsupported sans compute.
    static core::Result<std::unique_ptr<IblFeature>> create(rhi::Device &device, ShaderLibrary &library, PipelineCache &pipelines,
                                                            std::filesystem::path shaderDirectory, IblSettings settings = {});
    ~IblFeature() override;
    IblFeature(const IblFeature &) = delete;
    IblFeature &operator=(const IblFeature &) = delete;

    std::string name() const override { return kName; }
    bool supports(const rhi::Capabilities &capabilities) const noexcept override { return capabilities.compute; }
    core::Result<> declarePasses(RenderGraphBuilder &builder) override;

    // Remplace l'environnement : envoi de l'image, préfiltrage en compute (soumis tout de suite), irradiance SH.
    core::Result<> setEnvironment(const EquirectImage &image);
    // Intensités (appliquées à l'image suivante).
    void setIntensity(float diffuse, float specular) noexcept;
    // Sondes pour objets mobiles (copiées ; appliquées à l'image suivante).
    void setProbes(const ProbeSet &probes);

    const ShIrradiance &environmentIrradiance() const noexcept { return environmentSh_; }
    rhi::TextureHandle environmentCube() const noexcept { return cube_; }
    rhi::TextureHandle brdfLut() const noexcept { return lut_; }
    std::uint32_t cubeMipLevels() const noexcept { return cubeLevels_; }
    const IblSettings &settings() const noexcept { return settings_; }
    GpuIblBlock gpuBlock() const noexcept;

  private:
    IblFeature(rhi::Device &device, PipelineCache &pipelines, IblSettings settings) : device_(device), pipelines_(pipelines), settings_(settings) {}
    core::Result<> createStaticResources(ShaderLibrary &library, const std::filesystem::path &shaderDirectory);
    core::Result<> prefilter(rhi::TextureHandle equirect);
    void release() noexcept;

    rhi::Device &device_;
    PipelineCache &pipelines_;
    IblSettings settings_;
    std::shared_ptr<ShaderProgram> prefilterProgram_;
    rhi::TextureHandle cube_;
    rhi::TextureHandle lut_;
    rhi::SamplerHandle cubeSampler_;
    rhi::SamplerHandle lutSampler_;
    rhi::SamplerHandle equirectSampler_;
    GpuBuffer params_;
    std::uint32_t cubeLevels_ = 1;
    ShIrradiance environmentSh_;
    ProbeSet probes_;
    bool paramsDirty_ = true;
};

// Définitions des matériaux PBR qui lisent l'IBL (à passer en `extraDefines`, comme forwardPlusDefines).
std::vector<Define> iblDefines();

} // namespace orosmo::renderer
