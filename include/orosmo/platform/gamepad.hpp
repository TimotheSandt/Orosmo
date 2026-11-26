// PLAT-03 (sous-ensemble pour UI-09, A79 choix 7) : manettes à disposition standard (Xbox : A, B, X, Y, gâchettes hautes, croix,
// sticks). Boutons, axes et branchement à chaud seulement ; pas de vibration, de remappage ni de manette sans disposition connue
// (docs/GAPS.md). `Gamepads::poll` lit les manettes par GLFW (après `Window::init`) ; `simulate` remplace l'état d'un emplacement
// (tests, rejouage) sans manette ni fenêtre.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace orosmo::platform
{

// Boutons, dans l'ordre de la disposition standard (celui de GLFW_GAMEPAD_BUTTON_*).
enum class GamepadButton : std::uint8_t
{
    kA, // bas (Croix sur PlayStation)
    kB, // droite (Rond)
    kX, // gauche (Carré)
    kY, // haut (Triangle)
    kLeftBumper,
    kRightBumper,
    kBack,
    kStart,
    kGuide,
    kLeftThumb,
    kRightThumb,
    kDpadUp,
    kDpadRight,
    kDpadDown,
    kDpadLeft,
};
inline constexpr std::size_t kGamepadButtonCount = 15;

// Axes, de -1 à 1 (gâchettes : -1 relâchée, 1 enfoncée ; Y des sticks : positif vers le BAS, comme GLFW).
enum class GamepadAxis : std::uint8_t
{
    kLeftX,
    kLeftY,
    kRightX,
    kRightY,
    kLeftTrigger,
    kRightTrigger,
};
inline constexpr std::size_t kGamepadAxisCount = 6;

struct GamepadState
{
    bool connected = false;
    std::string name;
    std::array<bool, kGamepadButtonCount> buttons{};
    std::array<float, kGamepadAxisCount> axes{};

    bool pressed(GamepadButton button) const noexcept { return buttons[static_cast<std::size_t>(button)]; }
    float axis(GamepadAxis axis) const noexcept { return axes[static_cast<std::size_t>(axis)]; }
    void setButton(GamepadButton button, bool down) noexcept { buttons[static_cast<std::size_t>(button)] = down; }
    void setAxis(GamepadAxis axis, float value) noexcept { axes[static_cast<std::size_t>(axis)] = value; }
};

// Branchement ou débranchement observé par `poll`.
struct GamepadEvent
{
    int slot = 0;
    bool connected = false;
};

class Gamepads
{
  public:
    static constexpr int kMaxGamepads = 16; // GLFW_JOYSTICK_LAST + 1

    // Relit les manettes (à chaque image, après `Window::newFrame`, qui traite les événements du système). Une manette branchée
    // apparaît, une manette débranchée disparaît (événements dans `events()`). GLFW doit être initialisé (une fenêtre existe).
    // Les emplacements simulés ne sont pas relus.
    void poll();
    // Remplace l'état de l'emplacement `slot` (0..kMaxGamepads-1) jusqu'à `stopSimulation` ; un changement de `connected` produit
    // l'événement de branchement comme une vraie manette. Emplacement hors bornes : ignoré.
    void simulate(int slot, const GamepadState &state);
    void stopSimulation(int slot);

    // État de l'emplacement (déconnecté par défaut ; hors bornes : un état déconnecté).
    const GamepadState &state(int slot) const noexcept;
    // Première manette connectée (nulle : aucune).
    const GamepadState *first() const noexcept;
    // Branchements et débranchements dans l'ordre : ceux du dernier `poll` (qui vide la liste en commençant), puis ceux des
    // `simulate` / `stopSimulation` appelés depuis.
    const std::vector<GamepadEvent> &events() const noexcept { return events_; }

  private:
    void store(int slot, GamepadState state);

    std::array<GamepadState, kMaxGamepads> states_{};
    std::array<bool, kMaxGamepads> simulated_{};
    std::vector<GamepadEvent> events_;
};

} // namespace orosmo::platform
