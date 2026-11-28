#include <orosmo/platform/gamepad.hpp>

#include <orosmo/core/log.hpp>

#include <GLFW/glfw3.h>

#include <cstdlib>
#include <utility>

namespace orosmo::platform
{

static_assert(static_cast<int>(GamepadButton::kA) == GLFW_GAMEPAD_BUTTON_A);
static_assert(static_cast<int>(GamepadButton::kLeftBumper) == GLFW_GAMEPAD_BUTTON_LEFT_BUMPER);
static_assert(static_cast<int>(GamepadButton::kDpadLeft) == GLFW_GAMEPAD_BUTTON_DPAD_LEFT);
static_assert(kGamepadButtonCount == GLFW_GAMEPAD_BUTTON_LAST + 1);
static_assert(static_cast<int>(GamepadAxis::kLeftY) == GLFW_GAMEPAD_AXIS_LEFT_Y);
static_assert(static_cast<int>(GamepadAxis::kRightTrigger) == GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER);
static_assert(kGamepadAxisCount == GLFW_GAMEPAD_AXIS_LAST + 1);
static_assert(Gamepads::kMaxGamepads == GLFW_JOYSTICK_LAST + 1);

namespace
{
const GamepadState kDisconnected{};

bool validSlot(int slot) noexcept { return slot >= 0 && slot < Gamepads::kMaxGamepads; }

// Manettes absentes de la base de GLFW. Une ligne par GUID ; vérifier les numéros contre les codes du noyau avant d'ajouter.
constexpr const char *kEngineMappings =
    // Xbox Wireless Controller en Bluetooth (045e:0b13, micrologiciel 5.09), pilote hid de Linux.
    "050000005e040000130b000009050000,Xbox Wireless Controller,a:b0,b:b1,x:b3,y:b4,leftshoulder:b6,rightshoulder:b7,back:b10,"
    "start:b11,guide:b12,leftstick:b13,rightstick:b14,dpup:h0.1,dpright:h0.2,dpdown:h0.4,dpleft:h0.8,leftx:a0,lefty:a1,"
    "rightx:a2,righty:a3,righttrigger:a4,lefttrigger:a5,platform:Linux,\n";
} // namespace

std::string extraGamepadMappings(const char *environment)
{
    std::string text = kEngineMappings;
    if (environment != nullptr && *environment != '\0')
    {
        text += environment;
        if (text.back() != '\n')
            text += '\n';
    }
    return text;
}

void Gamepads::store(int slot, GamepadState state)
{
    GamepadState &current = states_[static_cast<std::size_t>(slot)];
    if (current.connected != state.connected)
        events_.push_back({slot, state.connected});
    if (!state.connected)
        state = GamepadState{}; // une manette débranchée n'a ni bouton tenu ni axe
    current = std::move(state);
}

void Gamepads::poll()
{
    events_.clear();
    if (!mappingsAdded_)
    {
        mappingsAdded_ = true;
        const std::string mappings = extraGamepadMappings(std::getenv("SDL_GAMECONTROLLERCONFIG"));
        if (glfwUpdateGamepadMappings(mappings.c_str()) != GLFW_TRUE)
            LOG_WARNING("Gamepads: cannot add the gamepad mappings (check SDL_GAMECONTROLLERCONFIG)");
    }
    for (int slot = 0; slot < kMaxGamepads; ++slot)
    {
        if (simulated_[static_cast<std::size_t>(slot)])
            continue;
        GamepadState state;
        GLFWgamepadstate raw{};
        // Seules les manettes à disposition connue (base de correspondances SDL intégrée à GLFW) : un joystick quelconque n'a pas
        // de croix ni de bouton A identifiables.
        if (glfwJoystickIsGamepad(GLFW_JOYSTICK_1 + slot) == GLFW_TRUE && glfwGetGamepadState(GLFW_JOYSTICK_1 + slot, &raw) == GLFW_TRUE)
        {
            state.connected = true;
            const char *name = glfwGetGamepadName(GLFW_JOYSTICK_1 + slot);
            state.name = name != nullptr ? name : "";
            for (std::size_t i = 0; i < kGamepadButtonCount; ++i)
                state.buttons[i] = raw.buttons[i] == GLFW_PRESS;
            for (std::size_t i = 0; i < kGamepadAxisCount; ++i)
                state.axes[i] = raw.axes[i];
        }
        // Branché mais sans disposition connue : dit une fois pourquoi il ne répond pas (le GUID sert à écrire sa correspondance).
        const bool ignored = !state.connected && glfwJoystickPresent(GLFW_JOYSTICK_1 + slot) == GLFW_TRUE;
        if (ignored && !ignored_[static_cast<std::size_t>(slot)])
        {
            const char *name = glfwGetJoystickName(GLFW_JOYSTICK_1 + slot);
            const char *guid = glfwGetJoystickGUID(GLFW_JOYSTICK_1 + slot);
            LOG_INFO("Gamepads: \"", name != nullptr ? name : "?", "\" ignored, no known layout (GUID ", guid != nullptr ? guid : "?",
                     "; add one with SDL_GAMECONTROLLERCONFIG)");
        }
        ignored_[static_cast<std::size_t>(slot)] = ignored;
        store(slot, std::move(state));
    }
}

void Gamepads::simulate(int slot, const GamepadState &state)
{
    if (!validSlot(slot))
        return;
    simulated_[static_cast<std::size_t>(slot)] = true;
    store(slot, state);
}

void Gamepads::stopSimulation(int slot)
{
    if (!validSlot(slot) || !simulated_[static_cast<std::size_t>(slot)])
        return;
    simulated_[static_cast<std::size_t>(slot)] = false;
    store(slot, GamepadState{}); // débranchée jusqu'au prochain poll
}

const GamepadState &Gamepads::state(int slot) const noexcept
{
    return validSlot(slot) ? states_[static_cast<std::size_t>(slot)] : kDisconnected;
}

const GamepadState *Gamepads::first() const noexcept
{
    for (const GamepadState &s : states_)
        if (s.connected)
            return &s;
    return nullptr;
}

} // namespace orosmo::platform
