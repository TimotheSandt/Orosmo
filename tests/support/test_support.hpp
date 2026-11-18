// Tests : support commun au-dessus de doctest (TEST-01).
// Les anciennes aides Assert/AssertEqual/AssertNear sont conservées pour que les corps de test
// existants restent identiques ; elles s'appuient maintenant sur doctest.
#pragma once

#include <doctest/doctest.h>

// GCC 13 signale à tort -Warray-bounds sur les copies de listes d'initialisation (`v = {Struct{...}}`) sous ASan -O3 (G34) :
// faux positif de la bibliothèque standard, constaté plusieurs fois dans les tests du RHI. Limité aux tests, à GCC.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif

#include <cmath>
#include <string>

namespace tests
{

inline void Assert(bool condition, const std::string &message) { REQUIRE_MESSAGE(condition, message); }

inline void AssertNear(double actual, double expected, double epsilon, const std::string &message)
{
    REQUIRE_MESSAGE(std::fabs(actual - expected) <= epsilon, message << " (actual=" << actual << ", expected=" << expected << ")");
}

template <typename T, typename U> void AssertEqual(const T &actual, const U &expected, const std::string &message)
{
    REQUIRE_MESSAGE(actual == expected, message);
}

} // namespace tests

// Some tests need a real driver (Vulkan creates its device through the loader). On a machine without one
// (CI runners, headless containers) the test reports a warning and ends instead of dereferencing a null device.
#define SKIP_TEST_IF_NULL(pointer, reason)                    \
    do                                                       \
    {                                                        \
        if ((pointer) == nullptr)                            \
        {                                                    \
            WARN_MESSAGE(false, "Skipped (no driver): " reason); \
            return;                                          \
        }                                                    \
    } while (0)
