#include "engine/renderer/render_target.hpp"

#include <cstring>

namespace engine::renderer
{

using core::ErrorCode;
using core::makeError;

core::Result<> RenderTarget::allocate()
{
    if (info_.colorFormats.empty() || info_.size.width == 0 || info_.size.height == 0)
    {
        return makeError(ErrorCode::kInvalidArgument, "RenderTarget: needs a color format and a non-empty size (" + info_.debugName + ")");
    }
    // Un format de profondeur (ou indéfini) n'est pas un attachement couleur : refusé ici, avant toute allocation, plutôt que par
    // la passe plus tard (reconfigure garde alors la cible précédente intacte).
    for (const rhi::Format format : info_.colorFormats)
    {
        if (format == rhi::Format::kUndefined || rhi::formatInfo(format).isDepth())
        {
            return makeError(ErrorCode::kInvalidArgument, std::string("RenderTarget: ") + rhi::toString(format) +
                                                              " is not a color format (" + info_.debugName + ")");
        }
    }
    rhi::SamplerDesc sd;
    sd.mipFilter = std::nullopt;
    sd.addressU = rhi::AddressMode::kClampToEdge;
    sd.addressV = rhi::AddressMode::kClampToEdge;
    sd.debugName = info_.debugName + ".sampler";
    auto sampler = device_->createSampler(sd);
    if (!sampler)
    {
        return std::unexpected(sampler.error());
    }
    sampler_ = *sampler;

    auto make = [&](rhi::Format format, bool sampled, const std::string &suffix) -> core::Result<rhi::TextureHandle>
    {
        rhi::TextureDesc d;
        d.format = format;
        d.size = {info_.size.width, info_.size.height, 1};
        d.usage = rhi::TextureUsage::kRenderTarget | rhi::TextureUsage::kCopySrc;
        if (sampled)
        {
            d.usage = d.usage | rhi::TextureUsage::kSampled;
        }
        d.debugName = info_.debugName + suffix;
        return device_->createTexture(d);
    };
    for (std::size_t i = 0; i < info_.colorFormats.size(); ++i)
    {
        auto color = make(info_.colorFormats[i], true, ".color" + std::to_string(i));
        if (!color)
        {
            release();
            return std::unexpected(color.error());
        }
        colors_.push_back(*color);
    }
    if (info_.depth)
    {
        auto depth = make(rhi::Format::kD32Float, info_.depthSampled, ".depth");
        if (!depth)
        {
            release();
            return std::unexpected(depth.error());
        }
        depth_ = *depth;
    }
    return {};
}

void RenderTarget::release() noexcept
{
    if (device_ != nullptr)
    {
        for (const rhi::TextureHandle t : colors_)
        {
            device_->destroy(t);
        }
        device_->destroy(depth_);
        device_->destroy(sampler_);
    }
    colors_.clear();
    depth_ = {};
    sampler_ = {};
}

core::Result<RenderTarget> RenderTarget::create(rhi::Device &device, const RenderTargetInfo &info)
{
    RenderTarget out;
    out.device_ = &device;
    out.info_ = info;
    if (info.colorFormats.size() > device.capabilities().maxColorAttachments)
    {
        out.device_ = nullptr;
        return makeError(ErrorCode::kOutOfRange, "RenderTarget: more color attachments than the device allows (" + info.debugName + ")");
    }
    if (auto r = out.allocate(); !r)
    {
        out.device_ = nullptr;
        return std::unexpected(r.error());
    }
    return out;
}

core::Result<> RenderTarget::resize(std::uint32_t width, std::uint32_t height)
{
    if (!valid())
    {
        return makeError(ErrorCode::kInvalidArgument, "RenderTarget::resize: empty target");
    }
    if (width == info_.size.width && height == info_.size.height)
    {
        return {};
    }
    RenderTargetInfo next = info_;
    next.size = {width, height};
    return reconfigure(next);
}

core::Result<> RenderTarget::reconfigure(const RenderTargetInfo &info)
{
    if (device_ == nullptr)
    {
        return makeError(ErrorCode::kInvalidArgument, "RenderTarget::reconfigure: empty target");
    }
    if (info.colorFormats.size() > device_->capabilities().maxColorAttachments)
    {
        return makeError(ErrorCode::kOutOfRange, "RenderTarget::reconfigure: too many color attachments");
    }
    // Les nouveaux attachements sont créés avant de libérer les anciens : un échec laisse la cible utilisable.
    RenderTarget fresh;
    fresh.device_ = device_;
    fresh.info_ = info;
    if (auto r = fresh.allocate(); !r)
    {
        fresh.device_ = nullptr;
        return r;
    }
    release();
    swap(fresh);
    return {};
}

rhi::RenderPassDesc RenderTarget::passDesc(const PassOptions &options) const
{
    rhi::RenderPassDesc pass;
    for (const rhi::TextureHandle c : colors_)
    {
        rhi::ColorAttachment a;
        a.texture = c;
        a.load = options.colorLoad;
        a.clearColor = options.clearColor;
        pass.colors.push_back(a);
    }
    if (!depth_.isNull())
    {
        rhi::DepthAttachment d;
        d.texture = depth_;
        d.load = options.depthLoad;
        d.clearDepth = options.clearDepth;
        pass.depth = d;
    }
    pass.debugName = info_.debugName;
    return pass;
}

namespace
{
core::Result<std::vector<std::byte>> readTexel(rhi::Device &device, rhi::TextureHandle texture, std::uint32_t x, std::uint32_t yTop, std::uint32_t height)
{
    rhi::TextureCopy region;
    (void)height; // repère du RHI (A26) : la ligne 0 est le haut, comme ici
    region.offset = {static_cast<std::int32_t>(x), static_cast<std::int32_t>(yTop), 0};
    region.size = {1, 1, 1};
    return device.readTexture(texture, region);
}
} // namespace

core::Result<std::uint32_t> RenderTarget::readPixelUInt(std::uint32_t attachment, std::uint32_t x, std::uint32_t y) const
{
    if (!valid() || attachment >= colors_.size() || x >= width() || y >= height())
    {
        return makeError(ErrorCode::kOutOfRange, "RenderTarget::readPixelUInt: attachment or position out of range");
    }
    if (info_.colorFormats[attachment] != rhi::Format::kR32Uint)
    {
        return makeError(ErrorCode::kInvalidArgument, "RenderTarget::readPixelUInt: the attachment is not R32Uint");
    }
    auto bytes = readTexel(*device_, colors_[attachment], x, y, height());
    if (!bytes)
    {
        return std::unexpected(bytes.error());
    }
    std::uint32_t value = 0;
    std::memcpy(&value, bytes->data(), sizeof value);
    return value;
}

core::Result<std::array<std::uint8_t, 4>> RenderTarget::readPixelRgba8(std::uint32_t attachment, std::uint32_t x, std::uint32_t y) const
{
    if (!valid() || attachment >= colors_.size() || x >= width() || y >= height())
    {
        return makeError(ErrorCode::kOutOfRange, "RenderTarget::readPixelRgba8: attachment or position out of range");
    }
    if (info_.colorFormats[attachment] != rhi::Format::kRGBA8Unorm && info_.colorFormats[attachment] != rhi::Format::kRGBA8Srgb)
    {
        return makeError(ErrorCode::kInvalidArgument, "RenderTarget::readPixelRgba8: the attachment is not RGBA8");
    }
    auto bytes = readTexel(*device_, colors_[attachment], x, y, height());
    if (!bytes)
    {
        return std::unexpected(bytes.error());
    }
    std::array<std::uint8_t, 4> out{};
    std::memcpy(out.data(), bytes->data(), 4);
    return out;
}

void RenderTarget::reset() noexcept
{
    release();
    device_ = nullptr;
    info_ = {};
}

} // namespace engine::renderer
