#include "orosmo/renderer/transparent_shadow.hpp"

#include <array>

namespace orosmo::renderer
{

namespace
{
// Même matrice que mesh.frag (BlendMode::kDithered).
constexpr std::array<float, 16> kBayer4 = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
} // namespace

float bayerThreshold4(std::uint32_t x, std::uint32_t y) noexcept
{
    return (kBayer4[(y & 3u) * 4u + (x & 3u)] + 0.5f) / 16.0f;
}

bool transparentShadowKeeps(const TransparentShadow &shadow, std::uint32_t x, std::uint32_t y) noexcept
{
    switch (shadow.mode)
    {
    case TransparentShadowMode::kNone:
        return false;
    case TransparentShadowMode::kFull:
        return true;
    case TransparentShadowMode::kMasked:
        return !(shadow.alpha < shadow.cutoff);
    case TransparentShadowMode::kDithered:
        return !(bayerThreshold4(x, y) > shadow.alpha);
    }
    return true;
}

float transparentShadowCoverage(const TransparentShadow &shadow) noexcept
{
    int kept = 0;
    for (std::uint32_t y = 0; y < 4; ++y)
    {
        for (std::uint32_t x = 0; x < 4; ++x)
        {
            kept += transparentShadowKeeps(shadow, x, y) ? 1 : 0;
        }
    }
    return static_cast<float>(kept) / 16.0f;
}

TransparentShadowMode defaultTransparentShadowMode(BlendMode mode) noexcept
{
    switch (mode)
    {
    case BlendMode::kOpaque:
        return TransparentShadowMode::kFull;
    case BlendMode::kMask:
    case BlendMode::kMaskA2C:
        return TransparentShadowMode::kMasked;
    case BlendMode::kDithered:
    case BlendMode::kBlend:
    case BlendMode::kPremultiplied:
    case BlendMode::kTransmission:
        return TransparentShadowMode::kDithered;
    case BlendMode::kAdditive:
        return TransparentShadowMode::kNone;
    }
    return TransparentShadowMode::kFull;
}

std::string_view transparentShadowModeName(TransparentShadowMode mode) noexcept
{
    switch (mode)
    {
    case TransparentShadowMode::kNone:
        return "none";
    case TransparentShadowMode::kFull:
        return "full";
    case TransparentShadowMode::kMasked:
        return "masked";
    case TransparentShadowMode::kDithered:
        return "dithered";
    }
    return "full";
}

core::Result<TransparentShadowMode> transparentShadowModeFromName(std::string_view name)
{
    for (const TransparentShadowMode m : {TransparentShadowMode::kNone, TransparentShadowMode::kFull, TransparentShadowMode::kMasked,
                                          TransparentShadowMode::kDithered})
    {
        if (transparentShadowModeName(m) == name)
        {
            return m;
        }
    }
    return core::makeError(core::ErrorCode::kInvalidArgument, "unknown transparent shadow mode (expected none, full, masked or dithered)");
}

} // namespace orosmo::renderer
