#include <doctest/doctest.h>

#include <orosmo/core/log.hpp>
#include <orosmo/core/system.hpp>
#include <orosmo/platform/dynamic_library.hpp>

#include <cstddef>
#include <filesystem>
#include <string>

using orosmo::core::ErrorCode;
using orosmo::platform::DynamicLibrary;
using orosmo::platform::DynamicLibraryOptions;
namespace fs = std::filesystem;

namespace
{
// Construite par `make test` à côté de l'exécutable des tests (apps/demo/Makefile, tests/platform/test_module/).
fs::path testModulePath()
{
    return orosmo::core::system::executablePath().parent_path() / ("test_module" + std::string(DynamicLibrary::extension()));
}
} // namespace

TEST_SUITE("platform/dynamic_library")
{
    TEST_CASE("DynamicLibrary: a missing file is kNotFound; an empty object has no symbol")
    {
        auto missing = DynamicLibrary::load("no/such/library" + std::string(DynamicLibrary::extension()));
        REQUIRE_FALSE(missing.has_value());
        CHECK((missing.error().code == ErrorCode::kNotFound));

        DynamicLibrary empty;
        CHECK_FALSE(empty.isLoaded());
        auto sym = empty.symbol("orosmoTestModuleAdd");
        REQUIRE_FALSE(sym.has_value());
        CHECK((sym.error().code == ErrorCode::kInvalidArgument));
    }

    TEST_CASE("DynamicLibrary: loads the test module, calls a function, refuses a missing symbol")
    {
        const fs::path path = testModulePath();
        REQUIRE_MESSAGE(fs::exists(path), "test module not built: " << path.string());

        auto library = DynamicLibrary::load(path);
        REQUIRE_MESSAGE(library.has_value(), (library ? std::string() : library.error().describe()));
        CHECK(library->isLoaded());

        auto add = library->function<int(int, int)>("orosmoTestModuleAdd");
        REQUIRE(add.has_value());
        CHECK((*add)(40, 2) == 42);

        auto absent = library->symbol("orosmoTestModuleDoesNotExist");
        REQUIRE_FALSE(absent.has_value());
        CHECK((absent.error().code == ErrorCode::kNotFound));
        CHECK(absent.error().message.find("orosmoTestModuleDoesNotExist") != std::string::npos);
    }

    TEST_CASE("DynamicLibrary: the library uses the host's engine (same Logger instance, no copy of the engine)")
    {
        auto library = DynamicLibrary::load(testModulePath());
        REQUIRE_MESSAGE(library.has_value(), (library ? std::string() : library.error().describe()));
        auto log = library->function<std::size_t(const char *)>("orosmoTestModuleLog");
        REQUIRE(log.has_value());

        orosmo::core::Logger::Clear();
        const std::size_t before = orosmo::core::Logger::KeptLogCount();
        const std::size_t seenByLibrary = (*log)("message written by the dynamic library");
        const std::size_t seenByHost = orosmo::core::Logger::KeptLogCount();
        CHECK(seenByHost == before + 1);    // le message de la bibliothèque est dans le journal de l'hôte
        CHECK(seenByLibrary == seenByHost); // et la bibliothèque lit ce même journal
        orosmo::core::Logger::Clear();
    }

    TEST_CASE("DynamicLibrary: loads a uniquely named copy, removed on unload; two versions can be loaded at once")
    {
        const fs::path path = testModulePath();
        auto first = DynamicLibrary::load(path);
        auto second = DynamicLibrary::load(path);
        REQUIRE(first.has_value());
        REQUIRE(second.has_value());

        const fs::path firstCopy = first->loadedPath();
        CHECK(first->sourcePath() == fs::absolute(path));
        CHECK(firstCopy != first->sourcePath());
        CHECK(firstCopy != second->loadedPath());
        CHECK(firstCopy.parent_path() == first->sourcePath().parent_path());
        CHECK(firstCopy.extension() == path.extension());
        CHECK(fs::exists(firstCopy));

        auto addSecond = second->function<int(int, int)>("orosmoTestModuleAdd");
        REQUIRE(addSecond.has_value());

        first->unload();
        CHECK_FALSE(first->isLoaded());
        CHECK_FALSE(fs::exists(firstCopy));
        CHECK(fs::exists(path));        // l'original n'est jamais touché
        CHECK((*addSecond)(1, 2) == 3); // l'autre version reste utilisable

        DynamicLibrary moved = std::move(*second);
        CHECK(moved.isLoaded());
        CHECK_FALSE(second->isLoaded());
    }

    TEST_CASE("DynamicLibrary: each loaded copy has its own static state, fresh after a reload (no GNU unique symbol)")
    {
        auto first = DynamicLibrary::load(testModulePath());
        auto second = DynamicLibrary::load(testModulePath());
        REQUIRE(first.has_value());
        REQUIRE(second.has_value());
        auto incFirst = first->function<int()>("orosmoTestModuleIncrement");
        auto incSecond = second->function<int()>("orosmoTestModuleIncrement");
        REQUIRE(incFirst.has_value());
        REQUIRE(incSecond.has_value());
        CHECK((*incFirst)() == 1);
        CHECK((*incFirst)() == 2);
        const int secondValue = (*incSecond)();

        first->unload();
        second->unload();
        auto reloaded = DynamicLibrary::load(testModulePath());
        REQUIRE(reloaded.has_value());
        auto incReloaded = reloaded->function<int()>("orosmoTestModuleIncrement");
        REQUIRE(incReloaded.has_value());
        const int reloadedValue = (*incReloaded)();
#if defined(__APPLE__)
        // dyld peut fusionner les définitions faibles de deux images : comportement relevé, pas encore exigé (G234).
        MESSAGE("macOS: second copy counter " << secondValue << ", after reload " << reloadedValue << " (1 and 1 = isolated)");
#else
        CHECK(secondValue == 1);   // l'autre copie ne partage pas la variable statique
        CHECK(reloadedValue == 1); // déchargée pour de bon : état neuf
#endif
    }

    TEST_CASE("DynamicLibrary: the copy path stays absolute, so unload removes it after a change of current directory")
    {
        const fs::path previous = fs::current_path();
        fs::current_path(testModulePath().parent_path());
        DynamicLibraryOptions options;
        options.copyDirectory = "."; // relatif au dossier courant au moment du chargement
        auto library = DynamicLibrary::load(testModulePath().filename(), options);
        fs::current_path(previous);
        REQUIRE(library.has_value());
        const fs::path copy = library->loadedPath();
        CHECK(copy.is_absolute());
        CHECK(library->sourcePath().is_absolute());
        CHECK(fs::exists(copy));
        library->unload();
        CHECK_FALSE(fs::exists(copy));
    }

    TEST_CASE("DynamicLibrary: copyBeforeLoad = false loads the file itself")
    {
        DynamicLibraryOptions options;
        options.copyBeforeLoad = false;
        auto library = DynamicLibrary::load(testModulePath(), options);
        REQUIRE(library.has_value());
        CHECK(library->loadedPath() == library->sourcePath());
        library->unload();
        CHECK(fs::exists(testModulePath()));
    }
}
