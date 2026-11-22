#include "launch_options.hpp"

#include <orosmo/rhi/device.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <istream>
#include <ostream>
#include <utility>

namespace demo
{

namespace
{

using orosmo::core::ErrorCode;
using orosmo::core::makeError;
using orosmo::rhi::Backend;

// Ordre d'affichage (menu, liste) : OpenGL d'abord, c'est le défaut.
constexpr std::array<Backend, 3> kAllBackends = {Backend::kOpenGL, Backend::kVulkan, Backend::kMetal};
constexpr std::array<std::string_view, 3> kValidThemes = {"dark", "light", "framed"};
constexpr std::array<std::string_view, 7> kValidTabs = {"scene", "widgets", "controls", "color", "windows", "layout", "themes"};
constexpr std::uint32_t kMinWidth = 640;
constexpr std::uint32_t kMinHeight = 400;
constexpr std::uint32_t kMaxDimension = 16384;

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

template <std::size_t N> std::optional<std::string_view> matchToken(std::string_view value, const std::array<std::string_view, N> &allowed) noexcept
{
    for (const std::string_view candidate : allowed)
    {
        if (equalsIgnoreCase(value, candidate))
        {
            return candidate;
        }
    }
    return std::nullopt;
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

std::optional<LightingMode> parseLightingMode(std::string_view value) noexcept
{
    std::string lower(value);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower == "forward")
    {
        return LightingMode::kForward;
    }
    if (lower == "forwardplus" || lower == "forward+")
    {
        return LightingMode::kForwardPlus;
    }
    return std::nullopt;
}

std::optional<UiMode> parseUiMode(std::string_view value) noexcept
{
    if (equalsIgnoreCase(value, "off"))
    {
        return UiMode::kOff;
    }
    if (equalsIgnoreCase(value, "hud"))
    {
        return UiMode::kHud;
    }
    if (equalsIgnoreCase(value, "full"))
    {
        return UiMode::kFull;
    }
    return std::nullopt;
}

std::optional<PostMode> parsePostMode(std::string_view value) noexcept
{
    constexpr std::pair<const char *, PostMode> kModes[] = {{"none", PostMode::kNone}, {"hdr", PostMode::kHdr}, {"bloom", PostMode::kBloom}, {"full", PostMode::kFull}};
    for (const auto &[name, mode] : kModes)
    {
        if (equalsIgnoreCase(value, name))
        {
            return mode;
        }
    }
    return std::nullopt;
}

orosmo::core::Result<LaunchOptions> parseLaunchOptions(int argc, const char *const *argv)
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
        auto matchOption = [&](std::string_view flag, std::optional<std::string> &outValue) -> bool
        {
            if (argument == flag)
            {
                outValue = next();
                return true;
            }
            if (argument.size() > flag.size() && argument.starts_with(flag) && argument[flag.size()] == '=')
            {
                outValue = argument.substr(flag.size() + 1);
                return true;
            }
            return false;
        };

