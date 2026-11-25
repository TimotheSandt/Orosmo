#include "orosmo/rhi/types.hpp"

namespace orosmo::rhi
{

const char *toString(Backend backend) noexcept
{
    switch (backend)
    {
        case Backend::kVulkan: return "vulkan";
        case Backend::kOpenGL: return "opengl";
        case Backend::kMetal: return "metal";
    }
    return "unknown";
}

FormatInfo formatInfo(Format format) noexcept
{
    switch (format)
    {
        case Format::kR8Unorm: return {1, 1, FormatKind::kUnorm};
        case Format::kRG8Unorm: return {2, 2, FormatKind::kUnorm};
        case Format::kRGBA8Unorm:
        case Format::kBGRA8Unorm: return {4, 4, FormatKind::kUnorm};
        case Format::kRGBA8Srgb: return {4, 4, FormatKind::kSrgb};
        case Format::kR16Float: return {2, 1, FormatKind::kFloat};
        case Format::kRG16Float: return {4, 2, FormatKind::kFloat};
        case Format::kRGBA16Float: return {8, 4, FormatKind::kFloat};
        case Format::kR32Float: return {4, 1, FormatKind::kFloat};
        case Format::kRG32Float: return {8, 2, FormatKind::kFloat};
        case Format::kRGBA32Float: return {16, 4, FormatKind::kFloat};
        case Format::kR32Uint: return {4, 1, FormatKind::kUint};
        case Format::kD32Float: return {4, 1, FormatKind::kDepth};
        case Format::kD24UnormS8Uint: return {4, 2, FormatKind::kDepthStencil};
        case Format::kBC7Unorm:
        case Format::kASTC4x4Unorm: return {0, 4, FormatKind::kUnorm, 4, 16};
        case Format::kBC7Srgb:
        case Format::kASTC4x4Srgb: return {0, 4, FormatKind::kSrgb, 4, 16};
        case Format::kUndefined: break;
    }
    return {};
}

std::uint64_t imageBytes(Format format, std::uint32_t width, std::uint32_t height, std::uint32_t depth) noexcept
{
    const FormatInfo info = formatInfo(format);
    if (info.compressed())
    {
        const std::uint64_t bx = (static_cast<std::uint64_t>(width) + info.blockExtent - 1) / info.blockExtent;
        const std::uint64_t by = (static_cast<std::uint64_t>(height) + info.blockExtent - 1) / info.blockExtent;
        return bx * by * depth * info.bytesPerBlock;
    }
    return static_cast<std::uint64_t>(width) * height * depth * info.bytesPerTexel;
}

bool blockAligned(Format format, std::int32_t x, std::int32_t y, std::uint32_t width, std::uint32_t height, std::uint32_t levelWidth,
                  std::uint32_t levelHeight) noexcept
{
    const FormatInfo info = formatInfo(format);
    if (!info.compressed())
    {
        return true;
    }
    const auto b = static_cast<std::int64_t>(info.blockExtent);
    const auto edgeOk = [&](std::int32_t offset, std::uint32_t size, std::uint32_t level)
    { return size % info.blockExtent == 0 || static_cast<std::uint64_t>(offset) + size == level; };
    return x % b == 0 && y % b == 0 && edgeOk(x, width, levelWidth) && edgeOk(y, height, levelHeight);
}

const char *toString(Format format) noexcept
{
    switch (format)
    {
        case Format::kUndefined: return "undefined";
        case Format::kR8Unorm: return "R8Unorm";
        case Format::kRG8Unorm: return "RG8Unorm";
        case Format::kRGBA8Unorm: return "RGBA8Unorm";
        case Format::kBGRA8Unorm: return "BGRA8Unorm";
        case Format::kRGBA8Srgb: return "RGBA8Srgb";
        case Format::kR16Float: return "R16Float";
        case Format::kRG16Float: return "RG16Float";
        case Format::kRGBA16Float: return "RGBA16Float";
        case Format::kR32Float: return "R32Float";
        case Format::kRG32Float: return "RG32Float";
        case Format::kRGBA32Float: return "RGBA32Float";
        case Format::kR32Uint: return "R32Uint";
        case Format::kD32Float: return "D32Float";
        case Format::kD24UnormS8Uint: return "D24UnormS8Uint";
        case Format::kBC7Unorm: return "BC7Unorm";
        case Format::kBC7Srgb: return "BC7Srgb";
        case Format::kASTC4x4Unorm: return "ASTC4x4Unorm";
        case Format::kASTC4x4Srgb: return "ASTC4x4Srgb";
    }
    return "unknown";
}

} // namespace orosmo::rhi
