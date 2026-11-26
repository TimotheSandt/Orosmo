// UI-09 : navigation de l'interface à la manette. Traduit l'état d'une manette (platform::GamepadState) en commandes de navigation
// de l'image (InputFrame::navigation) : la croix et le stick gauche déplacent le focus (répétés tant qu'ils sont tenus), A active
// (Entrée), B annule (Échap), RB et LB passent au widget suivant ou précédent (Tab, Maj+Tab). L'apparence du focus vient du thème
// (style "focusRing") ; seuls les réglages d'entrée (zone morte, répétition) sont ici.
#pragma once

#include "orosmo/ui/ui.hpp"

#include <orosmo/platform/gamepad.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace orosmo::ui
{

struct GamepadNavigationSettings
{
    float deadZone = 0.5f;        // inclinaison du stick (0..1) au-delà de laquelle il vaut une flèche
    float repeatDelay = 0.4f;     // secondes avant la première répétition d'une direction tenue
    float repeatInterval = 0.12f; // secondes entre deux répétitions
};

class GamepadNavigator
{
  public:
    GamepadNavigationSettings settings;

    // Ajoute les commandes de l'image à `frame.navigation` (durée de l'image : `frame.deltaTime`). Manette nulle ou débranchée :
    // aucune commande, et les boutons tenus sont oubliés. Un appui compte à l'image où il commence (front montant).
    void apply(const platform::GamepadState *pad, InputFrame &frame);

  private:
    enum Command : std::uint8_t
    {
        kUp,
        kDown,
        kLeft,
        kRight,
        kAccept,
        kCancel,
        kNext,
        kPrevious,
        kCommandCount,
    };
    std::array<bool, kCommandCount> held_{};
    std::array<float, kCommandCount> heldTime_{};
    std::array<float, kCommandCount> nextRepeat_{};
};

} // namespace orosmo::ui
