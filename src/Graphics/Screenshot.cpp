#include "Graphics/Screenshot.h"

#include "Graphics/RenderTarget.h"
#include "Graphics/Texture.h"
#include "Logger.h"

#include <stb_image_write.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <vector>

namespace Screenshot
{

bool SavePng(const Texture &texture, const std::string &path)
{
    int width = 0;
    int height = 0;
    TextureFormat format = TextureFormat::RGBA8;
    TexturePixelType pixelType = TexturePixelType::UnsignedByte;
    const std::unique_ptr<void, decltype(&std::free)> data(texture.GetTextureData(width, height, format, pixelType), &std::free);

    if (data == nullptr || width <= 0 || height <= 0)
    {
        LOG_ERROR(1, "Screenshot: empty texture readback for ", path);
        return false;
    }
    if (format != TextureFormat::RGBA8 || pixelType != TexturePixelType::UnsignedByte)
    {
        LOG_ERROR(1, "Screenshot: only RGBA8 textures can be saved (", path, ")");
        return false;
    }

    // Backends read rows bottom-up (OpenGL convention): flip to a top-left origin.
    const std::size_t rowBytes = static_cast<std::size_t>(width) * 4;
    std::vector<unsigned char> flipped(rowBytes * static_cast<std::size_t>(height));
    const auto *source = static_cast<const unsigned char *>(data.get());
    for (int y = 0; y < height; ++y)
    {
        std::copy_n(source + static_cast<std::size_t>(height - 1 - y) * rowBytes, rowBytes, flipped.data() + static_cast<std::size_t>(y) * rowBytes);
    }

    std::error_code ec;
    const std::filesystem::path parent = std::filesystem::path(path).parent_path();
    if (!parent.empty())
    {
        std::filesystem::create_directories(parent, ec);
    }

    if (stbi_write_png(path.c_str(), width, height, 4, flipped.data(), static_cast<int>(rowBytes)) == 0)
    {
        LOG_ERROR(1, "Screenshot: cannot write ", path);
        return false;
    }
    LOG_INFO("Screenshot saved: ", path, " (", width, "x", height, ")");
    return true;
}

bool SavePng(const RenderTarget &target, std::uint32_t colorAttachment, const std::string &path)
{
    if (!target.IsInitialized() || colorAttachment >= target.GetColorAttachmentCount())
    {
        LOG_ERROR(1, "Screenshot: render target not ready for ", path);
        return false;
    }
    return SavePng(target.GetTexture(colorAttachment), path);
}

} // namespace Screenshot
