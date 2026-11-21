#include "support/test_support.hpp"

#include "launch_options.hpp"
#include <orosmo/rhi/device.hpp>

#include <sstream>

namespace tests
{

using demo::BackendAvailability;
using orosmo::rhi::Backend;

TEST_SUITE("Demo launch options")
{
    TEST_CASE("parse opengl api aliases")
    {
        Assert(demo::parseBackend("opengl").value() == Backend::kOpenGL, "opengl should parse to OpenGL");
        Assert(demo::parseBackend("GL").value() == Backend::kOpenGL, "GL should parse to OpenGL");
    }

    TEST_CASE("parse vulkan and metal api aliases")
    {
        Assert(demo::parseBackend("vk").value() == Backend::kVulkan, "vk should parse to Vulkan");
        Assert(demo::parseBackend("metal").value() == Backend::kMetal, "metal should parse to Metal");
    }

    TEST_CASE("invalid api token is rejected")
    {
        Assert(!demo::parseBackend("directx").has_value(), "Unknown API should be rejected");
    }

    TEST_CASE("launch options default to opengl")
    {
        const char *argv[] = {"ProceduralGeneration"};
        const demo::LaunchOptions options = *demo::parseLaunchOptions(1, argv);
        Assert(options.api == Backend::kOpenGL, "Default graphics API should stay OpenGL");
        Assert(!options.chooseApiInteractively, "Interactive selection should be disabled by default");
        Assert(!options.apiExplicitlyRequested, "Default launch should not mark the API as explicit");
    }

    TEST_CASE("launch options parse explicit api")
    {
        const char *argv[] = {"ProceduralGeneration", "--api=vulkan"};
        const demo::LaunchOptions options = *demo::parseLaunchOptions(2, argv);
        Assert(options.api == Backend::kVulkan, "Explicit API argument should override the default");
        Assert(options.apiExplicitlyRequested, "Explicit API argument should be tracked");
    }

    TEST_CASE("launch options parse chooser flag")
    {
        const char *argv[] = {"ProceduralGeneration", "--choose-api"};
        const demo::LaunchOptions options = *demo::parseLaunchOptions(2, argv);
        Assert(options.chooseApiInteractively, "Chooser flag should enable interactive API selection");
    }

    TEST_CASE("launch options reject invalid arguments")
    {
        const char *argv[] = {"ProceduralGeneration", "--api", "unknown"};
        const auto parsed = demo::parseLaunchOptions(3, argv);
        Assert(!parsed.has_value(), "An invalid graphics API should be refused");
        Assert(parsed.error().code == orosmo::core::ErrorCode::kInvalidArgument, "the error should say the argument is invalid");
        AssertEqual(parsed.error().message, std::string("Unknown graphics API: unknown"), "the message should name the value");
    }

    TEST_CASE("launch options refuse an unknown argument with its name")
    {
        const char *argv[] = {"program", "--frobnicate"};
        const auto parsed = demo::parseLaunchOptions(2, argv);
        Assert(!parsed.has_value(), "an unknown argument should be refused");
        AssertEqual(parsed.error().message, std::string("Unknown argument: --frobnicate"), "the message should name the argument");
        const char *missing[] = {"program", "--api"};
        AssertEqual(demo::parseLaunchOptions(2, missing).error().message, std::string("Missing value after --api"), "a missing value is named");
    }

    TEST_CASE("backend display names follow the menu order")
    {
        AssertEqual(std::string(demo::backendDisplayName(Backend::kOpenGL)), std::string("OpenGL"), "OpenGL name");
        AssertEqual(std::string(demo::backendDisplayName(Backend::kVulkan)), std::string("Vulkan"), "Vulkan name");
        AssertEqual(std::string(demo::backendDisplayName(Backend::kMetal)), std::string("Metal"), "Metal name");
        std::ostringstream usage;
        demo::printUsage(usage);
        Assert(usage.str().find("Available API names: OpenGL, Vulkan, Metal") != std::string::npos, "the usage lists the API names");
    }

    TEST_CASE("launch options parse capture directory and frame count")
    {
        const char *argv[] = {"program", "--api", "opengl", "--capture", "out/ref", "--capture-frames", "25"};
        const demo::LaunchOptions options = *demo::parseLaunchOptions(7, argv);
        AssertEqual(options.captureDirectory, std::string("out/ref"), "Capture directory should be parsed");
        AssertEqual(options.captureFrames, 25u, "Capture frame count should be parsed");
    }

    TEST_CASE("launch options default to no capture")
    {
        const char *argv[] = {"program"};
        const demo::LaunchOptions options = *demo::parseLaunchOptions(1, argv);
        Assert(options.captureDirectory.empty(), "Capture should be off by default");
        AssertEqual(options.captureFrames, 10u, "Capture should default to 10 frames");
    }

    TEST_CASE("launch options parse the terrain seed")
    {
        const char *argv[] = {"program", "--seed", "42"};
        const demo::LaunchOptions options = *demo::parseLaunchOptions(3, argv);
        Assert(options.seed.has_value(), "A seed should be parsed");
        AssertEqual(*options.seed, 42u, "The seed value should be kept");
        const char *none[] = {"program"};
        Assert(!demo::parseLaunchOptions(1, none)->seed.has_value(), "Without --seed the terrain stays random as before");
    }

    TEST_CASE("launch options reject an invalid seed")
    {
        const char *negative[] = {"program", "--seed", "-1"};
        const char *text[] = {"program", "--seed", "abc"};
        const char *missing[] = {"program", "--seed"};
        for (const auto &args : {std::pair<const char *const *, int>{negative, 3}, {text, 3}, {missing, 2}})
        {
            Assert(!demo::parseLaunchOptions(args.second, args.first).has_value(), "An invalid seed should be refused");
        }
    }

    TEST_CASE("launch options parse --headless (PLAT-04)")
    {
        const char *argv[] = {"program", "--headless", "--capture", "out", "--capture-frames", "3"};
        const demo::LaunchOptions options = *demo::parseLaunchOptions(6, argv);
        Assert(options.headless, "--headless should be parsed");
        AssertEqual(options.captureFrames, 3u, "the frame count is kept in headless mode");
        const char *none[] = {"program"};
        Assert(!demo::parseLaunchOptions(1, none)->headless, "a window is the default");
    }

    TEST_CASE("launch options parse --vsync")
    {
        const char *argv[] = {"program", "--vsync"};
        Assert(demo::parseLaunchOptions(2, argv)->vsync, "--vsync should be parsed");
        const char *none[] = {"program"};
        Assert(!demo::parseLaunchOptions(1, none)->vsync, "vsync is off by default, as before");
    }

    TEST_CASE("launch options reject invalid capture arguments")
    {
        const char *missingValue[] = {"program", "--capture"};
        const char *zeroFrames[] = {"program", "--capture", "x", "--capture-frames", "0"};
        const char *textFrames[] = {"program", "--capture", "x", "--capture-frames", "ten"};
        for (const auto &args : {std::pair<const char *const *, int>{missingValue, 2}, {zeroFrames, 5}, {textFrames, 5}})
        {
            Assert(!demo::parseLaunchOptions(args.second, args.first).has_value(), "Invalid capture arguments should be refused");
        }
    }

    TEST_CASE("interactive selection accepts valid entries")
    {
        std::istringstream input("2\n");
        std::ostringstream output;
        Backend selectedApi = Backend::kOpenGL;

        Assert(demo::promptForBackend(input, output, selectedApi), "Interactive selection should accept valid choices");
        Assert(selectedApi == Backend::kVulkan, "Choice 2 should map to Vulkan");
    }

    TEST_CASE("interactive selection defaults to opengl on empty entry")
    {
        std::istringstream input("\n");
        std::ostringstream output;
        Backend selectedApi = Backend::kMetal;

        Assert(demo::promptForBackend(input, output, selectedApi), "Empty selection should accept the default");
        Assert(selectedApi == Backend::kOpenGL, "Empty selection should default to OpenGL");
    }

    TEST_CASE("interactive selection rejects invalid entries")
    {
        std::istringstream input("42\n");
        std::ostringstream output;
        Backend selectedApi = Backend::kOpenGL;

        Assert(!demo::promptForBackend(input, output, selectedApi), "Interactive selection should reject unknown choices");
        Assert(selectedApi == Backend::kOpenGL, "Rejected choices should not mutate the current API");
    }

    TEST_CASE("opengl backend stays available")
    {
        Assert(demo::backendAvailability(Backend::kOpenGL) == BackendAvailability::kAvailable,
                    "OpenGL backend should remain available");
        Assert(demo::isBackendAvailable(Backend::kOpenGL), "OpenGL should be runnable");
    }

    TEST_CASE("vulkan is selectable since M4 (the RHI backend is compiled); Metal waits for M9")
    {
        Assert(demo::backendAvailability(Backend::kVulkan) == BackendAvailability::kAvailable, "Vulkan is compiled into the RHI since M4");
        Assert(demo::isBackendAvailable(Backend::kVulkan), "Vulkan must be selectable");
        Assert(demo::isBackendAvailable(Backend::kOpenGL), "OpenGL is the RHI backend");
        Assert(!demo::isBackendAvailable(Backend::kMetal), "Metal is not built (M9)");
        Assert(!demo::backendAvailabilityMessage(Backend::kMetal).empty(), "refusing Metal should say why");
    }

    TEST_CASE("other backend messages are non-empty")
    {
        Assert(!demo::backendAvailabilityMessage(Backend::kVulkan).empty(), "Vulkan availability message should be present");
        Assert(!demo::backendAvailabilityMessage(Backend::kMetal).empty(), "Metal availability message should be present");
    }

    TEST_CASE("the selectable APIs match the backends compiled into the RHI (RHI-04)")
    {
        Assert(orosmo::rhi::isBackendCompiled(orosmo::rhi::Backend::kOpenGL), "the OpenGL backend is compiled");
        Assert(orosmo::rhi::isBackendCompiled(orosmo::rhi::Backend::kVulkan), "the Vulkan backend is compiled since M4");
        Assert(demo::isBackendAvailable(Backend::kOpenGL) == orosmo::rhi::isBackendCompiled(orosmo::rhi::Backend::kOpenGL),
                    "OpenGL availability should follow the RHI");
        Assert(demo::isBackendAvailable(Backend::kVulkan) == orosmo::rhi::isBackendCompiled(orosmo::rhi::Backend::kVulkan),
                    "Vulkan availability should follow the RHI");
    }
}

} // namespace tests
