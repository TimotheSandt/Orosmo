// REN-03 : données par image (groupe 0 de docs/03 §4 et §10). Un seul bloc std140, commun à toutes les passes, lu par les shaders
// au travers de `shaders/rhi/frame_data.glsl` (même disposition, vérifiée ici par `static_assert` et par un test de lecture de
// pixel sur chaque backend). Liaison : groupe 0, binding `kFrameDataBinding` (0 : caméra, 1 : lumières, 2 : ce bloc).
#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace orosmo::renderer
{

// Liaison du bloc dans le groupe 0 (shaders/rhi/frame_data.glsl : `layout(set = 0, binding = 2)`).
inline constexpr std::uint32_t kFrameDataBinding = 2;

struct alignas(16) FrameData // std140
{
    glm::mat4 view{1.0f};
    glm::mat4 proj{1.0f};
    glm::mat4 viewProj{1.0f};
    glm::mat4 invView{1.0f};
    glm::mat4 invProj{1.0f};
    glm::mat4 invViewProj{1.0f};
    glm::mat4 prevViewProj{1.0f}; // vecteurs de mouvement, TAA ; égale à viewProj à la première image
    glm::vec4 cameraRelativeOrigin{0.0f}; // xyz : position de la caméra vue par les shaders (0 en rendu relatif à la caméra)
    glm::vec4 cameraWorldHigh{0.0f};      // position monde (double) scindée en deux float : haute...
    glm::vec4 cameraWorldLow{0.0f};       // ...et basse (high + low = position en double)
    glm::vec4 jitter{0.0f};               // xy courant, zw précédent (coordonnées normalisées)
    glm::vec4 viewport{0.0f};             // largeur, hauteur, 1/largeur, 1/hauteur (résolution de rendu)
    glm::vec4 time{0.0f};                 // secondes cumulées, delta, numéro d'image, graine de bruit de l'image dans [0, 1)
    glm::vec4 sunDirection{0.0f};         // lumière principale : direction (xyz), w = 1 si elle existe
    glm::vec4 sunColor{0.0f};             // couleur × intensité (xyz)
    glm::vec4 planet{0.0f};               // rayon, hauteur de l'atmosphère, albédo du sol, réservé
    glm::vec4 scatteringRayleigh{0.0f};
    glm::vec4 scatteringMie{0.0f};
    glm::vec4 exposure{1.0f, 0.0f, 0.0f, 0.0f}; // exposition, réservé
};

static_assert(sizeof(glm::mat4) == 64 && sizeof(glm::vec4) == 16, "glm sans alignement ni remplissage (GLM_FORCE_* non pris en charge)");
static_assert(std::is_standard_layout_v<FrameData> && std::is_trivially_copyable_v<FrameData>, "FrameData est copié tel quel dans le tampon");
static_assert(sizeof(FrameData) == 640, "FrameData : 7 mat4 + 12 vec4 (std140)");
// Offsets std140, dans l'ordre de shaders/rhi/frame_data.glsl (un mat4 = 64 octets, un vec4 = 16, aucun remplissage).
static_assert(offsetof(FrameData, view) == 0 && offsetof(FrameData, proj) == 64 && offsetof(FrameData, viewProj) == 128);
static_assert(offsetof(FrameData, invView) == 192 && offsetof(FrameData, invProj) == 256 && offsetof(FrameData, invViewProj) == 320);
static_assert(offsetof(FrameData, prevViewProj) == 384 && offsetof(FrameData, cameraRelativeOrigin) == 448);
static_assert(offsetof(FrameData, cameraWorldHigh) == 464 && offsetof(FrameData, cameraWorldLow) == 480 && offsetof(FrameData, jitter) == 496);
static_assert(offsetof(FrameData, viewport) == 512 && offsetof(FrameData, time) == 528 && offsetof(FrameData, sunDirection) == 544);
static_assert(offsetof(FrameData, sunColor) == 560 && offsetof(FrameData, planet) == 576 && offsetof(FrameData, scatteringRayleigh) == 592);
static_assert(offsetof(FrameData, scatteringMie) == 608 && offsetof(FrameData, exposure) == 624, "exposure est le dernier vec4 du bloc");

// Paramètres d'environnement copiés dans FrameData (aucun sens imposé par le moteur ; les effets qui les lisent les documentent).
struct FrameEnvironment
{
    glm::vec4 planet{0.0f};
    glm::vec4 scatteringRayleigh{0.0f};
    glm::vec4 scatteringMie{0.0f};
    float exposure = 1.0f;
};

// Scinde un double en deux float (haut + bas) : rendu relatif à la caméra en grande coordonnée (docs/03 §10).
struct SplitDouble
{
    float high = 0.0f;
    float low = 0.0f;
};
constexpr SplitDouble splitDouble(double value) noexcept
{
    const float high = static_cast<float>(value);
    return {high, static_cast<float>(value - static_cast<double>(high))};
}

// Graine de bruit d'une image : hachage entier (PCG) du numéro d'image, ramené dans [0, 1). Déterministe (règle 9).
constexpr float frameNoiseSeed(std::uint64_t frameIndex) noexcept
{
    std::uint32_t state = static_cast<std::uint32_t>(frameIndex) * 747796405u + 2891336453u;
    std::uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    word = (word >> 22u) ^ word;
    return static_cast<float>(word >> 8u) / 16777216.0f;
}

} // namespace orosmo::renderer
