// REN-39 : ombre portée d'un objet transparent. Un mode par caster (matériau) : aucune, pleine, découpée (masque d'alpha) ou
// tramée (trame de Bayer 4x4). La carte d'ombre ne stocke que de la profondeur : une ombre « partielle » n'existe que par
// rejet de texels (`discard` dans `shadow_caster.frag`) puis lissage par le PCF du récepteur. Références CPU de la décision du
// shader (testées sans GPU) : `transparentShadowKeeps`.
#pragma once

#include "orosmo/core/error.hpp"
#include "orosmo/renderer/blend_mode.hpp"

#include <cstdint>
#include <string_view>

namespace orosmo::renderer
{

enum class TransparentShadowMode : std::uint8_t
{
    kNone,     // le caster ne projette aucune ombre (n'est pas dessiné dans la carte)
    kFull,     // ombre pleine, comme un objet opaque (comportement d'un caster ordinaire)
    kMasked,   // ombre pleine si alpha >= cutoff, aucune sinon (comme BlendMode::kMask)
    kDithered, // part de texels d'ombre ~ alpha, trame de Bayer 4x4 fixe en espace carte d'ombre (stable dans le temps)
};

struct TransparentShadow
{
    TransparentShadowMode mode = TransparentShadowMode::kFull;
    float alpha = 1.0f;  // opacité (kMasked : comparée à `cutoff` ; kDithered : part d'ombre). Ignorée par kNone et kFull.
    float cutoff = 0.5f; // kMasked : l'ombre existe si alpha >= cutoff (même règle que mesh.frag : rejet si alpha < cutoff)
    bool operator==(const TransparentShadow &) const = default;
};

// Seuil de la trame de Bayer 4x4 pour le texel (x, y) de la carte d'ombre, dans ]0, 1[ : (m + 0.5) / 16.
float bayerThreshold4(std::uint32_t x, std::uint32_t y) noexcept;

// Vrai si le texel (x, y) de la carte d'ombre reçoit la profondeur du caster (donc de l'ombre). Même décision que le shader.
bool transparentShadowKeeps(const TransparentShadow &shadow, std::uint32_t x, std::uint32_t y) noexcept;

// Part de texels d'ombre d'un motif de trame complet (16 texels) : kFull 1, kNone 0, kMasked 0 ou 1, kDithered : nombre de seuils <= alpha / 16
// (une quantification de 1/16 près de alpha).
float transparentShadowCoverage(const TransparentShadow &shadow) noexcept;

// Mode conseillé selon le mode de fusion du matériau : kOpaque -> kFull, kMask/kMaskA2C -> kMasked, kDithered, kBlend, kPremultiplied,
// kTransmission -> kDithered (ombre partielle ~ opacité), kAdditive -> kNone (une lueur ne fait pas d'ombre). Ce n'est qu'un conseil :
// ShadowFeature ne le prend jamais en silence (un caster sans réglage est une ombre pleine, comme avant REN-39).
TransparentShadowMode defaultTransparentShadowMode(BlendMode mode) noexcept;

// Noms stables : "none", "full", "masked", "dithered".
std::string_view transparentShadowModeName(TransparentShadowMode mode) noexcept;
core::Result<TransparentShadowMode> transparentShadowModeFromName(std::string_view name);

} // namespace orosmo::renderer
