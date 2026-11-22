#include "orosmo/renderer/aov.hpp"

#include <array>
#include <bit>

namespace orosmo::renderer
{

namespace
{
constexpr std::array<AovInfo, kAovCount> kInfos{{
    {"motion", "OROSMO_AOV_MOTION", rhi::Format::kRG16Float, AovResolve::kAverage, {0, 0, 0, 0}},
    {"normals", "OROSMO_AOV_NORMALS", rhi::Format::kRG16Float, AovResolve::kAverageRenormalized, {-1, -1, 0, 0}},
    {"material", "OROSMO_AOV_MATERIAL", rhi::Format::kRG8Unorm, AovResolve::kAverage, {0, 0, 0, 0}},
    {"object-id", "OROSMO_AOV_OBJECT_ID", rhi::Format::kR32Uint, AovResolve::kNearestNoBlend, {0, 0, 0, 0}},
    {"linear-depth", "OROSMO_AOV_LINEAR_DEPTH", rhi::Format::kR32Float, AovResolve::kNearestSample, {0, 0, 0, 0}},
    {"emission", "OROSMO_AOV_EMISSION", rhi::Format::kRGBA16Float, AovResolve::kAverage, {0, 0, 0, 0}},
    {"albedo", "OROSMO_AOV_ALBEDO", rhi::Format::kRGBA8Unorm, AovResolve::kAverage, {0, 0, 0, 0}},
}};
} // namespace

const AovInfo &aovInfo(Aov aov) noexcept { return kInfos[static_cast<std::size_t>(aov)]; }
AovResolve aovResolveRule(Aov aov) noexcept { return aovInfo(aov).resolve; }

std::optional<Aov> aovFromName(std::string_view name) noexcept
{
    for (std::size_t i = 0; i < kAovCount; ++i)
    {
        if (kInfos[i].name == name)
        {
            return static_cast<Aov>(i);
        }
    }
    return std::nullopt;
}

std::uint32_t AovSet::count() const noexcept { return static_cast<std::uint32_t>(std::popcount(bits_)); }

std::vector<Aov> AovSet::list() const
{
    std::vector<Aov> out;
    for (std::size_t i = 0; i < kAovCount; ++i)
    {
        if (has(static_cast<Aov>(i)))
        {
            out.push_back(static_cast<Aov>(i));
        }
    }
    return out;
}

AovLayout::AovLayout(AovSet set) : set_(set) {}

std::optional<std::uint32_t> AovLayout::index(Aov aov) const noexcept
{
    if (!set_.has(aov))
    {
        return std::nullopt;
    }
    std::uint32_t rank = 1;
    for (std::size_t i = 0; i < static_cast<std::size_t>(aov); ++i)
    {
        if (set_.has(static_cast<Aov>(i)))
        {
            ++rank;
        }
    }
    return rank;
}

std::vector<rhi::Format> AovLayout::colorFormats(rhi::Format sceneColor) const
{
    std::vector<rhi::Format> formats{sceneColor};
    for (const Aov aov : set_.list())
    {
        formats.push_back(aovInfo(aov).format);
    }
    return formats;
}

std::vector<std::pair<std::string, std::string>> aovShaderDefines(const AovSet &set)
{
    std::vector<std::pair<std::string, std::string>> defines;
    const AovLayout layout(set);
    for (const Aov aov : set.list())
    {
        defines.emplace_back(std::string(aovInfo(aov).define), std::to_string(*layout.index(aov)));
    }
    return defines;
}

AovSet aovWrites(const AovMaterial &material, const AovSet &active) noexcept
{
    return isTransparent(material.blend) ? (material.request & active) : active;
}

void aovColorTargets(std::vector<rhi::ColorTargetState> &targets, const AovLayout &layout, rhi::Format sceneColor, const AovSet &writes,
                     std::uint32_t programColorOutputs)
{
    const std::vector<rhi::Format> formats = layout.colorFormats(sceneColor);
    const std::vector<Aov> aovs = layout.set().list();
    targets.resize(formats.size());
    for (std::size_t i = 0; i < formats.size(); ++i)
    {
        targets[i].format = formats[i];
        if (i == 0)
        {
            targets[i].writeMask = rhi::ColorWriteMask::kAll;
            continue;
        }
        const bool written = writes.has(aovs[i - 1]) && i < programColorOutputs;
        targets[i].writeMask = written ? rhi::ColorWriteMask::kAll : rhi::ColorWriteMask::kNone;
    }
}

} // namespace orosmo::renderer
