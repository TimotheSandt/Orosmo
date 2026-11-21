#include "support/test_support.hpp"

#include "launch_options.hpp"
#include <orosmo/rhi/device.hpp>

#include <sstream>
#include <utility>

namespace tests
{

using demo::BackendAvailability;
using demo::UiMode;
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

    TEST_CASE("launch options default values")
    {
        const char *argv[] = {"Orosmo"};
        const demo::LaunchOptions options = *demo::parseLaunchOptions(1, argv);
        Assert(options.api == Backend::kOpenGL, "Default graphics API should stay OpenGL");
        Assert(!options.chooseApiInteractively, "Interactive selection should be disabled by default");
        Assert(!options.apiExplicitlyRequested, "Default launch should not mark the API as explicit");
        Assert(options.uiMode == UiMode::kHud, "Default UI mode should be hud");
        AssertEqual(options.theme, std::string("dark"), "Default theme should be dark");
        AssertEqual(options.tab, std::string("scene"), "Default tab should be scene");
        Assert(options.hoverId.empty(), "Hover ID should be empty by default");
        Assert(options.clickId.empty(), "Click ID should be empty by default");
        AssertEqual(options.width, 1280, "Default width should be 1280");
        AssertEqual(options.height, 720, "Default height should be 720");
    }

    TEST_CASE("launch options parse explicit api")
    {
        const char *argv[] = {"Orosmo", "--api=vulkan"};
        const demo::LaunchOptions options = *demo::parseLaunchOptions(2, argv);
        Assert(options.api == Backend::kVulkan, "Explicit API argument should override the default");
        Assert(options.apiExplicitlyRequested, "Explicit API argument should be tracked");
    }

    TEST_CASE("launch options parse chooser flag")
    {
        const char *argv[] = {"Orosmo", "--choose-api"};
        const demo::LaunchOptions options = *demo::parseLaunchOptions(2, argv);
        Assert(options.chooseApiInteractively, "Chooser flag should enable interactive API selection");
    }

    TEST_CASE("launch options reject invalid arguments")
    {
        const char *argv[] = {"Orosmo", "--api", "unknown"};
        const auto parsed = demo::parseLaunchOptions(3, argv);
        Assert(!parsed.has_value(), "An invalid graphics API should be refused");
        Assert(parsed.error().code == orosmo::core::ErrorCode::kInvalidArgument, "the error should say the argument is invalid");
        AssertEqual(parsed.error().message, std::string("Unknown graphics API: unknown"), "the message should name the value");
    }

    TEST_CASE("launch options refuse an unknown argument with its name")
    {
        const char *argv[] = {"Orosmo", "--frobnicate"};
        const auto parsed = demo::parseLaunchOptions(2, argv);
        Assert(!parsed.has_value(), "an unknown argument should be refused");
        AssertEqual(parsed.error().message, std::string("Unknown argument: --frobnicate"), "the message should name the argument");
        const char *missing[] = {"Orosmo", "--api"};
        AssertEqual(demo::parseLaunchOptions(2, missing).error().message, std::string("Missing value after --api"), "a missing value is named");
        const char *removedSeed[] = {"Orosmo", "--seed", "42"};
        Assert(!demo::parseLaunchOptions(3, removedSeed).has_value(), "--seed was removed and must be refused");
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
        const char *argv[] = {"Orosmo", "--api", "opengl", "--capture", "out/ref", "--capture-frames", "25"};
        const demo::LaunchOptions options = *demo::parseLaunchOptions(7, argv);
        AssertEqual(options.captureDirectory, std::string("out/ref"), "Capture directory should be parsed");
        AssertEqual(options.captureFrames, 25u, "Capture frame count should be parsed");
    }

    TEST_CASE("launch options default to no capture")
    {
        const char *argv[] = {"Orosmo"};
        const demo::LaunchOptions options = *demo::parseLaunchOptions(1, argv);
        Assert(options.captureDirectory.empty(), "Capture should be off by default");
        AssertEqual(options.captureFrames, 10u, "Capture should default to 10 frames");
    }

