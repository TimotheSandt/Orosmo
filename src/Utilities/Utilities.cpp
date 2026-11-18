#include "Utilities.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/rotate_vector.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include "Logger.h"
#include <engine/core/system.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace
{
fs::path EnsureAppDirectory(const fs::path &basePath)
{
    std::error_code ec;
    fs::path appPath = basePath / "ProceduralGeneration";
    fs::create_directories(appPath, ec);
    return appPath;
}
} // namespace

float lerp(float a, float b, float t) { return a + t * (b - a); }

void rotate(float &x, float &y, float angle)
{
    glm::vec2 v = glm::rotate(glm::vec2(x, y), angle);
    x = v.x;
    y = v.y;
}

void rotate(float &x, float &y, float &z, float angle)
{
    glm::vec3 axis = glm::normalize(glm::vec3(1, 1, 1));
    glm::quat q = glm::angleAxis(angle, axis);
    glm::vec3 rotatedQuat = q * glm::vec3(x, y, z);
    x = rotatedQuat.x;
    y = rotatedQuat.y;
    z = rotatedQuat.z;
}

void rotate(float &x, float &y, float &z, float &w, float angle)
{
    glm::mat4 rotXY(1.0f);
    rotXY[0][0] = std::cos(angle);
    rotXY[0][1] = std::sin(angle);
    rotXY[1][0] = -std::sin(angle);
    rotXY[1][1] = std::cos(angle);

    glm::mat4 rotZW(1.0f);
    rotZW[2][2] = std::cos(angle);
    rotZW[2][3] = std::sin(angle);
    rotZW[3][2] = -std::sin(angle);
    rotZW[3][3] = std::cos(angle);

    glm::mat4 combinedRot = rotZW * rotXY;
    glm::vec4 rotatedCombined = combinedRot * glm::vec4(x, y, z, w);
    x = rotatedCombined.x;
    y = rotatedCombined.y;
    z = rotatedCombined.z;
    w = rotatedCombined.w;
}

std::string GetExecutablePath() { return engine::core::system::executablePath().string(); } // OS : src/platform/system.cpp

std::string GetExecutableDirectory()
{
    const std::string executablePath = GetExecutablePath();
    if (executablePath.empty())
    {
        return {};
    }
    return fs::path(executablePath).parent_path().string();
}

void SetWorkingDirectoryToExe()
{
    const std::string executableDirectory = GetExecutableDirectory();
    if (executableDirectory.empty())
    {
        return;
    }

    std::error_code ec;
    fs::current_path(fs::path(executableDirectory), ec);
    if (ec)
    {
        LOG_ERROR(1, "Failed to set working directory to executable directory: ", ec.message());
    }
}

std::string GetUserDataPath()
{
    fs::path dataDirectory;

    // Dossier de données de la plateforme (APPDATA, Application Support, XDG_DATA_HOME) : src/platform/system.cpp
    if (const fs::path base = engine::core::system::userDataDirectory(); !base.empty())
    {
        dataDirectory = EnsureAppDirectory(base);
    }

    if (dataDirectory.empty())
    {
        return {};
    }

    std::string pathString = dataDirectory.string();
    if (!pathString.empty() && pathString.back() != fs::path::preferred_separator)
    {
        pathString.push_back(fs::path::preferred_separator);
    }
    return pathString;
}

bool ValidateAssets(const std::vector<std::string> &assetPaths, std::vector<std::string> *missingAssets)
{
    bool allAssetsPresent = true;

    if (missingAssets != nullptr)
    {
        missingAssets->clear();
    }

    for (const std::string &assetPathString : assetPaths)
    {
        const fs::path assetPath(assetPathString);
        std::error_code ec;
        const bool exists = fs::exists(assetPath, ec);

        if (!exists || ec)
        {
            allAssetsPresent = false;
            if (missingAssets != nullptr)
            {
                missingAssets->push_back(assetPath.generic_string());
            }
            LOG_ERROR(1, "Missing asset: ", assetPath.generic_string());
        }
    }

    return allAssetsPresent;
}
