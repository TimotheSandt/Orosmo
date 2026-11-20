// Cible de rendu : une ou plusieurs textures couleur (MRT) et une profondeur, au-dessus du RHI. Pas d'état global : la cible ne
// se « lie » pas ; `passDesc` fabrique la description de passe à donner à `CommandEncoder::beginRenderPass`.
#pragma once

#include "orosmo/core/error.hpp"
#include "orosmo/rhi/device.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace orosmo::renderer
{

struct RenderTargetInfo
{
    rhi::Extent2D size{1, 1};
    std::vector<rhi::Format> colorFormats{rhi::Format::kRGBA8Unorm}; // au moins un ; jusqu'à Capabilities::maxColorAttachments
    bool depth = true;
    bool depthSampled = false; // la profondeur peut être lue dans un shader (post-traitement)
    std::string debugName;
};

struct PassOptions
{
    rhi::LoadOp colorLoad = rhi::LoadOp::kClear;
    std::array<float, 4> clearColor{0.0f, 0.0f, 0.0f, 1.0f};
    rhi::LoadOp depthLoad = rhi::LoadOp::kClear;
    float clearDepth = 1.0f;
};

class RenderTarget
{
  public:
    RenderTarget() = default;
    ~RenderTarget() { reset(); }
    RenderTarget(const RenderTarget &) = delete;
    RenderTarget &operator=(const RenderTarget &) = delete;
    RenderTarget(RenderTarget &&other) noexcept { swap(other); }
    RenderTarget &operator=(RenderTarget &&other) noexcept
    {
        if (this != &other)
        {
            reset();
            swap(other);
        }
        return *this;
    }

    static core::Result<RenderTarget> create(rhi::Device &device, const RenderTargetInfo &info);

    // Nouvelles dimensions, mêmes formats : les attachements sont recréés (contenu perdu, handles changés). Sans effet si la
    // taille est identique.
    core::Result<> resize(std::uint32_t width, std::uint32_t height);
    // Autres formats ou profondeur : recrée tout.
    core::Result<> reconfigure(const RenderTargetInfo &info);

    rhi::RenderPassDesc passDesc(const PassOptions &options = {}) const;

    // Lit un texel (x, y depuis le coin HAUT gauche, comme l'ancienne API) d'un attachement R32Uint ou RGBA8.
    core::Result<std::uint32_t> readPixelUInt(std::uint32_t attachment, std::uint32_t x, std::uint32_t y) const;
    core::Result<std::array<std::uint8_t, 4>> readPixelRgba8(std::uint32_t attachment, std::uint32_t x, std::uint32_t y) const;

    void reset() noexcept;
    bool valid() const noexcept { return device_ != nullptr && !colors_.empty(); }
    rhi::TextureHandle color(std::uint32_t index = 0) const { return colors_.at(index); }
    rhi::TextureHandle depth() const noexcept { return depth_; }
    // Échantillonneur linéaire, bords étirés : celui qu'on lie avec `color(i)` pour lire la cible dans un shader.
    rhi::SamplerHandle sampler() const noexcept { return sampler_; }
    std::uint32_t colorCount() const noexcept { return static_cast<std::uint32_t>(colors_.size()); }
    std::uint32_t width() const noexcept { return info_.size.width; }
    std::uint32_t height() const noexcept { return info_.size.height; }
    const RenderTargetInfo &info() const noexcept { return info_; }

  private:
    core::Result<> allocate();
    void release() noexcept;
    void swap(RenderTarget &other) noexcept
    {
        std::swap(device_, other.device_);
        std::swap(colors_, other.colors_);
        std::swap(depth_, other.depth_);
        std::swap(sampler_, other.sampler_);
        std::swap(info_, other.info_);
    }

    rhi::Device *device_ = nullptr;
    std::vector<rhi::TextureHandle> colors_;
    rhi::TextureHandle depth_;
    rhi::SamplerHandle sampler_;
    RenderTargetInfo info_;
};

} // namespace orosmo::renderer
