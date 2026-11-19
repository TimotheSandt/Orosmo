#include "Graphics/Backends/GraphicsAPI.h"

#include <array>
#include <cctype>
#include <sstream>
#include <stdexcept>

namespace
{

bool EqualsIgnoreCase(std::string left, std::string right) noexcept
{
    if (left.size() != right.size())
    {
        return false;
    }

    for (std::size_t index = 0; index < left.size(); ++index)
    {
        if (std::tolower(static_cast<unsigned char>(left[index])) != std::tolower(static_cast<unsigned char>(right[index])))
        {
            return false;
        }
    }

    return true;
}

std::string JoinAvailableApis()
{
    constexpr std::array<GraphicsAPI, 3> allApis = {
        GraphicsAPI::OpenGL,
        GraphicsAPI::Vulkan,
        GraphicsAPI::Metal,
    };

    std::ostringstream builder;
    for (std::size_t index = 0; index < allApis.size(); ++index)
    {
        if (index != 0)
        {
            builder << ", ";
        }
        builder << GraphicsAPIToString(allApis[index]);
    }
    return builder.str();
}

} // namespace

std::string GraphicsAPIToString(GraphicsAPI api) noexcept
{
    switch (api)
    {
        case GraphicsAPI::OpenGL:
            return "OpenGL";
        case GraphicsAPI::Vulkan:
            return "Vulkan";
        case GraphicsAPI::Metal:
            return "Metal";
    }

    return "Unknown";
}

std::optional<GraphicsAPI> ParseGraphicsAPI(std::string value) noexcept
{
    if (EqualsIgnoreCase(value, "opengl") || EqualsIgnoreCase(value, "gl"))
    {
        return GraphicsAPI::OpenGL;
    }
    if (EqualsIgnoreCase(value, "vulkan") || EqualsIgnoreCase(value, "vk"))
    {
        return GraphicsAPI::Vulkan;
    }
    if (EqualsIgnoreCase(value, "metal"))
    {
        return GraphicsAPI::Metal;
    }

    return std::nullopt;
}

GraphicsLaunchOptions ParseGraphicsLaunchOptions(int argc, const char *const *argv)
{
    GraphicsLaunchOptions options;

    for (int index = 1; index < argc; ++index)
    {
        const std::string argument(argv[index]);

        if (argument == "--help" || argument == "-h")
        {
            options.showHelp = true;
            continue;
        }
        if (argument == "--list-apis")
        {
            options.listApis = true;
            continue;
        }
        if (argument == "--vsync")
        {
            options.vsync = true;
            continue;
        }
        if (argument == "--headless")
        {
            options.headless = true;
            continue;
        }
        if (argument == "--choose-api")
        {
            options.chooseApiInteractively = true;
            continue;
        }
        if (argument == "--api")
        {
            if (index + 1 >= argc)
            {
                throw std::invalid_argument("Missing value after --api");
            }

            const auto parsedApi = ParseGraphicsAPI(argv[++index]);
            if (!parsedApi.has_value())
            {
                throw std::invalid_argument("Unknown graphics API: " + std::string(argv[index]));
            }

            options.api = *parsedApi;
            options.apiExplicitlyRequested = true;
            continue;
        }
        if (argument.rfind("--api=", 0) == 0)
        {
            const auto parsedApi = ParseGraphicsAPI(argument.substr(6));
            if (!parsedApi.has_value())
            {
                throw std::invalid_argument("Unknown graphics API: " + argument.substr(6));
            }

            options.api = *parsedApi;
            options.apiExplicitlyRequested = true;
            continue;
        }

        if (argument == "--seed")
        {
            if (index + 1 >= argc)
            {
                throw std::invalid_argument("Missing value after --seed");
            }
            const std::string value(argv[++index]);
            std::size_t parsedChars = 0;
            unsigned long parsed = 0;
            try
            {
                parsed = std::stoul(value, &parsedChars);
            }
            catch (const std::exception &)
            {
                parsedChars = 0;
            }
            if (parsedChars != value.size() || parsed > 0x7fffffffUL)
            {
                throw std::invalid_argument("Invalid value for --seed: " + value);
            }
            options.seed = static_cast<std::uint32_t>(parsed);
            continue;
        }
        if (argument == "--capture" || argument == "--capture-frames")
        {
            if (index + 1 >= argc)
            {
                throw std::invalid_argument("Missing value after " + argument);
            }
            const std::string value(argv[++index]);
            if (argument == "--capture")
            {
                options.captureDirectory = value;
            }
            else
            {
                std::size_t parsedChars = 0;
                unsigned long frames = 0;
                try
                {
                    frames = std::stoul(value, &parsedChars);
                }
                catch (const std::exception &)
                {
                    parsedChars = 0;
                }
                if (parsedChars != value.size() || frames == 0 || frames > 100000)
                {
                    throw std::invalid_argument("Invalid frame count for --capture-frames: " + value);
                }
                options.captureFrames = static_cast<std::uint32_t>(frames);
            }
            continue;
        }

        throw std::invalid_argument("Unknown argument: " + argument);
    }

    return options;
}

