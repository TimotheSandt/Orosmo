#include "orosmo/renderer/blend_mode.hpp"

#include <string>

namespace orosmo::renderer
{

BlendModeState blendModeState(BlendMode mode) noexcept
{
    using rhi::BlendFactor;
    BlendModeState s;
    const auto blend = [&](BlendFactor src, BlendFactor dst)
    {
        s.blend = {.enabled = true, .srcColor = src, .dstColor = dst, .srcAlpha = BlendFactor::kOne, .dstAlpha = BlendFactor::kOneMinusSrcAlpha};
    };
    switch (mode)
    {
    case BlendMode::kOpaque:
        break;
    case BlendMode::kMask:
        s.discardInShader = true;
        break;
    case BlendMode::kMaskA2C:
        s.alphaToCoverage = true;
        break;
    case BlendMode::kDithered:
        s.discardInShader = true;
        break;
    case BlendMode::kBlend:
        blend(BlendFactor::kSrcAlpha, BlendFactor::kOneMinusSrcAlpha);
        s.depthWrite = false;
        s.needsSorting = true;
        break;
    case BlendMode::kAdditive:
        blend(BlendFactor::kSrcAlpha, BlendFactor::kOne);
        s.blend.dstAlpha = BlendFactor::kOne;
        s.depthWrite = false;
        break;
    case BlendMode::kPremultiplied:
        blend(BlendFactor::kOne, BlendFactor::kOneMinusSrcAlpha);
        s.depthWrite = false;
        s.needsSorting = true; // « over » prémultiplié ne commute pas : arrière vers avant
        break;
    case BlendMode::kTransmission:
        blend(BlendFactor::kSrcAlpha, BlendFactor::kOneMinusSrcAlpha);
        s.depthWrite = false;
        s.needsSorting = true;
        s.readsSceneColor = true;
        break;
    }
    return s;
}

bool isTransparent(BlendMode mode) noexcept { return !blendModeState(mode).depthWrite; }

std::string_view blendModeName(BlendMode mode) noexcept
{
    switch (mode)
    {
    case BlendMode::kOpaque: return "opaque";
    case BlendMode::kMask: return "mask";
    case BlendMode::kMaskA2C: return "mask_a2c";
    case BlendMode::kDithered: return "dithered";
    case BlendMode::kBlend: return "blend";
    case BlendMode::kAdditive: return "additive";
    case BlendMode::kPremultiplied: return "premultiplied";
    case BlendMode::kTransmission: return "transmission";
    }
    return "opaque";
}

core::Result<BlendMode> blendModeFromName(std::string_view name)
{
    for (const BlendMode mode : kAllBlendModes)
    {
        if (blendModeName(mode) == name)
        {
            return mode;
        }
    }
    return core::makeError(core::ErrorCode::kInvalidArgument, "unknown blend mode: " + std::string(name));
}

core::Result<BlendMode> blendModeFromGltf(std::string_view alphaMode)
{
    if (alphaMode == "OPAQUE")
    {
        return BlendMode::kOpaque;
    }
    if (alphaMode == "MASK")
    {
        return BlendMode::kMask;
    }
    if (alphaMode == "BLEND")
    {
        return BlendMode::kBlend;
    }
    return core::makeError(core::ErrorCode::kInvalidArgument, "unknown glTF alphaMode: " + std::string(alphaMode));
}

BlendModeState applyBlendMode(rhi::GraphicsPipelineDesc &desc, BlendMode mode)
{
    const BlendModeState s = blendModeState(mode);
    for (rhi::ColorTargetState &target : desc.colorTargets)
    {
        target.blend = s.blend;
    }
    desc.depth.write = desc.depth.test && s.depthWrite;
    return s;
}

} // namespace orosmo::renderer
