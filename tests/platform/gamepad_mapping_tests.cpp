// Correspondances de manettes ajoutées par le moteur (au-delà de la base intégrée à GLFW) : sans manette ni fenêtre.
#include <doctest/doctest.h>

#include <orosmo/platform/gamepad.hpp>

#include <string>

namespace platform = orosmo::platform;

TEST_SUITE("platform/gamepad")
{
    TEST_CASE("extra mappings: the Xbox Wireless Controller in Bluetooth is known, with the buttons used for navigation")
    {
        const std::string text = platform::extraGamepadMappings(nullptr);
        const auto start = text.find("050000005e040000130b000009050000,");
        REQUIRE(start != std::string::npos);
        const std::string line = text.substr(start, text.find('\n', start) - start) + ",";
        // Codes du noyau (BTN_SOUTH, EAST, TL, TR : boutons 0, 1, 6, 7 ; croix : chapeau 0 ; stick gauche : axes 0 et 1).
        for (const char *field : {",a:b0,", ",b:b1,", ",x:b3,", ",y:b4,", ",leftshoulder:b6,", ",rightshoulder:b7,", ",dpup:h0.1,",
                                  ",dpright:h0.2,", ",dpdown:h0.4,", ",dpleft:h0.8,", ",leftx:a0,", ",lefty:a1,", ",platform:Linux,"})
            CHECK_MESSAGE(line.find(field) != std::string::npos, field);
    }

    TEST_CASE("extra mappings: SDL_GAMECONTROLLERCONFIG comes last, so it replaces a mapping of the engine")
    {
        const std::string base = platform::extraGamepadMappings(nullptr);
        const std::string custom = "050000005e040000130b000009050000,Mine,a:b1,platform:Linux,";
        const std::string text = platform::extraGamepadMappings(custom.c_str());
        CHECK(text.starts_with(base));
        CHECK(text.ends_with(custom + "\n"));
        CHECK(platform::extraGamepadMappings("") == base);
    }
}
