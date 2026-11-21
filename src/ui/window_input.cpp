#include "orosmo/ui/window_input.hpp"

#include <orosmo/platform/input_manager.hpp>
#include <orosmo/platform/window.hpp>

#include <algorithm>

namespace orosmo::ui
{

InputFrame readWindowInput(platform::Window &window, float deltaTime)
{
    InputFrame in;
    in.deltaTime = deltaTime;
    platform::InputManager &input = platform::InputManager::getInstance(window.getWindow());
    in.mouse = glm::vec2(window.getCursorPosition());
    in.mouseDown = input.isMouseButtonPressed(platform::MouseButton::kLeft) || input.isMouseButtonJustPressed(platform::MouseButton::kLeft);
    in.secondaryDown = input.isMouseButtonPressed(platform::MouseButton::kRight) || input.isMouseButtonJustPressed(platform::MouseButton::kRight);
    in.wheel = input.getMouseScroll();
    in.text = input.getTextInput();
    in.shift = input.isKeyPressed(platform::KeyButton::kLeftShift) || input.isKeyPressed(platform::KeyButton::kRightShift) ||
               input.isKeyJustPressed(platform::KeyButton::kLeftShift) || input.isKeyJustPressed(platform::KeyButton::kRightShift);
    const std::string &typed = in.text;
    std::size_t textDone = 0;
    auto flushText = [&](std::size_t upTo)
    {
        upTo = std::min(upTo, typed.size());
        if (upTo > textDone)
            in.events.push_back({kKeyNone, in.shift, typed.substr(textDone, upTo - textDone)});
        textDone = std::max(textDone, upTo);
    };
    for (const platform::KeyPress &press : input.getKeyPresses())
    {
        // Touches d'édition et de navigation (répétées tant qu'elles sont tenues) ; avec Ctrl, Alt ou Super ce sont des raccourcis.
        if (press.ctrl || press.alt || press.super)
            continue;
        int key = kKeyNone;
        switch (press.key)
        {
        case platform::KeyButton::kBackspace: key = kKeyBackspace; break;
        case platform::KeyButton::kDelete: key = kKeyDelete; break;
        case platform::KeyButton::kEnter:
        case platform::KeyButton::kNumpadEnter: key = kKeyEnter; break;
        case platform::KeyButton::kEscape: key = kKeyEscape; break;
        case platform::KeyButton::kTab: key = kKeyTab; break;
        case platform::KeyButton::kLeft: key = kKeyLeft; break;
        case platform::KeyButton::kRight: key = kKeyRight; break;
        case platform::KeyButton::kUp: key = kKeyUp; break;
        case platform::KeyButton::kDown: key = kKeyDown; break;
        case platform::KeyButton::kHome: key = kKeyHome; break;
        case platform::KeyButton::kEnd: key = kKeyEnd; break;
        case platform::KeyButton::kSpace: key = kKeySpace; break;
        case platform::KeyButton::kPageUp: key = kKeyPageUp; break;
        case platform::KeyButton::kPageDown: key = kKeyPageDown; break;
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
    in.shortcuts = input.getShortcuts();
    return in;
}

void useSystemClipboard(Ui &ui, platform::Window &window)
{
    ui.setClipboard([&window] { return window.getClipboardText(); }, [&window](const std::string &text) { window.setClipboardText(text); });
}

} // namespace orosmo::ui
