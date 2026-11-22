// REN-21 / REN-41 : sorties auxiliaires (AOV) de la passe de scène. Un AOV est une cible couleur de plus (MRT) ajoutée à la passe de
// scène SEULEMENT si quelqu'un le demande (`SceneRenderer::declareAov`, `RenderFeature::requiredAovs`, mode d'affichage de debug,
// `setMotionVectorsEnabled`) : sans demande, la cible de scène n'a qu'une couleur et rien d'autre n'est alloué ni dessiné.
//
// Disposition des attachements (déterministe, partagée avec les shaders) : couleur 0 = la scène ; puis les AOV demandés dans
// l'ordre de l'énumération `Aov` (le mouvement d'abord, donc toujours à l'indice 1 quand il est demandé, comme avant M5).
// Un programme de maillage qui écrit des AOV est compilé avec `aovShaderDefines(set)` (`OROSMO_AOV_<NOM>` = emplacement de sortie,
// voir `shaders/rhi/aov.glsl`) ; un programme qui n'écrit que la couleur reste valable : ses AOV gardent leur valeur d'effacement.
//
// Résolution MSAA (la cible de scène est aujourd'hui mono-échantillon : règle documentée, appliquée par `aovResolveRule`) :
// les valeurs continues (mouvement, rugosité/métal, émission, albédo) sont moyennées sur les échantillons couverts ; les normales
// sont décodées, moyennées puis renormalisées avant ré-encodage ; la profondeur linéaire prend l'échantillon le plus proche ; les
// IDENTIFIANTS ne sont JAMAIS moyennés : on garde celui de l'échantillon le plus proche.
#pragma once

#include "orosmo/core/error.hpp"
#include "orosmo/renderer/blend_mode.hpp"
#include "orosmo/rhi/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace orosmo::renderer
{

enum class Aov : std::uint8_t
{
    kMotion,      // RG16Float : (NDC courant - NDC précédent) / 2, sans le jitter ; (0, 0) = immobile / pas de géométrie
    kNormals,     // RG16Float : normale monde, encodage octaédrique (coords.glsl octEncode, valeurs dans [0, 1]) ; la couverture se lit dans la profondeur linéaire ou l'identifiant
    kMaterial,    // RG8Unorm : rugosité, métal
    kObjectId,    // R32Uint : identifiant d'objet (Mesh::setObjectId) ; 0 = aucun objet
    kLinearDepth, // R32Float : distance le long de l'axe de la vue, en unités monde ; 0 = pas de géométrie
    kEmission,    // RGBA16Float : lumière émise (rgb), a inutilisé
    kAlbedo,      // RGBA8Unorm : couleur de base avant éclairage
    kCount,
};
inline constexpr std::size_t kAovCount = static_cast<std::size_t>(Aov::kCount);

enum class AovResolve : std::uint8_t
{
    kAverage,             // moyenne des échantillons couverts
    kAverageRenormalized, // décode, moyenne, renormalise, ré-encode (normales)
    kNearestSample,       // valeur de l'échantillon le plus proche de la caméra (profondeur linéaire)
    kNearestNoBlend,      // identifiants : valeur de l'échantillon le plus proche, jamais moyennée ni filtrée
};

struct AovInfo
{
    std::string_view name;   // "motion", "normals", "material", "object-id", "linear-depth", "emission", "albedo"
    std::string_view define; // macro GLSL de `aovShaderDefines`
    rhi::Format format;
    AovResolve resolve;
    std::array<float, 4> clear; // valeur d'effacement : les normales valent -1 (hors de [0, 1] : « pas de géométrie »), le reste 0
};
const AovInfo &aovInfo(Aov aov) noexcept;
AovResolve aovResolveRule(Aov aov) noexcept;
std::optional<Aov> aovFromName(std::string_view name) noexcept;

// Ensemble d'AOV (masque de bits).
class AovSet
{
  public:
    constexpr AovSet() = default;
    constexpr AovSet(std::initializer_list<Aov> list)
    {
        for (const Aov a : list)
        {
            bits_ |= bit(a);
        }
    }
    constexpr bool has(Aov a) const noexcept { return (bits_ & bit(a)) != 0; }
    constexpr void add(Aov a) noexcept { bits_ |= bit(a); }
    constexpr void remove(Aov a) noexcept { bits_ &= ~bit(a); }
    constexpr bool empty() const noexcept { return bits_ == 0; }
    std::uint32_t count() const noexcept;
    constexpr AovSet operator|(AovSet o) const noexcept { return fromBits(bits_ | o.bits_); }
    constexpr AovSet operator&(AovSet o) const noexcept { return fromBits(bits_ & o.bits_); }
    constexpr bool operator==(const AovSet &) const = default;
    std::vector<Aov> list() const; // dans l'ordre de l'énumération

  private:
    static constexpr std::uint32_t bit(Aov a) noexcept { return 1u << static_cast<std::uint32_t>(a); }
    static constexpr AovSet fromBits(std::uint32_t b) noexcept
    {
        AovSet s;
        s.bits_ = b;
        return s;
    }
    std::uint32_t bits_ = 0;
};

// Attachements de la cible de scène pour un ensemble d'AOV : l'indice d'un AOV est 1 + son rang dans l'ensemble.
class AovLayout
{
  public:
    AovLayout() = default;
    explicit AovLayout(AovSet set);
    const AovSet &set() const noexcept { return set_; }
    std::optional<std::uint32_t> index(Aov aov) const noexcept; // attachement couleur, vide si non demandé
    std::uint32_t attachmentCount() const noexcept { return set_.count() + 1; }
    // Formats des attachements couleur : `sceneColor` puis un par AOV.
    std::vector<rhi::Format> colorFormats(rhi::Format sceneColor) const;

  private:
    AovSet set_;
};

// Macros GLSL à donner à ShaderLibrary::loadGraphics pour un programme de maillage qui écrit les AOV de `set`
// (`OROSMO_AOV_NORMALS` = 2...). Vide si `set` est vide (le programme est alors celui d'avant M5, au bit près).
std::vector<std::pair<std::string, std::string>> aovShaderDefines(const AovSet &set);

// REN-41 : ce qu'un matériau écrit dans les AOV. Opaque, masqué, tramé : tout ce qui est actif. Transparent (le mode a
// `isTransparent`) : RIEN par défaut (ni normales, ni identifiant, ni mouvement) ; le matériau peut demander des AOV précis via
// `request` (ex. un verre qui veut ses normales). Un AOV non actif n'est jamais écrit.
struct AovMaterial
{
    BlendMode blend = BlendMode::kOpaque;
    AovSet request; // AOV écrits explicitement, utile seulement pour un mode transparent
};
AovSet aovWrites(const AovMaterial &material, const AovSet &active) noexcept;

// Remplit les cibles couleur d'un pipeline de la passe de scène : format et masque d'écriture de chaque attachement. L'attachement
// 0 écrit la couleur (`sceneWrites`) ; un AOV n'est écrit que s'il est dans `writes` ET si le fragment shader a assez de sorties
// (`programColorOutputs`, réflexion du programme : un programme à une seule sortie n'écrit que la couleur).
void aovColorTargets(std::vector<rhi::ColorTargetState> &targets, const AovLayout &layout, rhi::Format sceneColor, const AovSet &writes,
                     std::uint32_t programColorOutputs);

} // namespace orosmo::renderer
