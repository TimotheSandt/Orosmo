#include <engine/platform/input_manager.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <functional>
#include <iostream>
#include <utility>
#include "Logger.h"

#undef DELETE

KeyLayout InputManager::keyLayout = KeyLayout::UNDEFINED;
std::unordered_map<KeyButton, int> InputManager::keyMap;
std::unordered_map<MouseButton, int> InputManager::mouseButtonMap;
std::unordered_map<std::string, InputAction> InputManager::mapActionToInputAction;

std::unordered_map<void *, std::unique_ptr<InputManager>> InputManager::inputManagers;

namespace
{
// Molette : GLFW la livre par rappel ; on cumule jusqu'au prochain `Update`, qui l'expose dans `MouseMoveData::scroll`.
std::unordered_map<void *, glm::vec2> &pendingScroll()
{
    static std::unordered_map<void *, glm::vec2> pending;
    return pending;
}

// PLAT-05 : texte saisi (rappel de caractères de GLFW, répétition du clavier comprise), cumulé jusqu'au prochain `Update`.
std::unordered_map<void *, std::string> &pendingText()
{
    static std::unordered_map<void *, std::string> pending;
    return pending;
}

GLFWwindow *native(void *handle) { return static_cast<GLFWwindow *>(handle); }
} // namespace

void InputManager::AppendUtf8(std::string &out, unsigned int codepoint)
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
        glfwSetCharCallback(native(nativeWindow), [](GLFWwindow *w, unsigned int codepoint) { AppendUtf8(pendingText()[w], codepoint); });
    }
    Init();
}

InputManager::~InputManager() = default;

InputManager &InputManager::GetInstance(void *window)
{
    if (keyLayout == KeyLayout::UNDEFINED)
    {
        AutoDetectKeyLayout();
    }
    if (inputManagers.find(window) == inputManagers.end())
    {
        inputManagers[window] = std::unique_ptr<InputManager>(new InputManager(window));
    }
    return *inputManagers[window];
}

void InputManager::RemoveInstance(void *window)
{
    if (window == nullptr)
    {
        return;
    }
    if (inputManagers.find(window) != inputManagers.end())
    {
        glfwSetScrollCallback(native(window), nullptr);
        glfwSetCharCallback(native(window), nullptr);
    }
    pendingScroll().erase(window);
    pendingText().erase(window);
    inputManagers.erase(window);
}

