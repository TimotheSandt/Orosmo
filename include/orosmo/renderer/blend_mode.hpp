// Mode de fusion d'un matériau (REN-37, partie données ; docs/03 §11 bis). Un type et l'état de pipeline qui en découle : facteurs
// de mélange, écriture de profondeur, besoins du mode. Le rendu des transparents (tri, passes, réfraction) n'est pas ici : il
// dépend du graphe de passes (REN-32, REN-38, REN-41).
#pragma once

#include "orosmo/core/error.hpp"
#include "orosmo/rhi/types.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace orosmo::renderer
{

enum class BlendMode : std::uint8_t
{
    kOpaque,
    kMask,          // pixel gardé ou rejeté selon un seuil (discard dans le shader)
    kMaskA2C,       // comme kMask, bords adoucis par alpha-to-coverage (cible multi-échantillons)
    kDithered,      // fondu par trame de tramage (rejet stochastique dans le shader, lissé par le TAA)
    kBlend,         // alpha droit : src * a + dst * (1 - a)
    kAdditive,      // src * a + dst
    kPremultiplied, // alpha prémultiplié : src + dst * (1 - a)
    kTransmission,  // verre qui réfracte la scène opaque ; composé comme kBlend, lit la copie de la scène
};

inline constexpr BlendMode kAllBlendModes[] = {BlendMode::kOpaque,   BlendMode::kMask,          BlendMode::kMaskA2C,    BlendMode::kDithered,
                                               BlendMode::kBlend,    BlendMode::kAdditive,      BlendMode::kPremultiplied, BlendMode::kTransmission};

// Ce que le mode impose au pipeline et au renderer.
struct BlendModeState
{
    rhi::BlendState blend{};  // appliqué à chaque cible couleur de la passe
    bool depthWrite = true;   // false pour tous les transparents : test de profondeur sans écriture
    bool alphaToCoverage = false; // le pipeline doit activer l'alpha-to-coverage (le RHI actuel n'a pas ce champ : voir applyBlendMode)
    bool discardInShader = false; // le shader rejette des pixels (kMask : seuil ; kDithered : trame)
    bool needsSorting = false;    // tri arrière vers avant nécessaire (kBlend, kTransmission)
    bool readsSceneColor = false; // lit la copie de la couleur de la scène opaque (kTransmission)
};

BlendModeState blendModeState(BlendMode mode) noexcept;

// Vrai pour les modes rendus après les opaques, sans écriture de profondeur.
bool isTransparent(BlendMode mode) noexcept;

// Nom stable (fichiers de matériaux, journal) : "opaque", "mask", "mask_a2c", "dithered", "blend", "additive", "premultiplied",
// "transmission".
std::string_view blendModeName(BlendMode mode) noexcept;
core::Result<BlendMode> blendModeFromName(std::string_view name);

// Modes d'alpha de glTF (REN-10) : "OPAQUE", "MASK", "BLEND".
core::Result<BlendMode> blendModeFromGltf(std::string_view alphaMode);

// Reporte le mode sur un descripteur de pipeline : mélange sur chaque cible couleur, écriture de profondeur (le test de
// profondeur reste celui du descripteur). Retourne l'état du mode ; l'appelant lit `alphaToCoverage` (non représentable dans
// rhi::GraphicsPipelineDesc à ce jour).
BlendModeState applyBlendMode(rhi::GraphicsPipelineDesc &desc, BlendMode mode);

} // namespace orosmo::renderer
