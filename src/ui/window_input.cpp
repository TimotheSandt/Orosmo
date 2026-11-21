#include "orosmo/ui/window_input.hpp"

#include <orosmo/platform/input_manager.hpp>
#include <orosmo/platform/window.hpp>

#include <algorithm>

namespace orosmo::ui
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
    const std::string &typed = in.text;
    std::size_t textDone = 0;
    auto flushText = [&](std::size_t upTo)
    {
        upTo = std::min(upTo, typed.size());
        if (upTo > textDone)
            in.events.push_back({kKeyNone, in.shift, typed.substr(textDone, upTo - textDone)});
        textDone = std::max(textDone, upTo);
    };
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
        {
            in.keys.push_back(key);
            flushText(press.textOffset); // le texte saisi avant cette touche passe avant elle
            in.events.push_back({key, press.shift, {}});
        }
    }
    flushText(typed.size());
    in.shortcuts = input.GetShortcuts();
    return in;
}

void useSystemClipboard(Ui &ui, Window &window)
{
    ui.setClipboard([&window] { return window.GetClipboardText(); }, [&window](const std::string &text) { window.SetClipboardText(text); });
}

} // namespace orosmo::ui