        std::optional<std::string> value;
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
        else if (argument == "--strict-backend")
        {
            options.strictBackend = true;
        }
        else if (argument == "--choose-api")
        {
            options.chooseApiInteractively = true;
        }
        else if (matchOption("--api", value) || matchOption("--backend", value))
        {
            const std::string flag = argument.starts_with("--backend") ? "--backend" : "--api";
            if (!value)
            {
                return makeError(ErrorCode::kInvalidArgument, "Missing value after " + flag);
            }
            const auto api = parseBackend(*value);
            if (!api)
            {
                return makeError(ErrorCode::kInvalidArgument, "Unknown graphics API: " + *value);
            }
            options.api = *api;
            options.apiExplicitlyRequested = true;
        }
        else if (matchOption("--ui", value))
        {
            if (!value)
            {
                return makeError(ErrorCode::kInvalidArgument, "Missing value after --ui");
            }
            const auto uiMode = parseUiMode(*value);
            if (!uiMode)
            {
                return makeError(ErrorCode::kInvalidArgument, "Unknown UI mode (expected off|hud|full): " + *value);
            }
            options.uiMode = *uiMode;
        }
        else if (matchOption("--post", value))
        {
            if (!value)
            {
                return makeError(ErrorCode::kInvalidArgument, "Missing value after --post");
            }
            const auto post = parsePostMode(*value);
            if (!post)
            {
                return makeError(ErrorCode::kInvalidArgument, "Unknown post-processing mode (expected none|hdr|bloom|full): " + *value);
            }
            options.post = *post;
        }
        else if (matchOption("--theme", value))
        {
            if (!value)
            {
                return makeError(ErrorCode::kInvalidArgument, "Missing value after --theme");
            }
            const auto theme = matchToken(*value, kValidThemes);
            if (!theme)
            {
                return makeError(ErrorCode::kInvalidArgument, "Unknown theme (expected dark|light|framed): " + *value);
            }
            options.theme = std::string(*theme);
        }
        else if (matchOption("--tab", value))
        {
            if (!value)
            {
                return makeError(ErrorCode::kInvalidArgument, "Missing value after --tab");
            }
            const auto tab = matchToken(*value, kValidTabs);
            if (!tab)
            {
                return makeError(ErrorCode::kInvalidArgument,
                                 "Unknown tab (expected scene|widgets|controls|color|windows|layout|themes): " + *value);
            }
            options.tab = std::string(*tab);
        }
        else if (matchOption("--hover", value))
        {
            if (!value || value->empty())
            {
                return makeError(ErrorCode::kInvalidArgument, "Missing value after --hover");
            }
            options.hoverId = std::move(*value);
        }
        else if (matchOption("--click", value))
        {
            if (!value || value->empty())
            {
                return makeError(ErrorCode::kInvalidArgument, "Missing value after --click");
            }
            options.clickId = std::move(*value);
        }
        else if (matchOption("--size", value))
        {
            if (!value)
            {
                return makeError(ErrorCode::kInvalidArgument, "Missing value after --size");
            }
            const std::size_t sep = value->find_first_of("xX");
            const auto width = sep == std::string::npos ? std::nullopt : parseUnsigned(std::string_view(*value).substr(0, sep), kMaxDimension);
            const auto height = sep == std::string::npos ? std::nullopt : parseUnsigned(std::string_view(*value).substr(sep + 1), kMaxDimension);
            if (!width || !height || *width < kMinWidth || *height < kMinHeight)
            {
                return makeError(ErrorCode::kInvalidArgument, "Invalid value for --size (expected WxH, at least 640x400): " + *value);
            }
            options.width = static_cast<int>(*width);
            options.height = static_cast<int>(*height);
        }
        else if (matchOption("--capture", value))
        {
            if (!value || value->empty())
            {
                return makeError(ErrorCode::kInvalidArgument, "Missing value after --capture");
            }
            options.captureDirectory = std::move(*value);
        }
        else if (matchOption("--instances", value))
        {
            if (!value)
            {
                return makeError(ErrorCode::kInvalidArgument, "Missing value after --instances");
            }
            const auto count = parseUnsigned(*value, 1000000U);
            if (!count)
            {
                return makeError(ErrorCode::kInvalidArgument, "Invalid value for --instances (0 to 1000000): " + *value);
            }
            options.instances = *count;
        }
        else if (matchOption("--debug-view", value))
        {
            const auto view = value ? orosmo::renderer::debugViewFromName(*value) : std::nullopt;
            if (!view)
            {
                return makeError(ErrorCode::kInvalidArgument,
                                 "Invalid value for --debug-view (none|normals|albedo|overdraw|lod|wireframe|motion): " + (value ? *value : std::string("<missing>")));
            }
            options.debugView = *view;
        }
        else if (argument == "--freeze-frustum")
        {
            options.freezeFrustum = true;
        }
        else if (matchOption("--lighting", value))
        {
            const auto mode = value ? parseLightingMode(*value) : std::nullopt;
            if (!mode)
            {
                return makeError(ErrorCode::kInvalidArgument, "Invalid value for --lighting (forward or forwardplus): " + value.value_or(""));
            }
            options.lighting = *mode;
        }
        else if (matchOption("--lights", value))
        {
            const auto count = value ? parseUnsigned(*value, 100000U) : std::nullopt;
            if (!count)
            {
                return makeError(ErrorCode::kInvalidArgument, "Invalid value for --lights (0 to 100000): " + value.value_or(""));
            }
            options.pointLights = *count;
        }
        else if (matchOption("--scene", value))
        {
            if (!value || (*value != "default" && *value != "mixed"))
            {
                return makeError(ErrorCode::kInvalidArgument, "Invalid value for --scene (default or mixed): " + (value ? *value : std::string()));
            }
            options.mixedScene = *value == "mixed";
        }
        else if (matchOption("--capture-frames", value))
        {
            if (!value)
            {
                return makeError(ErrorCode::kInvalidArgument, "Missing value after --capture-frames");
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

    if (!options.clickId.empty() && options.captureFrames < 3)
    {
        return makeError(ErrorCode::kInvalidArgument, "--click requires --capture-frames 3 or more");
    }

    if (!options.hoverId.empty() && options.captureFrames < 2)
    {
        return makeError(ErrorCode::kInvalidArgument, "--hover requires --capture-frames 2 or more");
    }

    return options;
}

void printUsage(std::ostream &out)
{
    out << "Usage: Orosmo [--api|--backend <name>] [--strict-backend] [--choose-api] [--list-apis] [--ui off|hud|full] [--theme dark|light|framed]\n"
        << "              [--tab scene|widgets|controls|color|windows|layout|themes] [--size WxH]\n"
        << "              [--instances N] [--debug-view none|normals|albedo|overdraw|lod|wireframe|motion] [--freeze-frustum]\n"
        << "              [--scene default|mixed] [--lighting forward|forwardplus] [--lights N] [--post none|hdr|bloom|full]\n"
        << "              [--headless] [--vsync]\n"
        << "              [--capture <dir> [--capture-frames N] [--hover <id>] [--click <id>]] [--help]\n";
    out << "Available API names: " << joinBackendNames() << '\n';
    out << "Backend : --backend is an alias of --api. If Vulkan cannot start (no loader, no Vulkan 1.3 GPU), the reason is printed\n";
    out << "          and OpenGL is used instead; --strict-backend disables this fallback and exits with an error.\n";
    out << "Example: Orosmo --api opengl --ui full --theme dark --tab scene\n";
    out << "Capture : renders N frames (default 10) at WxH (default 1280x720, min 640x400), writes <dir>/scene.png and\n";
    out << "          <dir>/ui.png, then exits. --hover/--click simulate pointer interaction (--click needs >= 3 frames, --hover >= 2).\n";
    out << "Headless: --headless creates no window (offscreen device, fixed time step), renders N frames and exits;\n";
    out << "          combine it with --capture <dir> to save the images.\n";
    out << "Debug   : --debug-view shows normals, albedo, overdraw, LOD colors (needs --instances), wireframe or motion vectors;\n";
    out << "          --freeze-frustum freezes the culling frustum of the instanced objects (needs --instances) and draws it.\n";
    out << "Instances: --instances N adds N instanced objects around the courtyard (frustum culling, distance LOD); the draw\n";
    out << "          calls and culling statistics are logged.\n";
    out << "Post:     --post hdr renders the scene in HDR with ACES tonemapping; bloom adds bloom; full adds auto exposure,\n";
    out << "          a color grading LUT and a vignette. none (default) keeps the LDR scene unchanged.\n";
    out << "Lighting: --lighting forwardplus uses clustered forward+ lighting (depth pre-pass, clusters filled by a compute pass;\n";
    out << "          falls back to forward without compute shaders). --lights N adds N deterministic point lights.\n";
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
    if (orosmo::rhi::isBackendCompiled(api))
    {
        return BackendAvailability::kAvailable;
    }
#ifndef __APPLE__
    if (api == Backend::kMetal)
    {
        return BackendAvailability::kUnsupportedPlatform;
    }
#endif
    return BackendAvailability::kNotBuilt; // Metal : M9
}

bool isBackendAvailable(Backend api) noexcept { return backendAvailability(api) == BackendAvailability::kAvailable; }

std::string backendAvailabilityMessage(Backend api)
{
    const std::string name(backendDisplayName(api));
    switch (backendAvailability(api))
    {
        case BackendAvailability::kAvailable: return name + " backend is available.";
        case BackendAvailability::kNotBuilt:
            return name + " backend selection is recognized, but this backend is not available yet (Metal: M9).";
        case BackendAvailability::kUnsupportedPlatform: return name + " backend is not supported on this platform.";
    }
    return "Unknown graphics backend state.";
}

BackendDecision resolveBackend(Backend requested, bool strict, BackendAvailability availability, std::string_view failureReason)
{
    BackendDecision decision;
    decision.backend = requested;
    const bool failed = availability != BackendAvailability::kAvailable || !failureReason.empty();
    if (!failed)
    {
        return decision;
    }
    std::string reason = failureReason.empty() ? backendAvailabilityMessage(requested) : std::string(failureReason);
    if (requested != Backend::kVulkan)
    {
        // Pas de repli pour OpenGL (le repli lui-même) ni pour Metal (macOS seulement, M9).
        decision.refused = true;
        decision.message = failureReason.empty() ? std::move(reason) : std::string(backendDisplayName(requested)) + " cannot start: " + reason;
        return decision;
    }
    if (strict)
    {
        decision.refused = true;
        decision.message = "Vulkan cannot start and --strict-backend forbids the OpenGL fallback: " + reason;
        return decision;
    }
    decision.backend = Backend::kOpenGL;
    decision.fellBack = true;
    decision.message = "Vulkan cannot start (" + reason + "); falling back to OpenGL.";
    return decision;
}

} // namespace demo