void PrintGraphicsAPIUsage(std::ostream &out)
{
    out << "Usage: ProceduralGeneration [--api <name>] [--choose-api] [--list-apis] [--seed N] [--headless] [--vsync] [--capture <dir> [--capture-frames N]] [--help]\n";
    out << "Available API names: " << JoinAvailableApis() << '\n';
    out << "Example: ProceduralGeneration --api opengl\n";
    out << "Capture : renders N frames (default 10) at 1280x720 with seed 1 (unless --seed), writes <dir>/scene.png and\n";
    out << "          <dir>/ui.png (the UI shows live FPS: only scene.png is reproducible), then exits.\n";
    out << "Headless: --headless creates no window (offscreen device, 1280x720, fixed time step), renders N frames and exits ;\n";
    out << "          combine it with --capture <dir> to save the images.\n";
}

bool PromptForGraphicsAPI(std::istream &input, std::ostream &output, GraphicsAPI &selectedApi)
{
    constexpr std::array<GraphicsAPI, 3> allApis = {GraphicsAPI::OpenGL, GraphicsAPI::Vulkan, GraphicsAPI::Metal};

    output << "Select a graphics API:\n";
    for (std::size_t index = 0; index < allApis.size(); ++index)
    {
        output << "  " << (index + 1) << ". " << GraphicsAPIToString(allApis[index]) << " - "
               << GetGraphicsAPIAvailabilityMessage(allApis[index]) << '\n';
    }
    output << "Press Enter for " << GraphicsAPIToString(GraphicsAPI::OpenGL) << ".\n";
    output << "> ";

    std::string choice;
    if (!std::getline(input, choice))
    {
        return false;
    }

    if (choice.empty())
    {
        selectedApi = GraphicsAPI::OpenGL;
        return true;
    }

    for (std::size_t index = 0; index < allApis.size(); ++index)
    {
        if (choice == std::to_string(index + 1))
        {
            selectedApi = allApis[index];
            return true;
        }
    }

    return false;
}

GraphicsAPIAvailability GetGraphicsAPIAvailability(GraphicsAPI api) noexcept
{
    switch (api)
    {
        case GraphicsAPI::OpenGL:
            return GraphicsAPIAvailability::Available;
        case GraphicsAPI::Vulkan: // l'ancien backend est retiré (A1) ; réécrit sur le RHI en M4
            return GraphicsAPIAvailability::NotBuilt;
        case GraphicsAPI::Metal:
#ifdef __APPLE__
            return GraphicsAPIAvailability::NotBuilt;
#else
            return GraphicsAPIAvailability::UnsupportedPlatform;
#endif
    }

    return GraphicsAPIAvailability::UnsupportedPlatform;
}

bool IsGraphicsAPIAvailable(GraphicsAPI api) noexcept { return GetGraphicsAPIAvailability(api) == GraphicsAPIAvailability::Available; }

std::string GetGraphicsAPIAvailabilityMessage(GraphicsAPI api)
{
    switch (GetGraphicsAPIAvailability(api))
    {
        case GraphicsAPIAvailability::Available:
            return std::string(GraphicsAPIToString(api)) + " backend is available.";
        case GraphicsAPIAvailability::NotBuilt:
            return std::string(GraphicsAPIToString(api)) + " backend selection is recognized, but this backend is not available yet (Vulkan: M4, Metal: M9).";
        case GraphicsAPIAvailability::UnsupportedPlatform:
            return std::string(GraphicsAPIToString(api)) + " backend is not supported on this platform.";
    }

    return "Unknown graphics backend state.";
}
