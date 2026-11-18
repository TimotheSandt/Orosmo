// M1 bis : services système (largeur de console, chemin de l'exécutable, dossier de données, attente précise).
// Déclarés par core (include/engine/core/system.hpp), définis par platform (src/platform/system.cpp) : seul platform
// inclut <windows.h> / les en-têtes POSIX.
#include <doctest/doctest.h>

#include <engine/core/system.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>

using namespace engine::core::system;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

TEST_SUITE("platform/system")
{
    TEST_CASE("terminalWidth always returns a usable width (80 when there is no terminal)")
    {
        const std::size_t width = terminalWidth();
        CHECK(width >= 1);
        CHECK(width < 100000);
    }

    TEST_CASE("executablePath names the running test program")
    {
        const fs::path path = executablePath();
        REQUIRE_FALSE(path.empty());
        CHECK(path.is_absolute());
        std::error_code ec;
        CHECK(fs::is_regular_file(path, ec));
    }

    TEST_CASE("userDataDirectory follows the platform convention")
    {
#if defined(__linux__)
        const fs::path dir = userDataDirectory();
        const char *xdg = std::getenv("XDG_DATA_HOME");
        const char *home = std::getenv("HOME");
        if (xdg && *xdg)
            CHECK(dir == fs::path(xdg));
        else if (home)
            CHECK(dir == fs::path(home) / ".local" / "share");
        else
            CHECK(dir.empty());
#else
        CHECK_FALSE(userDataDirectory().empty()); // APPDATA (Windows) ou ~/Library/Application Support (macOS)
#endif
    }

    TEST_CASE("the high resolution timer request is paired and harmless")
    {
        const bool granted = beginHighResolutionTimer();
#if defined(_WIN32)
        CHECK(granted); // timeBeginPeriod(1) est accepté sur une machine normale
#else
        CHECK_FALSE(granted); // sans objet hors Windows
#endif
        if (granted)
            endHighResolutionTimer();
    }

    TEST_CASE("preciseSleep waits at least the requested time and returns quickly for nothing to wait for")
    {
        const auto start = std::chrono::steady_clock::now();
        preciseSleep(5ms);
        const auto elapsed = std::chrono::steady_clock::now() - start;
        CHECK(elapsed >= 4ms); // marge pour la granularité du minuteur système ; pas de borne haute (machine chargée)

        // Durée nulle, négative ou minimale : retour immédiat, sans débordement (nanoseconds::min() * fréquence).
        preciseSleep(0ns);
        preciseSleep(-5ms);
        preciseSleep(std::chrono::nanoseconds::min());
        preciseSleep(200us);
    }
}
