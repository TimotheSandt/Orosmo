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

std::optional<ShadowQuality> parseShadowQuality(std::string_view value) noexcept
{
    constexpr std::pair<std::string_view, ShadowQuality> kQualities[] = {{"low", ShadowQuality::kLow}, {"medium", ShadowQuality::kMedium}, {"high", ShadowQuality::kHigh}};
    for (const auto &[name, quality] : kQualities)
    {
        if (equalsIgnoreCase(value, name))
        {
            return quality;
        }
    }
    return std::nullopt;
}

std::string_view shadowQualityName(ShadowQuality quality) noexcept
{
    switch (quality)
    {
    case ShadowQuality::kLow:
        return "low";
    case ShadowQuality::kMedium:
        return "medium";
    case ShadowQuality::kHigh:
        break;
    }
    return "high";
}

orosmo::renderer::ShadowSettings shadowSettingsForQuality(ShadowQuality quality) noexcept
{
    orosmo::renderer::ShadowSettings settings{};
    switch (quality)
    {
    case ShadowQuality::kLow:
        settings.cascadeResolution = 256;
        settings.cascadeCount = 2;
        settings.atlasSize = 1024;
        settings.spotResolution = 256;
        settings.pcfRadius = 0;
        settings.maxDistance = 40.0f;
        settings.cascadeUpdateInterval = {1, 4, 8, 16};
        settings.maxViewUpdatesPerFrame = 4;
        break;
    case ShadowQuality::kMedium:
        settings.cascadeResolution = 512;
        settings.cascadeCount = 3;
        settings.atlasSize = 2048;
        settings.spotResolution = 512;
        settings.pcfRadius = 1;
        settings.maxDistance = 60.0f;
        settings.cascadeUpdateInterval = {1, 2, 4, 8};
        settings.maxViewUpdatesPerFrame = 6;
        break;
    case ShadowQuality::kHigh:
        break; // réglages du moteur
    }
    return settings;
}

bool sameShadowSettings(const orosmo::renderer::ShadowSettings &left, const orosmo::renderer::ShadowSettings &right) noexcept
{
    return left.atlasSize == right.atlasSize && left.minTileSize == right.minTileSize && left.cascadeCount == right.cascadeCount &&
           left.cascadeResolution == right.cascadeResolution && left.maxDistance == right.maxDistance && left.splitLambda == right.splitLambda &&
           left.fadeFraction == right.fadeFraction && left.depthExtension == right.depthExtension && left.cascadeUpdateInterval == right.cascadeUpdateInterval &&
           left.maxViewUpdatesPerFrame == right.maxViewUpdatesPerFrame && left.pcfRadius == right.pcfRadius && left.constantBias == right.constantBias &&
           left.slopeBias == right.slopeBias && left.normalOffset == right.normalOffset && left.spotResolution == right.spotResolution &&
           left.maxSpotShadows == right.maxSpotShadows;
}

