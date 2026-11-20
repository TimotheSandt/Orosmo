#include "orosmo/renderer/image.hpp"

#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace orosmo::renderer
{

using core::ErrorCode;
using core::makeError;

core::Result<Image> readImage(rhi::Device &device, rhi::TextureHandle texture)
{
    auto desc = device.textureDesc(texture);
    if (!desc)
    {
        return std::unexpected(desc.error());
    }
    const bool bgra = desc->format == rhi::Format::kBGRA8Unorm;
    if (desc->dimension != rhi::TextureDimension::k2D ||
        (desc->format != rhi::Format::kRGBA8Unorm && desc->format != rhi::Format::kRGBA8Srgb && !bgra))
    {
        return makeError(ErrorCode::kUnsupported, std::string("readImage: only 2D RGBA8/BGRA8 textures can be read as an image, got ") +
                                                      rhi::toString(desc->format));
    }
    auto raw = device.readTexture(texture);
    if (!raw)
    {
        return std::unexpected(raw.error());
    }
    Image image;
    image.width = desc->size.width;
    image.height = desc->size.height;
    const std::size_t rowBytes = static_cast<std::size_t>(image.width) * 4;
    image.rgba.resize(rowBytes * image.height);
    // Repère du RHI (A26) : la ligne 0 est le haut de la texture, comme celle d'une image : copie directe.
    std::memcpy(image.rgba.data(), raw->data(), rowBytes * image.height);
    if (bgra)
    {
        for (std::size_t i = 0; i < image.rgba.size(); i += 4)
        {
            std::swap(image.rgba[i], image.rgba[i + 2]);
        }
    }
    return image;
}

core::Result<> savePng(const Image &image, const std::filesystem::path &path)
{
    if (image.empty())
    {
        return makeError(ErrorCode::kInvalidArgument, "savePng: empty image");
    }
    std::error_code ec;
    if (const auto parent = path.parent_path(); !parent.empty())
    {
        std::filesystem::create_directories(parent, ec);
    }
    if (stbi_write_png(path.string().c_str(), static_cast<int>(image.width), static_cast<int>(image.height), 4, image.rgba.data(),
                       static_cast<int>(image.width * 4)) == 0)
    {
        return makeError(ErrorCode::kIo, "savePng: cannot write " + path.generic_string());
    }
    return {};
}

core::Result<> savePng(rhi::Device &device, rhi::TextureHandle texture, const std::filesystem::path &path)
{
    auto image = readImage(device, texture);
    if (!image)
    {
        return std::unexpected(image.error());
    }
    return savePng(*image, path);
}

core::Result<Image> loadPng(const std::filesystem::path &path)
{
    int w = 0, h = 0, channels = 0;
    // Pas de stbi_set_flip_vertically_on_load : le réglage est global (non synchronisé) ; personne ne le modifie plus, il reste à
    // sa valeur par défaut (pas de retournement).
    std::unique_ptr<unsigned char, decltype(&stbi_image_free)> data(stbi_load(path.string().c_str(), &w, &h, &channels, 4), &stbi_image_free);
    if (!data)
    {
        return makeError(ErrorCode::kIo, "loadPng: cannot read " + path.generic_string() + ": " + stbi_failure_reason());
    }
    Image image;
    image.width = static_cast<std::uint32_t>(w);
    image.height = static_cast<std::uint32_t>(h);
    image.rgba.assign(data.get(), data.get() + static_cast<std::size_t>(w) * h * 4);
    return image;
}

core::Result<ImageDiff> compareImages(const Image &a, const Image &b, std::uint8_t tolerance)
{
    if (a.empty() || b.empty() || a.width != b.width || a.height != b.height)
    {
        return makeError(ErrorCode::kInvalidArgument, "compareImages: images are empty or differ in size");
    }
    ImageDiff diff;
    diff.pixels = static_cast<std::uint64_t>(a.width) * a.height;
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < a.rgba.size(); i += 4)
    {
        std::uint8_t pixelMax = 0;
        for (std::size_t c = 0; c < 4; ++c)
        {
            const auto d = static_cast<std::uint8_t>(a.rgba[i + c] > b.rgba[i + c] ? a.rgba[i + c] - b.rgba[i + c] : b.rgba[i + c] - a.rgba[i + c]);
            pixelMax = std::max(pixelMax, d);
            sum += d;
        }
        diff.maxDelta = std::max(diff.maxDelta, pixelMax);
        if (pixelMax > tolerance)
        {
            ++diff.outOfTolerance;
        }
    }
    diff.meanDelta = static_cast<double>(sum) / static_cast<double>(a.rgba.size());
    return diff;
}

} // namespace orosmo::renderer
