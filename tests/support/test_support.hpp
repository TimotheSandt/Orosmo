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

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>

namespace tests
{

// Vrai si l'environnement de test promet un GPU et un affichage (variable ENGINE_TEST_REQUIRE_GPU différente de "0").
inline bool gpuRequired()
{
    const char *value = std::getenv("ENGINE_TEST_REQUIRE_GPU");
    return value != nullptr && std::string(value) != "0" && !std::string(value).empty();
}

// Vrai si le nom de backend `name` (« opengl », « vulkan »…) figure dans la liste `list` de ENGINE_TEST_BACKENDS (séparateurs
// virgule, espace ou point-virgule, sans casse). Une liste absente ou vide désigne tous les backends.
inline bool backendListed(const char *list, std::string_view name)
{
    if (list == nullptr || *list == '\0')
    {
        return true;
    }
    std::string current;
    const auto matches = [&]
    {
        std::transform(current.begin(), current.end(), current.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const bool same = current == name;
        current.clear();
        return same;
    };
    for (const char *p = list; *p != '\0'; ++p)
    {
        if (*p == ',' || *p == ' ' || *p == ';')
        {
            if (matches())
            {
                return true;
            }
        }
        else
        {
            current.push_back(*p);
        }
    }
    return matches();
}

// Vrai si l'absence du backend `name` doit faire échouer le test : ENGINE_TEST_REQUIRE_GPU est posée ET ENGINE_TEST_BACKENDS (si elle
// est posée) le désigne. La CI macOS (`ENGINE_TEST_BACKENDS=vulkan`) exige ainsi Vulkan sans exiger OpenGL 4.3, que macOS n'a pas
// (M9-L1, comme `backendsToRun` de tests/rhi/backends.hpp).
inline bool backendRequired(std::string_view name) { return gpuRequired() && backendListed(std::getenv("ENGINE_TEST_BACKENDS"), name); }

// Vrai si les tests doivent trouver la couche de validation active (ENGINE_TEST_REQUIRE_VALIDATION différente de "0", posée par la CI
// qui l'installe) : sans elle, « 0 message de validation » ne prouve rien.
inline bool validationRequired()
{
    const char *value = std::getenv("ENGINE_TEST_REQUIRE_VALIDATION");
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

// Comme SKIP_TEST_IF_NULL pour un test propre à un backend (`backendName` : "opengl", "vulkan") : l'absence n'échoue que si ce backend
// est exigé (tests::backendRequired), pas quand ENGINE_TEST_BACKENDS en désigne un autre.
#define SKIP_TEST_IF_NULL_FOR(pointer, backendName, reason)                                             \
    do                                                                                                  \
    {                                                                                                   \
        if ((pointer) == nullptr)                                                                       \
        {                                                                                               \
            if (tests::backendRequired(backendName))                                                    \
            {                                                                                           \
                FAIL("Required by ENGINE_TEST_REQUIRE_GPU but unavailable: " reason);                   \
            }                                                                                           \
            WARN_MESSAGE(false, "Skipped (no driver, " backendName " not required): " reason);          \
            return;                                                                                     \
        }                                                                                               \
    } while (0)