std::string_view shadowQualityLabel(const orosmo::renderer::ShadowSettings &settings) noexcept
{
    for (const ShadowQuality quality : {ShadowQuality::kLow, ShadowQuality::kMedium, ShadowQuality::kHigh})
    {
        if (sameShadowSettings(settings, shadowSettingsForQuality(quality)))
        {
            return shadowQualityName(quality);
        }
    }
    return "custom";
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

std::optional<HudLevel> parseHudLevel(std::string_view value) noexcept
{
    for (const HudLevel level : {HudLevel::kOff, HudLevel::kFps, HudLevel::kFull, HudLevel::kGraph})
    {
        if (equalsIgnoreCase(value, hudLevelName(level)))
        {
            return level;
        }
    }
    return std::nullopt;
}

HudLevel nextHudLevel(HudLevel level) noexcept
{
    switch (level)
    {
    case HudLevel::kOff:
        return HudLevel::kFps;
    case HudLevel::kFps:
        return HudLevel::kFull;
    case HudLevel::kFull:
        return HudLevel::kGraph;
    case HudLevel::kGraph:
        break;
    }
    return HudLevel::kOff;
}

std::string_view hudLevelName(HudLevel level) noexcept
{
    switch (level)
    {
    case HudLevel::kOff:
        return "off";
    case HudLevel::kFps:
        return "fps";
    case HudLevel::kFull:
        return "full";
    case HudLevel::kGraph:
        break;
    }
    return "graph";
}

std::string_view postModeName(PostMode mode) noexcept
{
    switch (mode)
    {
    case PostMode::kNone:
        return "none";
    case PostMode::kHdr:
        return "hdr";
    case PostMode::kBloom:
        return "bloom";
    case PostMode::kFull:
        break;
    }
    return "full";
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

std::optional<GpuTimingsMode> parseGpuTimingsMode(std::string_view value) noexcept
{
    for (const GpuTimingsMode mode : {GpuTimingsMode::kAuto, GpuTimingsMode::kOn, GpuTimingsMode::kOff})
    {
        if (equalsIgnoreCase(value, gpuTimingsModeName(mode)))
        {
            return mode;
        }
    }
    return std::nullopt;
}

std::string_view gpuTimingsModeName(GpuTimingsMode mode) noexcept
{
    switch (mode)
    {
    case GpuTimingsMode::kAuto:
        return "auto";
    case GpuTimingsMode::kOn:
        return "on";
    case GpuTimingsMode::kOff:
        return "off";
    }
    return "auto";
}

bool gpuTimingsWanted(GpuTimingsMode mode, UiMode uiMode, HudLevel level) noexcept
{
    if (mode != GpuTimingsMode::kAuto)
    {
        return mode == GpuTimingsMode::kOn;
    }
    return uiMode != UiMode::kOff && (level == HudLevel::kFull || level == HudLevel::kGraph);
}

orosmo::renderer::FramePacing framePacingFor(const LaunchOptions &options) noexcept
{
    using orosmo::renderer::FramePacingMode;
    if (options.fpsLimit > 0)
    {
        return {FramePacingMode::kLimited, static_cast<int>(options.fpsLimit)};
    }
    return {options.vsync ? FramePacingMode::kVSync : FramePacingMode::kUnlimited, 60};
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
        else if (argument == "--vsync" || argument.starts_with("--vsync="))
        {
            // « --vsync » seul = on ; « --vsync on|off » ou « --vsync=on|off ».
            std::optional<std::string> state;
            if (argument.size() > 7)
            {
                state = argument.substr(8);
            }
            else if (index + 1 < argc && (equalsIgnoreCase(argv[index + 1], "on") || equalsIgnoreCase(argv[index + 1], "off")))
            {
                state = std::string(argv[++index]);
            }
            options.vsync = !state || !equalsIgnoreCase(*state, "off");
            if (state && !equalsIgnoreCase(*state, "on") && !equalsIgnoreCase(*state, "off"))
            {
                return makeError(ErrorCode::kInvalidArgument, "Invalid value for --vsync (expected on|off): " + *state);
            }
            options.vsyncExplicit = true;
        }
        else if (matchOption("--fps-limit", value))
        {
            if (!value)
            {
                return makeError(ErrorCode::kInvalidArgument, "Missing value after --fps-limit");
            }
            const auto limit = parseUnsigned(*value, 1000U);
            if (!limit || (*limit != 0 && *limit < 10))
            {
                return makeError(ErrorCode::kInvalidArgument, "Invalid value for --fps-limit (0 for no limit, or 10 to 1000): " + *value);
            }
            options.fpsLimit = *limit;
        }
        else if (matchOption("--direct-present", value))
        {
            if (!value || (!equalsIgnoreCase(*value, "on") && !equalsIgnoreCase(*value, "off")))
            {
                return makeError(ErrorCode::kInvalidArgument, "Invalid value for --direct-present (expected on|off): " + value.value_or(""));
            }
            options.directPresentation = equalsIgnoreCase(*value, "on");
        }
        else if (matchOption("--frames-in-flight", value))
        {
            const auto count = value ? parseUnsigned(*value, orosmo::rhi::kMaxFramesInFlight) : std::nullopt;
            if (!count || *count == 0)
            {
                return makeError(ErrorCode::kInvalidArgument, "Invalid value for --frames-in-flight (1 to 3): " + value.value_or(""));
            }
            options.framesInFlight = *count;
        }
        else if (matchOption("--graph-cache", value))
        {
            if (!value || (!equalsIgnoreCase(*value, "on") && !equalsIgnoreCase(*value, "off")))
            {
                return makeError(ErrorCode::kInvalidArgument, "Invalid value for --graph-cache (expected on|off): " + value.value_or(""));
            }
            options.graphCache = equalsIgnoreCase(*value, "on");
        }
        else if (matchOption("--gpu-timings", value))
        {
            if (!value)
            {
                return makeError(ErrorCode::kInvalidArgument, "Missing value after --gpu-timings");
            }
            const auto mode = parseGpuTimingsMode(*value);
            if (!mode)
            {
                return makeError(ErrorCode::kInvalidArgument, "Unknown --gpu-timings value (expected auto|on|off): " + *value);
            }
            options.gpuTimings = *mode;
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
        else if (matchOption("--hud", value))
        {
            if (!value)
            {
                return makeError(ErrorCode::kInvalidArgument, "Missing value after --hud");
            }
            const auto level = parseHudLevel(*value);
            if (!level)
            {
                return makeError(ErrorCode::kInvalidArgument, "Unknown HUD level (expected off|fps|full|graph): " + *value);
            }
            options.hudLevel = *level;
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
        else if (matchOption("--focus", value))
        {
            if (!value || value->empty())
            {
                return makeError(ErrorCode::kInvalidArgument, "Missing value after --focus");
            }
            options.focusId = std::move(*value);
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
        else if (matchOption("--gpu-driven", value))
        {
            if (!value || (*value != "on" && *value != "off"))
            {
                return makeError(ErrorCode::kInvalidArgument, "Invalid value for --gpu-driven (expected on or off)");
            }
            options.gpuDriven = *value == "on";
        }
        else if (matchOption("--aa", value))
        {
            const auto mode = value ? orosmo::renderer::parseAntiAliasingMode(*value) : std::nullopt;
            if (!mode)
            {
                return makeError(ErrorCode::kInvalidArgument,
                                 "Invalid value for --aa (none|fxaa|smaa|taa): " + (value ? *value : std::string("<missing>")));
            }
            options.antiAliasing = *mode;
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
        else if (matchOption("--shadows", value))
        {
            if (!value || (*value != "on" && *value != "off"))
            {
                return makeError(ErrorCode::kInvalidArgument, "Invalid value for --shadows (expected on or off)");
            }
            options.shadows = *value == "on";
        }
        else if (matchOption("--shadow-quality", value))
        {
            const auto quality = value ? parseShadowQuality(*value) : std::nullopt;
            if (!quality)
            {
                return makeError(ErrorCode::kInvalidArgument, "Invalid value for --shadow-quality (low, medium or high): " + value.value_or(""));
            }
            options.shadowQuality = *quality;
        }
        else if (matchOption("--transparent-shadow", value))
        {
            const auto mode = value ? orosmo::renderer::transparentShadowModeFromName(*value) : std::unexpected(makeError(ErrorCode::kInvalidArgument, "").error());
            if (!mode)
            {
                return makeError(ErrorCode::kInvalidArgument, "Invalid value for --transparent-shadow (none, full, masked or dithered)");
            }
            options.transparentShadow = *mode;
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
            if (!value || (*value != "default" && *value != "mixed" && *value != "anim"))
            {
                return makeError(ErrorCode::kInvalidArgument, "Invalid value for --scene (default, mixed or anim): " + (value ? *value : std::string()));
            }
            options.mixedScene = *value == "mixed";
            options.animScene = *value == "anim";
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

    if (!options.focusId.empty() && options.captureFrames < 2)
    {
        return makeError(ErrorCode::kInvalidArgument, "--focus requires --capture-frames 2 or more");
    }

    if (!options.hoverId.empty() && options.captureFrames < 2)
    {
        return makeError(ErrorCode::kInvalidArgument, "--hover requires --capture-frames 2 or more");
    }

    if (options.fpsLimit > 0 && options.vsyncExplicit && options.vsync)
    {
        return makeError(ErrorCode::kInvalidArgument, "--fps-limit turns the vertical synchronization off: it cannot be combined with --vsync on");
    }
    if (options.fpsLimit > 0)
    {
        options.vsync = false;
    }

    return options;
}

void printUsage(std::ostream &out)
{
    out << "Usage: Orosmo [--api|--backend <name>] [--strict-backend] [--choose-api] [--list-apis] [--ui off|hud|full] [--hud off|fps|full|graph]\n"
        << "              [--theme dark|light|framed]"
        << " [--tab scene|widgets|controls|color|windows|layout|themes] [--size WxH]\n"
        << "              [--instances N] [--gpu-driven off|on] [--debug-view none|normals|albedo|overdraw|lod|wireframe|motion] [--freeze-frustum]\n"
        << "              [--shadows off|on] [--shadow-quality low|medium|high] [--transparent-shadow none|full|masked|dithered] [--scene default|mixed|anim] [--lighting forward|forwardplus] [--lights N] [--post none|hdr|bloom|full]\n"
        << "              [--aa none|fxaa|smaa|taa]\n"
        << "              [--headless] [--vsync [on|off]] [--fps-limit N] [--direct-present on|off] [--gpu-timings auto|on|off]\n"
        << "              [--frames-in-flight 1|2|3] [--graph-cache on|off]\n"
        << "              [--capture <dir> [--capture-frames N] [--hover <id>] [--click <id>] [--focus <id>]] [--help]\n";
    out << "Available API names: " << joinBackendNames() << '\n';
    out << "Backend : Vulkan by default; --backend is an alias of --api. If Vulkan cannot start (no loader, no Vulkan 1.3 GPU), the reason is printed\n";
    out << "          and OpenGL is used instead; --strict-backend disables this fallback and exits with an error.\n";
    out << "Example: Orosmo --api opengl --ui full --theme dark --tab scene\n";
    out << "Capture : renders N frames (default 10) at WxH (default 1280x720, min 640x400), writes <dir>/scene.png and\n";
    out << "          <dir>/ui.png, then exits. --hover/--click simulate pointer interaction (--click needs >= 3 frames, --hover >= 2);\n"
        << "          --focus gives the keyboard focus to a widget, focus ring shown (>= 2 frames).\n";
    out << "Headless: --headless creates no window (offscreen device, fixed time step), renders N frames and exits;\n";
    out << "          combine it with --capture <dir> to save the images.\n";
    out << "Pacing  : vertical synchronization is on by default; --vsync off removes it (no limit), --fps-limit N (10 to 1000) caps\n";
    out << "          the frame rate at N FPS instead (vsync off). No effect with --headless. The Esc UI changes the mode live.\n";
    out << "Present : --direct-present off makes Vulkan render into an offscreen image copied to the window at present (on by\n";
    out << "          default: drawn straight into the window image, one copy less). On may flicker at very high frame rates\n";
    out << "          without vsync on some compositors (UI missing, black areas). No effect with OpenGL or --headless.\n";
    out << "GPU timers: --gpu-timings auto (default) only measures GPU times while the HUD shows them (levels full and graph);\n";
    out << "          on / off force them (tools that read the 'Perf: RenderGraph/...' lines use on).\n";
    out << "Frames  : --frames-in-flight 1|2|3 (default 2) sets how many frames the CPU may prepare ahead of the GPU; --graph-cache\n";
    out << "          on (default) keeps the render graph between frames while plugins, targets and sizes stay the same, off rebuilds it.\n";
    out << "HUD     : --hud sets the performance panel shown with --ui hud: off (nothing), fps (FPS only), full (default: every\n";
    out << "          measurement), graph (full + CPU/GPU frame time history in the bottom right corner). Ctrl+F cycles the levels.\n";
    out << "Debug   : --debug-view shows normals, albedo, overdraw, LOD colors (needs --instances), wireframe or motion vectors;\n";
    out << "          --freeze-frustum freezes the culling frustum of the instanced objects (needs --instances) and draws it.\n";
    out << "Instances: --instances N adds N instanced objects around the courtyard (frustum culling, distance LOD); the draw\n";
    out << "          calls and culling statistics are logged.\n";
    out << "GPU-driven: --gpu-driven on culls those objects on the GPU (persistent GPU scene, multi-draw indirect, Hi-Z occlusion\n";
    out << "          from the previous frame); GPU and CPU culling counts are logged side by side. Falls back to CPU culling.\n";
    out << "Shadows  : --shadows on draws cascaded sun shadows with a cache (redrawn only where something moves); the shadow\n";
    out << "          update statistics are logged. Off by default.\n";
    out << "          --shadow-quality low|medium|high picks the shadow preset (cascade and spot resolution, cascade count, atlas size,\n";
    out << "          PCF radius, distance, update rate); high (default) is the engine defaults. Needs --shadows on.\n";
    out << "          --transparent-shadow none|full|masked|dithered makes the panes of --scene mixed cast a shadow of that kind\n";
    out << "          (needs --shadows on; absent: the panes cast no shadow).\n";
    out << "Post:     --post hdr renders the scene in HDR with ACES tonemapping; bloom adds bloom; full adds auto exposure,\n";
    out << "          a color grading LUT and a vignette. none (default) keeps the LDR scene unchanged.\n";
    out << "AA:       --aa fxaa|smaa run on the displayed values (after tonemapping); taa jitters the camera and accumulates\n";
    out << "          the HDR scene before the post-processing (motion vectors, history rejection). none (default): no AA.\n";
    out << "          taa is refused with --instances (the instanced objects write no motion vectors): AA is then disabled.\n";
    out << "Anim    : --scene anim adds a skinned, animated model generated by code (written to glTF, then imported), driven by a\n";
    out << "          state machine and skinned by a compute pass, and a crowd of 1000 agents (one draw call per skinned primitive).\n";
    out << "          Its color texture (path logged) is reloaded live when the file changes on disk.\n";
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
    output << "Press Enter for " << backendDisplayName(Backend::kVulkan) << ".\n";
    output << "> ";

    std::string choice;
    if (!std::getline(input, choice))
    {
        return false;
    }
    if (choice.empty())
    {
        selected = Backend::kVulkan;
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

AntiAliasingDecision resolveAntiAliasing(orosmo::renderer::AntiAliasingMode requested, std::uint32_t instances)
{
    if (requested == orosmo::renderer::AntiAliasingMode::kTaa && instances > 0)
    {
        return {orosmo::renderer::AntiAliasingMode::kNone,
                "--aa taa is not supported with --instances: the instanced objects write no motion vectors; anti-aliasing disabled"};
    }
    return {requested, {}};
}

} // namespace demo
