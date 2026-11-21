// Chemins d'application (orosmo/core/paths.hpp), repris des tests de l'ancien `Utilities`.
#include <doctest/doctest.h>

#include <orosmo/core/paths.hpp>
#include <orosmo/core/system.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace core = orosmo::core;

TEST_SUITE("core.paths")
{
    TEST_CASE("the executable path is available and its directory becomes the working directory")
    {
        const fs::path executable = core::system::executablePath();
        REQUIRE(!executable.empty());
        const fs::path original = fs::current_path();
        core::setWorkingDirectoryToExecutable();
        CHECK(fs::equivalent(fs::current_path(), executable.parent_path()));
        fs::current_path(original);
    }

    TEST_CASE("validateAssets reports the missing files")
    {
        const fs::path root = fs::temp_directory_path() / "engine_paths_suite";
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root / "assets", ec);
        const fs::path existing = root / "assets" / "existing.txt";
        const fs::path missing = root / "assets" / "missing.txt";
        std::ofstream(existing.string()) << "asset";

        std::vector<std::string> missingAssets{"stale"};
        CHECK(core::validateAssets({existing.string()}, &missingAssets));
        CHECK(missingAssets.empty());

        CHECK_FALSE(core::validateAssets({existing.string(), missing.string()}, &missingAssets));
        REQUIRE(missingAssets.size() == 1);
        CHECK(missingAssets.front() == missing.generic_string());
        fs::remove_all(root, ec);
    }

    TEST_CASE("resource paths are prefixed with res/ outside release builds")
    {
#ifdef RELEASE
        CHECK(std::string(GET_RESOURCE_PATH("fonts/a.ttf")) == "fonts/a.ttf");
#else
        CHECK(std::string(GET_RESOURCE_PATH("fonts/a.ttf")) == "res/fonts/a.ttf");
#endif
    }
}
