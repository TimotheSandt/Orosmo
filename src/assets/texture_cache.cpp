#include "orosmo/assets/texture_cache.hpp"

#include <system_error>
#include <utility>

namespace orosmo::assets
{

core::Result<renderer::Texture> createTexture(rhi::Device &device, const DecodedImage &image, const TextureRequest &request,
                                              std::string debugName)
{
    if (image.empty() || image.rgba8.size() != static_cast<std::size_t>(image.width) * image.height * 4)
    {
        return core::makeError(core::ErrorCode::kInvalidArgument, "createTexture: empty or inconsistent image (" + debugName + ")");
    }
    renderer::TextureInfo info;
    info.format = textureFormatFor(request.role);
    info.width = image.width;
    info.height = image.height;
    info.filter = request.filter;
    info.address = request.address;
    info.debugName = std::move(debugName);
    return renderer::Texture::create(device, info, image.rgba8);
}

core::Result<std::shared_ptr<renderer::Texture>> TextureCache::insert(Key key, const DecodedImage &image, const TextureRequest &request,
                                                                     const std::string &debugName)
{
    auto texture = createTexture(*device_, image, request, debugName);
    if (!texture)
    {
        return std::unexpected(texture.error());
    }
    auto shared = std::make_shared<renderer::Texture>(std::move(*texture));
    entries_.emplace(std::move(key), shared);
    return shared;
}

core::Result<std::shared_ptr<renderer::Texture>> TextureCache::load(const std::filesystem::path &path, const TextureRequest &request)
{
    std::error_code ec;
    std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
    if (ec)
    {
        canonical = std::filesystem::absolute(path, ec).lexically_normal();
    }
    Key key{canonical.generic_string(), request.role, request.filter, request.address};
    if (auto it = entries_.find(key); it != entries_.end())
    {
        return it->second;
    }
    auto image = loadImage(path);
    if (!image)
    {
        return std::unexpected(image.error());
    }
    return insert(std::move(key), *image, request, path.filename().generic_string());
}

core::Result<std::shared_ptr<renderer::Texture>> TextureCache::loadFromMemory(const std::string &key, std::span<const std::byte> encoded,
                                                                             const TextureRequest &request)
{
    Key k{"mem:" + key, request.role, request.filter, request.address};
    if (auto it = entries_.find(k); it != entries_.end())
    {
        return it->second;
    }
    auto image = decodeImage(encoded, key);
    if (!image)
    {
        return std::unexpected(image.error());
    }
    return insert(std::move(k), *image, request, key);
}

std::size_t TextureCache::purgeUnused()
{
    return std::erase_if(entries_, [](const auto &entry) { return entry.second.use_count() == 1; });
}

} // namespace orosmo::assets
