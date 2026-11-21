// PLAT-05 : texte saisi : les points de code reçus de GLFW sont écrits en UTF-8 (le rappel lui-même demande une fenêtre).
#include <doctest/doctest.h>

#include <orosmo/platform/input_manager.hpp>

namespace platform = orosmo::platform;

TEST_SUITE("platform/text")
{
    TEST_CASE("code points are encoded in UTF-8; invalid ones are dropped")
    {
        std::string out;
        platform::InputManager::appendUtf8(out, 'a');
        platform::InputManager::appendUtf8(out, 0xE9);    // é
        platform::InputManager::appendUtf8(out, 0x20AC);  // €
        platform::InputManager::appendUtf8(out, 0x1F600); // emoji, 4 octets
        CHECK(out == "a\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80");
        platform::InputManager::appendUtf8(out, 0xD800);   // demi-code de substitution
        platform::InputManager::appendUtf8(out, 0x110000); // hors d'Unicode
        CHECK(out.size() == 1 + 2 + 3 + 4);
    }

    TEST_CASE("shortcut names: a key with Ctrl, Alt or Super, or a function key; letters and arrows alone are not shortcuts")
    {
        CHECK(platform::InputManager::shortcutName({platform::KeyButton::kS, false, false, true}) == "Ctrl+S");
        CHECK(platform::InputManager::shortcutName({platform::KeyButton::kZ, false, true, true}) == "Ctrl+Shift+Z");
        CHECK(platform::InputManager::shortcutName({platform::KeyButton::kF5}) == "F5");
        CHECK(platform::InputManager::shortcutName({platform::KeyButton::kDelete, false, false, false, true}) == "Alt+Delete");
        CHECK(platform::InputManager::shortcutName({platform::KeyButton::kA}).empty());
        CHECK(platform::InputManager::shortcutName({platform::KeyButton::kLeft, false, true}).empty());
        CHECK(platform::InputManager::shortcutName({platform::KeyButton::kLeftControl, false, false, true}).empty());
        CHECK(platform::InputManager::keyName(platform::KeyButton::kPageDown) == std::string("PageDown"));
        CHECK(platform::InputManager::keyName(platform::KeyButton::kLeftShift) == nullptr);
    }
}
