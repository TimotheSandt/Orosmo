// PLAT-05 : texte saisi : les points de code reçus de GLFW sont écrits en UTF-8 (le rappel lui-même demande une fenêtre).
#include <doctest/doctest.h>

#include <orosmo/platform/input_manager.hpp>

TEST_SUITE("platform/text")
{
    TEST_CASE("code points are encoded in UTF-8; invalid ones are dropped")
    {
        std::string out;
        InputManager::AppendUtf8(out, 'a');
        InputManager::AppendUtf8(out, 0xE9);    // é
        InputManager::AppendUtf8(out, 0x20AC);  // €
        InputManager::AppendUtf8(out, 0x1F600); // emoji, 4 octets
        CHECK(out == "a\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80");
        InputManager::AppendUtf8(out, 0xD800);   // demi-code de substitution
        InputManager::AppendUtf8(out, 0x110000); // hors d'Unicode
        CHECK(out.size() == 1 + 2 + 3 + 4);
    }

    TEST_CASE("shortcut names: a key with Ctrl, Alt or Super, or a function key; letters and arrows alone are not shortcuts")
    {
        CHECK(InputManager::ShortcutName({KeyButton::S, false, false, true}) == "Ctrl+S");
        CHECK(InputManager::ShortcutName({KeyButton::Z, false, true, true}) == "Ctrl+Shift+Z");
        CHECK(InputManager::ShortcutName({KeyButton::F5}) == "F5");
        CHECK(InputManager::ShortcutName({KeyButton::DELETE, false, false, false, true}) == "Alt+Delete");
        CHECK(InputManager::ShortcutName({KeyButton::A}).empty());
        CHECK(InputManager::ShortcutName({KeyButton::LEFT, false, true}).empty());
        CHECK(InputManager::ShortcutName({KeyButton::LEFT_CONTROL, false, false, true}).empty());
        CHECK(InputManager::KeyName(KeyButton::PAGE_DOWN) == std::string("PageDown"));
        CHECK(InputManager::KeyName(KeyButton::LEFT_SHIFT) == nullptr);
    }
}
