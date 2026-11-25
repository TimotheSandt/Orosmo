#include "orosmo/assets/texture_cache.hpp"

#include "orosmo/core/log.hpp"

#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

namespace orosmo::assets
{

using core::ErrorCode;
using core::makeError;

namespace
{

core::Result<std::vector<std::byte>> readFile(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return makeError(ErrorCode::kIo, "TextureCache: cannot open " + path.generic_string());
    }
    std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad())
    {
        return makeError(ErrorCode::kIo, "TextureCache: cannot read " + path.generic_string());
    }
    const auto *p = reinterpret_cast<const std::byte *>(raw.data());
    return std::vector<std::byte>(p, p + raw.size());
}

// Date et taille d'un fichier ; {} si le fichier n'existe pas (ou plus).
std::pair<std::filesystem::file_time_type, std::uintmax_t> stampOf(const std::filesystem::path &path)
{
    std::error_code ec;
    const auto time = std::filesystem::last_write_time(path, ec);
    if (ec)
    {
        return {};
    }
    const auto size = std::filesystem::file_size(path, ec);
    return {time, ec ? 0 : size};
}

std::vector<std::span<const std::byte>> spansOf(const TextureLevels &levels)
{
    std::vector<std::span<const std::byte>> spans;
    spans.reserve(levels.levels.size());
    for (const auto &l : levels.levels)
    {
        spans.emplace_back(l);
    }
    return spans;
}

renderer::TextureInfo infoFor(rhi::Format format, std::uint32_t width, std::uint32_t height, const TextureRequest &request, std::string debugName)
{
    renderer::TextureInfo info;
    info.format = format;
    info.width = width;
    info.height = height;
    info.filter = request.filter;
    info.address = request.address;
    info.debugName = std::move(debugName);
    return info;
}

} // namespace

core::Result<renderer::Texture> createTexture(rhi::Device &device, const DecodedImage &image, const TextureRequest &request,
                                              std::string debugName)
{
    if (image.empty() || image.rgba8.size() != static_cast<std::size_t>(image.width) * image.height * 4)
    {
        return makeError(ErrorCode::kInvalidArgument, "createTexture: empty or inconsistent image (" + debugName + ")");
    }
    return renderer::Texture::create(device, infoFor(textureFormatFor(request.role), image.width, image.height, request, std::move(debugName)),
                                     image.rgba8);
}

core::Result<renderer::Texture> createTexture(rhi::Device &device, const TextureLevels &levels, const TextureRequest &request,
                                              std::string debugName)
{
    if (levels.empty())
    {
        return makeError(ErrorCode::kInvalidArgument, "createTexture: no level (" + debugName + ")");
    }
    TextureRequest effective = request;
    if (levels.levels.size() == 1 && effective.filter == renderer::FilterMode::kLinear)
    {
        effective.filter = renderer::FilterMode::kLinearNoMipmaps; // un seul niveau fourni : pas de mips à échantillonner
    }
    const auto spans = spansOf(levels);
    return renderer::Texture::createWithLevels(device, infoFor(levels.format, levels.width, levels.height, effective, std::move(debugName)), spans);
}

core::Result<renderer::Texture> TextureCache::build(std::span<const std::byte> encoded, const TextureRequest &request, const std::string &name)
{
    if (isKtx2(encoded))
    {
        auto levels = decodeKtx2(encoded, device_->capabilities(), name);
        if (!levels)
        {
            return std::unexpected(levels.error());
        }
        return createTexture(*device_, *levels, request, name);
    }
    auto image = decodeImage(encoded, name);
    if (!image)
    {
        return std::unexpected(image.error());
    }
    return createTexture(*device_, *image, request, name);
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
        return it->second.texture;
    }
    const auto stamp = stampOf(path);
    auto bytes = readFile(path);
    if (!bytes)
    {
        return std::unexpected(bytes.error());
    }
    auto texture = build(*bytes, request, path.filename().generic_string());
    if (!texture)
    {
        return std::unexpected(texture.error());
    }
    auto shared = std::make_shared<renderer::Texture>(std::move(*texture));
    entries_.emplace(std::move(key), Entry{shared, canonical, stamp.first, stamp.second});
    return shared;
}

