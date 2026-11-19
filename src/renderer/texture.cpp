#include "engine/renderer/texture.hpp"

#include <stb_image.h>

#include <memory>

namespace engine::renderer
{

using core::ErrorCode;
using core::makeError;

std::size_t Texture::byteSize() const noexcept
{
    return static_cast<std::size_t>(info_.width) * info_.height * rhi::formatInfo(info_.format).bytesPerTexel;
}

core::Result<> Texture::allocate()
{
    rhi::TextureDesc desc;
    desc.format = info_.format;
    desc.size = {info_.width, info_.height, 1};
    const bool depth = rhi::formatInfo(info_.format).isDepth();
    desc.mipLevels = info_.filter == FilterMode::kLinear && !info_.renderTarget && !info_.storage && !depth ? 0 : 1;
    desc.usage = rhi::TextureUsage::kSampled | rhi::TextureUsage::kCopySrc | rhi::TextureUsage::kCopyDst;
    if (info_.renderTarget)
    {
        desc.usage = desc.usage | rhi::TextureUsage::kRenderTarget;
    }
    if (info_.storage)
    {
        desc.usage = desc.usage | rhi::TextureUsage::kStorage;
    }
    desc.debugName = info_.debugName;
    auto texture = device_->createTexture(desc);
    if (!texture)
    {
        return std::unexpected(texture.error());
    }
    auto real = device_->textureDesc(*texture);
    if (!real)
    {
        device_->destroy(*texture);
        return std::unexpected(real.error());
    }

    rhi::SamplerDesc sd;
    const bool nearest = info_.filter == FilterMode::kNearest;
    sd.minFilter = nearest ? rhi::Filter::kNearest : rhi::Filter::kLinear;
    sd.magFilter = sd.minFilter;
    sd.mipFilter = (info_.filter == FilterMode::kLinear && real->mipLevels > 1) ? std::optional<rhi::Filter>(rhi::Filter::kLinear) : std::nullopt;
    sd.addressU = info_.address;
    sd.addressV = info_.address;
    sd.debugName = info_.debugName;
    auto sampler = device_->createSampler(sd);
    if (!sampler)
    {
        device_->destroy(*texture);
        return std::unexpected(sampler.error());
    }
    texture_ = *texture;
    sampler_ = *sampler;
    mips_ = real->mipLevels;
    return {};
}

core::Result<> Texture::buildMips()
{
    if (mips_ <= 1)
    {
        return {};
    }
    auto encoder = device_->createCommandEncoder();
    encoder->generateMips(texture_);
    return device_->submit(std::move(encoder));
}

core::Result<Texture> Texture::create(rhi::Device &device, const TextureInfo &info, std::span<const std::byte> pixels)
{
    if (info.width == 0 || info.height == 0)
    {
        return makeError(ErrorCode::kInvalidArgument, "Texture::create: empty size (" + info.debugName + ")");
    }
    Texture out;
    out.device_ = &device;
    out.info_ = info;
    if (auto r = out.allocate(); !r)
    {
        out.device_ = nullptr;
        return std::unexpected(r.error());
    }
    if (!pixels.empty())
    {
        if (auto r = out.upload(pixels); !r)
        {
            return std::unexpected(r.error());
        }
    }
    return out;
}

core::Result<Texture> Texture::fromFile(rhi::Device &device, const std::filesystem::path &file, FilterMode filter, rhi::AddressMode address)
{
    // Réglage global de stb jamais modifié (deux chargements simultanés se le disputeraient). Repère du RHI (A26) : la première
    // ligne envoyée est le HAUT de la texture, comme la première ligne d'un fichier image : aucun retournement.
    int w = 0, h = 0, channels = 0;
    std::unique_ptr<unsigned char, decltype(&stbi_image_free)> data(stbi_load(file.string().c_str(), &w, &h, &channels, 4), &stbi_image_free);
    if (!data)
    {
        return makeError(ErrorCode::kIo, "Texture::fromFile: cannot read " + file.generic_string() + ": " + stbi_failure_reason());
    }
    TextureInfo info;
    info.width = static_cast<std::uint32_t>(w);
    info.height = static_cast<std::uint32_t>(h);
    info.filter = filter;
    info.address = address;
    info.debugName = file.filename().generic_string();
    return create(device, info, std::as_bytes(std::span(data.get(), static_cast<std::size_t>(w) * h * 4)));
}

core::Result<> Texture::upload(std::span<const std::byte> pixels)
{
    if (!valid())
    {
        return makeError(ErrorCode::kInvalidArgument, "Texture::upload: empty texture");
    }
    if (pixels.size() != byteSize())
    {
        return makeError(ErrorCode::kInvalidArgument, "Texture::upload: expected " + std::to_string(byteSize()) + " bytes, got " +
                                                          std::to_string(pixels.size()) + " (" + info_.debugName + ")");
    }
    if (auto r = device_->updateTexture(texture_, {}, pixels); !r)
    {
        return r;
    }
    return buildMips();
}

core::Result<> Texture::resize(std::uint32_t width, std::uint32_t height)
{
    if (!valid())
    {
        return makeError(ErrorCode::kInvalidArgument, "Texture::resize: empty texture");
    }
    if (width == 0 || height == 0)
    {
        return makeError(ErrorCode::kInvalidArgument, "Texture::resize: empty size");
    }
    if (width == info_.width && height == info_.height)
    {
        return {};
    }
    const rhi::TextureHandle oldTexture = texture_;
    const rhi::SamplerHandle oldSampler = sampler_;
    const TextureInfo oldInfo = info_;
    info_.width = width;
    info_.height = height;
    if (auto r = allocate(); !r)
    {
        info_ = oldInfo;
        texture_ = oldTexture;
        sampler_ = oldSampler;
        return r;
    }
    device_->destroy(oldTexture);
    device_->destroy(oldSampler);
    return {};
}

core::Result<std::vector<std::byte>> Texture::readback() const
{
    if (!valid())
    {
        return makeError(ErrorCode::kInvalidArgument, "Texture::readback: empty texture");
    }
    return device_->readTexture(texture_);
}

void Texture::reset() noexcept
{
    if (device_ != nullptr)
    {
        device_->destroy(texture_);
        device_->destroy(sampler_);
    }
    device_ = nullptr;
    texture_ = {};
    sampler_ = {};
    info_ = {};
    mips_ = 1;
}

} // namespace engine::renderer
