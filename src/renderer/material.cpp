#include "orosmo/renderer/material.hpp"

#include "orosmo/renderer/shadow_feature.hpp"

#include <cstddef>
#include <cstring>

namespace orosmo::renderer
{

using core::ErrorCode;
using core::makeError;

namespace
{

// Bloc MaterialBlock de shaders/rhi/mesh_pbr.frag (std140).
struct ParamsBlock
{
    glm::vec4 baseColorFactor;
    glm::vec4 emissive;
    glm::vec4 params; // métal, rugosité, échelle de normale, force d'occlusion
    glm::vec4 alpha;  // x : seuil
    glm::uvec4 flags; // x : combinaison de variante
};
static_assert(offsetof(ParamsBlock, emissive) == 16 && offsetof(ParamsBlock, params) == 32);
static_assert(offsetof(ParamsBlock, alpha) == 48 && offsetof(ParamsBlock, flags) == 64);
static_assert(sizeof(ParamsBlock) == PbrMaterial::kParamsBlockSize);

ParamsBlock packParams(const PbrParams &p, std::uint32_t features)
{
    ParamsBlock block{};
    block.baseColorFactor = p.baseColorFactor;
    block.emissive = glm::vec4(p.emissive, 0.0f);
    block.params = glm::vec4(p.metallic, p.roughness, p.normalScale, p.occlusionStrength);
    block.alpha = glm::vec4(p.alphaCutoff, 0.0f, 0.0f, 0.0f);
    block.flags = glm::uvec4(features, 0u, 0u, 0u);
    return block;
}

core::Result<Texture> solid(rhi::Device &device, std::uint8_t r, std::uint8_t g, std::uint8_t b, const char *name)
{
    TextureInfo info;
    info.width = 1;
    info.height = 1;
    info.filter = FilterMode::kNearest;
    info.debugName = name;
    const std::uint8_t px[4] = {r, g, b, 255};
    return Texture::create(device, info, std::as_bytes(std::span(px)));
}

} // namespace

std::vector<Define> pbrDefines(std::uint32_t features)
{
    static constexpr std::pair<std::uint32_t, const char *> kNames[] = {
        {kPbrBaseColorMap, "OROSMO_PBR_BASE_COLOR_MAP"}, {kPbrMetallicRoughnessMap, "OROSMO_PBR_METALLIC_ROUGHNESS_MAP"},
        {kPbrNormalMap, "OROSMO_PBR_NORMAL_MAP"},        {kPbrOcclusionMap, "OROSMO_PBR_OCCLUSION_MAP"},
        {kPbrEmissiveMap, "OROSMO_PBR_EMISSIVE_MAP"},    {kPbrAlphaMask, "OROSMO_PBR_ALPHA_MASK"},
        {kPbrAlphaBlend, "OROSMO_PBR_ALPHA_BLEND"},      {kPbrDoubleSided, "OROSMO_PBR_DOUBLE_SIDED"},
        {kPbrUnlit, "OROSMO_PBR_UNLIT"},                 {kPbrShadows, "OROSMO_SHADOWS"},
    };
    std::vector<Define> out;
    for (const auto &[bit, name] : kNames)
    {
        if ((features & bit) != 0)
        {
            out.emplace_back(name, "1");
        }
    }
    return out;
}

std::uint32_t PbrMaterialDesc::features() const noexcept
{
    std::uint32_t f = 0;
    static constexpr std::uint32_t kMapBits[kPbrMapCount] = {kPbrBaseColorMap, kPbrMetallicRoughnessMap, kPbrNormalMap, kPbrOcclusionMap,
                                                             kPbrEmissiveMap};
    for (std::size_t i = 0; i < kPbrMapCount; ++i)
    {
        if (maps[i] && maps[i]->valid())
        {
            f |= kMapBits[i];
        }
    }
    if (blend == BlendMode::kMask || blend == BlendMode::kMaskA2C)
    {
        f |= kPbrAlphaMask;
    }
    else if (blend != BlendMode::kOpaque)
    {
        f |= kPbrAlphaBlend;
    }
    if (doubleSided)
    {
        f |= kPbrDoubleSided;
    }
    if (unlit)
    {
        f |= kPbrUnlit;
    }
    return f;
}

core::Result<std::shared_ptr<PbrDefaults>> PbrDefaults::create(rhi::Device &device)
{
    auto white = solid(device, 255, 255, 255, "pbr.white");
    auto normal = solid(device, 128, 128, 255, "pbr.flatNormal");
    if (!white || !normal)
    {
        return std::unexpected(!white ? white.error() : normal.error());
    }
    auto out = std::shared_ptr<PbrDefaults>(new PbrDefaults());
    out->white_ = std::move(*white);
    out->flatNormal_ = std::move(*normal);
    return out;
}

PbrMaterial::~PbrMaterial()
{
    if (device_ != nullptr)
    {
        device_->destroy(group_);
    }
}

core::Result<std::shared_ptr<PbrMaterial>> PbrMaterial::create(rhi::Device &device, PbrMaterialDesc desc, std::shared_ptr<const PbrDefaults> defaults,
                                                              const ShadowFeature *shadows)
{
    if (!defaults)
    {
        return makeError(ErrorCode::kInvalidArgument, "PbrMaterial::create: no default textures (" + desc.name + ")");
    }
    auto material = std::shared_ptr<PbrMaterial>(new PbrMaterial());
    material->device_ = &device;
    material->desc_ = std::move(desc);
    material->defaults_ = std::move(defaults);
    material->features_ = material->desc_.features();
    const ParamsBlock block = packParams(material->desc_.params, material->features_);
    rhi::BufferDesc bd;
    bd.size = sizeof(ParamsBlock);
    bd.usage = rhi::BufferUsage::kUniform | rhi::BufferUsage::kCopyDst;
    bd.debugName = material->desc_.name + ".params";
    auto buffer = GpuBuffer::create(device, bd, std::as_bytes(std::span(&block, 1)));
    if (!buffer)
    {
        return std::unexpected(buffer.error());
    }
    material->params_ = std::move(*buffer);
    if (auto r = material->buildGroup(shadows); !r)
    {
        return std::unexpected(r.error());
    }
    return material;
}

core::Result<> PbrMaterial::buildGroup(const ShadowFeature *shadows)
{
    rhi::BindGroupDesc desc;
    desc.debugName = desc_.name;
    for (std::size_t i = 0; i < kPbrMapCount; ++i)
    {
        const Texture *texture = desc_.maps[i] && desc_.maps[i]->valid() ? desc_.maps[i].get()
                                 : i == static_cast<std::size_t>(PbrMap::kNormal) ? &defaults_->flatNormal()
                                                                                  : &defaults_->white();
        desc.entries.push_back({.binding = static_cast<std::uint32_t>(i), .type = rhi::BindingType::kSampledTexture, .texture = texture->handle(),
                                .sampler = texture->sampler()});
    }
    desc.entries.push_back({.binding = kPbrParamsBinding, .type = rhi::BindingType::kUniformBuffer, .buffer = params_.handle(), .size = sizeof(ParamsBlock)});
    if (shadows != nullptr)
    {
        shadows->appendReceiverBindings(desc);
    }
    auto group = device_->createBindGroup(desc);
    if (!group)
    {
        return std::unexpected(group.error());
    }
    device_->destroy(group_);
    group_ = *group;
    shadows_ = shadows;
    features_ = (features_ & ~kPbrShadows) | (shadows != nullptr ? kPbrShadows : 0u);
    return {};
}

core::Result<> PbrMaterial::rebuild(const ShadowFeature *shadows) { return buildGroup(shadows); }

core::Result<> PbrMaterial::setParams(const PbrParams &params)
{
    const ParamsBlock block = packParams(params, features_);
    if (auto r = params_.update(0, std::as_bytes(std::span(&block, 1))); !r)
    {
        return r;
    }
    desc_.params = params;
    return {};
}

core::Result<std::shared_ptr<PbrMaterial>> PbrMaterial::instantiate(const PbrParams &params, std::string name) const
{
    PbrMaterialDesc desc = desc_;
    desc.params = params;
    desc.name = name.empty() ? desc_.name + ".instance" : std::move(name);
    return create(*device_, std::move(desc), defaults_, shadows_);
}

core::Result<std::shared_ptr<ShaderProgram>> PbrShaderCache::program(std::uint32_t features, const std::vector<Define> &extraDefines)
{
    auto key = std::make_pair(features, extraDefines);
    if (auto it = programs_.find(key); it != programs_.end())
    {
        return it->second;
    }
    std::vector<Define> defines = pbrDefines(features);
    defines.insert(defines.end(), extraDefines.begin(), extraDefines.end());
    auto program = library_->loadGraphics(shaderDirectory_ / "mesh_pbr.vert", shaderDirectory_ / "mesh_pbr.frag", defines);
    if (!program)
    {
        return std::unexpected(program.error());
    }
    programs_.emplace(std::move(key), *program);
    return *program;
}

} // namespace orosmo::renderer
