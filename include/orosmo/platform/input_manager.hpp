#pragma once


#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>
#include <string>
#include <glm/glm.hpp>
#include <unordered_map>

namespace orosmo::platform
{

enum class InputState : std::uint8_t
{
    kRelease,    // Key not pressed
    kPressed,    // Key pressed
    kPressBegin, // Key was just pressed
    kPressEnd,   // Key was just released
};

enum class MouseButton : std::uint8_t
{
    kLeft,
    kRight,
    kMiddle,
    kX1,
    kX2,
    kX3,
    kX4,
    kX5,
};

struct MouseMoveData
{
    glm::vec2 position;
    glm::vec2 delta;
    glm::vec2 scroll;
};

enum class KeyLayout : std::uint8_t
{
    kUndefined = 0,
    kQwerty,
    kQwertz,
    kAzerty,
    kDvorak
};

enum class KeyButton : std::uint8_t
{
    // Alphabetic
    kA,
    kB,
    kC,
    kD,
    kE,
    kF,
    kG,
    kH,
    kI,
    kJ,
    kK,
    kL,
    kM,
    kN,
    kO,
    kP,
    kQ,
    kR,
    kS,
    kT,
    kU,
    kV,
    kW,
    kX,
    kY,
    kZ,
    // Function
    kF1,
    kF2,
    kF3,
    kF4,
    kF5,
    kF6,
    kF7,
    kF8,
    kF9,
    kF10,
    kF11,
    kF12,
    kF13,
    kF14,
    kF15,
    kF16,
    kF17,
    kF18,
    kF19,
    kF20,
    kF21,
    kF22,
    kF23,
    kF24,
    kF25,
    // Numeric
    kNum0,
    kNum1,
    kNum2,
    kNum3,
    kNum4,
    kNum5,
    kNum6,
    kNum7,
    kNum8,
    kNum9,
    // Special
    kSpace,
    kEnter,
    kTab,
    kEscape,
    kBackspace,
    kInsert,
    kDelete,
    kHome,
    kEnd,
    kPageUp,
    kPageDown,
    kLeft,
    kRight,
    kUp,
    kDown,
    kMinus,
    kEquals,
    kLeftBracket,
    kRightBracket,
    kBackslash,
    kSemicolon,
    kApostrophe,
    kComma,
    kPeriod,
    kSlash,
    kNumpad0,
    kNumpad1,
    kNumpad2,
    kNumpad3,
    kNumpad4,
    kNumpad5,
    kNumpad6,
    kNumpad7,
    kNumpad8,
    kNumpad9,
    kNumpadDecimal,
    kNumpadDivide,
    kNumpadMultiply,
    kNumpadSubtract,
    kNumpadAdd,
    kNumpadEnter,
    // Modifiers
    kCapsLock,
    kNumLock,
    kScrollLock,
    kLeftShift,
    kLeftControl,
    kLeftAlt,
    kLeftSuper,
    kRightShift,
    kRightControl,
    kRightAlt,
    kRightSuper
};

struct KeyButtonEvent
{
    InputState state;
    KeyButton key;
};

// Appui d'une touche pendant l'image, répétition automatique du système comprise (`repeat`), avec les modificateurs tenus.
struct KeyPress
{
    KeyButton key;
    bool repeat = false;
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    bool super = false;
    std::size_t textOffset = 0; // octets de getTextInput() saisis AVANT cet appui (« a » puis Retour arrière, ou l'inverse)
};

struct MouseButtonEvent
{
    InputState state;
    MouseButton button;
};

struct InputEvent
{
    std::vector<KeyButtonEvent> keys;
    std::vector<MouseButtonEvent> mouseButtons;
    MouseMoveData mouseMoveData;
    std::string textInput;
};

struct InputAction
{
    std::vector<KeyButton> keys;
    std::vector<MouseButton> mouseButtons;
};

class InputManager
{

  private:
    InputManager(void *nativeWindow);
    ~InputManager();

    InputManager(const InputManager &) = delete;
    InputManager &operator=(const InputManager &) = delete;

    friend struct std::default_delete<InputManager>;

  public:
    static InputManager &getInstance(void *nativeWindow);
    static void removeInstance(void *nativeWindow);

    void init();
    void update();

    // Détecte la disposition du clavier et la sélectionne. Windows : disposition active du système. Ailleurs (Linux,
    // macOS) : noms que le système donne aux touches physiques (glfwGetKeyName) ; GLFW doit être initialisé.
    static void autoDetectKeyLayout();
    // Reconnaît la disposition depuis `keyName(code GLFW de la touche à la position US)` = caractère imprimé sur cette
    // touche (nullptr si inconnu). Disposition non reconnue ou noms indisponibles : QWERTY. Sans effet de bord (testable).
    static KeyLayout detectKeyLayoutFromKeyNames(const std::function<const char *(int)> &keyName);
    static void setKeyLayout(KeyLayout layout);
    static KeyLayout getKeyLayout() { return keyLayout; }
    // Code GLFW de la touche physique qui porte la touche logique `key` dans la disposition active ; GLFW_KEY_UNKNOWN
    // si la touche n'a pas de correspondance.
    static int getKeyCode(KeyButton key);

#pragma region Getters
  public:
    InputState getKeyState(KeyButton key) const
    {
        if (keyStateMap.find(key) == keyStateMap.end())
        {
            return InputState::kRelease;
        }
        return keyStateMap.at(key);
    }
    bool isKeyRelease(KeyButton key) const { return getKeyState(key) == InputState::kRelease; }
    bool isKeyPressed(KeyButton key) const { return getKeyState(key) == InputState::kPressed; }
    bool isKeyJustPressed(KeyButton key) const { return getKeyState(key) == InputState::kPressBegin; }
    bool isKeyJustReleased(KeyButton key) const { return getKeyState(key) == InputState::kPressEnd; }

