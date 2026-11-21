#include "Game/Game.h"
#include "launch_options.hpp"

#include <orosmo/core/log.hpp>
#include <orosmo/core/paths.hpp>
#include <orosmo/platform/window.hpp>

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace platform = orosmo::platform;

int main(int argc, char **argv)
{
    try
    {
        const std::filesystem::path launchDirectory = std::filesystem::current_path();
        orosmo::core::setWorkingDirectoryToExecutable();

#ifdef DEBUG
        SET_LOG_FILE("logs/log.log");
#else
        SET_LOG_FILE_DEFAULT;
#endif

        const auto parsedOptions = demo::parseLaunchOptions(argc, argv);
        if (!parsedOptions)
        {
            LOG_ERROR(1, parsedOptions.error().message);
            std::cerr << parsedOptions.error().message << '\n';
            demo::printUsage(std::cout);
            FLUSH_LOG_TO_FILE;
            return EXIT_FAILURE;
        }
        const demo::LaunchOptions &launchOptions = *parsedOptions;

        if (launchOptions.showHelp)
        {
            demo::printUsage(std::cout);
            return EXIT_SUCCESS;
        }

        if (launchOptions.listApis)
        {
            for (const orosmo::rhi::Backend api : {orosmo::rhi::Backend::kOpenGL, orosmo::rhi::Backend::kVulkan, orosmo::rhi::Backend::kMetal})
            {
                std::cout << demo::backendDisplayName(api) << ": " << demo::backendAvailabilityMessage(api) << '\n';
            }
            return EXIT_SUCCESS;
        }

        // The working directory is the executable's: resolve a relative capture directory against the launch directory.
        const std::string captureDirectory =
            launchOptions.captureDirectory.empty() ? std::string() : (launchDirectory / launchOptions.captureDirectory).string();

        orosmo::rhi::Backend selectedApi = launchOptions.api;
        if (launchOptions.chooseApiInteractively && !demo::promptForBackend(std::cin, std::cout, selectedApi))
        {
            LOG_ERROR(1, "Interactive graphics API selection failed");
            demo::printUsage(std::cout);
            FLUSH_LOG_TO_FILE;
            return EXIT_FAILURE;
        }

        // Seul le backend OpenGL du RHI existe avant M4 (Vulkan) et M9 (Metal) : refus clair avec la raison (RHI-04).
        if (!demo::isBackendAvailable(selectedApi))
        {
            LOG_ERROR(1, demo::backendAvailabilityMessage(selectedApi));
            std::cerr << demo::backendAvailabilityMessage(selectedApi) << '\n';
            demo::printUsage(std::cout);
            FLUSH_LOG_TO_FILE;
            return EXIT_FAILURE;
        }

        const std::vector<std::string> requiredAssets = {
            GET_RESOURCE_PATH("fonts/Roboto-Regular.ttf"),   GET_RESOURCE_PATH("shader/rhi/mesh.vert"),     GET_RESOURCE_PATH("shader/rhi/mesh.frag"),
            GET_RESOURCE_PATH("shader/rhi/blit.vert"),       GET_RESOURCE_PATH("shader/rhi/blit.frag"),     GET_RESOURCE_PATH("shader/rhi/ui_rect.vert"),
            GET_RESOURCE_PATH("shader/rhi/ui_rect.frag"),    GET_RESOURCE_PATH("shader/rhi/ui_glyph.vert"), GET_RESOURCE_PATH("shader/rhi/ui_glyph.frag"),
            GET_RESOURCE_PATH("shader/rhi/ui_image.vert"),   GET_RESOURCE_PATH("shader/rhi/ui_image.frag"), GET_RESOURCE_PATH("shader/rhi/ui_layer.vert"),
            GET_RESOURCE_PATH("shader/rhi/ui_layer.frag")};

        if (!orosmo::core::validateAssets(requiredAssets))
        {
            FLUSH_LOG_TO_FILE;
            return EXIT_FAILURE;
        }

        int exitCode = EXIT_SUCCESS;
        {
            auto created = Game::create(launchOptions.headless, launchOptions.width, launchOptions.height, selectedApi);
            if (!created)
            {
                LOG_ERROR(1, created.error().message);
                std::cerr << created.error().message << '\n';
                platform::Window::terminate();
                FLUSH_LOG_TO_FILE;
                return EXIT_FAILURE;
            }
            Game &game = **created;
            LOG_INFO("Starting demo with graphics API: ", demo::backendDisplayName(selectedApi));
            if (launchOptions.headless || !launchOptions.captureDirectory.empty())
            {
                game.enableCapture(captureDirectory, launchOptions.captureFrames);
            }
            game.setUiMode(launchOptions.uiMode);
            game.setInitialTheme(launchOptions.theme);
            game.setInitialTab(launchOptions.tab);
            game.setCaptureInteraction(launchOptions.hoverId, launchOptions.clickId);
            game.setVSync(launchOptions.vsync);
            LOG_TRACE("Game created");
            if (auto ready = game.init(); !ready)
            {
                LOG_ERROR(1, ready.error().message);
                std::cerr << ready.error().message << '\n';
                exitCode = EXIT_FAILURE;
            }
            else
            {
                LOG_TRACE("Game initialized");
                game.run();
                LOG_INFO("Game stopped");
                if (!game.headlessSucceeded())
                {
                    LOG_ERROR(1, "Headless: a frame failed");
                    exitCode = EXIT_FAILURE;
                }
                else if (!game.captureSucceeded())
                {
                    LOG_ERROR(1, "Capture requested but not completed");
                    exitCode = EXIT_FAILURE;
                }
            }
        } // `Game` libère le périphérique graphique puis la fenêtre
        platform::Window::terminate();
        FLUSH_LOG_TO_FILE;
        return exitCode;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(1, "Unhandled exception: ", e.what());
        FLUSH_LOG_TO_FILE;
        return EXIT_FAILURE;
    }
    catch (...)
    {
        LOG_ERROR(1, "Unhandled non-standard exception");
        FLUSH_LOG_TO_FILE;
        return EXIT_FAILURE;
    }
}
