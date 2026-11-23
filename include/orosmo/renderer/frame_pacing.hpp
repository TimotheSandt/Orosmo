// Cadence d'images : trois modes (synchronisation verticale, limite à N images par seconde, sans limite), changeables à chaud.
// `FramePacer` ne connaît ni la fenêtre ni le périphérique : l'application lui donne deux fonctions (présentation synchronisée,
// limiteur d'images) qu'elle relie à `rhi::Device::setVSync` et `platform::Window::setMaxFPS`. Sans fenêtre (mode sans affichage),
// l'application n'en crée pas : il n'y a ni vsync ni limite.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace orosmo::renderer
{

enum class FramePacingMode : std::uint8_t
{
    kVSync,     // présentation synchronisée sur le rafraîchissement de l'écran, pas de limiteur
    kLimited,   // vsync coupée, limiteur à `fpsLimit` images par seconde
    kUnlimited, // vsync coupée, pas de limiteur
};

struct FramePacing
{
    FramePacingMode mode = FramePacingMode::kVSync; // défaut : vsync
    int fpsLimit = 60;                              // utilisé par kLimited seulement ; borné à [kMinFpsLimit, kMaxFpsLimit]

    bool operator==(const FramePacing &) const = default;
};

inline constexpr int kMinFpsLimit = 10;
inline constexpr int kMaxFpsLimit = 1000;

// « vsync », « limited », « unlimited ».
std::string_view framePacingModeName(FramePacingMode mode) noexcept;
// Texte du panneau de performances : « vsync », « limit 120 FPS », « unlimited ».
std::string describeFramePacing(const FramePacing &pacing);
// Limite ramenée dans [kMinFpsLimit, kMaxFpsLimit].
int clampFpsLimit(int fps) noexcept;

class FramePacer
{
  public:
    using VSyncSetter = std::function<void(bool enabled)>;
    using LimitSetter = std::function<void(int maxFps)>; // 0 : pas de limite

    FramePacer() = default;
    FramePacer(VSyncSetter setVSync, LimitSetter setLimit) : setVSync_(std::move(setVSync)), setLimit_(std::move(setLimit)) {}

    // Relie les deux fonctions (peut être appelé avant ou après setMode) et applique le mode courant.
    void bind(VSyncSetter setVSync, LimitSetter setLimit);
    // Change le mode à chaud : la limite est bornée ; une fonction appelée seulement si sa valeur change (pas de travail inutile à chaque image).
    void setMode(FramePacing pacing);
    void setMode(FramePacingMode mode) { setMode(FramePacing{mode, current_.fpsLimit}); }
    void setFpsLimit(int fps) { setMode(FramePacing{FramePacingMode::kLimited, fps}); }
    const FramePacing &current() const noexcept { return current_; }
    bool vsyncActive() const noexcept { return current_.mode == FramePacingMode::kVSync; }
    // Limite effective du limiteur : 0 hors kLimited.
    int effectiveLimit() const noexcept { return current_.mode == FramePacingMode::kLimited ? current_.fpsLimit : 0; }

  private:
    void apply();

    VSyncSetter setVSync_;
    LimitSetter setLimit_;
    FramePacing current_;
    int appliedVSync_ = -1; // -1 : jamais appliqué
    int appliedLimit_ = -1;
};

} // namespace orosmo::renderer