    TEST_CASE("launch options parse --ui mode")
    {
        const char *off[] = {"Orosmo", "--ui", "off"};
        const char *hud[] = {"Orosmo", "--ui=hud"};
        const char *full[] = {"Orosmo", "--ui", "FULL"};
        Assert(demo::parseLaunchOptions(3, off)->uiMode == UiMode::kOff, "--ui off should parse to kOff");
        Assert(demo::parseLaunchOptions(2, hud)->uiMode == UiMode::kHud, "--ui=hud should parse to kHud");
        Assert(demo::parseLaunchOptions(3, full)->uiMode == UiMode::kFull, "--ui FULL should parse to kFull");

        const char *invalid[] = {"Orosmo", "--ui", "minimal"};
        const char *missing[] = {"Orosmo", "--ui"};
        Assert(!demo::parseLaunchOptions(3, invalid).has_value(), "Unknown --ui mode should be rejected");
        Assert(demo::parseLaunchOptions(3, invalid).error().code == orosmo::core::ErrorCode::kInvalidArgument, "Invalid --ui returns kInvalidArgument");
        Assert(!demo::parseLaunchOptions(2, missing).has_value(), "Missing --ui value should be rejected");
    }

    TEST_CASE("launch options parse --theme")
    {
        const char *dark[] = {"Orosmo", "--theme", "dark"};
        const char *light[] = {"Orosmo", "--theme=light"};
        const char *framed[] = {"Orosmo", "--theme", "FRAMED"};
        AssertEqual(demo::parseLaunchOptions(3, dark)->theme, std::string("dark"), "--theme dark");
        AssertEqual(demo::parseLaunchOptions(2, light)->theme, std::string("light"), "--theme=light");
        AssertEqual(demo::parseLaunchOptions(3, framed)->theme, std::string("framed"), "--theme FRAMED normalizes to lowercase");

        const char *invalid[] = {"Orosmo", "--theme", "neon"};
        const char *missing[] = {"Orosmo", "--theme"};
        Assert(!demo::parseLaunchOptions(3, invalid).has_value(), "Unknown --theme should be rejected");
        Assert(!demo::parseLaunchOptions(2, missing).has_value(), "Missing --theme value should be rejected");
    }

    TEST_CASE("launch options parse --tab")
    {
        for (const char *tab : {"scene", "widgets", "controls", "color", "windows", "layout", "themes"})
        {
            const char *argv[] = {"Orosmo", "--tab", tab};
            const auto parsed = demo::parseLaunchOptions(3, argv);
            Assert(parsed.has_value(), std::string("Valid tab should be accepted: ") + tab);
            AssertEqual(parsed->tab, std::string(tab), "Tab value should match");
        }
        const char *upper[] = {"Orosmo", "--tab=WIDGETS"};
        AssertEqual(demo::parseLaunchOptions(2, upper)->tab, std::string("widgets"), "--tab normalizes case");

        const char *invalid[] = {"Orosmo", "--tab", "parameters"};
        const char *missing[] = {"Orosmo", "--tab"};
        Assert(!demo::parseLaunchOptions(3, invalid).has_value(), "Unknown --tab should be rejected");
        Assert(!demo::parseLaunchOptions(2, missing).has_value(), "Missing --tab value should be rejected");
    }

