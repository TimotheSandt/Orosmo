#include "Game/Game.h"
#include "Sim/agent_simulation.hpp"
#include "launch_options.hpp"

#include <orosmo/core/log.hpp>
#include <orosmo/core/paths.hpp>
#include <orosmo/core/profiler.hpp>
#include <orosmo/platform/window.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace platform = orosmo::platform;

// Bilan lu par `tools/perf_compare.py` : durée totale de la boucle et moyennes du Profiler (le temps de "Render"
// est mesuré par les requêtes de temps du GPU quand le backend les fournit, sinon par l'horloge du processeur).
// `frameMs` : durée de chaque image (Game::frameTimes) ; médiane, première image et maximum (pics : premier affichage).
// `waitMs` : part bloquée de chaque image (Game::frameWaitTimes : image précédente au GPU, vsync) ; travail = durée - attente.
static float median(std::vector<float> values)
{
    const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::nth_element(values.begin(), middle, values.end());
    return *middle;
}

static void reportPerformance(std::chrono::steady_clock::duration elapsed, const std::vector<float> &frameMs, const std::vector<float> &waitMs)
{
    using orosmo::core::Profiler;
    LOG_INFO("Perf: loopMs=", std::chrono::duration<double, std::milli>(elapsed).count());
    if (!frameMs.empty())
    {
        const float frame = median(frameMs);
        std::vector<float> workMs(frameMs.size());
        for (std::size_t i = 0; i < frameMs.size(); ++i)
        {
            workMs[i] = frameMs[i] - (i < waitMs.size() ? waitMs[i] : 0.0f);
        }
        const float work = median(workMs);
        const float wait = waitMs.empty() ? 0.0f : median(waitMs);
        LOG_INFO("Perf: frameMedianMs=", frame);
        LOG_INFO("Perf: workMedianMs=", work);
        LOG_INFO("Perf: waitMedianMs=", wait);
        LOG_INFO("Perf: firstFrameMs=", frameMs.front());
        LOG_INFO("Perf: frameMaxMs=", *std::max_element(frameMs.begin(), frameMs.end()));
        LOG_INFO("Perf: frames=", frameMs.size(), " median frame ", frame, " ms = CPU work ", work, " ms + waiting ", wait,
                 " ms (GPU, vsync); ~", work > 0.0f ? 1000.0f / work : 0.0f, " FPS if the CPU were the only limit");
    }
    for (const char *name : {"Render", "RenderGraph/depth-prepass", "RenderGraph/forwardplus.clusters", "RenderGraph/gpudriven.cull",
                             "RenderGraph/gpudriven.hiz", "RenderGraph/scene", "RenderGraph/upscale",
                             "RenderGraph/ui", "RenderGraph/present", "SwapBuffers"})
    {
        LOG_INFO("Perf: ", name, " avgMs=", Profiler::GetAverageTimeMs(name));
    }
}

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

        // Mode simulation (SIM-01, A79 lot L3) : ni fenêtre ni périphérique graphique, aucune ressource lue.
        if (launchOptions.simAgents > 0)
        {
            const int simulationExit = demo::runSimulationMode(launchOptions, std::cout);
            FLUSH_LOG_TO_FILE;
            return simulationExit;
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

        // Disponibilité dans cette construction ; la création du périphérique peut encore échouer (repli plus bas).
        const auto refuse = [](const std::string &message)
        {
            LOG_ERROR(1, message);
            std::cerr << message << '\n';
            demo::printUsage(std::cout);
            FLUSH_LOG_TO_FILE;
            return EXIT_FAILURE;
        };
        demo::BackendDecision decision = demo::resolveBackend(selectedApi, launchOptions.strictBackend, demo::backendAvailability(selectedApi));
        if (decision.refused)
        {
            return refuse(decision.message);
        }
        if (decision.fellBack)
        {
            LOG_WARNING(decision.message);
            std::cerr << decision.message << '\n';
        }
        selectedApi = decision.backend;

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
            if (!created && created.error().code == orosmo::core::ErrorCode::kUnsupported && selectedApi == orosmo::rhi::Backend::kVulkan)
            {
                // Vulkan ne démarre pas (pas de chargeur, pas de GPU 1.3...) : le Game raté a déjà détruit sa fenêtre ; le suivant
                // en recrée une avec les indices d'OpenGL (prepareWindowHints).
                decision = demo::resolveBackend(selectedApi, launchOptions.strictBackend, demo::BackendAvailability::kAvailable, created.error().message);
                if (decision.refused)
                {
                    platform::Window::terminate();
                    return refuse(decision.message);
                }
                LOG_WARNING(decision.message);
                std::cerr << decision.message << '\n';
                selectedApi = decision.backend;
                created = Game::create(launchOptions.headless, launchOptions.width, launchOptions.height, selectedApi);
            }
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
            game.setHudLevel(launchOptions.hudLevel);
            game.setInitialTheme(launchOptions.theme);
            game.setInitialTab(launchOptions.tab);
            game.setDebugTools(launchOptions.debugTools, launchOptions.debugTab);
            game.setCaptureInteraction(launchOptions.hoverId, launchOptions.clickId);
            game.setCaptureFocus(launchOptions.focusId);
            game.setFramePacing(demo::framePacingFor(launchOptions));
            game.setGpuTimings(launchOptions.gpuTimings);
            game.setDirectPresentation(launchOptions.directPresentation);
            game.setInstanceCount(launchOptions.instances);
            game.setGpuDriven(launchOptions.gpuDriven);
            game.setDebugView(launchOptions.debugView, launchOptions.freezeFrustum);
            game.setShadowsEnabled(launchOptions.shadows);
            game.setShadowSettings(demo::shadowSettingsForQuality(launchOptions.shadowQuality));
            game.setPostMode(launchOptions.post);
            game.setAntiAliasing(launchOptions.antiAliasing);
            game.setLighting(launchOptions.lighting, launchOptions.pointLights);
            game.setMixedScene(launchOptions.mixedScene);
            game.setAnimScene(launchOptions.animScene);
            game.setTransparentShadow(launchOptions.transparentShadow);
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
                const auto runStart = std::chrono::steady_clock::now();
                game.run();
                reportPerformance(std::chrono::steady_clock::now() - runStart, game.frameTimes(), game.frameWaitTimes());
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
