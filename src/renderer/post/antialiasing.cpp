// REN-19 : anticrénelage spatial (FXAA, SMAA 1x), noms des modes, enregistrement des plugins. Le TAA est dans taa.cpp.
#include "orosmo/renderer/antialiasing.hpp"

#include "orosmo/renderer/scene_renderer.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <cctype>

namespace orosmo::renderer
{

using core::ErrorCode;
using core::makeError;

namespace
{

constexpr rhi::Format kOutFormat = rhi::Format::kRGBA16Float; // sortie avant la recopie dans la scène (comme post_effects)

template <typename T> std::span<const std::byte> asBytes(const T &value) { return std::as_bytes(std::span(&value, 1)); }

bool equalsIgnoreCase(std::string_view a, std::string_view b) noexcept
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y)
                                              { return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y)); });
}

glm::vec2 texelSize(rhi::Extent2D size)
{
    return {1.0f / static_cast<float>(std::max(size.width, 1u)), 1.0f / static_cast<float>(std::max(size.height, 1u))};
}

} // namespace

const char *toString(AntiAliasingMode mode) noexcept
{
    switch (mode)
    {
    case AntiAliasingMode::kNone:
        return "none";
    case AntiAliasingMode::kFxaa:
        return "fxaa";
    case AntiAliasingMode::kSmaa:
        return "smaa";
    case AntiAliasingMode::kTaa:
        return "taa";
    }
    return "none";
}

std::optional<AntiAliasingMode> parseAntiAliasingMode(std::string_view name) noexcept
{
    for (const AntiAliasingMode mode : {AntiAliasingMode::kNone, AntiAliasingMode::kFxaa, AntiAliasingMode::kSmaa, AntiAliasingMode::kTaa})
    {
        if (equalsIgnoreCase(name, toString(mode)))
        {
            return mode;
        }
    }
    return std::nullopt;
}

// ── FXAA ────────────────────────────────────────────────────────────────────────────────────────────────────────────

core::Result<std::unique_ptr<FxaaFeature>> FxaaFeature::create(std::shared_ptr<PostContext> context)
{
    if (context == nullptr)
    {
        return makeError(ErrorCode::kInvalidArgument, "FxaaFeature::create: null context");
    }
    std::unique_ptr<FxaaFeature> feature(new FxaaFeature(std::move(context)));
    auto program = feature->context_->fullscreenProgram("fxaa.frag");
    if (!program)
    {
        return std::unexpected(program.error());
    }
    feature->program_ = *program;
    return feature;
}

core::Result<> FxaaFeature::declare(RenderGraphBuilder &builder)
{
    RenderGraph &graph = builder.graph();
    const RgTexture scene = builder.resources().sceneColor;
    const rhi::Extent2D size = builder.resources().renderSize;
    const RgTexture out = graph.createTexture("aa.fxaa.out", {.format = kOutFormat, .size = size});
    struct Push
    {
        glm::vec4 params;
        glm::vec4 extra;
    } push{{texelSize(size), settings_.edgeThreshold, settings_.edgeThresholdMin}, {settings_.subpixel, 0.0f, 0.0f, 0.0f}};
    graph
        .addPass(kName, PassType::kRaster,
                 [this, scene, push](RenderPassContext &ctx) -> core::Result<>
                 {
                     auto pipeline = fullscreenPipeline(*program_, kOutFormat);
                     if (!pipeline)
                     {
                         return std::unexpected(pipeline.error());
                     }
                     rhi::BindGroupDesc desc;
                     desc.entries.push_back(
                         {.binding = 0, .type = rhi::BindingType::kSampledTexture, .texture = ctx.texture(scene), .sampler = context_->linearSampler()});
                     desc.debugName = "aa.fxaa";
                     auto group = frameGroup(desc);
                     if (!group)
                     {
                         return std::unexpected(group.error());
                     }
                     ctx.encoder().setPipeline(*pipeline);
                     ctx.encoder().setBindGroup(1, *group);
                     ctx.encoder().setPushConstants(asBytes(push));
                     ctx.encoder().draw(3);
                     return {};
                 })
        .read(scene)
        .writeColor(out, rhi::LoadOp::kDontCare);
    declareCopyToScene(builder, "aa.fxaa.copy", out);
    return {};
}