    InputState getMouseButtonState(MouseButton button) const
    {
        if (mouseButtonStateMap.find(button) == mouseButtonStateMap.end())
        {
            return InputState::kRelease;
        }
        return mouseButtonStateMap.at(button);
    }
    bool isMouseButtonRelease(MouseButton button) const { return getMouseButtonState(button) == InputState::kRelease; }
    bool isMouseButtonPressed(MouseButton button) const { return getMouseButtonState(button) == InputState::kPressed; }
    bool isMouseButtonJustPressed(MouseButton button) const { return getMouseButtonState(button) == InputState::kPressBegin; }
    bool isMouseButtonJustReleased(MouseButton button) const { return getMouseButtonState(button) == InputState::kPressEnd; }

    glm::vec2 getMousePosition() const { return mouseMoveData.position; }
    glm::vec2 getMouseDelta() const { return mouseMoveData.delta; }
    glm::vec2 getMouseScroll() const { return mouseMoveData.scroll; }
    // PLAT-05 : texte saisi depuis le `update` précédent, en UTF-8 (caractères Unicode, répétition du clavier comprise ; pas
    // de composition IME ni de presse-papiers : docs/GAPS.md).
    const std::string &getTextInput() const { return textInput; }
    // Appuis depuis le `update` précédent, dans l'ordre, répétitions du clavier comprises (Retour arrière ou flèche tenus).
    const std::vector<KeyPress> &getKeyPresses() const { return keyPresses; }
    // Raccourcis appuyés depuis le `update` précédent ("Ctrl+S", "Ctrl+Shift+Z", "F5") : une touche avec Ctrl, Alt ou Super, ou
    // une touche de fonction. La lettre est celle de la disposition active (AZERTY : Ctrl + la touche marquée A donne "Ctrl+A").
    std::vector<std::string> getShortcuts() const;
    // Nom d'une touche dans un raccourci ("A", "F5", "Delete", "PageUp"...) ; nullptr pour un modificateur ou une touche sans nom.
    static const char *keyName(KeyButton key);
    // Raccourci d'un appui (vide s'il n'en est pas un) ; sans effet de bord (testable).
    static std::string shortcutName(const KeyPress &press);
    // Ajoute le point de code `codepoint` à `out` en UTF-8 (ignoré s'il n'est pas un caractère Unicode valide).
    static void appendUtf8(std::string &out, unsigned int codepoint);

    InputEvent getCurrentInputEvent() const { return inputEvent; };
    bool isCurrentInputEventEmpty();

#pragma endregion Getters

  private:
    void updateInputEvent();

#pragma region Actions
  private:
    static InputAction getInputAction(InputAction inputAction, KeyButton key)
    {
        inputAction.keys.push_back(key);
        return inputAction;
    }
    static InputAction getInputAction(InputAction inputAction, MouseButton mouseButton)
    {
        inputAction.mouseButtons.push_back(mouseButton);
        return inputAction;
    }
    template <typename... Args, typename T> static InputAction getInputAction(InputAction inputAction, T arg, Args... args)
    {
        inputAction = getInputAction(inputAction, arg);
        return getInputAction(inputAction, args...);
    }

  public:
    InputAction getInputActionOfAction(const std::string &action) { return mapActionToInputAction[action]; }

    static void bindActionToInput(const std::string &action, InputAction inputAction);
    template <typename... Args> static void bindActionToInput(const std::string &action, Args... args)
    {
        InputAction inputAction = getInputAction(InputAction(), args...);
        bindActionToInput(action, std::move(inputAction));
    }

    static void unbindAction(const std::string &action) { mapActionToInputAction.erase(action); }

    static bool isActionBound(const std::string &action) { return mapActionToInputAction.find(action) != mapActionToInputAction.end(); }
    bool isActionActive(const std::string &action);

#pragma endregion Actions

  private:
    void *window; // GLFWwindow*, opaque hors de src/platform/

    InputEvent inputEvent;
    std::unordered_map<KeyButton, InputState> keyStateMap;
    std::unordered_map<MouseButton, InputState> mouseButtonStateMap;
    MouseMoveData mouseMoveData;
    std::string textInput;
    std::vector<KeyPress> keyPresses;

  private:
    static std::unordered_map<std::string, InputAction> mapActionToInputAction;

    static KeyLayout keyLayout;

    static std::unordered_map<KeyButton, int> keyMap;
    static std::unordered_map<MouseButton, int> mouseButtonMap;

    static std::unordered_map<void *, std::unique_ptr<InputManager>> inputManagers;
};

} // namespace orosmo::platform
