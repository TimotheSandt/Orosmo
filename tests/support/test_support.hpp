// Tests : support commun au-dessus de doctest (TEST-01).
// Les anciennes aides Assert/AssertEqual/AssertNear sont conservées pour que les corps de test
// existants restent identiques ; elles s'appuient maintenant sur doctest.
#pragma once

#include <doctest/doctest.h>

// GCC 13 signale à tort -Warray-bounds sur les copies de listes d'initialisation (`v = {Struct{...}}`) sous ASan -O3 (G34) :
// faux positif de la bibliothèque standard, constaté plusieurs fois dans les tests du RHI. Limité aux tests, à GCC.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Warray-bounds"
#pragma GCC diagnostic ignored "-Wstringop-overread"
#endif

#include <cmath>
#include <cstdlib>
#include <string>

namespace tests
{

// Vrai si l'environnement de test promet un GPU et un affichage (variable ENGINE_TEST_REQUIRE_GPU différente de "0").
inline bool gpuRequired()
{
    const char *value = std::getenv("ENGINE_TEST_REQUIRE_GPU");
    return value != nullptr && std::string(value) != "0" && !std::string(value).empty();
}

// Vrai pendant `make demo-test-unit` (variable ENGINE_TEST_NO_GPU posée par le Makefile) : les suites de ce passage ne doivent
// créer aucun périphérique graphique (jobs de CI sans GPU, TSan). Un cas qui en crée un doit être dans une suite de
// GPU_TEST_SUITES du Makefile (par exemple « <suite>/gpu » par `* doctest::test_suite(...)`).
inline bool gpuForbidden()
{
    const char *value = std::getenv("ENGINE_TEST_NO_GPU");
    return value != nullptr && std::string(value) != "0" && !std::string(value).empty();
}

// Échec explicite si un test sans GPU s'apprête à créer un périphérique (voir gpuForbidden).
#define REQUIRE_GPU_ALLOWED()                                                                                                      \
    do                                                                                                                             \
    {                                                                                                                              \
        if (tests::gpuForbidden())                                                                                                 \
        {                                                                                                                          \
            FAIL("graphics device created by a test outside GPU_TEST_SUITES (ENGINE_TEST_NO_GPU): move it to a GPU suite");        \
        }                                                                                                                          \
    } while (0)

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
// Sans pilote ni affichage, un test GPU/fenêtre s'arrête avec un avertissement. Si ENGINE_TEST_REQUIRE_GPU est posée (CI Linux
// sous xvfb, où OpenGL 4.3 est garanti), l'absence est un ÉCHEC : une régression de création ne passe plus pour un environnement
// sans affichage (G100).
#define SKIP_TEST_IF_NULL(pointer, reason)                                                              \
    do                                                                                                  \
    {                                                                                                   \
        if ((pointer) == nullptr)                                                                       \
        {                                                                                               \
            if (tests::gpuRequired())                                                                   \
            {                                                                                           \
                FAIL("Required by ENGINE_TEST_REQUIRE_GPU but unavailable: " reason);                   \
            }                                                                                           \
            WARN_MESSAGE(false, "Skipped (no driver): " reason);                                        \
            return;                                                                                     \
        }                                                                                               \
    } while (0)
