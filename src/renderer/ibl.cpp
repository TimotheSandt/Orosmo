#include "orosmo/renderer/ibl.hpp"

#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>

namespace orosmo::renderer
{

using core::ErrorCode;
using core::makeError;

namespace
{

constexpr float kPi = std::numbers::pi_v<float>;

// Base réelle des harmoniques sphériques d'ordre 2 (même ordre que ibl_common.glsl).
std::array<float, 9> shBasis(const glm::vec3 &n) noexcept
{
    return {0.28209479177f,
            0.48860251190f * n.y,
            0.48860251190f * n.z,
            0.48860251190f * n.x,
            1.09254843059f * n.x * n.y,
            1.09254843059f * n.y * n.z,
            0.31539156525f * (3.0f * n.z * n.z - 1.0f),
            1.09254843059f * n.x * n.z,
            0.54627421529f * (n.x * n.x - n.y * n.y)};
}

glm::vec2 hammersley(std::uint32_t i, std::uint32_t count) noexcept
{
    std::uint32_t bits = i;
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return {(static_cast<float>(i) + 0.5f) / static_cast<float>(count), static_cast<float>(bits) * 2.3283064365386963e-10f};
}

// Visibilité de Smith-GGX corrélée (pbr.glsl).
float visibilitySmith(float nDotV, float nDotL, float alpha) noexcept
{
    const float a2 = alpha * alpha;
    const float gv = nDotL * std::sqrt(nDotV * nDotV * (1.0f - a2) + a2);
    const float gl = nDotV * std::sqrt(nDotL * nDotL * (1.0f - a2) + a2);
    const float sum = gv + gl;
    return sum > 0.0f ? 0.5f / sum : 0.0f;
}

std::uint32_t fullChain(std::uint32_t size) noexcept { return static_cast<std::uint32_t>(std::bit_width(std::max(size, 1u))); }

struct PrefilterBlock
{
    glm::vec4 params;
};

} // namespace

glm::vec2 equirectUv(const glm::vec3 &d) noexcept
{
    return {std::atan2(d.x, -d.z) * (0.5f / kPi) + 0.5f, std::acos(std::clamp(d.y, -1.0f, 1.0f)) / kPi};
}

glm::vec3 equirectDirection(const glm::vec2 &uv) noexcept
{
    const float phi = (uv.x - 0.5f) * 2.0f * kPi;
    const float theta = uv.y * kPi;
    const float s = std::sin(theta);
    return {s * std::sin(phi), std::cos(theta), -s * std::cos(phi)};
}

EquirectImage uniformEquirect(const glm::vec3 &radiance, std::uint32_t width, std::uint32_t height)
{
    EquirectImage image;
    image.width = width;
    image.height = height;
    image.texels.assign(std::size_t{width} * height, radiance);
    return image;
}

glm::vec3 ShIrradiance::evaluate(const glm::vec3 &normal) const noexcept
{
    const std::array<float, 9> y = shBasis(normal);
    glm::vec3 e(0.0f);
    for (std::size_t k = 0; k < 9; ++k)
    {
        e += coefficients[k] * y[k];
    }
    return glm::max(e, glm::vec3(0.0f));
}

ShIrradiance ShIrradiance::uniform(const glm::vec3 &radiance) noexcept
{
    ShIrradiance sh;
    sh.coefficients[0] = radiance * (1.0f / 0.28209479177f); // 4π × Y00 = 1 / Y00
    return sh;
}

core::Result<ShIrradiance> projectEquirect(const EquirectImage &image)
{
    if (image.empty())
    {
        return makeError(ErrorCode::kInvalidArgument, "projectEquirect: empty image");
    }
    // Projection par moindres carrés pondérés (angle solide de chaque texel) : la somme discrète de la base n'est pas exactement
    // orthonormée sur une grille équirectangulaire, on résout donc G c = b avec la matrice de Gram G. Une image constante donne
    // alors exactement sa valeur. Sommes en double dans un ordre fixe : le résultat est déterministe.
    std::array<std::array<double, 9>, 9> gram{};
    std::array<glm::dvec3, 9> sums{};
    for (std::uint32_t y = 0; y < image.height; ++y)
    {
        const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(image.height);
        const double weight = std::sin(static_cast<double>(v) * std::numbers::pi);
        for (std::uint32_t x = 0; x < image.width; ++x)
        {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(image.width);
            const std::array<float, 9> basis = shBasis(equirectDirection({u, v}));
            const glm::dvec3 radiance(image.texels[std::size_t{y} * image.width + x]);
            for (std::size_t k = 0; k < 9; ++k)
            {
                sums[k] += radiance * (weight * basis[k]);
                for (std::size_t j = 0; j < 9; ++j)
                {
                    gram[k][j] += weight * static_cast<double>(basis[k]) * static_cast<double>(basis[j]);
                }
            }
        }
    }
    // Élimination de Gauss avec pivot partiel (G est symétrique définie positive dès que l'image a au moins 3 x 3 texels).
    for (std::size_t col = 0; col < 9; ++col)
    {
        std::size_t pivot = col;
        for (std::size_t row = col + 1; row < 9; ++row)
        {
            if (std::abs(gram[row][col]) > std::abs(gram[pivot][col]))
            {
                pivot = row;
            }
        }
        if (std::abs(gram[pivot][col]) < 1e-12)
        {
            return makeError(ErrorCode::kInvalidArgument, "projectEquirect: image too small for an order-2 projection");
        }
        std::swap(gram[col], gram[pivot]);
        std::swap(sums[col], sums[pivot]);
        for (std::size_t row = col + 1; row < 9; ++row)
        {
            const double f = gram[row][col] / gram[col][col];
            for (std::size_t j = col; j < 9; ++j)
            {
                gram[row][j] -= f * gram[col][j];
            }
            sums[row] -= f * sums[col];
        }
    }
    std::array<glm::dvec3, 9> radianceSh{};
    for (std::size_t i = 9; i-- > 0;)
    {
        glm::dvec3 acc = sums[i];
        for (std::size_t j = i + 1; j < 9; ++j)
        {
            acc -= gram[i][j] * radianceSh[j];
        }
        radianceSh[i] = acc / gram[i][i];
    }
    // Convolution par le lobe cosinus divisée par π : bandes × 1, 2/3, 1/4.
    static constexpr double kBand[9] = {1.0, 2.0 / 3.0, 2.0 / 3.0, 2.0 / 3.0, 0.25, 0.25, 0.25, 0.25, 0.25};
    ShIrradiance sh;
    for (std::size_t k = 0; k < 9; ++k)
    {
        sh.coefficients[k] = glm::vec3(radianceSh[k] * kBand[k]);
    }
    return sh;
}

std::vector<glm::vec2> computeBrdfLut(std::uint32_t size, std::uint32_t samples)
{
    size = std::max(size, 1u);
    samples = std::max(samples, 1u);
    std::vector<glm::vec2> lut(std::size_t{size} * size);
    for (std::uint32_t row = 0; row < size; ++row)
    {
        const float roughness = (static_cast<float>(row) + 0.5f) / static_cast<float>(size);
        const float alpha = std::max(roughness * roughness, 0.002f);
        for (std::uint32_t col = 0; col < size; ++col)
        {
            const float nDotV = (static_cast<float>(col) + 0.5f) / static_cast<float>(size);
            const glm::vec3 v(std::sqrt(1.0f - nDotV * nDotV), 0.0f, nDotV);
            float a = 0.0f;
            float b = 0.0f;
            for (std::uint32_t i = 0; i < samples; ++i)
            {
                const glm::vec2 xi = hammersley(i, samples);
                const float phi = 2.0f * kPi * xi.x;
                const float cosTheta = std::sqrt((1.0f - xi.y) / (1.0f + (alpha * alpha - 1.0f) * xi.y));
                const float sinTheta = std::sqrt(std::max(1.0f - cosTheta * cosTheta, 0.0f));
                const glm::vec3 h(sinTheta * std::cos(phi), sinTheta * std::sin(phi), cosTheta);
                const float vDotH = glm::dot(v, h);
                const glm::vec3 l = 2.0f * vDotH * h - v;
                const float nDotL = l.z;
                if (nDotL <= 0.0f || vDotH <= 0.0f)
                {
                    continue;
                }
                // Poids = BRDF spéculaire × n·l / pdf, avec pdf = D × n·h / (4 v·h) : D se simplifie.
                const float weight = visibilitySmith(nDotV, nDotL, alpha) * 4.0f * vDotH * nDotL / std::max(h.z, 1e-6f);
                const float fc = std::pow(1.0f - vDotH, 5.0f);
                a += (1.0f - fc) * weight;
                b += fc * weight;
            }
            lut[std::size_t{row} * size + col] = glm::vec2(a, b) / static_cast<float>(samples);
        }
    }
    return lut;
}

core::Result<> ProbeSet::add(const LightProbe &probe)
{
    if (probes_.size() >= kMaxLightProbes)
    {
        return makeError(ErrorCode::kInvalidArgument, "ProbeSet::add: at most " + std::to_string(kMaxLightProbes) + " probes");
    }
    if (!(probe.radius > 0.0f))
    {
        return makeError(ErrorCode::kInvalidArgument, "ProbeSet::add: the radius must be positive");
    }
    probes_.push_back(probe);
    return {};
}

ShIrradiance ProbeSet::sample(const glm::vec3 &position, const ShIrradiance &environment) const noexcept
{
    ShIrradiance mixed;
    float total = 0.0f;
    for (const LightProbe &probe : probes_)
    {
        float w = std::clamp(1.0f - glm::length(position - probe.position) / probe.radius, 0.0f, 1.0f);
        w *= w;
        if (w > 0.0f)
        {
            for (std::size_t k = 0; k < 9; ++k)
            {
                mixed.coefficients[k] += w * probe.irradiance.coefficients[k];
            }
            total += w;
        }
    }
    for (std::size_t k = 0; k < 9; ++k)
    {
        mixed.coefficients[k] = total >= 1.0f ? mixed.coefficients[k] / total : mixed.coefficients[k] + (1.0f - total) * environment.coefficients[k];
    }
    return mixed;
}

std::vector<Define> iblDefines() { return {{"OROSMO_IBL", "1"}}; }

// ── IblFeature ──────────────────────────────────────────────────────────────────────────────────────────────────

core::Result<std::unique_ptr<IblFeature>> IblFeature::create(rhi::Device &device, ShaderLibrary &library, PipelineCache &pipelines,
                                                             std::filesystem::path shaderDirectory, IblSettings settings)
{
    if (!device.capabilities().compute)
    {
        return makeError(ErrorCode::kUnsupported, "IblFeature: compute shaders are not available");
    }
    if (settings.cubeSize == 0 || settings.mipLevels == 0 || settings.samples == 0 || settings.brdfLutSize < 2)
    {
        return makeError(ErrorCode::kInvalidArgument, "IblFeature: cubeSize, mipLevels and samples must be > 0, brdfLutSize >= 2");
    }
    auto feature = std::unique_ptr<IblFeature>(new IblFeature(device, pipelines, settings));
    if (auto r = feature->createStaticResources(library, shaderDirectory); !r)
    {
        return std::unexpected(r.error());
    }
    if (auto r = feature->setEnvironment(uniformEquirect(glm::vec3(0.0f))); !r)
    {
        return std::unexpected(r.error());
    }
    return feature;
}

IblFeature::~IblFeature() { release(); }

void IblFeature::release() noexcept
{
    device_.destroy(cube_);
    device_.destroy(lut_);
    device_.destroy(cubeSampler_);
    device_.destroy(lutSampler_);
    device_.destroy(equirectSampler_);
    cube_ = {};
    lut_ = {};
    cubeSampler_ = {};
    lutSampler_ = {};
    equirectSampler_ = {};
}

core::Result<> IblFeature::createStaticResources(ShaderLibrary &library, const std::filesystem::path &shaderDirectory)
{
    auto program = library.loadCompute(shaderDirectory / "ibl_prefilter.comp");
    if (!program)
    {
        return std::unexpected(program.error());
    }
    prefilterProgram_ = *program;

    rhi::SamplerDesc cubeSampler;
    cubeSampler.addressU = cubeSampler.addressV = cubeSampler.addressW = rhi::AddressMode::kClampToEdge;
    cubeSampler.debugName = "ibl.cube";
    rhi::SamplerDesc lutSampler = cubeSampler;
    lutSampler.mipFilter = std::nullopt;
    lutSampler.debugName = "ibl.brdfLut";
    rhi::SamplerDesc equirectSampler;
    equirectSampler.addressU = rhi::AddressMode::kRepeat;
    equirectSampler.addressV = equirectSampler.addressW = rhi::AddressMode::kClampToEdge;
    equirectSampler.debugName = "ibl.equirect";
    auto cs = device_.createSampler(cubeSampler);
    auto ls = device_.createSampler(lutSampler);
    auto es = device_.createSampler(equirectSampler);
    if (!cs || !ls || !es)
    {
        return std::unexpected(!cs ? cs.error() : !ls ? ls.error() : es.error());
    }
    cubeSampler_ = *cs;
    lutSampler_ = *ls;
    equirectSampler_ = *es;

    // Table BRDF (RG16F).
    const std::uint32_t lutSize = settings_.brdfLutSize;
    const std::vector<glm::vec2> lut = computeBrdfLut(lutSize);
    std::vector<std::uint16_t> halves;
    halves.reserve(lut.size() * 2);
    for (const glm::vec2 &ab : lut)
    {
        halves.push_back(glm::packHalf1x16(ab.x));
        halves.push_back(glm::packHalf1x16(ab.y));
    }
    rhi::TextureDesc lutDesc;
    lutDesc.format = rhi::Format::kRG16Float;
    lutDesc.size = {lutSize, lutSize, 1};
    lutDesc.usage = rhi::TextureUsage::kSampled | rhi::TextureUsage::kCopyDst;
    lutDesc.debugName = "ibl.brdfLut";
    auto lutTexture = device_.createTexture(lutDesc);
    if (!lutTexture)
    {
        return std::unexpected(lutTexture.error());
    }
    lut_ = *lutTexture;
    if (auto r = device_.updateTexture(lut_, {}, std::as_bytes(std::span(halves))); !r)
    {
        return r;
    }

    // Cube préfiltré : un niveau par rugosité.
    cubeLevels_ = std::min(settings_.mipLevels, fullChain(settings_.cubeSize));
    rhi::TextureDesc cubeDesc;
    cubeDesc.dimension = rhi::TextureDimension::kCube;
    cubeDesc.format = rhi::Format::kRGBA16Float;
    cubeDesc.size = {settings_.cubeSize, settings_.cubeSize, 1};
    cubeDesc.mipLevels = cubeLevels_;
    cubeDesc.layers = 6;
    cubeDesc.usage = rhi::TextureUsage::kSampled | rhi::TextureUsage::kStorage;
    cubeDesc.debugName = "ibl.environment";
    auto cube = device_.createTexture(cubeDesc);
    if (!cube)
    {
        return std::unexpected(cube.error());
    }
    cube_ = *cube;

    rhi::BufferDesc paramsDesc;
    paramsDesc.size = sizeof(GpuIblBlock);
    paramsDesc.usage = rhi::BufferUsage::kUniform | rhi::BufferUsage::kCopyDst;
    paramsDesc.debugName = "ibl.params";
    const GpuIblBlock block = gpuBlock();
    auto params = GpuBuffer::create(device_, paramsDesc, std::as_bytes(std::span(&block, 1)));
    if (!params)
    {
        return std::unexpected(params.error());
    }
    params_ = std::move(*params);
    return {};
}

core::Result<> IblFeature::setEnvironment(const EquirectImage &image)
{
    auto sh = projectEquirect(image);
    if (!sh)
    {
        return std::unexpected(sh.error());
    }
    // Environnement RGBA16F avec sa chaîne de mips (lue par le préfiltrage, puis libérée).
    std::vector<std::uint16_t> halves;
    halves.reserve(image.texels.size() * 4);
    for (const glm::vec3 &c : image.texels)
    {
        halves.push_back(glm::packHalf1x16(c.r));
        halves.push_back(glm::packHalf1x16(c.g));
        halves.push_back(glm::packHalf1x16(c.b));
        halves.push_back(glm::packHalf1x16(1.0f));
    }
    rhi::TextureDesc desc;
    desc.format = rhi::Format::kRGBA16Float;
    desc.size = {image.width, image.height, 1};
    desc.mipLevels = 0;
    desc.usage = rhi::TextureUsage::kSampled | rhi::TextureUsage::kCopySrc | rhi::TextureUsage::kCopyDst;
    desc.debugName = "ibl.equirect";
    auto equirect = device_.createTexture(desc);
    if (!equirect)
    {
        return std::unexpected(equirect.error());
    }
    core::Result<> result = device_.updateTexture(*equirect, {}, std::as_bytes(std::span(halves)));
    if (result)
    {
        result = prefilter(*equirect);
    }
    device_.destroy(*equirect); // différé : les commandes soumises qui la lisent se terminent d'abord
    if (!result)
    {
        return result;
    }
    environmentSh_ = *sh;
    paramsDirty_ = true;
    return {};
}

core::Result<> IblFeature::prefilter(rhi::TextureHandle equirect)
{
    auto pipeline = pipelines_.compute(*prefilterProgram_);
    if (!pipeline)
    {
        return std::unexpected(pipeline.error());
    }
    auto desc = device_.textureDesc(equirect);
    if (!desc)
    {
        return std::unexpected(desc.error());
    }
    const float texelSolidAngle = 4.0f * kPi / static_cast<float>(desc->size.width * desc->size.height);
    std::vector<rhi::BufferHandle> buffers;
    std::vector<rhi::BindGroupHandle> groups;
    auto cleanup = [&]
    {
        for (rhi::BindGroupHandle g : groups)
        {
            device_.destroy(g);
        }
        for (rhi::BufferHandle b : buffers)
        {
            device_.destroy(b);
        }
    };
    for (std::uint32_t level = 0; level < cubeLevels_; ++level)
    {
        const std::uint32_t size = std::max(settings_.cubeSize >> level, 1u);
        const float roughness = cubeLevels_ > 1 ? static_cast<float>(level) / static_cast<float>(cubeLevels_ - 1) : 0.0f;
        const PrefilterBlock block{glm::vec4(roughness, static_cast<float>(size), static_cast<float>(settings_.samples), texelSolidAngle)};
        rhi::BufferDesc bd;
        bd.size = sizeof(PrefilterBlock);
        bd.usage = rhi::BufferUsage::kUniform;
        bd.debugName = "ibl.prefilter.params";
        auto buffer = device_.createBuffer(bd, std::as_bytes(std::span(&block, 1)));
        if (!buffer)
        {
            cleanup();
            return std::unexpected(buffer.error());
        }
        buffers.push_back(*buffer);
        rhi::BindGroupDesc gd;
        gd.debugName = "ibl.prefilter";
        gd.entries.push_back({.binding = 0, .type = rhi::BindingType::kSampledTexture, .texture = equirect, .sampler = equirectSampler_});
        gd.entries.push_back({.binding = 1, .type = rhi::BindingType::kStorageImage, .texture = cube_, .mipLevel = level});
        gd.entries.push_back({.binding = 2, .type = rhi::BindingType::kUniformBuffer, .buffer = *buffer, .size = sizeof(PrefilterBlock)});
        auto group = device_.createBindGroup(gd);
        if (!group)
        {
            cleanup();
            return std::unexpected(group.error());
        }
        groups.push_back(*group);
    }
    std::unique_ptr<rhi::CommandEncoder> encoder = device_.createCommandEncoder();
    encoder->beginDebugGroup("ibl.prefilter");
    encoder->generateMips(equirect);
    encoder->bindComputePipeline(*pipeline);
    for (std::uint32_t level = 0; level < cubeLevels_; ++level)
    {
        const std::uint32_t size = std::max(settings_.cubeSize >> level, 1u);
        encoder->setBindGroup(0, groups[level]);
        encoder->dispatch((size + 7) / 8, (size + 7) / 8, 6);
    }
    encoder->endDebugGroup();
    core::Result<> submitted = device_.submit(std::move(encoder));
    cleanup(); // destruction différée : après la fin des commandes soumises
    return submitted;
}

void IblFeature::setIntensity(float diffuse, float specular) noexcept
{
    settings_.diffuseIntensity = diffuse;
    settings_.specularIntensity = specular;
    paramsDirty_ = true;
}

void IblFeature::setProbes(const ProbeSet &probes)
{
    probes_ = probes;
    paramsDirty_ = true;
}

GpuIblBlock IblFeature::gpuBlock() const noexcept
{
    GpuIblBlock block{};
    const std::span<const LightProbe> probes = probes_.probes();
    block.params = glm::vec4(settings_.diffuseIntensity, settings_.specularIntensity, static_cast<float>(cubeLevels_ - 1),
                             static_cast<float>(probes.size()));
    for (std::size_t k = 0; k < 9; ++k)
    {
        block.environmentSh[k] = glm::vec4(environmentSh_.coefficients[k], 0.0f);
    }
    for (std::size_t i = 0; i < probes.size(); ++i)
    {
        block.probePositions[i] = glm::vec4(probes[i].position, probes[i].radius);
        for (std::size_t k = 0; k < 9; ++k)
        {
            block.probeSh[i * 9 + k] = glm::vec4(probes[i].irradiance.coefficients[k], 0.0f);
        }
    }
    return block;
}

core::Result<> IblFeature::declarePasses(RenderGraphBuilder &builder)
{
    if (paramsDirty_)
    {
        const GpuIblBlock block = gpuBlock();
        if (auto r = params_.update(0, std::as_bytes(std::span(&block, 1))); !r)
        {
            return r;
        }
        paramsDirty_ = false;
    }
    if (auto r = builder.addFrameBinding({.binding = kIblEnvironmentBinding, .type = rhi::BindingType::kSampledTexture, .texture = cube_, .sampler = cubeSampler_});
        !r)
    {
        return r;
    }
    if (auto r = builder.addFrameBinding({.binding = kIblBrdfLutBinding, .type = rhi::BindingType::kSampledTexture, .texture = lut_, .sampler = lutSampler_}); !r)
    {
        return r;
    }
    return builder.addFrameBinding({.binding = kIblParamsBinding, .type = rhi::BindingType::kUniformBuffer, .buffer = params_.handle(), .size = sizeof(GpuIblBlock)});
}

} // namespace orosmo::renderer