void InputManager::Init()
{
    // Keyboard
    for (auto it = keyMap.begin(); it != keyMap.end(); ++it)
    {
        keyStateMap[it->first] = InputState::Release;
    }
    // Mouse
    for (auto it = mouseButtonMap.begin(); it != mouseButtonMap.end(); ++it)
    {
        mouseButtonStateMap[it->first] = InputState::Release;
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

// Update method implementation
void InputManager::Update()
{
    // Keyboard
    for (auto it = keyStateMap.begin(); it != keyStateMap.end(); ++it)
    {
        bool isPressed = glfwGetKey(native(window), keyMap[it->first]) == GLFW_PRESS;
        it->second = isPressed ? ((it->second == InputState::Release || it->second == InputState::PressEnd) ? InputState::PressBegin
                                                                                                            : InputState::Pressed)
                               : ((it->second == InputState::Pressed || it->second == InputState::PressBegin) ? InputState::PressEnd
                                                                                                              : InputState::Release);
    }

    // Mouse
    for (auto it = mouseButtonStateMap.begin(); it != mouseButtonStateMap.end(); ++it)
    {
        bool isPressed = glfwGetMouseButton(native(window), mouseButtonMap[it->first]) == GLFW_PRESS;
        it->second = isPressed ? ((it->second == InputState::Release || it->second == InputState::PressEnd) ? InputState::PressBegin
                                                                                                            : InputState::Pressed)
                               : ((it->second == InputState::Pressed || it->second == InputState::PressBegin) ? InputState::PressEnd
                                                                                                              : InputState::Release);
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
    UpdateInputEvent();
}

void InputManager::AutoDetectKeyLayout()
{
    KeyLayout layout = KeyLayout::QWERTY;
#ifdef _WIN32
    HKL keyboardLayout = GetKeyboardLayout(0);
    DWORD layoutID = LOWORD(reinterpret_cast<DWORD_PTR>(keyboardLayout));

    switch (layoutID)
    {                // Use the LOWORD for the switch
        case 0x0409: // US English (Primary QWERTY)
            if (reinterpret_cast<UINT_PTR>(keyboardLayout) == 0x00010409)
            {
                layout = KeyLayout::DVORAK;
            }
            else
            {
                layout = KeyLayout::QWERTY;
            }
            break;

        case 0x040C: // French (AZERTY)
            layout = KeyLayout::AZERTY;
            break;

        case 0x0407: // German (QWERTZ)
            layout = KeyLayout::QWERTZ;
            break;

        default:
            layout = KeyLayout::QWERTY;
            break;
    }
#else
    // Linux, macOS : GLFW donne le caractère imprimé sur chaque touche physique d'après la disposition du système
    // (XKB, Wayland ou X11 ; TIS sur macOS). C'est ce que la démo utilisait mal : QWERTY en dur donnait Z -> « w » sur AZERTY.
    layout = DetectKeyLayoutFromKeyNames([](int key) { return glfwGetKeyName(key, 0); });
    const char *name = layout == KeyLayout::AZERTY  ? "AZERTY"
                       : layout == KeyLayout::QWERTZ ? "QWERTZ"
                       : layout == KeyLayout::DVORAK ? "Dvorak"
                                                     : "QWERTY";
    LOG_INFO("Keyboard layout detected: ", name);
#endif

    InputManager::SetKeyLayout(layout);
}

KeyLayout InputManager::DetectKeyLayoutFromKeyNames(const std::function<const char *(int)> &keyName)
{
    auto is = [&](int key, const char *expected) {
        const char *name = keyName(key);
        return name != nullptr && std::string(name) == expected;
    };
    // Les codes GLFW désignent la position US de la touche : en AZERTY la touche en position Q porte « a », en QWERTZ
    // celle en position Y porte « z », en Dvorak celle en position Q porte « ' ».
    if (is(GLFW_KEY_Q, "a") && is(GLFW_KEY_W, "z"))
        return KeyLayout::AZERTY;
    if (is(GLFW_KEY_Y, "z") && is(GLFW_KEY_Z, "y"))
        return KeyLayout::QWERTZ;
    if (is(GLFW_KEY_Q, "'") && is(GLFW_KEY_W, ","))
        return KeyLayout::DVORAK;
    return KeyLayout::QWERTY;
}

int InputManager::GetKeyCode(KeyButton key)
{
    auto it = keyMap.find(key);
    return it == keyMap.end() ? GLFW_KEY_UNKNOWN : it->second;
}

#pragma region Key Layout Mapping
void InputManager::SetKeyLayout(KeyLayout layout)
{
    InputManager::keyLayout = layout;
    InputManager::keyMap.clear();
    InputManager::mouseButtonMap.clear();

    switch (layout)
    {
#pragma region AZERTY
        case KeyLayout::AZERTY:
            // Lettres AZERTY
            keyMap[KeyButton::A] = GLFW_KEY_Q;
            keyMap[KeyButton::B] = GLFW_KEY_B;
            keyMap[KeyButton::C] = GLFW_KEY_C;
            keyMap[KeyButton::D] = GLFW_KEY_D;
            keyMap[KeyButton::E] = GLFW_KEY_E;
            keyMap[KeyButton::F] = GLFW_KEY_F;
            keyMap[KeyButton::G] = GLFW_KEY_G;
            keyMap[KeyButton::H] = GLFW_KEY_H;
            keyMap[KeyButton::I] = GLFW_KEY_I;
            keyMap[KeyButton::J] = GLFW_KEY_J;
            keyMap[KeyButton::K] = GLFW_KEY_K;
            keyMap[KeyButton::L] = GLFW_KEY_L;
            keyMap[KeyButton::M] = GLFW_KEY_SEMICOLON;
            keyMap[KeyButton::N] = GLFW_KEY_N;
            keyMap[KeyButton::O] = GLFW_KEY_O;
            keyMap[KeyButton::P] = GLFW_KEY_P;
            keyMap[KeyButton::Q] = GLFW_KEY_A;
            keyMap[KeyButton::R] = GLFW_KEY_R;
            keyMap[KeyButton::S] = GLFW_KEY_S;
            keyMap[KeyButton::T] = GLFW_KEY_T;
            keyMap[KeyButton::U] = GLFW_KEY_U;
            keyMap[KeyButton::V] = GLFW_KEY_V;
            keyMap[KeyButton::W] = GLFW_KEY_Z;
            keyMap[KeyButton::X] = GLFW_KEY_X;
            keyMap[KeyButton::Y] = GLFW_KEY_Y;
            keyMap[KeyButton::Z] = GLFW_KEY_W;

            // Symboles AZERTY
            keyMap[KeyButton::MINUS] = GLFW_KEY_MINUS;
            keyMap[KeyButton::EQUALS] = GLFW_KEY_EQUAL;
            keyMap[KeyButton::LEFT_BRACKET] = GLFW_KEY_LEFT_BRACKET;
            keyMap[KeyButton::RIGHT_BRACKET] = GLFW_KEY_RIGHT_BRACKET;
            keyMap[KeyButton::SEMICOLON] = GLFW_KEY_COMMA;
            keyMap[KeyButton::COMMA] = GLFW_KEY_M;
            keyMap[KeyButton::PERIOD] = GLFW_KEY_PERIOD;
            keyMap[KeyButton::SLASH] = GLFW_KEY_SLASH;
            keyMap[KeyButton::BACKSLASH] = GLFW_KEY_BACKSLASH;
            keyMap[KeyButton::APOSTROPHE] = GLFW_KEY_APOSTROPHE;
            break;
#pragma endregion AZERTY

#pragma region QWERTZ
        case KeyLayout::QWERTZ:
            // Lettres QWERTZ (seuls Y et Z sont échangés par rapport à QWERTY)
            keyMap[KeyButton::A] = GLFW_KEY_A;
            keyMap[KeyButton::B] = GLFW_KEY_B;
            keyMap[KeyButton::C] = GLFW_KEY_C;
            keyMap[KeyButton::D] = GLFW_KEY_D;
            keyMap[KeyButton::E] = GLFW_KEY_E;
            keyMap[KeyButton::F] = GLFW_KEY_F;
            keyMap[KeyButton::G] = GLFW_KEY_G;
            keyMap[KeyButton::H] = GLFW_KEY_H;
            keyMap[KeyButton::I] = GLFW_KEY_I;
            keyMap[KeyButton::J] = GLFW_KEY_J;
            keyMap[KeyButton::K] = GLFW_KEY_K;
            keyMap[KeyButton::L] = GLFW_KEY_L;
            keyMap[KeyButton::M] = GLFW_KEY_M;
            keyMap[KeyButton::N] = GLFW_KEY_N;
            keyMap[KeyButton::O] = GLFW_KEY_O;
            keyMap[KeyButton::P] = GLFW_KEY_P;
            keyMap[KeyButton::Q] = GLFW_KEY_Q;
            keyMap[KeyButton::R] = GLFW_KEY_R;
            keyMap[KeyButton::S] = GLFW_KEY_S;
            keyMap[KeyButton::T] = GLFW_KEY_T;
            keyMap[KeyButton::U] = GLFW_KEY_U;
            keyMap[KeyButton::V] = GLFW_KEY_V;
            keyMap[KeyButton::W] = GLFW_KEY_W;
            keyMap[KeyButton::X] = GLFW_KEY_X;
            keyMap[KeyButton::Y] = GLFW_KEY_Z;
            keyMap[KeyButton::Z] = GLFW_KEY_Y;

            // Symboles QWERTZ
            keyMap[KeyButton::MINUS] = GLFW_KEY_MINUS;
            keyMap[KeyButton::EQUALS] = GLFW_KEY_EQUAL;
            keyMap[KeyButton::LEFT_BRACKET] = GLFW_KEY_LEFT_BRACKET;
            keyMap[KeyButton::RIGHT_BRACKET] = GLFW_KEY_RIGHT_BRACKET;
            keyMap[KeyButton::SEMICOLON] = GLFW_KEY_SEMICOLON;
            keyMap[KeyButton::COMMA] = GLFW_KEY_COMMA;
            keyMap[KeyButton::PERIOD] = GLFW_KEY_PERIOD;
            keyMap[KeyButton::SLASH] = GLFW_KEY_SLASH;
            keyMap[KeyButton::BACKSLASH] = GLFW_KEY_BACKSLASH;
            keyMap[KeyButton::APOSTROPHE] = GLFW_KEY_APOSTROPHE;
            break;
#pragma endregion QWERTZ

#pragma region DVORAK
        case KeyLayout::DVORAK:
            // Disposition Dvorak (US). keyMap[touche logique] = touche PHYSIQUE (désignée par sa position US) qui porte
            // cette lettre en Dvorak. Rangée du haut (positions Q W E R T Y U I O P) : ' , . p y f g c r l
            keyMap[KeyButton::P] = GLFW_KEY_R;
            keyMap[KeyButton::Y] = GLFW_KEY_T;
            keyMap[KeyButton::F] = GLFW_KEY_Y;
            keyMap[KeyButton::G] = GLFW_KEY_U;
            keyMap[KeyButton::C] = GLFW_KEY_I;
            keyMap[KeyButton::R] = GLFW_KEY_O;
            keyMap[KeyButton::L] = GLFW_KEY_P;

            // Rangée du milieu (positions A S D F G H J K L ;) : a o e u i d h t n s
            keyMap[KeyButton::A] = GLFW_KEY_A;
            keyMap[KeyButton::O] = GLFW_KEY_S;
            keyMap[KeyButton::E] = GLFW_KEY_D;
            keyMap[KeyButton::U] = GLFW_KEY_F;
            keyMap[KeyButton::I] = GLFW_KEY_G;
            keyMap[KeyButton::D] = GLFW_KEY_H;
            keyMap[KeyButton::H] = GLFW_KEY_J;
            keyMap[KeyButton::T] = GLFW_KEY_K;
            keyMap[KeyButton::N] = GLFW_KEY_L;
            keyMap[KeyButton::S] = GLFW_KEY_SEMICOLON;

            // Rangée du bas (positions Z X C V B N M , . /) : ; q j k x b m w v z
            keyMap[KeyButton::Q] = GLFW_KEY_X;
            keyMap[KeyButton::J] = GLFW_KEY_C;
            keyMap[KeyButton::K] = GLFW_KEY_V;
            keyMap[KeyButton::X] = GLFW_KEY_B;
            keyMap[KeyButton::B] = GLFW_KEY_N;
            keyMap[KeyButton::M] = GLFW_KEY_M;
            keyMap[KeyButton::W] = GLFW_KEY_COMMA;
            keyMap[KeyButton::V] = GLFW_KEY_PERIOD;
            keyMap[KeyButton::Z] = GLFW_KEY_SLASH;

            // Symboles Dvorak : ' , . sont sur la rangée du haut, ; en bas à gauche, [ ] / = autour de la touche -
            keyMap[KeyButton::APOSTROPHE] = GLFW_KEY_Q;
            keyMap[KeyButton::COMMA] = GLFW_KEY_W;
            keyMap[KeyButton::PERIOD] = GLFW_KEY_E;
            keyMap[KeyButton::SEMICOLON] = GLFW_KEY_Z;
            keyMap[KeyButton::MINUS] = GLFW_KEY_APOSTROPHE;
            keyMap[KeyButton::EQUALS] = GLFW_KEY_RIGHT_BRACKET;
            keyMap[KeyButton::LEFT_BRACKET] = GLFW_KEY_MINUS;
            keyMap[KeyButton::RIGHT_BRACKET] = GLFW_KEY_EQUAL;
            keyMap[KeyButton::SLASH] = GLFW_KEY_LEFT_BRACKET;
            keyMap[KeyButton::BACKSLASH] = GLFW_KEY_BACKSLASH;  // Backslash remains Backslash
            break;
#pragma endregion DVORAK

#pragma region QWERTY
        case KeyLayout::QWERTY:
        default:
            // Lettres QWERTY (mapping direct)
            keyMap[KeyButton::A] = GLFW_KEY_A;
            keyMap[KeyButton::B] = GLFW_KEY_B;
            keyMap[KeyButton::C] = GLFW_KEY_C;
            keyMap[KeyButton::D] = GLFW_KEY_D;
            keyMap[KeyButton::E] = GLFW_KEY_E;
            keyMap[KeyButton::F] = GLFW_KEY_F;
            keyMap[KeyButton::G] = GLFW_KEY_G;
            keyMap[KeyButton::H] = GLFW_KEY_H;
            keyMap[KeyButton::I] = GLFW_KEY_I;
            keyMap[KeyButton::J] = GLFW_KEY_J;
            keyMap[KeyButton::K] = GLFW_KEY_K;
            keyMap[KeyButton::L] = GLFW_KEY_L;
            keyMap[KeyButton::M] = GLFW_KEY_M;
            keyMap[KeyButton::N] = GLFW_KEY_N;
            keyMap[KeyButton::O] = GLFW_KEY_O;
            keyMap[KeyButton::P] = GLFW_KEY_P;
            keyMap[KeyButton::Q] = GLFW_KEY_Q;
            keyMap[KeyButton::R] = GLFW_KEY_R;
            keyMap[KeyButton::S] = GLFW_KEY_S;
            keyMap[KeyButton::T] = GLFW_KEY_T;
            keyMap[KeyButton::U] = GLFW_KEY_U;
            keyMap[KeyButton::V] = GLFW_KEY_V;
            keyMap[KeyButton::W] = GLFW_KEY_W;
            keyMap[KeyButton::X] = GLFW_KEY_X;
            keyMap[KeyButton::Y] = GLFW_KEY_Y;
            keyMap[KeyButton::Z] = GLFW_KEY_Z;

            // Symboles QWERTY
            keyMap[KeyButton::MINUS] = GLFW_KEY_MINUS;
            keyMap[KeyButton::EQUALS] = GLFW_KEY_EQUAL;
            keyMap[KeyButton::LEFT_BRACKET] = GLFW_KEY_LEFT_BRACKET;
            keyMap[KeyButton::RIGHT_BRACKET] = GLFW_KEY_RIGHT_BRACKET;
            keyMap[KeyButton::SEMICOLON] = GLFW_KEY_SEMICOLON;
            keyMap[KeyButton::COMMA] = GLFW_KEY_COMMA;
            keyMap[KeyButton::PERIOD] = GLFW_KEY_PERIOD;
            keyMap[KeyButton::SLASH] = GLFW_KEY_SLASH;
            keyMap[KeyButton::BACKSLASH] = GLFW_KEY_BACKSLASH;
            keyMap[KeyButton::APOSTROPHE] = GLFW_KEY_APOSTROPHE;
            break;
#pragma endregion QWERTY
    }

#pragma region All Layout Mapping
    // Modificateurs (identiques pour tous les layouts)
    keyMap[KeyButton::LEFT_SHIFT] = GLFW_KEY_LEFT_SHIFT;
    keyMap[KeyButton::LEFT_CONTROL] = GLFW_KEY_LEFT_CONTROL;
    keyMap[KeyButton::LEFT_ALT] = GLFW_KEY_LEFT_ALT;
    keyMap[KeyButton::LEFT_SUPER] = GLFW_KEY_LEFT_SUPER;
    keyMap[KeyButton::RIGHT_SHIFT] = GLFW_KEY_RIGHT_SHIFT;
    keyMap[KeyButton::RIGHT_CONTROL] = GLFW_KEY_RIGHT_CONTROL;
    keyMap[KeyButton::RIGHT_ALT] = GLFW_KEY_RIGHT_ALT;
    keyMap[KeyButton::RIGHT_SUPER] = GLFW_KEY_RIGHT_SUPER;
    keyMap[KeyButton::CAPS_LOCK] = GLFW_KEY_CAPS_LOCK;
    keyMap[KeyButton::NUM_LOCK] = GLFW_KEY_NUM_LOCK;
    keyMap[KeyButton::SCROLL_LOCK] = GLFW_KEY_SCROLL_LOCK;

    // Navigation (identique pour tous les layouts)
    keyMap[KeyButton::SPACE] = GLFW_KEY_SPACE;
    keyMap[KeyButton::ENTER] = GLFW_KEY_ENTER;
    keyMap[KeyButton::TAB] = GLFW_KEY_TAB;
    keyMap[KeyButton::ESCAPE] = GLFW_KEY_ESCAPE;
    keyMap[KeyButton::BACKSPACE] = GLFW_KEY_BACKSPACE;
    keyMap[KeyButton::INSERT] = GLFW_KEY_INSERT;
    keyMap[KeyButton::DELETE] = GLFW_KEY_DELETE;
    keyMap[KeyButton::HOME] = GLFW_KEY_HOME;
    keyMap[KeyButton::END] = GLFW_KEY_END;
    keyMap[KeyButton::PAGE_UP] = GLFW_KEY_PAGE_UP;
    keyMap[KeyButton::PAGE_DOWN] = GLFW_KEY_PAGE_DOWN;
    keyMap[KeyButton::LEFT] = GLFW_KEY_LEFT;
    keyMap[KeyButton::RIGHT] = GLFW_KEY_RIGHT;
    keyMap[KeyButton::UP] = GLFW_KEY_UP;
    keyMap[KeyButton::DOWN] = GLFW_KEY_DOWN;

    // Chiffres (identique pour tous les layouts)
    keyMap[KeyButton::NUM_0] = GLFW_KEY_0;
    keyMap[KeyButton::NUM_1] = GLFW_KEY_1;
    keyMap[KeyButton::NUM_2] = GLFW_KEY_2;
    keyMap[KeyButton::NUM_3] = GLFW_KEY_3;
    keyMap[KeyButton::NUM_4] = GLFW_KEY_4;
    keyMap[KeyButton::NUM_5] = GLFW_KEY_5;
    keyMap[KeyButton::NUM_6] = GLFW_KEY_6;
    keyMap[KeyButton::NUM_7] = GLFW_KEY_7;
    keyMap[KeyButton::NUM_8] = GLFW_KEY_8;
    keyMap[KeyButton::NUM_9] = GLFW_KEY_9;

    // Pavé numérique (identique pour tous les layouts)
    keyMap[KeyButton::NUMPAD_0] = GLFW_KEY_KP_0;
    keyMap[KeyButton::NUMPAD_1] = GLFW_KEY_KP_1;
    keyMap[KeyButton::NUMPAD_2] = GLFW_KEY_KP_2;
    keyMap[KeyButton::NUMPAD_3] = GLFW_KEY_KP_3;
    keyMap[KeyButton::NUMPAD_4] = GLFW_KEY_KP_4;
    keyMap[KeyButton::NUMPAD_5] = GLFW_KEY_KP_5;
    keyMap[KeyButton::NUMPAD_6] = GLFW_KEY_KP_6;
    keyMap[KeyButton::NUMPAD_7] = GLFW_KEY_KP_7;
    keyMap[KeyButton::NUMPAD_8] = GLFW_KEY_KP_8;
    keyMap[KeyButton::NUMPAD_9] = GLFW_KEY_KP_9;
    keyMap[KeyButton::NUMPAD_DECIMAL] = GLFW_KEY_KP_DECIMAL;
    keyMap[KeyButton::NUMPAD_DIVIDE] = GLFW_KEY_KP_DIVIDE;
    keyMap[KeyButton::NUMPAD_MULTIPLY] = GLFW_KEY_KP_MULTIPLY;
    keyMap[KeyButton::NUMPAD_SUBTRACT] = GLFW_KEY_KP_SUBTRACT;
    keyMap[KeyButton::NUMPAD_ADD] = GLFW_KEY_KP_ADD;
    keyMap[KeyButton::NUMPAD_ENTER] = GLFW_KEY_KP_ENTER;

    // Touches de fonction (identique pour tous les layouts)
    keyMap[KeyButton::F1] = GLFW_KEY_F1;
    keyMap[KeyButton::F2] = GLFW_KEY_F2;
    keyMap[KeyButton::F3] = GLFW_KEY_F3;
    keyMap[KeyButton::F4] = GLFW_KEY_F4;
    keyMap[KeyButton::F5] = GLFW_KEY_F5;
    keyMap[KeyButton::F6] = GLFW_KEY_F6;
    keyMap[KeyButton::F7] = GLFW_KEY_F7;
    keyMap[KeyButton::F8] = GLFW_KEY_F8;
    keyMap[KeyButton::F9] = GLFW_KEY_F9;
    keyMap[KeyButton::F10] = GLFW_KEY_F10;
    keyMap[KeyButton::F11] = GLFW_KEY_F11;
    keyMap[KeyButton::F12] = GLFW_KEY_F12;
    keyMap[KeyButton::F13] = GLFW_KEY_F13;
    keyMap[KeyButton::F14] = GLFW_KEY_F14;
    keyMap[KeyButton::F15] = GLFW_KEY_F15;
    keyMap[KeyButton::F16] = GLFW_KEY_F16;
    keyMap[KeyButton::F17] = GLFW_KEY_F17;
    keyMap[KeyButton::F18] = GLFW_KEY_F18;
    keyMap[KeyButton::F19] = GLFW_KEY_F19;
    keyMap[KeyButton::F20] = GLFW_KEY_F20;
    keyMap[KeyButton::F21] = GLFW_KEY_F21;
    keyMap[KeyButton::F22] = GLFW_KEY_F22;
    keyMap[KeyButton::F23] = GLFW_KEY_F23;
    keyMap[KeyButton::F24] = GLFW_KEY_F24;
    keyMap[KeyButton::F25] = GLFW_KEY_F25;

    // Boutons de souris (identique pour tous les layouts)
    mouseButtonMap[MouseButton::LEFT] = GLFW_MOUSE_BUTTON_LEFT;
    mouseButtonMap[MouseButton::RIGHT] = GLFW_MOUSE_BUTTON_RIGHT;
    mouseButtonMap[MouseButton::MIDDLE] = GLFW_MOUSE_BUTTON_MIDDLE;
    mouseButtonMap[MouseButton::X1] = GLFW_MOUSE_BUTTON_4;
    mouseButtonMap[MouseButton::X2] = GLFW_MOUSE_BUTTON_5;
    mouseButtonMap[MouseButton::X3] = GLFW_MOUSE_BUTTON_6;
    mouseButtonMap[MouseButton::X4] = GLFW_MOUSE_BUTTON_7;
    mouseButtonMap[MouseButton::X5] = GLFW_MOUSE_BUTTON_8;
#pragma endregion All Layouts Mapping

    // Réinitialise tous les InputManagers existants avec le nouveau layout
    for (auto it = inputManagers.begin(); it != inputManagers.end(); ++it)
    {
        it->second->Init();
    }
}
#pragma endregion Key Layout Mapping

void InputManager::UpdateInputEvent()
{
    InputEvent event;
    for (const auto &[key, state] : keyStateMap)
    {
        if (state != InputState::Release)
        {
            KeyButtonEvent keyEvent;
            keyEvent.key = key;
            keyEvent.state = state;
            event.keys.push_back(keyEvent);
        }
    }

    for (const auto &[button, state] : mouseButtonStateMap)
    {
        if (state != InputState::Release)
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

void InputManager::BindActionToInput(const std::string &action, InputAction inputAction)
{
    if (IsActionBound(action))
    {
        LOG_WARNING("Action ", action, " already bound. Overwriting.");
    }
    mapActionToInputAction[action] = std::move(inputAction);
}

bool InputManager::IsCurrentInputEventEmpty() { return inputEvent.keys.empty() && inputEvent.mouseButtons.empty(); }

bool InputManager::IsActionActive(const std::string &action)
{
    if (!IsActionBound(action))
    {
        LOG_WARNING("Action ", action, " is not bound.");
        return false;
    }

    InputAction &actionInput = mapActionToInputAction[action];

    // Check keys
    for (const auto &key : actionInput.keys)
    {
        if (!IsKeyPressed(key))
        {
            return false;
        }
    }

    // Check mouse buttons
    for (const auto &button : actionInput.mouseButtons)
    {
        if (!IsMouseButtonPressed(button))
        {
            return false;
        }
    }

    return true;
}
