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
};

std::string GraphicsAPIToString(GraphicsAPI api) noexcept;
std::optional<GraphicsAPI> ParseGraphicsAPI(std::string value) noexcept;

GraphicsLaunchOptions ParseGraphicsLaunchOptions(int argc, const char *const *argv);
void PrintGraphicsAPIUsage(std::ostream &out);
bool PromptForGraphicsAPI(std::istream &input, std::ostream &output, GraphicsAPI &selectedApi);

GraphicsAPIAvailability GetGraphicsAPIAvailability(GraphicsAPI api) noexcept;
bool IsGraphicsAPIAvailable(GraphicsAPI api) noexcept;
std::string GetGraphicsAPIAvailabilityMessage(GraphicsAPI api);
