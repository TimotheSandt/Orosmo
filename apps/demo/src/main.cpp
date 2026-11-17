#include "Game/Game.h"
#include "Graphics/Backend/GraphicsAPI.h"
#include "Graphics/Core/GraphicsBackend.h"
#include "Graphics/Core/GraphicsRuntime.h"
#include "Logger.h"

#include <cstdlib>
#include <filesystem>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

int main(int argc, char **argv)
{
    GraphicsAPI selectedApi = GraphicsAPI::OpenGL;
    std::unique_ptr<IGraphicsBackend> graphicsBackend;
    std::unique_ptr<IGraphicsDevice> graphicsDevice;

    try
    {
        const std::filesystem::path launchDirectory = std::filesystem::current_path();
        SetWorkingDirectoryToExe();

#ifdef DEBUG
        SET_LOG_FILE("logs/log.log");
#else
        SET_LOG_FILE_DEFAULT;
#endif

        const GraphicsLaunchOptions launchOptions = ParseGraphicsLaunchOptions(argc, argv);

        if (launchOptions.showHelp)
        {
            PrintGraphicsAPIUsage(std::cout);
            return EXIT_SUCCESS;
        }

        if (launchOptions.listApis)
        {
            for (const GraphicsAPI api : {GraphicsAPI::OpenGL, GraphicsAPI::Vulkan, GraphicsAPI::Metal})
            {
                std::cout << GraphicsAPIToString(api) << ": " << GetGraphicsAPIAvailabilityMessage(api) << '\n';
            }
            return EXIT_SUCCESS;
        }

        // The working directory is the executable's: resolve a relative capture directory against the launch directory.
        const std::string captureDirectory =
            launchOptions.captureDirectory.empty() ? std::string() : (launchDirectory / launchOptions.captureDirectory).string();

        selectedApi = launchOptions.api;
        const bool shouldPromptForApi = launchOptions.chooseApiInteractively;
        if (shouldPromptForApi && !PromptForGraphicsAPI(std::cin, std::cout, selectedApi))
        {
            LOG_ERROR(1, "Interactive graphics API selection failed");
            PrintGraphicsAPIUsage(std::cout);
            FLUSH_LOG_TO_FILE;
            return EXIT_FAILURE;
        }

        graphicsBackend = CreateGraphicsBackend({selectedApi});
        if (!graphicsBackend->IsAvailable())
        {
            LOG_ERROR(1, graphicsBackend->DescribeAvailability());
            PrintGraphicsAPIUsage(std::cout);
            FLUSH_LOG_TO_FILE;
            return EXIT_FAILURE;
        }

        const std::vector<std::string> requiredAssets = {
            GET_RESOURCE_PATH("fonts/Roboto-Regular.ttf"),      GET_RESOURCE_PATH("shader/default.vert"),
            GET_RESOURCE_PATH("shader/default.frag"),           GET_RESOURCE_PATH("shader/upscaling/upscale.vert"),
            GET_RESOURCE_PATH("shader/upscaling/upscale.frag"), GET_RESOURCE_PATH("shader/UI/default.vert"),
            GET_RESOURCE_PATH("shader/UI/default.frag"),        GET_RESOURCE_PATH("shader/UI/container.vert"),
            GET_RESOURCE_PATH("shader/UI/container.frag")};

        if (!ValidateAssets(requiredAssets))
        {
            FLUSH_LOG_TO_FILE;
            return EXIT_FAILURE;
        }

        int exitCode = EXIT_SUCCESS;

        if (!graphicsBackend->Initialize())
        {
            exitCode = EXIT_FAILURE;
        }
        else
        {
            graphicsDevice = graphicsBackend->CreateDevice({});
            BindGraphicsRuntime({.api = selectedApi, .backend = graphicsBackend.get(), .device = graphicsDevice.get()});
            LOG_INFO("Starting game with graphics API: ", GraphicsAPIToString(selectedApi));

            Game game;
            if (!launchOptions.captureDirectory.empty())
            {
                game.enableCapture(captureDirectory, launchOptions.captureFrames);
                game.setSeed(launchOptions.seed.value_or(1u));
            }
            else if (launchOptions.seed.has_value())
            {
                game.setSeed(*launchOptions.seed);
            }
            LOG_TRACE("Game created");
            game.init();
            LOG_TRACE("Game initialized");
            game.run();
            LOG_INFO("Game stopped");
            if (!game.captureSucceeded())
            {
                LOG_ERROR(1, "Capture requested but not completed");
                exitCode = EXIT_FAILURE;
            }
        }

        if (graphicsBackend)
        {
            ClearGraphicsRuntime();
            graphicsDevice.reset();
            graphicsBackend->Shutdown();
        }
        FLUSH_LOG_TO_FILE;
        return exitCode;
    }
    catch (const std::exception &e)
    {
        if (graphicsBackend)
        {
            ClearGraphicsRuntime();
            graphicsDevice.reset();
            graphicsBackend->Shutdown();
        }
        LOG_ERROR(1, "Unhandled exception: ", e.what());
        FLUSH_LOG_TO_FILE;
        return EXIT_FAILURE;
    }
    catch (...)
    {
        if (graphicsBackend)
        {
            ClearGraphicsRuntime();
            graphicsDevice.reset();
            graphicsBackend->Shutdown();
        }
        LOG_ERROR(1, "Unhandled non-standard exception");
        FLUSH_LOG_TO_FILE;
        return EXIT_FAILURE;
    }
}