// ── SMAA 1x ─────────────────────────────────────────────────────────────────────────────────────────────────────────

core::Result<std::unique_ptr<SmaaFeature>> SmaaFeature::create(std::shared_ptr<PostContext> context)
{
    if (context == nullptr)
    {
        return makeError(ErrorCode::kInvalidArgument, "SmaaFeature::create: null context");
    }
    std::unique_ptr<SmaaFeature> feature(new SmaaFeature(std::move(context)));
    for (auto [file, slot] : {std::pair{"smaa_edges.frag", &feature->edges_}, std::pair{"smaa_weights.frag", &feature->weights_},
                              std::pair{"smaa_blend.frag", &feature->blend_}})
    {
        auto program = feature->context_->fullscreenProgram(file);
        if (!program)
        {
            return std::unexpected(program.error());
        }
        *slot = *program;
    }
    return feature;
}

core::Result<> SmaaFeature::declare(RenderGraphBuilder &builder)
{
    RenderGraph &graph = builder.graph();
    const RgTexture scene = builder.resources().sceneColor;
    const rhi::Extent2D size = builder.resources().renderSize;
    const RgTexture edges = graph.createTexture(kEdgesTexture, {.format = rhi::Format::kRG8Unorm, .size = size});
    const RgTexture weights = graph.createTexture(kWeightsTexture, {.format = rhi::Format::kRGBA8Unorm, .size = size});
    const RgTexture out = graph.createTexture("aa.smaa.out", {.format = kOutFormat, .size = size});
    const glm::vec4 edgeParams(settings_.threshold, std::max(settings_.localContrastFactor, 1.0f), 0.0f, 0.0f);
    const glm::vec4 weightParams(static_cast<float>(std::clamp(settings_.maxSearchSteps, 1u, 64u)), 0.0f, 0.0f, 0.0f);

    // Une passe plein écran : `inputs` (liaisons 0, 1...) lus au plus proche, `params` en constantes poussées.
    auto fullscreen = [this](RenderPassContext &ctx, const ShaderProgram &program, rhi::Format format, std::initializer_list<RgTexture> inputs,
                             const glm::vec4 *params, const char *name) -> core::Result<>
    {
        auto pipeline = fullscreenPipeline(program, format);
        if (!pipeline)
        {
            return std::unexpected(pipeline.error());
        }
        rhi::BindGroupDesc desc;
        std::uint32_t binding = 0;
        for (const RgTexture input : inputs)
        {
            desc.entries.push_back(
                {.binding = binding++, .type = rhi::BindingType::kSampledTexture, .texture = ctx.texture(input), .sampler = context_->nearestSampler()});
        }
        desc.debugName = name;
        auto group = frameGroup(desc);
        if (!group)
        {
            return std::unexpected(group.error());
        }
        ctx.encoder().setPipeline(*pipeline);
        ctx.encoder().setBindGroup(1, *group);
        if (params != nullptr)
        {
            ctx.encoder().setPushConstants(asBytes(*params));
        }
        ctx.encoder().draw(3);
        return {};
    };

    graph
        .addPass("aa.smaa.edges", PassType::kRaster,
                 [this, fullscreen, scene, edgeParams](RenderPassContext &ctx)
                 { return fullscreen(ctx, *edges_, rhi::Format::kRG8Unorm, {scene}, &edgeParams, "aa.smaa.edges"); })
        .read(scene)
        .writeColor(edges, rhi::LoadOp::kDontCare);
    graph
        .addPass("aa.smaa.weights", PassType::kRaster,
                 [this, fullscreen, edges, weightParams](RenderPassContext &ctx)
                 { return fullscreen(ctx, *weights_, rhi::Format::kRGBA8Unorm, {edges}, &weightParams, "aa.smaa.weights"); })
        .read(edges)
        .writeColor(weights, rhi::LoadOp::kDontCare);
    graph
        .addPass("aa.smaa.blend", PassType::kRaster,
                 [this, fullscreen, scene, weights](RenderPassContext &ctx)
                 { return fullscreen(ctx, *blend_, kOutFormat, {scene, weights}, nullptr, "aa.smaa.blend"); })
        .read(scene)
        .read(weights)
        .writeColor(out, rhi::LoadOp::kDontCare);
    declareCopyToScene(builder, "aa.smaa.copy", out);
    return {};
}

