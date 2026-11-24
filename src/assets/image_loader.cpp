#include "orosmo/assets/image_loader.hpp"

#include <stb_image.h>

#include <climits>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>

namespace orosmo::assets
{

using core::ErrorCode;
using core::makeError;

ColorSpace colorSpaceOf(TextureRole role) noexcept
{
    return role == TextureRole::kColor ? ColorSpace::kSrgb : ColorSpace::kLinear;
}

rhi::Format textureFormatFor(TextureRole role) noexcept
{
    return colorSpaceOf(role) == ColorSpace::kSrgb ? rhi::Format::kRGBA8Srgb : rhi::Format::kRGBA8Unorm;
}

core::Result<DecodedImage> decodeImage(std::span<const std::byte> encoded, std::string_view name)
{
    if (encoded.empty())
    {
        return makeError(ErrorCode::kInvalidArgument, "decodeImage: no bytes (" + std::string(name) + ")");
    }
    if (encoded.size() > static_cast<std::size_t>(INT_MAX))
    {
        return makeError(ErrorCode::kUnsupported, "decodeImage: file too large (" + std::string(name) + ")");
    }
    // Réglage global de stb jamais modifié (pas de retournement) : la première ligne décodée est le haut de l'image, comme le
    // repère du RHI (A26). stbi_load_from_memory est réentrant tant que ce réglage ne change pas.
    int w = 0, h = 0, channels = 0;
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> data(
        stbi_load_from_memory(reinterpret_cast<const stbi_uc *>(encoded.data()), static_cast<int>(encoded.size()), &w, &h, &channels, 4),
        &stbi_image_free);
    if (!data)
    {
        return makeError(ErrorCode::kParse, "decodeImage: cannot decode " + std::string(name) + ": " + stbi_failure_reason());
    }
    DecodedImage out;
    out.width = static_cast<std::uint32_t>(w);
    out.height = static_cast<std::uint32_t>(h);
    out.sourceChannels = static_cast<std::uint32_t>(channels);
    out.rgba8.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    std::memcpy(out.rgba8.data(), data.get(), out.rgba8.size());
    return out;
}

core::Result<DecodedImage> loadImage(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return makeError(ErrorCode::kIo, "loadImage: cannot open " + path.generic_string());
    }
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad())
    {
        return makeError(ErrorCode::kIo, "loadImage: cannot read " + path.generic_string());
    }
    return decodeImage(std::as_bytes(std::span(bytes)), path.generic_string());
}

} // namespace orosmo::assets
