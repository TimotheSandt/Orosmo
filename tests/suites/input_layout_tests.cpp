// PLAT-02 : la disposition du clavier (AZERTY, QWERTZ, Dvorak, QWERTY) est détectée depuis les noms que le système donne
// aux touches physiques (glfwGetKeyName), pas seulement sous Windows. Aucun GLFW ni fenêtre : la détection reçoit une
// fonction « code de touche -> caractère imprimé », ici des tables écrites à la main.
#include <doctest/doctest.h>

#include <orosmo/platform/input_manager.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <GLFW/glfw3.h>

#include <map>
#include <string>

namespace platform = orosmo::platform;

namespace
{
// Caractère imprimé sur la touche physique placée à la position US `glfwKey`, pour chaque disposition.
using NameTable = std::map<int, std::string>;

NameTable qwerty()
{
    NameTable t;
    for (int c = 'A'; c <= 'Z'; ++c)
        t[c] = std::string(1, static_cast<char>(c - 'A' + 'a'));
    t[GLFW_KEY_SEMICOLON] = ";";
    t[GLFW_KEY_COMMA] = ",";
    t[GLFW_KEY_PERIOD] = ".";
    t[GLFW_KEY_SLASH] = "/";
    return t;
}
NameTable azerty() // fr : A et Q, Z et W échangés, M à la place du point-virgule
{
    NameTable t = qwerty();
    t[GLFW_KEY_A] = "q";
    t[GLFW_KEY_Q] = "a";
    t[GLFW_KEY_Z] = "w";
    t[GLFW_KEY_W] = "z";
    t[GLFW_KEY_M] = ",";
    t[GLFW_KEY_SEMICOLON] = "m";
    t[GLFW_KEY_COMMA] = ";";
    return t;
}
NameTable qwertz() // de : Y et Z échangés
{
    NameTable t = qwerty();
    t[GLFW_KEY_Y] = "z";
    t[GLFW_KEY_Z] = "y";
    return t;
}
NameTable dvorak()
{
    NameTable t;
    const std::pair<int, const char *> rows[] = {
        {GLFW_KEY_APOSTROPHE, "-"}, {GLFW_KEY_Q, "'"}, {GLFW_KEY_W, ","}, {GLFW_KEY_E, "."}, {GLFW_KEY_R, "p"},
        {GLFW_KEY_T, "y"},          {GLFW_KEY_Y, "f"}, {GLFW_KEY_U, "g"}, {GLFW_KEY_I, "c"}, {GLFW_KEY_O, "r"},
        {GLFW_KEY_P, "l"},          {GLFW_KEY_A, "a"}, {GLFW_KEY_S, "o"}, {GLFW_KEY_D, "e"}, {GLFW_KEY_F, "u"},
        {GLFW_KEY_G, "i"},          {GLFW_KEY_H, "d"}, {GLFW_KEY_J, "h"}, {GLFW_KEY_K, "t"}, {GLFW_KEY_L, "n"},
        {GLFW_KEY_SEMICOLON, "s"},  {GLFW_KEY_Z, ";"}, {GLFW_KEY_X, "q"}, {GLFW_KEY_C, "j"}, {GLFW_KEY_V, "k"},
        {GLFW_KEY_B, "x"},          {GLFW_KEY_N, "b"}, {GLFW_KEY_M, "m"}, {GLFW_KEY_COMMA, "w"}, {GLFW_KEY_PERIOD, "v"},
        {GLFW_KEY_SLASH, "z"},      {GLFW_KEY_MINUS, "["}, {GLFW_KEY_EQUAL, "]"}, {GLFW_KEY_LEFT_BRACKET, "/"},
        {GLFW_KEY_RIGHT_BRACKET, "="}};
    for (const auto &[key, name] : rows)
        t[key] = name;
    return t;
}

platform::KeyLayout detect(const NameTable &table)
{
    return platform::InputManager::detectKeyLayoutFromKeyNames([&](int key) -> const char * {
        auto it = table.find(key);
        return it == table.end() ? nullptr : it->second.c_str();
    });
}

// La touche logique « lettre » doit pointer sur la touche physique qui porte cette lettre dans la disposition donnée.
void checkEveryLetter(platform::KeyLayout layout, const NameTable &table)
{
    platform::InputManager::setKeyLayout(layout);
    for (int i = 0; i < 26; ++i)
    {
        const int code =
            platform::InputManager::getKeyCode(static_cast<platform::KeyButton>(static_cast<int>(platform::KeyButton::kA) + i));
        const auto it = table.find(code);
        INFO("letter ", static_cast<char>('a' + i), " -> GLFW key ", code);
        REQUIRE(it != table.end());
        CHECK(it->second == std::string(1, static_cast<char>('a' + i)));
    }
}
} // namespace

