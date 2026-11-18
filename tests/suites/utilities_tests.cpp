#include "support/test_support.hpp"

#include "Utilities/Utilities.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace tests
{

namespace
{

double LengthSquared2(float x, float y) { return static_cast<double>(x) * x + static_cast<double>(y) * y; }

double LengthSquared3(float x, float y, float z)
{
    return static_cast<double>(x) * x + static_cast<double>(y) * y + static_cast<double>(z) * z;
}

double LengthSquared4(float x, float y, float z, float w)
{
    return static_cast<double>(x) * x + static_cast<double>(y) * y + static_cast<double>(z) * z + static_cast<double>(w) * w;
}

} // namespace

TEST_SUITE("Utilities")
{
    TEST_CASE("lerp returns start at zero")
    {
AssertNear(lerp(5.0f, 10.0f, 0.0f), 5.0, 1e-6, "lerp should return the start value");
    }

    TEST_CASE("lerp returns midpoint")
    {
AssertNear(lerp(10.0f, 20.0f, 0.5f), 15.0, 1e-6, "lerp should compute the midpoint");
    }

    TEST_CASE("rotate 2d keeps magnitude")
    {
        float x = 1.0f;
        float y = 0.0f;
        rotate(x, y, 3.1415926535f / 2.0f);
        AssertNear(LengthSquared2(x, y), 1.0, 1e-5, "2D rotation should preserve vector length");
        AssertNear(x, 0.0, 1e-4, "rotated x should be close to zero");
        AssertNear(y, 1.0, 1e-4, "rotated y should be close to one");
    }

    TEST_CASE("rotate 3d keeps magnitude")
    {
        float x = 1.0f;
        float y = 2.0f;
        float z = 3.0f;
        const double original = LengthSquared3(x, y, z);
        rotate(x, y, z, 0.35f);
        AssertNear(LengthSquared3(x, y, z), original, 1e-4, "3D rotation should preserve vector length");
    }

    TEST_CASE("rotate 4d keeps magnitude")
    {
        float x = 1.0f;
        float y = 2.0f;
        float z = 3.0f;
        float w = 4.0f;
        const double original = LengthSquared4(x, y, z, w);
        rotate(x, y, z, w, 0.2f);
        AssertNear(LengthSquared4(x, y, z, w), original, 1e-3, "4D rotation should preserve vector length");
    }

    TEST_CASE("executable path is available")
    {
Assert(!GetExecutablePath().empty(), "Executable path should not be empty");
    }

    TEST_CASE("executable directory matches executable path")
    {
        const fs::path executablePath(GetExecutablePath());
        Assert(!executablePath.empty(), "Executable path should be available");
        AssertEqual(GetExecutableDirectory(), executablePath.parent_path().string(),
                    "Executable directory should match the executable path parent");
    }

    TEST_CASE("set working directory moves to executable directory")
    {
        const fs::path originalPath = fs::current_path();
        SetWorkingDirectoryToExe();
        AssertEqual(fs::current_path().string(), GetExecutableDirectory(),
                    "Working directory should become the executable directory");
        fs::current_path(originalPath);
    }

    TEST_CASE("user data path is available")
    {
        const std::string userDataPath = GetUserDataPath();
        Assert(!userDataPath.empty(), "User data path should not be empty");
        Assert(fs::exists(fs::path(userDataPath)), "User data directory should exist");
    }

    TEST_CASE("validate assets reports missing files")
    {
        const fs::path tempRoot = fs::temp_directory_path() / "proceduralgeneration_utilities_suite";
        std::error_code ec;
        fs::remove_all(tempRoot, ec);
        fs::create_directories(tempRoot / "assets", ec);

        const fs::path existing = tempRoot / "assets" / "existing.txt";
        const fs::path missing = tempRoot / "assets" / "missing.txt";
        std::ofstream(existing.string()) << "asset";

        std::vector<std::string> missingAssets;
        Assert(ValidateAssets({existing.string()}, &missingAssets), "Existing assets should validate");
        Assert(missingAssets.empty(), "Missing asset list should stay empty when every asset exists");

        Assert(!ValidateAssets({existing.string(), missing.string()}, &missingAssets), "Missing assets should fail validation");
        AssertEqual(missingAssets.size(), static_cast<size_t>(1), "One missing asset should be reported");
        AssertEqual(missingAssets.front(), missing.generic_string(), "Missing asset path should match the missing file");

        fs::remove_all(tempRoot, ec);
    }
}

} // namespace tests
