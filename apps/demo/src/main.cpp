#include "Game/Game.h"
#include "launch_options.hpp"
#include <engine/core/log.hpp>
#include <engine/platform/window.hpp>

#include <random>
#include <cstdlib>
#include <filesystem>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

int main(int argc, char **argv)
{
    try
    {
        const std::filesystem::path launchDirectory = std::filesystem::current_path();
        engine::core::setWorkingDirectoryToExecutable();

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
            for (const engine::rhi::Backend api : {engine::rhi::Backend::kOpenGL, engine::rhi::Backend::kVulkan, engine::rhi::Backend::kMetal})
            {
                std::cout << demo::backendDisplayName(api) << ": " << demo::backendAvailabilityMessage(api) << '\n';
            }
            return EXIT_SUCCESS;
        }

        // The working directory is the executable's: resolve a relative capture directory against the launch directory.
        const std::string captureDirectory =
            launchOptions.captureDirectory.empty() ? std::string() : (launchDirectory / launchOptions.captureDirectory).string();

        engine::rhi::Backend selectedApi = launchOptions.api;
        const bool shouldPromptForApi = launchOptions.chooseApiInteractively;
        if (shouldPromptForApi && !demo::promptForBackend(std::cin, std::cout, selectedApi))
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
            GET_RESOURCE_PATH("shader/rhi/ui_layer.vert"),   GET_RESOURCE_PATH("shader/rhi/ui_layer.frag")};

        if (!engine::core::validateAssets(requiredAssets))
        {
            FLUSH_LOG_TO_FILE;
            return EXIT_FAILURE;
        }

        int exitCode = EXIT_SUCCESS;
        {
            auto created = Game::create(launchOptions.headless);
            if (!created)
            {
                LOG_ERROR(1, created.error().message);
                std::cerr << created.error().message << '\n';
                Window::Terminate();
                FLUSH_LOG_TO_FILE;
                return EXIT_FAILURE;
            }
            Game &game = **created;
            LOG_INFO("Starting game with graphics API: ", demo::backendDisplayName(selectedApi));
            // The seed is chosen HERE, explicitly (rule 9: no clock inside the generation). Captures use seed 1 unless
            // given; otherwise a random seed is drawn and logged so that the terrain can be replayed with --seed N.
            std::uint32_t terrainSeed = 1u;
            if (launchOptions.headless)
            {
                game.enableCapture(captureDirectory, launchOptions.captureFrames); // sans dossier : seulement le nombre d'images
                terrainSeed = launchOptions.seed.value_or(1u);
            }
            else if (!launchOptions.captureDirectory.empty())
            {
                game.enableCapture(captureDirectory, launchOptions.captureFrames);
                terrainSeed = launchOptions.seed.value_or(1u);
            }
            else if (launchOptions.seed.has_value())
            {
                terrainSeed = *launchOptions.seed;
            }
            else
            {
                terrainSeed = std::random_device{}() & 0x7fffffffu;
                LOG_INFO("Random terrain seed: ", terrainSeed, " (replay it with --seed ", terrainSeed, ")");
            }
            game.setSeed(terrainSeed);
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
                if (!game.captureSucceeded())
                {
                    LOG_ERROR(1, "Capture requested but not completed");
                    exitCode = EXIT_FAILURE;
                }
            }
        } // `Game` libère le périphérique graphique puis la fenêtre
        Window::Terminate();
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
