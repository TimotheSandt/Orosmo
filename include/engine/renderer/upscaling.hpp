// Points d'extension de fin d'image : mises à l'échelle (upscale), post-traitements, génération d'images. Mêmes concepts et mêmes
// champs que l'ancien dossier `Graphics/Upscaling/`, exprimés avec des handles du RHI et des commandes explicites.
#pragma once

#include "engine/core/error.hpp"
#include "engine/renderer/fullscreen_blit.hpp"
#include "engine/renderer/render_target.hpp"
#include "engine/rhi/device.hpp"

#include <glm/vec2.hpp>

#include <cstdint>
#include <string>

namespace engine::renderer
{

enum class UpscaleQualityMode : std::uint8_t
{
    kNative,
    kQuality,
    kBalanced,
    kPerformance,
    kUltraPerformance,
};

enum class UpscaleAlphaMode : std::uint8_t
{
    kOpaque,
    kStraight,
    kPremultiplied,
};

enum class FrameGenerationQualityMode : std::uint8_t
{
    kOff,
    kOn,
};

struct UpscaleRequirements
{
    bool needsColor = true;
    bool needsDepth = false;
    bool needsMotionVectors = false;
    bool needsExposure = false;
    bool needsReactiveMask = false;
    bool needsTransparencyMask = false;
    bool needsJitter = false;
    bool needsHistory = false;
};

struct UpscaleInput
{
    rhi::TextureHandle color;
    rhi::TextureHandle depth;
    rhi::TextureHandle motionVectors;
    rhi::TextureHandle exposure;
    rhi::TextureHandle reactiveMask;
    rhi::TextureHandle transparencyMask;
    rhi::TextureHandle historyColor;
    glm::vec2 renderResolution{0.0f};
    glm::vec2 outputResolution{0.0f};
    glm::vec2 jitter{0.0f};
    float sharpness = 0.0f;
    float deltaTimeSeconds = 0.0f;
    UpscaleAlphaMode alphaMode = UpscaleAlphaMode::kOpaque;
    bool resetHistory = false;
};

struct UpscaleOutput
{
    rhi::TextureHandle output;
    rhi::TextureHandle newHistoryColor;
};

struct FrameGenerationInput
{
    rhi::TextureHandle currentColor;
    rhi::TextureHandle previousColor; // couleur de l'image précédente à la résolution de rendu
    rhi::TextureHandle depth;         // exige depthAsTexture
    rhi::TextureHandle motionVectors; // exige la cible de vecteurs de mouvement
    glm::vec2 renderResolution{0.0f};
    glm::vec2 outputResolution{0.0f};
    glm::vec2 jitter{0.0f};
    float deltaTimeSeconds = 0.0f;
    bool resetHistory = false; // vrai aux coupures de scène et aux téléportations de caméra
};

struct FrameGenerationOutput
{
    rhi::TextureHandle generatedFrame;
};

struct UpscaleModeDesc
{
    std::string name;
    UpscaleQualityMode quality = UpscaleQualityMode::kNative;
    UpscaleRequirements requirements{};
    bool supportsReactiveMask = false;
    bool supportsTransparencyMask = false;
    bool supportsAlphaComposition = false;
    bool supportsJitter = false;
};

struct FrameGenerationModeDesc
{
    std::string name;
    FrameGenerationQualityMode quality = FrameGenerationQualityMode::kOff;
    bool needsDepth = false;
    bool needsMotionVectors = true;
    bool needsHistory = true;
};

// Ce que reçoit chaque extension pendant `SceneRenderer::endFrame` : le périphérique, l'encodeur de l'image (aucune passe ouverte)
// et l'outil de copie plein écran.
struct FrameContext
{
    rhi::Device &device;
    rhi::CommandEncoder &encoder;
    FullscreenBlit &blit;
};

struct UpscaleTarget
{
    rhi::TextureHandle texture;
    rhi::Format format = rhi::Format::kRGBA8Unorm;
    rhi::Extent2D size{};
};

class IUpscaleMode
{
  public:
    virtual ~IUpscaleMode() = default;
    virtual std::string name() const noexcept = 0;
    virtual bool supports(const rhi::Capabilities &capabilities) const noexcept
    {
        (void)capabilities;
        return true;
    }
    virtual core::Result<> upscale(FrameContext &context, const RenderTarget &source, const UpscaleTarget &destination) = 0;
    virtual UpscaleRequirements requirements() const noexcept { return {}; }
};

class IAdvancedUpscaleMode : public IUpscaleMode
{
  public:
    virtual const UpscaleModeDesc &description() const noexcept = 0;
    virtual bool isSupported(const rhi::Capabilities &capabilities) const noexcept = 0;
    virtual bool initialize(rhi::Device &device) = 0;
    virtual void shutdown() = 0;
    virtual core::Result<> execute(FrameContext &context, const UpscaleInput &input, UpscaleOutput &output) = 0;

    UpscaleRequirements requirements() const noexcept override { return description().requirements; }
    bool supports(const rhi::Capabilities &capabilities) const noexcept override { return isSupported(capabilities); }
    core::Result<> upscale(FrameContext &context, const RenderTarget &source, const UpscaleTarget &destination) override
    {
        UpscaleInput input;
        input.color = source.color(0);
        input.renderResolution = {static_cast<float>(source.width()), static_cast<float>(source.height())};
        input.outputResolution = {static_cast<float>(destination.size.width), static_cast<float>(destination.size.height)};
        UpscaleOutput output;
        output.output = destination.texture;
        return execute(context, input, output);
    }
};

class IPostProcessPass
{
  public:
    virtual ~IPostProcessPass() = default;
    virtual std::string name() const noexcept = 0;
    virtual core::Result<> process(FrameContext &context, RenderTarget &target) = 0;
};

class IFrameGenerationMode
{
  public:
    virtual ~IFrameGenerationMode() = default;
    virtual const FrameGenerationModeDesc &description() const noexcept = 0;
    virtual bool isSupported(const rhi::Capabilities &capabilities) const noexcept = 0;
    virtual bool initialize(rhi::Device &device) = 0;
    virtual void shutdown() = 0;
    virtual core::Result<> generateFrame(FrameContext &context, const FrameGenerationInput &input, FrameGenerationOutput &output) = 0;
};

// Mode par défaut : copie la cible de rendu sur la destination avec un filtre bilinéaire (ancien « bilinear-blit »).
class BilinearBlitUpscaleMode final : public IUpscaleMode
{
  public:
    std::string name() const noexcept override { return "bilinear-blit"; }
    core::Result<> upscale(FrameContext &context, const RenderTarget &source, const UpscaleTarget &destination) override
    {
        return context.blit.copy(context.encoder, source.color(0), context.blit.linearSampler(), destination.texture, destination.format, "upscale");
    }
};

} // namespace engine::renderer
