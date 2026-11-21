#include <orosmo/platform/input_manager.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <functional>
#include <iostream>
#include <utility>
#include <orosmo/core/log.hpp>

namespace orosmo::platform
{

KeyLayout InputManager::keyLayout = KeyLayout::kUndefined;
std::unordered_map<KeyButton, int> InputManager::keyMap;
std::unordered_map<MouseButton, int> InputManager::mouseButtonMap;
std::unordered_map<std::string, InputAction> InputManager::mapActionToInputAction;

std::unordered_map<void *, std::unique_ptr<InputManager>> InputManager::inputManagers;

namespace
{
// Molette : GLFW la livre par rappel ; on cumule jusqu'au prochain `update`, qui l'expose dans `MouseMoveData::scroll`.
std::unordered_map<void *, glm::vec2> &pendingScroll()
{
    static std::unordered_map<void *, glm::vec2> pending;
    return pending;
}

// PLAT-05 : texte saisi (rappel de caractères de GLFW, répétition du clavier comprise), cumulé jusqu'au prochain `update`.
std::unordered_map<void *, std::string> &pendingText()
{
    static std::unordered_map<void *, std::string> pending;
    return pending;
}

// Appuis et répétitions du clavier (rappel de touches de GLFW : code GLFW, répétition, modificateurs), jusqu'au prochain `update`.
struct RawKeyPress
{
    int key;
    bool repeat;
    int mods;
    std::size_t textBefore; // octets de texte saisis avant cet appui depuis le dernier update (ordre texte / touches)
};
std::unordered_map<void *, std::vector<RawKeyPress>> &pendingKeys()
{
    static std::unordered_map<void *, std::vector<RawKeyPress>> pending;
    return pending;
}

GLFWwindow *native(void *handle) { return static_cast<GLFWwindow *>(handle); }
} // namespace

void InputManager::appendUtf8(std::string &out, unsigned int codepoint)
{
    if (codepoint > 0x10FFFFU || (codepoint >= 0xD800U && codepoint <= 0xDFFFU))
    {
        return; // hors d'Unicode ou demi-code de substitution : ignoré
    }
    if (codepoint < 0x80U)
    {
        out.push_back(static_cast<char>(codepoint));
    }
    else if (codepoint < 0x800U)
    {
        out.push_back(static_cast<char>(0xC0U | (codepoint >> 6U)));
        out.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
    }
    else if (codepoint < 0x10000U)
    {
        out.push_back(static_cast<char>(0xE0U | (codepoint >> 12U)));
        out.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
    }
    else
    {
        out.push_back(static_cast<char>(0xF0U | (codepoint >> 18U)));
        out.push_back(static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
    }
}

InputManager::InputManager(void *nativeWindow) : window(nativeWindow)
{
    if (nativeWindow != nullptr)
    {
        glfwSetScrollCallback(native(nativeWindow),
                              [](GLFWwindow *w, double dx, double dy)
                              {
                                  pendingScroll()[w] += glm::vec2(static_cast<float>(dx), static_cast<float>(dy));
                              });
        glfwSetCharCallback(native(nativeWindow), [](GLFWwindow *w, unsigned int codepoint) { appendUtf8(pendingText()[w], codepoint); });
        glfwSetKeyCallback(native(nativeWindow),
                           [](GLFWwindow *w, int key, int, int action, int mods)
                           {
                               if (action == GLFW_PRESS || action == GLFW_REPEAT)
                                   pendingKeys()[w].push_back({key, action == GLFW_REPEAT, mods, pendingText()[w].size()});
                           });
    }
    init();
}

InputManager::~InputManager() = default;

InputManager &InputManager::getInstance(void *window)
{
    if (keyLayout == KeyLayout::kUndefined)
    {
        autoDetectKeyLayout();
    }
    if (inputManagers.find(window) == inputManagers.end())
    {
        inputManagers[window] = std::unique_ptr<InputManager>(new InputManager(window));
    }
    return *inputManagers[window];
}

void InputManager::removeInstance(void *window)
{
    if (window == nullptr)
    {
        return;
    }
    if (inputManagers.find(window) != inputManagers.end())
    {
        glfwSetScrollCallback(native(window), nullptr);
        glfwSetCharCallback(native(window), nullptr);
        glfwSetKeyCallback(native(window), nullptr);
    }
    pendingScroll().erase(window);
    pendingText().erase(window);
    pendingKeys().erase(window);
    inputManagers.erase(window);
}

void InputManager::init()
{
    // Keyboard
    for (auto it = keyMap.begin(); it != keyMap.end(); ++it)
    {
        keyStateMap[it->first] = InputState::kRelease;
    }
    // Mouse
    for (auto it = mouseButtonMap.begin(); it != mouseButtonMap.end(); ++it)
    {
        mouseButtonStateMap[it->first] = InputState::kRelease;
    }

    // Mouse position
    // GLFW écrit des double : lus dans des double puis convertis (écrire un double dans un float débordait sur le champ suivant).
    double cursorX = 0.0;
    double cursorY = 0.0;
    glfwGetCursorPos(native(window), &cursorX, &cursorY);
    this->mouseMoveData.position = glm::vec2(static_cast<float>(cursorX), static_cast<float>(cursorY));

    // Mouse delta
    this->mouseMoveData.delta = glm::vec2(0.0f, 0.0f);
}

// update method implementation
void InputManager::update()
{
    // Keyboard
    for (auto it = keyStateMap.begin(); it != keyStateMap.end(); ++it)
    {
        bool isPressed = glfwGetKey(native(window), keyMap[it->first]) == GLFW_PRESS;
        it->second = isPressed ? ((it->second == InputState::kRelease || it->second == InputState::kPressEnd) ? InputState::kPressBegin
                                                                                                            : InputState::kPressed)
                               : ((it->second == InputState::kPressed || it->second == InputState::kPressBegin) ? InputState::kPressEnd
                                                                                                              : InputState::kRelease);
    }

    // Mouse
    for (auto it = mouseButtonStateMap.begin(); it != mouseButtonStateMap.end(); ++it)
    {
        bool isPressed = glfwGetMouseButton(native(window), mouseButtonMap[it->first]) == GLFW_PRESS;
        it->second = isPressed ? ((it->second == InputState::kRelease || it->second == InputState::kPressEnd) ? InputState::kPressBegin
                                                                                                            : InputState::kPressed)
                               : ((it->second == InputState::kPressed || it->second == InputState::kPressBegin) ? InputState::kPressEnd
                                                                                                              : InputState::kRelease);
    }

    // Mouse Position
    glm::vec2 lastMousePos = this->mouseMoveData.position;
    // GLFW écrit des double : lus dans des double puis convertis (écrire un double dans un float débordait sur le champ suivant).
    double cursorX = 0.0;
    double cursorY = 0.0;
    glfwGetCursorPos(native(window), &cursorX, &cursorY);
    this->mouseMoveData.position = glm::vec2(static_cast<float>(cursorX), static_cast<float>(cursorY));

    // Mouse delta
    this->mouseMoveData.delta = this->mouseMoveData.position - lastMousePos;

    // Mouse scroll
    {
        auto &pending = pendingScroll();
        const auto found = pending.find(window);
        this->mouseMoveData.scroll = found != pending.end() ? found->second : glm::vec2(0.0f, 0.0f);
        if (found != pending.end())
        {
            found->second = glm::vec2(0.0f, 0.0f);
        }
    }
    {
        auto &pending = pendingText();
        const auto found = pending.find(window);
        this->textInput.clear();
        if (found != pending.end())
        {
            this->textInput.swap(found->second);
        }
    }
    {
        // Code GLFW -> touche logique de la disposition active.
        this->keyPresses.clear();
        auto &pending = pendingKeys();
        if (const auto found = pending.find(window); found != pending.end())
        {
            for (const RawKeyPress &raw : found->second)
            {
                for (const auto &[button, code] : keyMap)
                {
                    if (code == raw.key)
                    {
                        this->keyPresses.push_back({button, raw.repeat, (raw.mods & GLFW_MOD_SHIFT) != 0, (raw.mods & GLFW_MOD_CONTROL) != 0,
                                                    (raw.mods & GLFW_MOD_ALT) != 0, (raw.mods & GLFW_MOD_SUPER) != 0, raw.textBefore});
                        break;
                    }
                }
            }
            found->second.clear();
        }
    }
    updateInputEvent();
}

const char *InputManager::keyName(KeyButton key)
{
    static const char *const kLetters[] = {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
                                           "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z"};
    static const char *const kFunctions[] = {"F1",  "F2",  "F3",  "F4",  "F5",  "F6",  "F7",  "F8",  "F9",  "F10", "F11", "F12", "F13",
                                             "F14", "F15", "F16", "F17", "F18", "F19", "F20", "F21", "F22", "F23", "F24", "F25"};
    static const char *const kDigits[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
    const auto k = static_cast<int>(key);
    if (k >= static_cast<int>(KeyButton::kA) && k <= static_cast<int>(KeyButton::kZ))
        return kLetters[k - static_cast<int>(KeyButton::kA)];
    if (k >= static_cast<int>(KeyButton::kF1) && k <= static_cast<int>(KeyButton::kF25))
        return kFunctions[k - static_cast<int>(KeyButton::kF1)];
    if (k >= static_cast<int>(KeyButton::kNum0) && k <= static_cast<int>(KeyButton::kNum9))
        return kDigits[k - static_cast<int>(KeyButton::kNum0)];
    if (k >= static_cast<int>(KeyButton::kNumpad0) && k <= static_cast<int>(KeyButton::kNumpad9))
        return kDigits[k - static_cast<int>(KeyButton::kNumpad0)];
    switch (key)
    {
    case KeyButton::kSpace: return "Space";
    case KeyButton::kEnter:
    case KeyButton::kNumpadEnter: return "Enter";
    case KeyButton::kTab: return "Tab";
    case KeyButton::kEscape: return "Escape";
    case KeyButton::kBackspace: return "Backspace";
    case KeyButton::kInsert: return "Insert";
    case KeyButton::kDelete: return "Delete";
    case KeyButton::kHome: return "Home";
    case KeyButton::kEnd: return "End";
    case KeyButton::kPageUp: return "PageUp";
    case KeyButton::kPageDown: return "PageDown";
    case KeyButton::kLeft: return "Left";
    case KeyButton::kRight: return "Right";
    case KeyButton::kUp: return "Up";
    case KeyButton::kDown: return "Down";
    case KeyButton::kMinus:
    case KeyButton::kNumpadSubtract: return "-";
    case KeyButton::kEquals: return "=";
    case KeyButton::kNumpadAdd: return "+";
    default: return nullptr;
    }
}

std::string InputManager::shortcutName(const KeyPress &press)
{
    const char *name = keyName(press.key);
    if (name == nullptr)
        return {};
    const bool function = press.key >= KeyButton::kF1 && press.key <= KeyButton::kF25;
    if (!press.ctrl && !press.alt && !press.super && !function)
        return {}; // une lettre seule est du texte, une flèche seule une touche d'édition
    std::string out;
    if (press.ctrl)
        out += "Ctrl+";
    if (press.alt)
        out += "Alt+";
    if (press.shift)
        out += "Shift+";
    if (press.super)
        out += "Super+";
    return out + name;
}

std::vector<std::string> InputManager::getShortcuts() const
{
    std::vector<std::string> out;
    for (const KeyPress &press : keyPresses)
        if (!press.repeat)
            if (std::string name = shortcutName(press); !name.empty())
                out.push_back(std::move(name));
    return out;
}

void InputManager::autoDetectKeyLayout()
{
    KeyLayout layout = KeyLayout::kQwerty;
#ifdef _WIN32
    HKL keyboardLayout = GetKeyboardLayout(0);
    DWORD layoutID = LOWORD(reinterpret_cast<DWORD_PTR>(keyboardLayout));

    switch (layoutID)
    {                // Use the LOWORD for the switch
        case 0x0409: // US English (Primary QWERTY)
            if (reinterpret_cast<UINT_PTR>(keyboardLayout) == 0x00010409)
            {
                layout = KeyLayout::kDvorak;
            }
            else
            {
                layout = KeyLayout::kQwerty;
            }
            break;

        case 0x040C: // French (AZERTY)
            layout = KeyLayout::kAzerty;
            break;

        case 0x0407: // German (QWERTZ)
            layout = KeyLayout::kQwertz;
            break;

        default:
            layout = KeyLayout::kQwerty;
            break;
    }
#else
    // Linux, macOS : GLFW donne le caractère imprimé sur chaque touche physique d'après la disposition du système
    // (XKB, Wayland ou X11 ; TIS sur macOS). C'est ce que la démo utilisait mal : QWERTY en dur donnait Z -> « w » sur AZERTY.
    layout = detectKeyLayoutFromKeyNames([](int key) { return glfwGetKeyName(key, 0); });
    const char *name = layout == KeyLayout::kAzerty  ? "AZERTY"
                       : layout == KeyLayout::kQwertz ? "QWERTZ"
                       : layout == KeyLayout::kDvorak ? "Dvorak"
                                                     : "QWERTY";
    LOG_INFO("Keyboard layout detected: ", name);
#endif

    InputManager::setKeyLayout(layout);
}

KeyLayout InputManager::detectKeyLayoutFromKeyNames(const std::function<const char *(int)> &keyName)
{
    auto is = [&](int key, const char *expected) {
        const char *name = keyName(key);
        return name != nullptr && std::string(name) == expected;
    };
    // Les codes GLFW désignent la position US de la touche : en AZERTY la touche en position Q porte « a », en QWERTZ
    // celle en position Y porte « z », en Dvorak celle en position Q porte « ' ».
    if (is(GLFW_KEY_Q, "a") && is(GLFW_KEY_W, "z"))
        return KeyLayout::kAzerty;
    if (is(GLFW_KEY_Y, "z") && is(GLFW_KEY_Z, "y"))
        return KeyLayout::kQwertz;
    if (is(GLFW_KEY_Q, "'") && is(GLFW_KEY_W, ","))
        return KeyLayout::kDvorak;
    return KeyLayout::kQwerty;
}

int InputManager::getKeyCode(KeyButton key)
{
    auto it = keyMap.find(key);
    return it == keyMap.end() ? GLFW_KEY_UNKNOWN : it->second;
}

#pragma region Key Layout Mapping
void InputManager::setKeyLayout(KeyLayout layout)
{
    InputManager::keyLayout = layout;
    InputManager::keyMap.clear();
    InputManager::mouseButtonMap.clear();

    switch (layout)
    {
#pragma region AZERTY
        case KeyLayout::kAzerty:
            // Lettres AZERTY
            keyMap[KeyButton::kA] = GLFW_KEY_Q;
            keyMap[KeyButton::kB] = GLFW_KEY_B;
            keyMap[KeyButton::kC] = GLFW_KEY_C;
            keyMap[KeyButton::kD] = GLFW_KEY_D;
            keyMap[KeyButton::kE] = GLFW_KEY_E;
            keyMap[KeyButton::kF] = GLFW_KEY_F;
            keyMap[KeyButton::kG] = GLFW_KEY_G;
            keyMap[KeyButton::kH] = GLFW_KEY_H;
            keyMap[KeyButton::kI] = GLFW_KEY_I;
            keyMap[KeyButton::kJ] = GLFW_KEY_J;
            keyMap[KeyButton::kK] = GLFW_KEY_K;
            keyMap[KeyButton::kL] = GLFW_KEY_L;
            keyMap[KeyButton::kM] = GLFW_KEY_SEMICOLON;
            keyMap[KeyButton::kN] = GLFW_KEY_N;
            keyMap[KeyButton::kO] = GLFW_KEY_O;
            keyMap[KeyButton::kP] = GLFW_KEY_P;
            keyMap[KeyButton::kQ] = GLFW_KEY_A;
            keyMap[KeyButton::kR] = GLFW_KEY_R;
            keyMap[KeyButton::kS] = GLFW_KEY_S;
            keyMap[KeyButton::kT] = GLFW_KEY_T;
            keyMap[KeyButton::kU] = GLFW_KEY_U;
            keyMap[KeyButton::kV] = GLFW_KEY_V;
            keyMap[KeyButton::kW] = GLFW_KEY_Z;
            keyMap[KeyButton::kX] = GLFW_KEY_X;
            keyMap[KeyButton::kY] = GLFW_KEY_Y;
            keyMap[KeyButton::kZ] = GLFW_KEY_W;

            // Symboles AZERTY
            keyMap[KeyButton::kMinus] = GLFW_KEY_MINUS;
            keyMap[KeyButton::kEquals] = GLFW_KEY_EQUAL;
            keyMap[KeyButton::kLeftBracket] = GLFW_KEY_LEFT_BRACKET;
            keyMap[KeyButton::kRightBracket] = GLFW_KEY_RIGHT_BRACKET;
            keyMap[KeyButton::kSemicolon] = GLFW_KEY_COMMA;
            keyMap[KeyButton::kComma] = GLFW_KEY_M;
            keyMap[KeyButton::kPeriod] = GLFW_KEY_PERIOD;
            keyMap[KeyButton::kSlash] = GLFW_KEY_SLASH;
            keyMap[KeyButton::kBackslash] = GLFW_KEY_BACKSLASH;
            keyMap[KeyButton::kApostrophe] = GLFW_KEY_APOSTROPHE;
            break;
#pragma endregion AZERTY

#pragma region QWERTZ
        case KeyLayout::kQwertz:
            // Lettres QWERTZ (seuls Y et Z sont échangés par rapport à QWERTY)
            keyMap[KeyButton::kA] = GLFW_KEY_A;
            keyMap[KeyButton::kB] = GLFW_KEY_B;
            keyMap[KeyButton::kC] = GLFW_KEY_C;
            keyMap[KeyButton::kD] = GLFW_KEY_D;
            keyMap[KeyButton::kE] = GLFW_KEY_E;
            keyMap[KeyButton::kF] = GLFW_KEY_F;
            keyMap[KeyButton::kG] = GLFW_KEY_G;
            keyMap[KeyButton::kH] = GLFW_KEY_H;
            keyMap[KeyButton::kI] = GLFW_KEY_I;
            keyMap[KeyButton::kJ] = GLFW_KEY_J;
            keyMap[KeyButton::kK] = GLFW_KEY_K;
            keyMap[KeyButton::kL] = GLFW_KEY_L;
            keyMap[KeyButton::kM] = GLFW_KEY_M;
            keyMap[KeyButton::kN] = GLFW_KEY_N;
            keyMap[KeyButton::kO] = GLFW_KEY_O;
            keyMap[KeyButton::kP] = GLFW_KEY_P;
            keyMap[KeyButton::kQ] = GLFW_KEY_Q;
            keyMap[KeyButton::kR] = GLFW_KEY_R;
            keyMap[KeyButton::kS] = GLFW_KEY_S;
            keyMap[KeyButton::kT] = GLFW_KEY_T;
            keyMap[KeyButton::kU] = GLFW_KEY_U;
            keyMap[KeyButton::kV] = GLFW_KEY_V;
            keyMap[KeyButton::kW] = GLFW_KEY_W;
            keyMap[KeyButton::kX] = GLFW_KEY_X;
            keyMap[KeyButton::kY] = GLFW_KEY_Z;
            keyMap[KeyButton::kZ] = GLFW_KEY_Y;

            // Symboles QWERTZ
            keyMap[KeyButton::kMinus] = GLFW_KEY_MINUS;
            keyMap[KeyButton::kEquals] = GLFW_KEY_EQUAL;
            keyMap[KeyButton::kLeftBracket] = GLFW_KEY_LEFT_BRACKET;
            keyMap[KeyButton::kRightBracket] = GLFW_KEY_RIGHT_BRACKET;
            keyMap[KeyButton::kSemicolon] = GLFW_KEY_SEMICOLON;
            keyMap[KeyButton::kComma] = GLFW_KEY_COMMA;
            keyMap[KeyButton::kPeriod] = GLFW_KEY_PERIOD;
            keyMap[KeyButton::kSlash] = GLFW_KEY_SLASH;
            keyMap[KeyButton::kBackslash] = GLFW_KEY_BACKSLASH;
            keyMap[KeyButton::kApostrophe] = GLFW_KEY_APOSTROPHE;
            break;
#pragma endregion QWERTZ

#pragma region DVORAK
        case KeyLayout::kDvorak:
            // Disposition Dvorak (US). keyMap[touche logique] = touche PHYSIQUE (désignée par sa position US) qui porte
            // cette lettre en Dvorak. Rangée du haut (positions Q W E R T Y U I O P) : ' , . p y f g c r l
            keyMap[KeyButton::kP] = GLFW_KEY_R;
            keyMap[KeyButton::kY] = GLFW_KEY_T;
            keyMap[KeyButton::kF] = GLFW_KEY_Y;
            keyMap[KeyButton::kG] = GLFW_KEY_U;
            keyMap[KeyButton::kC] = GLFW_KEY_I;
            keyMap[KeyButton::kR] = GLFW_KEY_O;
            keyMap[KeyButton::kL] = GLFW_KEY_P;

            // Rangée du milieu (positions A S D F G H J K L ;) : a o e u i d h t n s
            keyMap[KeyButton::kA] = GLFW_KEY_A;
            keyMap[KeyButton::kO] = GLFW_KEY_S;
            keyMap[KeyButton::kE] = GLFW_KEY_D;
            keyMap[KeyButton::kU] = GLFW_KEY_F;
            keyMap[KeyButton::kI] = GLFW_KEY_G;
            keyMap[KeyButton::kD] = GLFW_KEY_H;
            keyMap[KeyButton::kH] = GLFW_KEY_J;
            keyMap[KeyButton::kT] = GLFW_KEY_K;
            keyMap[KeyButton::kN] = GLFW_KEY_L;
            keyMap[KeyButton::kS] = GLFW_KEY_SEMICOLON;

            // Rangée du bas (positions Z X C V B N M , . /) : ; q j k x b m w v z
            keyMap[KeyButton::kQ] = GLFW_KEY_X;
            keyMap[KeyButton::kJ] = GLFW_KEY_C;
            keyMap[KeyButton::kK] = GLFW_KEY_V;
            keyMap[KeyButton::kX] = GLFW_KEY_B;
            keyMap[KeyButton::kB] = GLFW_KEY_N;
            keyMap[KeyButton::kM] = GLFW_KEY_M;
            keyMap[KeyButton::kW] = GLFW_KEY_COMMA;
            keyMap[KeyButton::kV] = GLFW_KEY_PERIOD;
            keyMap[KeyButton::kZ] = GLFW_KEY_SLASH;

            // Symboles Dvorak : ' , . sont sur la rangée du haut, ; en bas à gauche, [ ] / = autour de la touche -
            keyMap[KeyButton::kApostrophe] = GLFW_KEY_Q;
            keyMap[KeyButton::kComma] = GLFW_KEY_W;
            keyMap[KeyButton::kPeriod] = GLFW_KEY_E;
            keyMap[KeyButton::kSemicolon] = GLFW_KEY_Z;
            keyMap[KeyButton::kMinus] = GLFW_KEY_APOSTROPHE;
            keyMap[KeyButton::kEquals] = GLFW_KEY_RIGHT_BRACKET;
            keyMap[KeyButton::kLeftBracket] = GLFW_KEY_MINUS;
            keyMap[KeyButton::kRightBracket] = GLFW_KEY_EQUAL;
            keyMap[KeyButton::kSlash] = GLFW_KEY_LEFT_BRACKET;
            keyMap[KeyButton::kBackslash] = GLFW_KEY_BACKSLASH;  // Backslash remains Backslash
            break;
#pragma endregion DVORAK

#pragma region QWERTY
        case KeyLayout::kQwerty:
        default:
            // Lettres QWERTY (mapping direct)
            keyMap[KeyButton::kA] = GLFW_KEY_A;
            keyMap[KeyButton::kB] = GLFW_KEY_B;
            keyMap[KeyButton::kC] = GLFW_KEY_C;
            keyMap[KeyButton::kD] = GLFW_KEY_D;
            keyMap[KeyButton::kE] = GLFW_KEY_E;
            keyMap[KeyButton::kF] = GLFW_KEY_F;
            keyMap[KeyButton::kG] = GLFW_KEY_G;
            keyMap[KeyButton::kH] = GLFW_KEY_H;
            keyMap[KeyButton::kI] = GLFW_KEY_I;
            keyMap[KeyButton::kJ] = GLFW_KEY_J;
            keyMap[KeyButton::kK] = GLFW_KEY_K;
            keyMap[KeyButton::kL] = GLFW_KEY_L;
            keyMap[KeyButton::kM] = GLFW_KEY_M;
            keyMap[KeyButton::kN] = GLFW_KEY_N;
            keyMap[KeyButton::kO] = GLFW_KEY_O;
            keyMap[KeyButton::kP] = GLFW_KEY_P;
            keyMap[KeyButton::kQ] = GLFW_KEY_Q;
            keyMap[KeyButton::kR] = GLFW_KEY_R;
            keyMap[KeyButton::kS] = GLFW_KEY_S;
            keyMap[KeyButton::kT] = GLFW_KEY_T;
            keyMap[KeyButton::kU] = GLFW_KEY_U;
            keyMap[KeyButton::kV] = GLFW_KEY_V;
            keyMap[KeyButton::kW] = GLFW_KEY_W;
            keyMap[KeyButton::kX] = GLFW_KEY_X;
            keyMap[KeyButton::kY] = GLFW_KEY_Y;
            keyMap[KeyButton::kZ] = GLFW_KEY_Z;

            // Symboles QWERTY
            keyMap[KeyButton::kMinus] = GLFW_KEY_MINUS;
            keyMap[KeyButton::kEquals] = GLFW_KEY_EQUAL;
            keyMap[KeyButton::kLeftBracket] = GLFW_KEY_LEFT_BRACKET;
            keyMap[KeyButton::kRightBracket] = GLFW_KEY_RIGHT_BRACKET;
            keyMap[KeyButton::kSemicolon] = GLFW_KEY_SEMICOLON;
            keyMap[KeyButton::kComma] = GLFW_KEY_COMMA;
            keyMap[KeyButton::kPeriod] = GLFW_KEY_PERIOD;
            keyMap[KeyButton::kSlash] = GLFW_KEY_SLASH;
            keyMap[KeyButton::kBackslash] = GLFW_KEY_BACKSLASH;
            keyMap[KeyButton::kApostrophe] = GLFW_KEY_APOSTROPHE;
            break;
#pragma endregion QWERTY
    }

#pragma region All Layout Mapping
    // Modificateurs (identiques pour tous les layouts)
    keyMap[KeyButton::kLeftShift] = GLFW_KEY_LEFT_SHIFT;
    keyMap[KeyButton::kLeftControl] = GLFW_KEY_LEFT_CONTROL;
    keyMap[KeyButton::kLeftAlt] = GLFW_KEY_LEFT_ALT;
    keyMap[KeyButton::kLeftSuper] = GLFW_KEY_LEFT_SUPER;
    keyMap[KeyButton::kRightShift] = GLFW_KEY_RIGHT_SHIFT;
    keyMap[KeyButton::kRightControl] = GLFW_KEY_RIGHT_CONTROL;
    keyMap[KeyButton::kRightAlt] = GLFW_KEY_RIGHT_ALT;
    keyMap[KeyButton::kRightSuper] = GLFW_KEY_RIGHT_SUPER;
    keyMap[KeyButton::kCapsLock] = GLFW_KEY_CAPS_LOCK;
    keyMap[KeyButton::kNumLock] = GLFW_KEY_NUM_LOCK;
    keyMap[KeyButton::kScrollLock] = GLFW_KEY_SCROLL_LOCK;

    // Navigation (identique pour tous les layouts)
    keyMap[KeyButton::kSpace] = GLFW_KEY_SPACE;
    keyMap[KeyButton::kEnter] = GLFW_KEY_ENTER;
    keyMap[KeyButton::kTab] = GLFW_KEY_TAB;
    keyMap[KeyButton::kEscape] = GLFW_KEY_ESCAPE;
    keyMap[KeyButton::kBackspace] = GLFW_KEY_BACKSPACE;
    keyMap[KeyButton::kInsert] = GLFW_KEY_INSERT;
    keyMap[KeyButton::kDelete] = GLFW_KEY_DELETE;
    keyMap[KeyButton::kHome] = GLFW_KEY_HOME;
    keyMap[KeyButton::kEnd] = GLFW_KEY_END;
    keyMap[KeyButton::kPageUp] = GLFW_KEY_PAGE_UP;
    keyMap[KeyButton::kPageDown] = GLFW_KEY_PAGE_DOWN;
    keyMap[KeyButton::kLeft] = GLFW_KEY_LEFT;
    keyMap[KeyButton::kRight] = GLFW_KEY_RIGHT;
    keyMap[KeyButton::kUp] = GLFW_KEY_UP;
    keyMap[KeyButton::kDown] = GLFW_KEY_DOWN;

    // Chiffres (identique pour tous les layouts)
    keyMap[KeyButton::kNum0] = GLFW_KEY_0;
    keyMap[KeyButton::kNum1] = GLFW_KEY_1;
    keyMap[KeyButton::kNum2] = GLFW_KEY_2;
    keyMap[KeyButton::kNum3] = GLFW_KEY_3;
    keyMap[KeyButton::kNum4] = GLFW_KEY_4;
    keyMap[KeyButton::kNum5] = GLFW_KEY_5;
    keyMap[KeyButton::kNum6] = GLFW_KEY_6;
    keyMap[KeyButton::kNum7] = GLFW_KEY_7;
    keyMap[KeyButton::kNum8] = GLFW_KEY_8;
    keyMap[KeyButton::kNum9] = GLFW_KEY_9;

    // Pavé numérique (identique pour tous les layouts)
    keyMap[KeyButton::kNumpad0] = GLFW_KEY_KP_0;
    keyMap[KeyButton::kNumpad1] = GLFW_KEY_KP_1;
    keyMap[KeyButton::kNumpad2] = GLFW_KEY_KP_2;
    keyMap[KeyButton::kNumpad3] = GLFW_KEY_KP_3;
    keyMap[KeyButton::kNumpad4] = GLFW_KEY_KP_4;
    keyMap[KeyButton::kNumpad5] = GLFW_KEY_KP_5;
    keyMap[KeyButton::kNumpad6] = GLFW_KEY_KP_6;
    keyMap[KeyButton::kNumpad7] = GLFW_KEY_KP_7;
    keyMap[KeyButton::kNumpad8] = GLFW_KEY_KP_8;
    keyMap[KeyButton::kNumpad9] = GLFW_KEY_KP_9;
    keyMap[KeyButton::kNumpadDecimal] = GLFW_KEY_KP_DECIMAL;
    keyMap[KeyButton::kNumpadDivide] = GLFW_KEY_KP_DIVIDE;
    keyMap[KeyButton::kNumpadMultiply] = GLFW_KEY_KP_MULTIPLY;
    keyMap[KeyButton::kNumpadSubtract] = GLFW_KEY_KP_SUBTRACT;
    keyMap[KeyButton::kNumpadAdd] = GLFW_KEY_KP_ADD;
    keyMap[KeyButton::kNumpadEnter] = GLFW_KEY_KP_ENTER;

    // Touches de fonction (identique pour tous les layouts)
    keyMap[KeyButton::kF1] = GLFW_KEY_F1;
    keyMap[KeyButton::kF2] = GLFW_KEY_F2;
    keyMap[KeyButton::kF3] = GLFW_KEY_F3;
    keyMap[KeyButton::kF4] = GLFW_KEY_F4;
    keyMap[KeyButton::kF5] = GLFW_KEY_F5;
    keyMap[KeyButton::kF6] = GLFW_KEY_F6;
    keyMap[KeyButton::kF7] = GLFW_KEY_F7;
    keyMap[KeyButton::kF8] = GLFW_KEY_F8;
    keyMap[KeyButton::kF9] = GLFW_KEY_F9;
    keyMap[KeyButton::kF10] = GLFW_KEY_F10;
    keyMap[KeyButton::kF11] = GLFW_KEY_F11;
    keyMap[KeyButton::kF12] = GLFW_KEY_F12;
    keyMap[KeyButton::kF13] = GLFW_KEY_F13;
    keyMap[KeyButton::kF14] = GLFW_KEY_F14;
    keyMap[KeyButton::kF15] = GLFW_KEY_F15;
    keyMap[KeyButton::kF16] = GLFW_KEY_F16;
    keyMap[KeyButton::kF17] = GLFW_KEY_F17;
    keyMap[KeyButton::kF18] = GLFW_KEY_F18;
    keyMap[KeyButton::kF19] = GLFW_KEY_F19;
    keyMap[KeyButton::kF20] = GLFW_KEY_F20;
    keyMap[KeyButton::kF21] = GLFW_KEY_F21;
    keyMap[KeyButton::kF22] = GLFW_KEY_F22;
    keyMap[KeyButton::kF23] = GLFW_KEY_F23;
    keyMap[KeyButton::kF24] = GLFW_KEY_F24;
    keyMap[KeyButton::kF25] = GLFW_KEY_F25;

    // Boutons de souris (identique pour tous les layouts)
    mouseButtonMap[MouseButton::kLeft] = GLFW_MOUSE_BUTTON_LEFT;
    mouseButtonMap[MouseButton::kRight] = GLFW_MOUSE_BUTTON_RIGHT;
    mouseButtonMap[MouseButton::kMiddle] = GLFW_MOUSE_BUTTON_MIDDLE;
    mouseButtonMap[MouseButton::kX1] = GLFW_MOUSE_BUTTON_4;
    mouseButtonMap[MouseButton::kX2] = GLFW_MOUSE_BUTTON_5;
    mouseButtonMap[MouseButton::kX3] = GLFW_MOUSE_BUTTON_6;
    mouseButtonMap[MouseButton::kX4] = GLFW_MOUSE_BUTTON_7;
    mouseButtonMap[MouseButton::kX5] = GLFW_MOUSE_BUTTON_8;
#pragma endregion All Layouts Mapping

    // Réinitialise tous les InputManagers existants avec le nouveau layout
    for (auto it = inputManagers.begin(); it != inputManagers.end(); ++it)
    {
        it->second->init();
    }
}
#pragma endregion Key Layout Mapping

void InputManager::updateInputEvent()
{
    InputEvent event;
    for (const auto &[key, state] : keyStateMap)
    {
        if (state != InputState::kRelease)
        {
            KeyButtonEvent keyEvent;
            keyEvent.key = key;
            keyEvent.state = state;
            event.keys.push_back(keyEvent);
        }
    }

    for (const auto &[button, state] : mouseButtonStateMap)
    {
        if (state != InputState::kRelease)
        {
            MouseButtonEvent mouseButtonEvent;
            mouseButtonEvent.button = button;
            mouseButtonEvent.state = state;
            event.mouseButtons.push_back(mouseButtonEvent);
        }
    }
    event.mouseMoveData = this->mouseMoveData;
    this->inputEvent = event;
}

void InputManager::bindActionToInput(const std::string &action, InputAction inputAction)
{
    if (isActionBound(action))
    {
        LOG_WARNING("Action ", action, " already bound. Overwriting.");
    }
    mapActionToInputAction[action] = std::move(inputAction);
}

bool InputManager::isCurrentInputEventEmpty() { return inputEvent.keys.empty() && inputEvent.mouseButtons.empty(); }

bool InputManager::isActionActive(const std::string &action)
{
    if (!isActionBound(action))
    {
        LOG_WARNING("Action ", action, " is not bound.");
        return false;
    }

    InputAction &actionInput = mapActionToInputAction[action];

    // Check keys
    for (const auto &key : actionInput.keys)
    {
        if (!isKeyPressed(key))
        {
            return false;
        }
    }

    // Check mouse buttons
    for (const auto &button : actionInput.mouseButtons)
    {
        if (!isMouseButtonPressed(button))
        {
            return false;
        }
    }

    return true;
}

} // namespace orosmo::platform