core::Result<std::shared_ptr<renderer::Texture>> TextureCache::loadFromMemory(const std::string &key, std::span<const std::byte> encoded,
                                                                             const TextureRequest &request)
{
    Key k{"mem:" + key, request.role, request.filter, request.address};
    if (auto it = entries_.find(k); it != entries_.end())
    {
        return it->second.texture;
    }
    auto texture = build(encoded, request, key);
    if (!texture)
    {
        return std::unexpected(texture.error());
    }
    auto shared = std::make_shared<renderer::Texture>(std::move(*texture));
    entries_.emplace(std::move(k), Entry{shared, {}, {}, 0});
    return shared;
}

std::size_t TextureCache::reloadChanged()
{
    std::size_t reloaded = 0;
    for (auto &[key, entry] : entries_)
    {
        if (entry.file.empty())
        {
            continue;
        }
        const auto stamp = stampOf(entry.file);
        if (stamp.first == std::filesystem::file_time_type{} || (stamp.first == entry.time && stamp.second == entry.size))
        {
            continue; // absent (peut-être en cours d'écriture) ou inchangé
        }
        entry.time = stamp.first;
        entry.size = stamp.second;
        const TextureRequest request{std::get<1>(key), std::get<2>(key), std::get<3>(key)};
        const std::string name = entry.file.filename().generic_string();
        auto bytes = readFile(entry.file);
        if (!bytes)
        {
            LOG(::orosmo::core::L_ERROR, "TextureCache: reload of ", entry.file.generic_string(), " failed, the previous texture is kept: ", bytes.error().message);
            continue;
        }
        renderer::Texture &current = *entry.texture;
        core::Result<> inPlace = makeError(ErrorCode::kUnsupported, "shape changed");
        if (isKtx2(*bytes))
        {
            auto levels = decodeKtx2(*bytes, device_->capabilities(), name);
            if (!levels)
            {
                LOG(::orosmo::core::L_ERROR, "TextureCache: reload of ", entry.file.generic_string(), " failed, the previous texture is kept: ", levels.error().message);
                continue;
            }
            if (levels->format == current.format() && levels->width == current.width() && levels->height == current.height() &&
                levels->levels.size() == current.mipLevels())
            {
                inPlace = current.uploadLevels(spansOf(*levels));
            }
        }
        else
        {
            auto image = decodeImage(*bytes, name);
            if (!image)
            {
                LOG(::orosmo::core::L_ERROR, "TextureCache: reload of ", entry.file.generic_string(), " failed, the previous texture is kept: ", image.error().message);
                continue;
            }
            if (image->width == current.width() && image->height == current.height())
            {
                inPlace = current.upload(image->rgba8); // même handle : les groupes de liaison des matériaux restent bons
            }
        }
        if (!inPlace)
        {
            // Taille ou format changés : nouvelle texture, nouveau handle. Les matériaux qui la lient doivent reconstruire leur groupe.
            auto fresh = build(*bytes, request, name);
            if (!fresh)
            {
                LOG(::orosmo::core::L_ERROR, "TextureCache: reload of ", entry.file.generic_string(), " failed, the previous texture is kept: ", fresh.error().message);
                continue;
            }
            LOG_WARNING("TextureCache: ", entry.file.generic_string(), " changed size or format; materials using it must rebuild their bind group");
            current = std::move(*fresh);
        }
        ++reloaded;
    }
    return reloaded;
}

std::size_t TextureCache::purgeUnused()
{
    return std::erase_if(entries_, [](const auto &entry) { return entry.second.texture.use_count() == 1; });
}

} // namespace orosmo::assets
