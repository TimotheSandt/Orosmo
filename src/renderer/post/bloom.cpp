// REN-18 : bloom. Réductions successives (1/2, 1/4, ... de la taille de rendu ; la première applique le seuil doux), puis
// agrandissements additifs du plus petit niveau vers le plus grand, puis ajout du niveau 1/2 à la scène (mélange additif, alpha
// inchangé). Chaque passe déclare sa source (`read`) et sa cible (`writeColor`) ; les niveaux sont des textures transitoires.
#include "orosmo/renderer/post_effects.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace orosmo::renderer
{

using core::ErrorCode;
using core::makeError;

namespace
{
template <typename T> std::span<const std::byte> asBytes(const T &value) { return std::as_bytes(std::span(&value, 1)); }

glm::vec2 texelSize(rhi::Extent2D size) { return {1.0f / static_cast<float>(std::max(size.width, 1u)), 1.0f / static_cast<float>(std::max(size.height, 1u))}; }
} // namespace

core::Result<std::unique_ptr<BloomFeature>> BloomFeature::create(std::shared_ptr<PostContext> context)
{
    if (context == nullptr)
    {
        return makeError(ErrorCode::kInvalidArgument, "BloomFeature::create: null context");
    }
    std::unique_ptr<BloomFeature> feature(new BloomFeature(std::move(context)));
    auto down = feature->context_->fullscreenProgram("bloom_down.frag");
    if (!down)
    {
        return std::unexpected(down.error());
    }
    auto up = feature->context_->fullscreenProgram("bloom_up.frag");
    if (!up)
    {
        return std::unexpected(up.error());
    }
    feature->down_ = *down;
    feature->up_ = *up;
    return feature;
}

core::Result<> BloomFeature::declare(RenderGraphBuilder &builder)
{
    RenderGraph &graph = builder.graph();
    const RgTexture scene = builder.resources().sceneColor;
    const rhi::Extent2D renderSize = builder.resources().renderSize;
    const BloomSettings s = settings_;

    // Niveaux : moitié du précédent, arrêt à 1 pixel de côté.
    std::vector<rhi::Extent2D> sizes;
    rhi::Extent2D size = renderSize;
    const std::uint32_t wanted = std::clamp<std::uint32_t>(s.levels, 1, 8);
    while (sizes.size() < wanted && (size.width > 1 || size.height > 1))
    {
        size = {std::max(size.width / 2, 1u), std::max(size.height / 2, 1u)};
        sizes.push_back(size);
    }
    if (sizes.empty())
    {
        return {}; // scène de 1×1 : rien à flouter
    }
    std::vector<RgTexture> levels;
    for (std::size_t i = 0; i < sizes.size(); ++i)
    {
        levels.push_back(graph.createTexture("post.bloom." + std::to_string(i), {.format = rhi::Format::kRGBA16Float, .size = sizes[i]}));
    }

    const rhi::BlendState additive{true, rhi::BlendFactor::kOne, rhi::BlendFactor::kOne, rhi::BlendOp::kAdd,
                                   rhi::BlendFactor::kZero, rhi::BlendFactor::kOne, rhi::BlendOp::kAdd};
    const rhi::ColorWriteMask rgb = rhi::ColorWriteMask::kR | rhi::ColorWriteMask::kG | rhi::ColorWriteMask::kB;

    // Passe plein écran : `program` lit `source` (échantillonneur linéaire) et écrit l'attachement de la passe.
    auto draw = [this](RenderPassContext &ctx, const ShaderProgram &program, RgTexture source, rhi::Format format, const rhi::BlendState &blend,
                       rhi::ColorWriteMask mask, std::span<const std::byte> push) -> core::Result<>
    {
        auto pipeline = fullscreenPipeline(program, format, blend, mask);
        if (!pipeline)
        {
            return std::unexpected(pipeline.error());
        }
        rhi::BindGroupDesc desc;
        desc.entries.push_back(
            {.binding = 0, .type = rhi::BindingType::kSampledTexture, .texture = ctx.texture(source), .sampler = context_->linearSampler()});
        desc.debugName = "post.bloom";
        auto group = frameGroup(desc);
        if (!group)
        {
            return std::unexpected(group.error());
        }
        ctx.encoder().setPipeline(*pipeline);
        ctx.encoder().setBindGroup(1, *group);
        ctx.encoder().setPushConstants(push);
        ctx.encoder().draw(3);
        return {};
    };

    // Réductions.
    for (std::size_t i = 0; i < levels.size(); ++i)
    {
        const RgTexture source = i == 0 ? scene : levels[i - 1];
        const rhi::Extent2D sourceSize = i == 0 ? renderSize : sizes[i - 1];
        struct DownPush
        {
            glm::vec4 params;
            glm::vec4 options;
        } push{{texelSize(sourceSize), s.threshold, std::max(s.knee, 0.0f)}, {i == 0 ? 1.0f : 0.0f, std::max(s.maxLuminance, 0.0f), 0.0f, 0.0f}};
        graph
            .addPass("post.bloom.down" + std::to_string(i), PassType::kRaster,
                     [this, draw, source, push](RenderPassContext &ctx)
                     { return draw(ctx, *down_, source, rhi::Format::kRGBA16Float, {}, rhi::ColorWriteMask::kAll, asBytes(push)); })
            .read(source)
            .writeColor(levels[i], rhi::LoadOp::kDontCare);
    }
    // Agrandissements : le niveau i + 1 flouté s'ajoute au niveau i.
    for (std::size_t i = levels.size() - 1; i-- > 0;)
    {
        const RgTexture source = levels[i + 1];
        const glm::vec4 push(texelSize(sizes[i + 1]), s.radius, 1.0f);
        graph
            .addPass("post.bloom.up" + std::to_string(i), PassType::kRaster,
                     [this, draw, source, push, additive, rgb](RenderPassContext &ctx)
                     { return draw(ctx, *up_, source, rhi::Format::kRGBA16Float, additive, rgb, asBytes(push)); })
            .read(source)
            .writeColor(levels[i], rhi::LoadOp::kLoad);
    }
    // Ajout à la scène.
    const glm::vec4 push(texelSize(sizes[0]), s.radius, std::max(s.intensity, 0.0f));
    graph
        .addPass("post.bloom.composite", PassType::kRaster,
                 [this, draw, scene, source = levels[0], push, additive, rgb](RenderPassContext &ctx)
                 { return draw(ctx, *up_, source, ctx.desc(scene).format, additive, rgb, asBytes(push)); })
        .read(levels[0])
        .writeColor(scene, rhi::LoadOp::kLoad);
    return {};
}

} // namespace orosmo::renderer
