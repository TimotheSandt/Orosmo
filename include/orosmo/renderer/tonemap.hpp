// REN-17, REN-18 : fonctions de référence CPU des post-traitements HDR (tonemapping, sRGB, histogramme de luminance, exposition
// automatique, vignettage, LUT). Ce sont les mêmes formules que `shaders/rhi/post/post_common.glsl`, écrites une seconde fois pour
// que les tests comparent les valeurs du GPU à une référence indépendante. Pures : aucune ressource, aucun état.
#pragma once

#include "orosmo/core/error.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace orosmo::renderer
{

// Opérateur de tonemapping (valeur envoyée telle quelle au shader : ne pas renuméroter).
enum class Tonemapper : std::uint8_t
{
    kClamp = 0,            // borne à [0, 1] (aucune compression : sert aux tests et aux comparaisons)
    kAces = 1,             // ajustement ACES RRT+ODT de S. Hill (« BakingLab »), entrée et sortie en sRGB linéaire
    kAgx = 2,              // AgX (T. Sobotka), approximation polynomiale de B. Wrensch, sortie en sRGB linéaire
    kReinhardExtended = 3, // Reinhard étendu sur la luminance : L (1 + L / w²) / (1 + L), w = point blanc
};

const char *toString(Tonemapper tonemapper) noexcept;
std::optional<Tonemapper> parseTonemapper(std::string_view name) noexcept; // « clamp », « aces », « agx », « reinhard »

// Luminance Rec. 709 d'une couleur linéaire.
float luminance(const glm::vec3 &linearRgb) noexcept;
// Tonemapping d'une couleur linéaire DÉJÀ exposée ; résultat linéaire dans [0, 1]. `whitePoint` ne sert qu'à Reinhard étendu.
glm::vec3 tonemap(Tonemapper tonemapper, const glm::vec3 &color, float whitePoint = 4.0f) noexcept;
// Courbe sRGB exacte par morceaux (IEC 61966-2-1), composante par composante, entrée bornée à [0, 1].
glm::vec3 linearToSrgb(const glm::vec3 &linear) noexcept;
glm::vec3 srgbToLinear(const glm::vec3 &encoded) noexcept;

// ── Histogramme de luminance (exposition automatique, REN-18) ───────────────────────────────────────────────────────
// 256 cases : la case 0 reçoit les pixels sous exp2(minLog2) (trop sombres, ignorés par la moyenne) ; les cases 1 à 255 couvrent
// [minLog2, maxLog2] en log2 de la luminance, uniformément (au-dessus : case 255).
inline constexpr std::uint32_t kHistogramBins = 256;
std::uint32_t histogramBin(float luminance, float minLog2, float maxLog2) noexcept;
// Moyenne géométrique de la luminance d'un histogramme (centre de chaque case, case 0 exclue) ; exp2(minLog2) si toutes les
// valeurs sont dans la case 0.
float histogramAverageLuminance(std::span<const std::uint32_t> bins, float minLog2, float maxLog2) noexcept;
// Exposition visée : key / luminance moyenne, bornée à [2^minEv, 2^maxEv].
float targetExposure(float averageLuminance, float key, float minEv, float maxEv) noexcept;
// Adaptation de l'œil : facteur de rapprochement 1 - exp(-dt * vitesse) (1 : immédiat, vitesse <= 0 ou dt <= 0).
float adaptationFactor(float deltaSeconds, float speed) noexcept;

// ── Vignettage (REN-18) ──────────────────────────────────────────────────────────────────────────────────────────────
struct VignetteSettings
{
    float intensity = 0.35f; // assombrissement maximal (0 : aucun, 1 : noir dans les coins)
    float radius = 0.55f;    // distance normalisée (0 au centre, 1 dans les coins) où l'assombrissement commence
    float softness = 0.45f;  // largeur de la transition
    float roundness = 1.0f;  // 1 : cercle (rapport d'aspect corrigé) ; 0 : ellipse qui suit le cadre
};
// Facteur multiplicatif en `uv` (0 à 1, origine en haut à gauche) pour une cible de rapport largeur / hauteur `aspect`.
float vignetteFactor(const glm::vec2 &uv, float aspect, const VignetteSettings &settings) noexcept;

// ── LUT 3D d'étalonnage (REN-18) ─────────────────────────────────────────────────────────────────────────────────────
// Table de `size`³ couleurs, rouge le plus rapide, puis vert, puis bleu (ordre du format .cube). Valeurs dans [0, 1] attendues.
struct ColorLut
{
    std::uint32_t size = 0;
    std::vector<glm::vec3> values;
    // size >= 2 et size³ couleurs ; le cube est calculé sans dépassement (une taille énorme est invalide, pas un repli à zéro).
    bool valid() const noexcept
    {
        const auto n = static_cast<std::size_t>(size);
        return size >= 2 && n <= std::numeric_limits<std::size_t>::max() / n / n && values.size() == n * n * n;
    }
};
ColorLut identityLut(std::uint32_t size);
// Lecture trilinéaire de la LUT (même interpolation que l'échantillonneur linéaire du GPU, centres des texels). Entrée bornée.
glm::vec3 sampleLut(const ColorLut &lut, const glm::vec3 &color) noexcept;
// Format Adobe/Resolve .cube : `LUT_3D_SIZE N`, puis N³ lignes « r g b » ; `TITLE`, `DOMAIN_MIN`/`DOMAIN_MAX` (0 et 1 seulement)
// et les commentaires « # » sont acceptés. kInvalidArgument si la taille manque, est hors de [2, 256], si une ligne est illisible
// ou si le nombre de couleurs est faux ; une LUT 1D (`LUT_1D_SIZE`) est refusée (kUnsupported).
core::Result<ColorLut> parseCubeLut(std::string_view text);

} // namespace orosmo::renderer
