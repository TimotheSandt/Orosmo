#include "support/test_support.hpp"

#include "Graphics/Backend/GraphicsAPI.h"
#include "Graphics/Core/GraphicsBackend.h"

#include <sstream>
#include <stdexcept>

namespace tests
{

TEST_SUITE("GraphicsAPI")
{
    TEST_CASE("parse opengl api aliases")
    {
        AssertEqual(ParseGraphicsAPI("opengl").value(), GraphicsAPI::OpenGL, "opengl should parse to OpenGL");
        AssertEqual(ParseGraphicsAPI("GL").value(), GraphicsAPI::OpenGL, "GL should parse to OpenGL");
    }

    TEST_CASE("parse vulkan and metal api aliases")
    {
        AssertEqual(ParseGraphicsAPI("vk").value(), GraphicsAPI::Vulkan, "vk should parse to Vulkan");
        AssertEqual(ParseGraphicsAPI("metal").value(), GraphicsAPI::Metal, "metal should parse to Metal");
    }

    TEST_CASE("invalid api token is rejected")
    {
Assert(!ParseGraphicsAPI("directx").has_value(), "Unknown API should be rejected");
    }

    TEST_CASE("launch options default to opengl")
    {
        const char *argv[] = {"ProceduralGeneration"};
        const GraphicsLaunchOptions options = ParseGraphicsLaunchOptions(1, argv);
        AssertEqual(options.api, GraphicsAPI::OpenGL, "Default graphics API should stay OpenGL");
        Assert(!options.chooseApiInteractively, "Interactive selection should be disabled by default");
        Assert(!options.apiExplicitlyRequested, "Default launch should not mark the API as explicit");
    }

    TEST_CASE("launch options parse explicit api")
    {
        const char *argv[] = {"ProceduralGeneration", "--api=vulkan"};
        const GraphicsLaunchOptions options = ParseGraphicsLaunchOptions(2, argv);
        AssertEqual(options.api, GraphicsAPI::Vulkan, "Explicit API argument should override the default");
        Assert(options.apiExplicitlyRequested, "Explicit API argument should be tracked");
    }

    TEST_CASE("launch options parse chooser flag")
    {
        const char *argv[] = {"ProceduralGeneration", "--choose-api"};
        const GraphicsLaunchOptions options = ParseGraphicsLaunchOptions(2, argv);
        Assert(options.chooseApiInteractively, "Chooser flag should enable interactive API selection");
    }

    TEST_CASE("launch options reject invalid arguments")
    {
        const char *argv[] = {"ProceduralGeneration", "--api", "unknown"};
        bool thrown = false;

        try
        {
            static_cast<void>(ParseGraphicsLaunchOptions(3, argv));
        }
        catch (const std::invalid_argument &)
        {
            thrown = true;
        }

        Assert(thrown, "Invalid graphics API should throw");
    }

    TEST_CASE("launch options parse capture directory and frame count")
    {
        const char *argv[] = {"program", "--api", "opengl", "--capture", "out/ref", "--capture-frames", "25"};
        const GraphicsLaunchOptions options = ParseGraphicsLaunchOptions(7, argv);
        AssertEqual(options.captureDirectory, std::string("out/ref"), "Capture directory should be parsed");
        AssertEqual(options.captureFrames, 25u, "Capture frame count should be parsed");
    }

    TEST_CASE("launch options default to no capture")
    {
        const char *argv[] = {"program"};
        const GraphicsLaunchOptions options = ParseGraphicsLaunchOptions(1, argv);
        Assert(options.captureDirectory.empty(), "Capture should be off by default");
        AssertEqual(options.captureFrames, 10u, "Capture should default to 10 frames");
    }

    TEST_CASE("launch options reject invalid capture arguments")
    {
        const char *missingValue[] = {"program", "--capture"};
        const char *zeroFrames[] = {"program", "--capture", "x", "--capture-frames", "0"};
        const char *textFrames[] = {"program", "--capture", "x", "--capture-frames", "ten"};
        for (const auto &args : {std::pair<const char *const *, int>{missingValue, 2}, {zeroFrames, 5}, {textFrames, 5}})
        {
            bool thrown = false;
            try
            {
                static_cast<void>(ParseGraphicsLaunchOptions(args.second, args.first));
            }
            catch (const std::invalid_argument &)
            {
                thrown = true;
            }
            Assert(thrown, "Invalid capture arguments should throw");
        }
    }

    TEST_CASE("interactive selection accepts valid entries")
    {
        std::istringstream input("2\n");
        std::ostringstream output;
        GraphicsAPI selectedApi = GraphicsAPI::OpenGL;

        Assert(PromptForGraphicsAPI(input, output, selectedApi), "Interactive selection should accept valid choices");
        AssertEqual(selectedApi, GraphicsAPI::Vulkan, "Choice 2 should map to Vulkan");
    }

    TEST_CASE("interactive selection defaults to opengl on empty entry")
    {
        std::istringstream input("\n");
        std::ostringstream output;
        GraphicsAPI selectedApi = GraphicsAPI::Metal;

        Assert(PromptForGraphicsAPI(input, output, selectedApi), "Empty selection should accept the default");
        AssertEqual(selectedApi, GraphicsAPI::OpenGL, "Empty selection should default to OpenGL");
    }

    TEST_CASE("interactive selection rejects invalid entries")
    {
        std::istringstream input("42\n");
        std::ostringstream output;
        GraphicsAPI selectedApi = GraphicsAPI::OpenGL;

        Assert(!PromptForGraphicsAPI(input, output, selectedApi), "Interactive selection should reject unknown choices");
        AssertEqual(selectedApi, GraphicsAPI::OpenGL, "Rejected choices should not mutate the current API");
    }

    TEST_CASE("opengl backend stays available")
    {
        AssertEqual(GetGraphicsAPIAvailability(GraphicsAPI::OpenGL), GraphicsAPIAvailability::Available,
                    "OpenGL backend should remain available");
        Assert(IsGraphicsAPIAvailable(GraphicsAPI::OpenGL), "OpenGL should be runnable");
    }

    TEST_CASE("vulkan backend is reported available")
    {
        AssertEqual(GetGraphicsAPIAvailability(GraphicsAPI::Vulkan), GraphicsAPIAvailability::Available,
                    "Vulkan backend should be advertised as available");
        Assert(IsGraphicsAPIAvailable(GraphicsAPI::Vulkan), "Vulkan should now be runnable through the runtime");
    }

    TEST_CASE("other backend messages are non-empty")
    {
        Assert(!GetGraphicsAPIAvailabilityMessage(GraphicsAPI::Vulkan).empty(), "Vulkan availability message should be present");
        Assert(!GetGraphicsAPIAvailabilityMessage(GraphicsAPI::Metal).empty(), "Metal availability message should be present");
    }

    TEST_CASE("backend factory returns matching api")
    {
        const std::unique_ptr<IGraphicsBackend> backend = CreateGraphicsBackend({GraphicsAPI::OpenGL});
        AssertEqual(backend->GetAPI(), GraphicsAPI::OpenGL, "Factory should return a backend matching the requested API");
        Assert(backend->IsAvailable(), "OpenGL backend returned by the factory should be available");
    }
}

} // namespace tests
