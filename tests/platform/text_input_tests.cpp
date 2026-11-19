// PLAT-05 : texte saisi : les points de code reçus de GLFW sont écrits en UTF-8 (le rappel lui-même demande une fenêtre).
#include <doctest/doctest.h>

#include <engine/platform/input_manager.hpp>

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
}
