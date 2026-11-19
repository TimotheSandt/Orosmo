#include "engine/ui/window_input.hpp"

#include <engine/platform/input_manager.hpp>
#include <engine/platform/window.hpp>

namespace engine::ui
{

InputFrame readWindowInput(Window &window, float deltaTime)
{
    InputFrame in;
    in.deltaTime = deltaTime;
    InputManager &input = InputManager::GetInstance(window.GetWindow());
    in.mouse = glm::vec2(window.GetCursorPosition());
    in.mouseDown = input.IsMouseButtonPressed(MouseButton::LEFT) || input.IsMouseButtonJustPressed(MouseButton::LEFT);
    in.secondaryDown = input.IsMouseButtonPressed(MouseButton::RIGHT) || input.IsMouseButtonJustPressed(MouseButton::RIGHT);
    in.wheel = input.GetMouseScroll();
    in.text = input.GetTextInput();
    in.shift = input.IsKeyPressed(KeyButton::LEFT_SHIFT) || input.IsKeyPressed(KeyButton::RIGHT_SHIFT) ||
               input.IsKeyJustPressed(KeyButton::LEFT_SHIFT) || input.IsKeyJustPressed(KeyButton::RIGHT_SHIFT);
    for (const KeyPress &press : input.GetKeyPresses())
    {
        // Touches d'édition et de navigation (répétées tant qu'elles sont tenues) ; avec Ctrl, Alt ou Super ce sont des raccourcis.
        if (press.ctrl || press.alt || press.super)
            continue;
        int key = kKeyNone;
        switch (press.key)
        {
        case KeyButton::BACKSPACE: key = kKeyBackspace; break;
        case KeyButton::DELETE: key = kKeyDelete; break;
        case KeyButton::ENTER:
        case KeyButton::NUMPAD_ENTER: key = kKeyEnter; break;
        case KeyButton::ESCAPE: key = kKeyEscape; break;
        case KeyButton::TAB: key = kKeyTab; break;
        case KeyButton::LEFT: key = kKeyLeft; break;
        case KeyButton::RIGHT: key = kKeyRight; break;
        case KeyButton::UP: key = kKeyUp; break;
        case KeyButton::DOWN: key = kKeyDown; break;
        case KeyButton::HOME: key = kKeyHome; break;
        case KeyButton::END: key = kKeyEnd; break;
        case KeyButton::SPACE: key = kKeySpace; break;
        case KeyButton::PAGE_UP: key = kKeyPageUp; break;
        case KeyButton::PAGE_DOWN: key = kKeyPageDown; break;
        default: break;
        }
        if (key != kKeyNone)
            in.keys.push_back(key);
    }
    in.shortcuts = input.GetShortcuts();
    return in;
}

void useSystemClipboard(Ui &ui, Window &window)
{
    ui.setClipboard([&window] { return window.GetClipboardText(); }, [&window](const std::string &text) { window.SetClipboardText(text); });
}

} // namespace engine::ui
