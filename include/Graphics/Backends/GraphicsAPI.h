#pragma once

#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <string>

enum class GraphicsAPI : std::uint8_t
{
    OpenGL,
    Vulkan,
    Metal
};

enum class GraphicsAPIAvailability : std::uint8_t
{
    Available,
    NotBuilt,
    UnsupportedPlatform
};

struct GraphicsLaunchOptions
{
    GraphicsAPI api = GraphicsAPI::OpenGL;
    bool showHelp = false;
    bool listApis = false;
    bool chooseApiInteractively = false;
    bool apiExplicitlyRequested = false;
    // --capture <dir> [--capture-frames N] : render N frames, save scene.png and ui.png in <dir>, exit (TOOL-03).
    std::string captureDirectory;
    std::uint32_t captureFrames = 10;
    // --seed N : terrain seed of the demo (default: drawn at random by main() and logged). --capture implies seed 1 unless given.
    std::optional<std::uint32_t> seed;
    // --headless (PLAT-04) : no window at all (offscreen device, fixed 1280x720 view, fixed time step, no input). The demo renders
    // --capture-frames N frames (default 10), saves the images if --capture is given, then exits.
    bool headless = false;
};

std::string GraphicsAPIToString(GraphicsAPI api) noexcept;
std::optional<GraphicsAPI> ParseGraphicsAPI(std::string value) noexcept;

GraphicsLaunchOptions ParseGraphicsLaunchOptions(int argc, const char *const *argv);
void PrintGraphicsAPIUsage(std::ostream &out);
bool PromptForGraphicsAPI(std::istream &input, std::ostream &output, GraphicsAPI &selectedApi);

GraphicsAPIAvailability GetGraphicsAPIAvailability(GraphicsAPI api) noexcept;
bool IsGraphicsAPIAvailable(GraphicsAPI api) noexcept;
std::string GetGraphicsAPIAvailabilityMessage(GraphicsAPI api);