TEST_SUITE("InputLayout")
{
    TEST_CASE("the layout is recognised from the names of the physical keys")
    {
        CHECK((detect(qwerty()) == platform::KeyLayout::kQwerty));
        CHECK((detect(azerty()) == platform::KeyLayout::kAzerty));
        CHECK((detect(qwertz()) == platform::KeyLayout::kQwertz));
        CHECK((detect(dvorak()) == platform::KeyLayout::kDvorak));
    }

    TEST_CASE("unknown or unavailable key names fall back to QWERTY")
    {
        CHECK((detect(NameTable{}) == platform::KeyLayout::kQwerty)); // glfwGetKeyName renvoie nullptr (pas d'écran, GLFW non initialisé)
        NameTable colemakLike = qwerty();
        colemakLike[GLFW_KEY_E] = "f"; // disposition non gérée : on reste sur QWERTY plutôt que d'en deviner une autre
        CHECK((detect(colemakLike) == platform::KeyLayout::kQwerty));
    }

    TEST_CASE("every logical letter reaches the physical key that prints it, in each layout")
    {
        checkEveryLetter(platform::KeyLayout::kQwerty, qwerty());
        checkEveryLetter(platform::KeyLayout::kAzerty, azerty());
        checkEveryLetter(platform::KeyLayout::kQwertz, qwertz());
        checkEveryLetter(platform::KeyLayout::kDvorak, dvorak());
    }

    TEST_CASE("Dvorak: symbols reach the physical key that prints them")
    {
        const NameTable table = dvorak();
        platform::InputManager::setKeyLayout(platform::KeyLayout::kDvorak);
        const std::pair<platform::KeyButton, const char *> symbols[] = {
            {platform::KeyButton::kMinus, "-"},     {platform::KeyButton::kEquals, "="},     {platform::KeyButton::kLeftBracket, "["},
            {platform::KeyButton::kRightBracket, "]"}, {platform::KeyButton::kSemicolon, ";"}, {platform::KeyButton::kApostrophe, "'"},
            {platform::KeyButton::kComma, ","},     {platform::KeyButton::kPeriod, "."},     {platform::KeyButton::kSlash, "/"}};
        for (const auto &[key, expected] : symbols)
        {
            const auto it = table.find(platform::InputManager::getKeyCode(key));
            INFO("symbol ", expected);
            REQUIRE(it != table.end());
            CHECK(it->second == expected);
        }
    }

    TEST_CASE("AZERTY: the forward key of the camera (Z) is the key labelled Z, not the one labelled W")
    {
        platform::InputManager::setKeyLayout(platform::KeyLayout::kAzerty);
        // la touche physique en position US « W » porte « z »
        CHECK(platform::InputManager::getKeyCode(platform::KeyButton::kZ) == GLFW_KEY_W);
        CHECK(platform::InputManager::getKeyCode(platform::KeyButton::kQ) == GLFW_KEY_A);
        platform::InputManager::setKeyLayout(platform::KeyLayout::kQwerty);
        CHECK(platform::InputManager::getKeyCode(platform::KeyButton::kZ) == GLFW_KEY_Z);
    }

    TEST_CASE("a key that is not mapped has no code")
    {
        platform::InputManager::setKeyLayout(platform::KeyLayout::kQwerty);
        CHECK(platform::InputManager::getKeyCode(static_cast<platform::KeyButton>(250)) == GLFW_KEY_UNKNOWN);
    }
}