    TEST_CASE("launch options parse --hover and --click")
    {
        const char *hoverArgs[] = {"Orosmo", "--hover", "volume"};
        AssertEqual(demo::parseLaunchOptions(3, hoverArgs)->hoverId, std::string("volume"), "--hover should be parsed");

        const char *clickArgs[] = {"Orosmo", "--click", "openDialog", "--capture-frames", "3"};
        const auto clickParsed = demo::parseLaunchOptions(5, clickArgs);
        Assert(clickParsed.has_value(), "--click with 3 frames should succeed");
        AssertEqual(clickParsed->clickId, std::string("openDialog"), "--click ID should be parsed");

        const char *clickTooFew[] = {"Orosmo", "--click", "openDialog", "--capture-frames", "2"};
        const auto rejected = demo::parseLaunchOptions(5, clickTooFew);
        Assert(!rejected.has_value(), "--click with --capture-frames 2 must be rejected");
        Assert(rejected.error().code == orosmo::core::ErrorCode::kInvalidArgument, "Error code should be kInvalidArgument");

        const char *clickTooFewReversed[] = {"Orosmo", "--capture-frames", "2", "--click", "openDialog"};
        Assert(!demo::parseLaunchOptions(5, clickTooFewReversed).has_value(), "--click with --capture-frames 2 must be rejected in any order");

        const char *missingHover[] = {"Orosmo", "--hover"};
        const char *missingClick[] = {"Orosmo", "--click"};
        Assert(!demo::parseLaunchOptions(2, missingHover).has_value(), "Missing --hover value should be rejected");
        Assert(!demo::parseLaunchOptions(2, missingClick).has_value(), "Missing --click value should be rejected");
    }

    TEST_CASE("launch options parse --size")
    {
        const char *minSize[] = {"Orosmo", "--size", "640x400"};
        const auto parsedMin = demo::parseLaunchOptions(3, minSize);
        Assert(parsedMin.has_value(), "640x400 is the minimum valid size");
        AssertEqual(parsedMin->width, 640, "Min width should be 640");
        AssertEqual(parsedMin->height, 400, "Min height should be 400");

        const char *hdSize[] = {"Orosmo", "--size=1920X1080"};
        const auto parsedHd = demo::parseLaunchOptions(2, hdSize);
        Assert(parsedHd.has_value(), "1920X1080 should be parsed");
        AssertEqual(parsedHd->width, 1920, "HD width");
        AssertEqual(parsedHd->height, 1080, "HD height");

        const char *tooNarrow[] = {"Orosmo", "--size", "639x400"};
        const char *tooShort[] = {"Orosmo", "--size", "640x399"};
        const char *noSep[] = {"Orosmo", "--size", "1280"};
        const char *extraSep[] = {"Orosmo", "--size", "1280x720x1"};
        const char *negative[] = {"Orosmo", "--size", "-800x600"};
        const char *missing[] = {"Orosmo", "--size"};
        for (const auto &args : {std::pair<const char *const *, int>{tooNarrow, 3}, {tooShort, 3}, {noSep, 3}, {extraSep, 3}, {negative, 3}, {missing, 2}})
        {
            const auto parsed = demo::parseLaunchOptions(args.second, args.first);
            Assert(!parsed.has_value(), "Invalid --size should be refused");
            Assert(parsed.error().code == orosmo::core::ErrorCode::kInvalidArgument, "Invalid --size should return kInvalidArgument");
        }
    }

    TEST_CASE("launch options parse --headless (PLAT-04)")
    {
        const char *argv[] = {"Orosmo", "--headless", "--capture", "out", "--capture-frames", "3"};
        const demo::LaunchOptions options = *demo::parseLaunchOptions(6, argv);
        Assert(options.headless, "--headless should be parsed");
        AssertEqual(options.captureFrames, 3u, "the frame count is kept in headless mode");
        const char *none[] = {"Orosmo"};
        Assert(!demo::parseLaunchOptions(1, none)->headless, "a window is the default");
    }

    TEST_CASE("launch options parse --vsync")
    {
        const char *argv[] = {"Orosmo", "--vsync"};
        Assert(demo::parseLaunchOptions(2, argv)->vsync, "--vsync should be parsed");
        const char *none[] = {"Orosmo"};
        Assert(!demo::parseLaunchOptions(1, none)->vsync, "vsync is off by default, as before");
    }

    TEST_CASE("launch options reject invalid capture arguments")
    {
        const char *missingValue[] = {"Orosmo", "--capture"};
        const char *zeroFrames[] = {"Orosmo", "--capture", "x", "--capture-frames", "0"};
        const char *textFrames[] = {"Orosmo", "--capture", "x", "--capture-frames", "ten"};
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