// ── Sélection et enregistrement ─────────────────────────────────────────────────────────────────────────────────────

core::Result<> AntiAliasing::select(AntiAliasingMode wanted) noexcept
{
    const bool available = wanted == AntiAliasingMode::kNone || (wanted == AntiAliasingMode::kTaa && taa != nullptr) ||
                           (wanted == AntiAliasingMode::kFxaa && fxaa != nullptr) || (wanted == AntiAliasingMode::kSmaa && smaa != nullptr);
    if (!available)
    {
        return makeError(ErrorCode::kUnsupported, std::string("AntiAliasing::select: '") + toString(wanted) + "' is not registered");
    }
    if (taa != nullptr)
    {
        taa->setEnabled(wanted == AntiAliasingMode::kTaa);
    }
    if (fxaa != nullptr)
    {
        fxaa->setEnabled(wanted == AntiAliasingMode::kFxaa);
    }
    if (smaa != nullptr)
    {
        smaa->setEnabled(wanted == AntiAliasingMode::kSmaa);
    }
    return {};
}

AntiAliasingMode AntiAliasing::mode() const noexcept
{
    if (taa != nullptr && taa->enabled())
    {
        return AntiAliasingMode::kTaa;
    }
    if (smaa != nullptr && smaa->enabled())
    {
        return AntiAliasingMode::kSmaa;
    }
    if (fxaa != nullptr && fxaa->enabled())
    {
        return AntiAliasingMode::kFxaa;
    }
    return AntiAliasingMode::kNone;
}

core::Result<TaaFeature *> addTemporalAntiAliasing(SceneRenderer &renderer, std::shared_ptr<PostContext> context)
{
    auto taa = TaaFeature::create(std::move(context));
    if (!taa)
    {
        return std::unexpected(taa.error());
    }
    (*taa)->setEnabled(false);
    auto registered = renderer.addFeature(std::move(*taa));
    if (!registered)
    {
        return std::unexpected(registered.error());
    }
    return static_cast<TaaFeature *>(*registered);
}

core::Result<AntiAliasing> addEdgeAntiAliasing(SceneRenderer &renderer, std::shared_ptr<PostContext> context)
{
    // Tout est créé avant d'enregistrer : un échec ne laisse aucun plugin à moitié installé.
    auto fxaa = FxaaFeature::create(context);
    if (!fxaa)
    {
        return std::unexpected(fxaa.error());
    }
    auto smaa = SmaaFeature::create(std::move(context));
    if (!smaa)
    {
        return std::unexpected(smaa.error());
    }
    (*fxaa)->setEnabled(false);
    (*smaa)->setEnabled(false);
    AntiAliasing aa;
    auto fx = renderer.addFeature(std::move(*fxaa));
    if (!fx)
    {
        return std::unexpected(fx.error());
    }
    auto sm = renderer.addFeature(std::move(*smaa));
    if (!sm)
    {
        renderer.removeFeature(FxaaFeature::kName);
        return std::unexpected(sm.error());
    }
    aa.fxaa = static_cast<FxaaFeature *>(*fx);
    aa.smaa = static_cast<SmaaFeature *>(*sm);
    return aa;
}

} // namespace orosmo::renderer
