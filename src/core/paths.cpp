#include "orosmo/core/paths.hpp"

#include "orosmo/core/log.hpp"
#include "orosmo/core/system.hpp"

#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace orosmo::core
{

void setWorkingDirectoryToExecutable()
{
    const fs::path directory = system::executablePath().parent_path(); // OS : src/platform/system.cpp
    if (directory.empty())
    {
        return;
    }
    std::error_code ec;
    fs::current_path(directory, ec);
    if (ec)
    {
        LOG_ERROR(1, "Failed to set working directory to executable directory: ", ec.message());
    }
}

bool validateAssets(const std::vector<std::string> &assetPaths, std::vector<std::string> *missingAssets)
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

} // namespace orosmo::core
