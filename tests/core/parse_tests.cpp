// Lecture de nombres (core/parse.hpp) : même résultat que std::from_chars sur tous les systèmes (macOS Intel compris).
#include <doctest/doctest.h>

#include <orosmo/core/parse.hpp>

#include <cmath>
#include <limits>

namespace core = orosmo::core;

TEST_SUITE("core.parse")
{
    TEST_CASE("decimal and scientific forms are read exactly, the whole text is consumed")
    {
        CHECK(core::parseDouble("0.1") == 0.1);
        CHECK(core::parseDouble("-2.5e3") == -2500.0);
        CHECK(core::parseDouble("1e301") == 1e301);
        CHECK(core::parseFloat("0.1") == 0.1f);
        CHECK(core::parseFloat("3.4e38") == 3.4e38f);
        CHECK(core::parseFloat("16") == 16.0f);
        CHECK(core::parseDouble("4503599627370497") == 4503599627370497.0);
    }

    TEST_CASE("what std::from_chars refuses is refused: empty, spaces, plus sign, hexadecimal, trailing text, out of range")
    {
        for (const char *bad : {"", " 1", "1 ", "+1", "0x10", "1.5px", "abc", "-", "1e"})
        {
            CAPTURE(bad);
            CHECK_FALSE(core::parseDouble(bad).has_value());
            CHECK_FALSE(core::parseFloat(bad).has_value());
        }
        CHECK_FALSE(core::parseDouble("1e400").has_value());
        CHECK_FALSE(core::parseFloat("1e39").has_value());
    }

    TEST_CASE("representable subnormals are read, an underflow to zero is refused")
    {
        const auto smallFloat = core::parseFloat("1e-40"); // sous FLT_MIN (1.17e-38), au-dessus du plus petit subnormal (1.4e-45)
        REQUIRE(smallFloat.has_value());
        CHECK(*smallFloat > 0.0f);
        CHECK(*smallFloat < std::numeric_limits<float>::min());
        const auto smallDouble = core::parseDouble("1e-310");
        REQUIRE(smallDouble.has_value());
        CHECK(*smallDouble > 0.0);
        CHECK(*smallDouble < std::numeric_limits<double>::min());
        CHECK_FALSE(core::parseFloat("1e-50").has_value());
        CHECK_FALSE(core::parseDouble("1e-400").has_value());
        CHECK(core::parseDouble("0") == 0.0);
        CHECK(core::parseDouble("0e-400") == 0.0); // zéro écrit, pas un sous-flux
    }

    TEST_CASE("the result does not depend on a decimal comma locale")
    {
        CHECK(core::parseDouble("1,5") == std::nullopt);
        CHECK(core::parseDouble("1.5") == 1.5);
    }
}
