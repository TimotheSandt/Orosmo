#include "launch_options.hpp"

#include <engine/rhi/device.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <ostream>
#include <istream>

namespace demo
{

namespace
{

using engine::core::ErrorCode;
using engine::core::makeError;
using engine::rhi::Backend;

// Ordre d'affichage (menu, liste) : OpenGL d'abord, c'est le défaut.
constexpr std::array<Backend, 3> kAllBackends = {Backend::kOpenGL, Backend::kVulkan, Backend::kMetal};

bool equalsIgnoreCase(std::string_view left, std::string_view right) noexcept
{
    return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin(), [](char a, char b)
                                                     { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
}

// Entier décimal non signé occupant toute la chaîne, au plus `maximum` ; rien sinon (signe, texte, dépassement).
std::optional<std::uint32_t> parseUnsigned(std::string_view text, std::uint32_t maximum) noexcept
{
    std::uint32_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || error != std::errc{} || end != text.data() + text.size() || value > maximum)
    {
        return std::nullopt;
    }
    return value;
}

std::string joinBackendNames()
{
    std::string names;
    for (const Backend api : kAllBackends)
    {
        if (!names.empty())
        {
            names += ", ";
        }
        names += backendDisplayName(api);
    }
    return names;
}

} // namespace

std::string_view backendDisplayName(Backend api) noexcept
{
    switch (api)
    {
        case Backend::kOpenGL: return "OpenGL";
        case Backend::kVulkan: return "Vulkan";
        case Backend::kMetal: return "Metal";
    }
    return "Unknown";
}

std::optional<Backend> parseBackend(std::string_view value) noexcept
{
    if (equalsIgnoreCase(value, "opengl") || equalsIgnoreCase(value, "gl"))
    {
        return Backend::kOpenGL;
    }
    if (equalsIgnoreCase(value, "vulkan") || equalsIgnoreCase(value, "vk"))
    {
        return Backend::kVulkan;
    }
    if (equalsIgnoreCase(value, "metal"))
    {
        return Backend::kMetal;
    }
    return std::nullopt;
}

engine::core::Result<LaunchOptions> parseLaunchOptions(int argc, const char *const *argv)
{
    LaunchOptions options;
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument(argv[index]);
        // Valeur de l'option courante (argument suivant), ou rien s'il manque.
        auto next = [&]() -> std::optional<std::string>
        {
            if (index + 1 >= argc)
            {
                return std::nullopt;
            }
            return std::string(argv[++index]);
        };

        if (argument == "--help" || argument == "-h")
        {
            options.showHelp = true;
        }
        else if (argument == "--list-apis")
        {
            options.listApis = true;
        }
        else if (argument == "--vsync")
        {
            options.vsync = true;
        }
        else if (argument == "--headless")
        {
            options.headless = true;
        }
        else if (argument == "--choose-api")
        {
            options.chooseApiInteractively = true;
        }
        else if (argument == "--api" || argument.starts_with("--api="))
        {
            std::optional<std::string> value = argument == "--api" ? next() : std::optional<std::string>(argument.substr(6));
            if (!value)
            {
                return makeError(ErrorCode::kInvalidArgument, "Missing value after --api");
            }
            const auto api = parseBackend(*value);
            if (!api)
            {
                return makeError(ErrorCode::kInvalidArgument, "Unknown graphics API: " + *value);
            }
            options.api = *api;
            options.apiExplicitlyRequested = true;
        }
        else if (argument == "--seed")
        {
            const auto value = next();
            if (!value)
            {
                return makeError(ErrorCode::kInvalidArgument, "Missing value after --seed");
            }
            const auto seed = parseUnsigned(*value, 0x7fffffffU);
            if (!seed)
            {
                return makeError(ErrorCode::kInvalidArgument, "Invalid value for --seed: " + *value);
            }
            options.seed = *seed;
        }
        else if (argument == "--capture" || argument == "--capture-frames")
        {
            const auto value = next();
            if (!value)
            {
                return makeError(ErrorCode::kInvalidArgument, "Missing value after " + argument);
            }
            if (argument == "--capture")
            {
                options.captureDirectory = *value;
                continue;
            }
            const auto frames = parseUnsigned(*value, 100000U);
            if (!frames || *frames == 0)
            {
                return makeError(ErrorCode::kInvalidArgument, "Invalid frame count for --capture-frames: " + *value);
            }
            options.captureFrames = *frames;
        }
        else
        {
            return makeError(ErrorCode::kInvalidArgument, "Unknown argument: " + argument);
        }
    }
    return options;
}

void printUsage(std::ostream &out)
{
    out << "Usage: ProceduralGeneration [--api <name>] [--choose-api] [--list-apis] [--seed N] [--headless] [--vsync] [--capture <dir> [--capture-frames N]] [--help]\n";
    out << "Available API names: " << joinBackendNames() << '\n';
    out << "Example: ProceduralGeneration --api opengl\n";
    out << "Capture : renders N frames (default 10) at 1280x720 with seed 1 (unless --seed), writes <dir>/scene.png and\n";
    out << "          <dir>/ui.png (the UI shows live FPS: only scene.png is reproducible), then exits.\n";
    out << "Headless: --headless creates no window (offscreen device, 1280x720, fixed time step), renders N frames and exits ;\n";
    out << "          combine it with --capture <dir> to save the images.\n";
}

bool promptForBackend(std::istream &input, std::ostream &output, Backend &selected)
{
    output << "Select a graphics API:\n";
    for (std::size_t index = 0; index < kAllBackends.size(); ++index)
    {
        output << "  " << (index + 1) << ". " << backendDisplayName(kAllBackends[index]) << " - " << backendAvailabilityMessage(kAllBackends[index]) << '\n';
    }
    output << "Press Enter for " << backendDisplayName(Backend::kOpenGL) << ".\n";
    output << "> ";

    std::string choice;
    if (!std::getline(input, choice))
    {
        return false;
    }
    if (choice.empty())
    {
        selected = Backend::kOpenGL;
        return true;
    }
    for (std::size_t index = 0; index < kAllBackends.size(); ++index)
    {
        if (choice == std::to_string(index + 1))
        {
            selected = kAllBackends[index];
            return true;
        }
    }
    return false;
}

BackendAvailability backendAvailability(Backend api) noexcept
{
    if (engine::rhi::isBackendCompiled(api))
    {
        return BackendAvailability::kAvailable;
    }
#ifndef __APPLE__
    if (api == Backend::kMetal)
    {
        return BackendAvailability::kUnsupportedPlatform;
    }
#endif
    return BackendAvailability::kNotBuilt; // Vulkan réécrit sur le RHI en M4, Metal en M9
}

bool isBackendAvailable(Backend api) noexcept { return backendAvailability(api) == BackendAvailability::kAvailable; }

std::string backendAvailabilityMessage(Backend api)
{
    const std::string name(backendDisplayName(api));
    switch (backendAvailability(api))
    {
        case BackendAvailability::kAvailable: return name + " backend is available.";
        case BackendAvailability::kNotBuilt:
            return name + " backend selection is recognized, but this backend is not available yet (Vulkan: M4, Metal: M9).";
        case BackendAvailability::kUnsupportedPlatform: return name + " backend is not supported on this platform.";
    }
    return "Unknown graphics backend state.";
}

} // namespace demo
