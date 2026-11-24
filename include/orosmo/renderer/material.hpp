// Matériaux PBR métal/rugosité (REN-10) et leurs variantes et instances (REN-11). Un `PbrMaterial` possède son bloc de paramètres
// (tampon uniforme) et son groupe 1 ; il garde ses textures vivantes (partagées). Ses variantes de shader (cartes présentes, alpha,
// double face, sans éclairage) sont des définitions de préprocesseur (A48 ; le RHI n'a pas de constantes de spécialisation, A71),
// compilées une fois par combinaison par `PbrShaderCache`. Une instance (`instantiate`) partage textures et variante et surcharge
// les paramètres.
//
// Disposition du groupe 1 (shaders/rhi/mesh_pbr.frag) : 0 couleur de base (sRGB), 1 métal/rugosité (B = métal, G = rugosité),
// 2 normale (espace tangent), 3 occlusion (R), 4 émission (sRGB), 5 bloc `MaterialBlock` (std140), 6 et 7 ombres (ShadowFeature).
// La couleur de base est à la liaison 0 comme dans la variante texturée de mesh.frag : les modes de debug (REN-35) la lisent.
#pragma once

#include "orosmo/core/error.hpp"
#include "orosmo/renderer/blend_mode.hpp"
#include "orosmo/renderer/gpu_buffer.hpp"
#include "orosmo/renderer/shader_library.hpp"
#include "orosmo/renderer/texture.hpp"
#include "orosmo/rhi/device.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace orosmo::renderer
{

class ShadowFeature;

enum class PbrMap : std::uint8_t
{
    kBaseColor,
    kMetallicRoughness,
    kNormal,
    kOcclusion,
    kEmissive,
};
inline constexpr std::size_t kPbrMapCount = 5;
inline constexpr std::uint32_t kPbrParamsBinding = 5;

// Combinaison de variante : un bit par carte présente, puis les modes.
enum PbrFeature : std::uint32_t
{
    kPbrBaseColorMap = 1u << 0,
    kPbrMetallicRoughnessMap = 1u << 1,
    kPbrNormalMap = 1u << 2,
    kPbrOcclusionMap = 1u << 3,
    kPbrEmissiveMap = 1u << 4,
    kPbrAlphaMask = 1u << 5,  // seuil (discard)
    kPbrAlphaBlend = 1u << 6, // alpha de la couleur de base écrit dans la sortie (mélange par le pipeline)
    kPbrDoubleSided = 1u << 7, // normale retournée sur les faces arrière
    kPbrUnlit = 1u << 8,       // KHR_materials_unlit : couleur de base seule
    kPbrShadows = 1u << 9,     // récepteur d'ombres (liaisons 6 et 7)
};

// Définitions de préprocesseur d'une combinaison (OROSMO_PBR_*), dans un ordre stable.
std::vector<Define> pbrDefines(std::uint32_t features);

struct PbrParams
{
    glm::vec4 baseColorFactor{1.0f};
    glm::vec3 emissive{0.0f}; // facteur d'émission × force (KHR_materials_emissive_strength), linéaire
    float metallic = 1.0f;
    float roughness = 1.0f;
    float normalScale = 1.0f;
    float occlusionStrength = 1.0f;
    float alphaCutoff = 0.5f;
};

struct PbrMaterialDesc
{
    std::string name = "material";
    PbrParams params;
    std::array<std::shared_ptr<const Texture>, kPbrMapCount> maps{}; // nul = carte absente (texture neutre liée)
    BlendMode blend = BlendMode::kOpaque; // kOpaque, kMask ou kBlend depuis glTF (`blendModeForAlpha`)
    bool doubleSided = false;
    bool unlit = false;

    std::uint32_t features() const noexcept; // sans kPbrShadows (décidé par `PbrMaterial::create`)
};

// Textures neutres partagées par les matériaux sans carte : blanc (couleur, occlusion, métal/rugosité = facteurs seuls) et normale
// plate (0,5 ; 0,5 ; 1).
class PbrDefaults
{
  public:
    static core::Result<std::shared_ptr<PbrDefaults>> create(rhi::Device &device);
    const Texture &white() const noexcept { return white_; }
    const Texture &flatNormal() const noexcept { return flatNormal_; }

  private:
    Texture white_;
    Texture flatNormal_;
};

class PbrMaterial
{
  public:
    // `shadows` non nul : le groupe porte les liaisons des ombres et la variante est réceptrice (à recréer par `rebuild` si
    // l'atlas change, voir ShadowFeature::setSettings).
    static core::Result<std::shared_ptr<PbrMaterial>> create(rhi::Device &device, PbrMaterialDesc desc, std::shared_ptr<const PbrDefaults> defaults,
                                                            const ShadowFeature *shadows = nullptr);
    ~PbrMaterial();
    PbrMaterial(const PbrMaterial &) = delete;
    PbrMaterial &operator=(const PbrMaterial &) = delete;

    // REN-11 : instance qui partage textures et variante, avec ses propres paramètres (son propre tampon et son groupe).
    core::Result<std::shared_ptr<PbrMaterial>> instantiate(const PbrParams &params, std::string name = {}) const;
    // Change les paramètres (envoi ordonné : les images en vol gardent les anciens).
    core::Result<> setParams(const PbrParams &params);
    // Recrée le groupe 1 (ex. après un changement d'atlas d'ombres) ; `shadows` nul retire la réception.
    core::Result<> rebuild(const ShadowFeature *shadows);

    rhi::BindGroupHandle group() const noexcept { return group_; }
    std::uint32_t features() const noexcept { return features_; }
    const PbrParams &params() const noexcept { return desc_.params; }
    const PbrMaterialDesc &desc() const noexcept { return desc_; }
    BlendMode blend() const noexcept { return desc_.blend; }
    bool doubleSided() const noexcept { return desc_.doubleSided; }
    const std::string &name() const noexcept { return desc_.name; }

    // Bloc std140 de mesh_pbr.frag (80 octets), vérifié par static_assert dans material.cpp.
    static constexpr std::size_t kParamsBlockSize = 80;

  private:
    PbrMaterial() = default;
    core::Result<> buildGroup(const ShadowFeature *shadows);

    rhi::Device *device_ = nullptr;
    PbrMaterialDesc desc_;
    std::shared_ptr<const PbrDefaults> defaults_;
    GpuBuffer params_;
    rhi::BindGroupHandle group_;
    std::uint32_t features_ = 0;
    const ShadowFeature *shadows_ = nullptr;
};

// Programmes PBR par combinaison de variante (shaders/rhi/mesh_pbr.vert + mesh_pbr.frag) et définitions en plus (AOV, forward+).
// Une combinaison déjà demandée rend le même programme ; le rechargement à chaud reste celui de la ShaderLibrary.
class PbrShaderCache
{
  public:
    PbrShaderCache(ShaderLibrary &library, std::filesystem::path shaderDirectory)
        : library_(&library), shaderDirectory_(std::move(shaderDirectory))
    {
    }
    core::Result<std::shared_ptr<ShaderProgram>> program(std::uint32_t features, const std::vector<Define> &extraDefines = {});
    std::size_t size() const noexcept { return programs_.size(); }

  private:
    ShaderLibrary *library_;
    std::filesystem::path shaderDirectory_;
    std::map<std::pair<std::uint32_t, std::vector<Define>>, std::shared_ptr<ShaderProgram>> programs_;
};

// Disposition des sommets des maillages PBR (Mesh::create) : position 3, normale 3, uv 2, tangente 4 (w = signe), couleur 4.
inline constexpr std::array<std::uint32_t, 5> kPbrVertexAttributeSizes{3, 3, 2, 4, 4};
inline constexpr std::uint32_t kPbrVertexFloats = 16;

} // namespace orosmo::renderer
