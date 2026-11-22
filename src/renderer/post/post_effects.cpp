// REN-17, REN-18 : ressources partagées des post-traitements, base des effets, tonemapping, étalonnage par LUT, vignettage, chaîne
// complète. L'exposition automatique et le bloom sont dans auto_exposure.cpp et bloom.cpp.
#include "orosmo/renderer/post_effects.hpp"

#include "orosmo/renderer/scene_renderer.hpp"

#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace orosmo::renderer
{

using core::ErrorCode;
using core::makeError;

namespace
{

constexpr rhi::Format kPostFormat = rhi::Format::kRGBA16Float; // intermédiaires des effets

template <typename T> std::span<const std::byte> asBytes(const T &value) { return std::as_bytes(std::span(&value, 1)); }

} // namespace

// ── PostContext ─────────────────────────────────────────────────────────────────────────────────────────────────────

core::Result<std::shared_ptr<PostContext>> PostContext::create(rhi::Device &device, ShaderLibrary &library, PipelineCache &pipelines,
                                                               const std::filesystem::path &shaderDirectory)
{
    std::shared_ptr<PostContext> context(new PostContext(device, library, pipelines));
    context->shaderDirectory_ = shaderDirectory;
    auto blit = FullscreenBlit::create(device, library, pipelines, shaderDirectory);
    if (!blit)
    {
        return std::unexpected(blit.error());
    }
    context->blit_ = std::move(*blit);
    rhi::SamplerDesc sd;
    sd.mipFilter = std::nullopt;
    sd.addressU = rhi::AddressMode::kClampToEdge;
    sd.addressV = rhi::AddressMode::kClampToEdge;
    sd.addressW = rhi::AddressMode::kClampToEdge;
    sd.debugName = "post.linear";
    auto linear = device.createSampler(sd);
    if (!linear)
    {
        return std::unexpected(linear.error());
    }
    context->linear_ = *linear;
    sd.minFilter = rhi::Filter::kNearest;
    sd.magFilter = rhi::Filter::kNearest;
    sd.debugName = "post.nearest";
    auto nearest = device.createSampler(sd);
    if (!nearest)
    {
        return std::unexpected(nearest.error());
    }
    context->nearest_ = *nearest;
    return context;
}

PostContext::~PostContext()
{
    blit_.reset();
    device_.destroy(linear_);
    device_.destroy(nearest_);
}

core::Result<std::shared_ptr<ShaderProgram>> PostContext::fullscreenProgram(const std::string &fragment) const
{
    return library_.loadGraphics(shaderDirectory_ / "blit.vert", shaderDirectory_ / "post" / fragment);
}

core::Result<std::shared_ptr<ShaderProgram>> PostContext::computeProgram(const std::string &file) const
{
    return library_.loadCompute(shaderDirectory_ / "post" / file);
}

// ── PostEffect ──────────────────────────────────────────────────────────────────────────────────────────────────────

PostEffect::~PostEffect()
{
    for (const rhi::BindGroupHandle group : groups_)
    {
        context_->device().destroy(group);
    }
}

core::Result<> PostEffect::declarePasses(RenderGraphBuilder &builder)
{
    // Les groupes de l'image précédente ne servent plus (leur destruction est différée par le RHI jusqu'à la fin de son exécution).
    for (const rhi::BindGroupHandle group : groups_)
    {
        context_->device().destroy(group);
    }
    groups_.clear();
    if (!enabled_ || !builder.resources().sceneColor.valid())
    {
        return {};
    }
    return declare(builder);
}

core::Result<rhi::BindGroupHandle> PostEffect::frameGroup(const rhi::BindGroupDesc &desc)
{
    auto group = context_->device().createBindGroup(desc);
    if (!group)
    {
        return std::unexpected(group.error());
    }
    groups_.push_back(*group);
    return *group;
}

core::Result<rhi::PipelineHandle> PostEffect::fullscreenPipeline(const ShaderProgram &program, rhi::Format format, const rhi::BlendState &blend,
                                                                 rhi::ColorWriteMask mask)
{
    GraphicsState state;
    state.raster.cull = rhi::CullMode::kNone;
    state.colorTargets.assign(1, rhi::ColorTargetState{.format = format, .blend = blend, .writeMask = mask});
    state.debugName = program.name();
    return context_->pipelines().graphics(program, state);
}

void PostEffect::declareCopyToScene(RenderGraphBuilder &builder, const std::string &name, RgTexture source)
{
    const RgTexture scene = builder.resources().sceneColor;
    builder.graph()
        .addPass(name, PassType::kRaster,
                 [this, source, scene](RenderPassContext &ctx) -> core::Result<>
                 { return context_->blit().draw(ctx.encoder(), ctx.texture(source), context_->nearestSampler(), ctx.desc(scene).format); })
        .read(source)
        .writeColor(scene, rhi::LoadOp::kDontCare);
}

// ── TonemapFeature (REN-17) ─────────────────────────────────────────────────────────────────────────────────────────

core::Result<std::unique_ptr<TonemapFeature>> TonemapFeature::create(std::shared_ptr<PostContext> context)
{
    if (context == nullptr)
    {
        return makeError(ErrorCode::kInvalidArgument, "TonemapFeature::create: null context");
    }
    std::unique_ptr<TonemapFeature> feature(new TonemapFeature(std::move(context)));
    auto program = feature->context_->fullscreenProgram("tonemap.frag");
    if (!program)
    {
        return std::unexpected(program.error());
    }
    feature->program_ = *program;
    const glm::vec4 unit(1.0f, 0.0f, 1.0f, 0.0f);
    rhi::BufferDesc desc;
    desc.size = sizeof unit;
    desc.usage = rhi::BufferUsage::kUniform | rhi::BufferUsage::kCopyDst;
    desc.debugName = "post.tonemap.unit-exposure";
    auto buffer = feature->context_->device().createBuffer(desc, asBytes(unit));
    if (!buffer)
    {
        return std::unexpected(buffer.error());
    }
    feature->unitExposure_ = *buffer;
    return feature;
}

TonemapFeature::~TonemapFeature() { context_->device().destroy(unitExposure_); }

core::Result<> TonemapFeature::prepare(const FrameData &frame)
{
    frameExposure_ = frame.exposure.x;
    return {};
}

core::Result<> TonemapFeature::declare(RenderGraphBuilder &builder)
{
    RenderGraph &graph = builder.graph();
    const RgTexture scene = builder.resources().sceneColor;
    const RgTexture out = graph.createTexture("post.tonemap.out", {.format = kPostFormat, .size = builder.resources().renderSize});
    const RgBuffer exposure = settings_.useAutoExposure ? graph.findBuffer(AutoExposureFeature::kExposureBuffer) : RgBuffer{};
    const glm::vec4 params(frameExposure_ * std::exp2(settings_.exposureEv), settings_.whitePoint, static_cast<float>(settings_.tonemapper),
                           settings_.encodeSrgb ? 1.0f : 0.0f);
    auto pass = graph.addPass(kName, PassType::kRaster,
                              [this, scene, exposure, params](RenderPassContext &ctx) -> core::Result<>
                              {
                                  auto pipeline = fullscreenPipeline(*program_, kPostFormat);
                                  if (!pipeline)
                                  {
                                      return std::unexpected(pipeline.error());
                                  }
                                  rhi::BindGroupDesc desc;
                                  desc.entries.push_back({.binding = 0,
                                                          .type = rhi::BindingType::kSampledTexture,
                                                          .texture = ctx.texture(scene),
                                                          .sampler = context_->nearestSampler()});
                                  desc.entries.push_back({.binding = 1,
                                                          .type = rhi::BindingType::kUniformBuffer,
                                                          .buffer = exposure.valid() ? ctx.buffer(exposure) : unitExposure_,
                                                          .size = sizeof(glm::vec4)});
                                  desc.debugName = "post.tonemap";
                                  auto group = frameGroup(desc);
                                  if (!group)
                                  {
                                      return std::unexpected(group.error());
                                  }
                                  ctx.encoder().setPipeline(*pipeline);
                                  ctx.encoder().setBindGroup(1, *group);
                                  ctx.encoder().setPushConstants(asBytes(params));
                                  ctx.encoder().draw(3);
                                  return {};
                              });
    pass.read(scene).writeColor(out, rhi::LoadOp::kDontCare);
    if (exposure.valid())
    {
        pass.read(exposure);
    }
    declareCopyToScene(builder, "post.tonemap.copy", out);
    return {};
}

// ── ColorGradingFeature (REN-18) ────────────────────────────────────────────────────────────────────────────────────

core::Result<std::unique_ptr<ColorGradingFeature>> ColorGradingFeature::create(std::shared_ptr<PostContext> context)
{
    if (context == nullptr)
    {
        return makeError(ErrorCode::kInvalidArgument, "ColorGradingFeature::create: null context");
    }
    std::unique_ptr<ColorGradingFeature> feature(new ColorGradingFeature(std::move(context)));
    auto program = feature->context_->fullscreenProgram("color_grading.frag");
    if (!program)
    {
        return std::unexpected(program.error());
    }
    feature->program_ = *program;
    if (auto r = feature->setLut(identityLut(16)); !r)
    {
        return std::unexpected(r.error());
    }
    return feature;
}

ColorGradingFeature::~ColorGradingFeature()
{
    context_->device().destroy(lut_);
    context_->device().destroy(pendingLut_);
}

void ColorGradingFeature::setStrength(float strength) noexcept { strength_ = std::clamp(strength, 0.0f, 1.0f); }

core::Result<> ColorGradingFeature::setLut(const ColorLut &lut)
{
    if (!lut.valid())
    {
        return makeError(ErrorCode::kInvalidArgument, "ColorGradingFeature::setLut: the LUT needs size^3 colors (size >= 2)");
    }
    // Toutes les composantes dans [0, 1] et finies, avant toute création : la LUT précédente reste intacte sinon.
    const bool inRange = std::ranges::all_of(lut.values, [](const glm::vec3 &v)
                                             { return std::isfinite(v.r) && std::isfinite(v.g) && std::isfinite(v.b) && glm::all(glm::greaterThanEqual(v, glm::vec3(0.0f))) &&
                                                      glm::all(glm::lessThanEqual(v, glm::vec3(1.0f))); });
    if (!inRange)
    {
        return makeError(ErrorCode::kInvalidArgument, "ColorGradingFeature::setLut: every LUT component must be finite and in [0, 1]");
    }
    rhi::Device &device = context_->device();
    rhi::TextureDesc desc;
    desc.dimension = rhi::TextureDimension::k3D;
    desc.format = rhi::Format::kRGBA16Float;
    desc.size = {lut.size, lut.size, lut.size};
    desc.usage = rhi::TextureUsage::kSampled | rhi::TextureUsage::kCopyDst;
    desc.debugName = "post.color-grading.lut";
    auto texture = device.createTexture(desc);
    if (!texture)
    {
        return std::unexpected(texture.error());
    }
    // Rouge le plus rapide, puis vert (lignes), puis bleu (tranches) : x = r, y = g, z = b à l'échantillonnage.
    std::vector<std::uint16_t> halves;
    halves.reserve(lut.values.size() * 4);
    for (const glm::vec3 &v : lut.values)
    {
        halves.push_back(glm::packHalf1x16(v.r));
        halves.push_back(glm::packHalf1x16(v.g));
        halves.push_back(glm::packHalf1x16(v.b));
        halves.push_back(glm::packHalf1x16(1.0f));
    }
    rhi::TextureCopy region;
    region.size = {lut.size, lut.size, lut.size};
    if (auto r = device.updateTexture(*texture, region, std::as_bytes(std::span(halves))); !r)
    {
        device.destroy(*texture);
        return r;
    }
    // Le remplacement est différé au prochain declare() : une passe déjà déclarée garde sa texture jusqu'à son exécution.
    device.destroy(pendingLut_);
    pendingLut_ = *texture;
    lutSize_ = lut.size;
    return {};
}

core::Result<> ColorGradingFeature::declare(RenderGraphBuilder &builder)
{
    RenderGraph &graph = builder.graph();
    const RgTexture scene = builder.resources().sceneColor;
    const RgTexture out = graph.createTexture("post.color-grading.out", {.format = kPostFormat, .size = builder.resources().renderSize});
    if (pendingLut_)
    {
        // Début d'image : la LUT en attente devient la LUT active ; l'ancienne n'est plus référencée par une passe à exécuter.
        context_->device().destroy(lut_);
        lut_ = pendingLut_;
        pendingLut_ = {};
        activeLutSize_ = lutSize_;
    }
    const float n = static_cast<float>(activeLutSize_);
    const glm::vec4 params((n - 1.0f) / n, 0.5f / n, strength_, 0.0f);
    const rhi::TextureHandle lut = lut_;
    graph
        .addPass(kName, PassType::kRaster,
                 [this, scene, lut, params](RenderPassContext &ctx) -> core::Result<>
                 {
                     auto pipeline = fullscreenPipeline(*program_, kPostFormat);
                     if (!pipeline)
                     {
                         return std::unexpected(pipeline.error());
                     }
                     rhi::BindGroupDesc desc;
                     desc.entries.push_back(
                         {.binding = 0, .type = rhi::BindingType::kSampledTexture, .texture = ctx.texture(scene), .sampler = context_->nearestSampler()});
                     desc.entries.push_back({.binding = 1, .type = rhi::BindingType::kSampledTexture, .texture = lut, .sampler = context_->linearSampler()});
                     desc.debugName = "post.color-grading";
                     auto group = frameGroup(desc);
                     if (!group)
                     {
                         return std::unexpected(group.error());
                     }
                     ctx.encoder().setPipeline(*pipeline);
                     ctx.encoder().setBindGroup(1, *group);
                     ctx.encoder().setPushConstants(asBytes(params));
                     ctx.encoder().draw(3);
                     return {};
                 })
        .read(scene)
        .writeColor(out, rhi::LoadOp::kDontCare);
    declareCopyToScene(builder, "post.color-grading.copy", out);
    return {};
}

// ── VignetteFeature (REN-18) ────────────────────────────────────────────────────────────────────────────────────────

core::Result<std::unique_ptr<VignetteFeature>> VignetteFeature::create(std::shared_ptr<PostContext> context)
{
    if (context == nullptr)
    {
        return makeError(ErrorCode::kInvalidArgument, "VignetteFeature::create: null context");
    }
    std::unique_ptr<VignetteFeature> feature(new VignetteFeature(std::move(context)));
    auto program = feature->context_->fullscreenProgram("vignette.frag");
    if (!program)
    {
        return std::unexpected(program.error());
    }
    feature->program_ = *program;
    return feature;
}

core::Result<> VignetteFeature::declare(RenderGraphBuilder &builder)
{
    const RgTexture scene = builder.resources().sceneColor;
    const rhi::Extent2D size = builder.resources().renderSize;
    struct Push
    {
        glm::vec4 settings;
        glm::vec4 target;
    } push{{settings_.intensity, settings_.radius, settings_.softness, settings_.roundness},
           {static_cast<float>(size.width) / static_cast<float>(std::max(size.height, 1u)), 0.0f, 0.0f, 0.0f}};
    // Multiplication de la scène : destination × source, alpha inchangé.
    const rhi::BlendState multiply{true, rhi::BlendFactor::kZero, rhi::BlendFactor::kSrcColor, rhi::BlendOp::kAdd,
                                   rhi::BlendFactor::kZero, rhi::BlendFactor::kOne, rhi::BlendOp::kAdd};
    builder.graph()
        .addPass(kName, PassType::kRaster,
                 [this, scene, push, multiply](RenderPassContext &ctx) -> core::Result<>
                 {
                     auto pipeline = fullscreenPipeline(*program_, ctx.desc(scene).format, multiply,
                                                        rhi::ColorWriteMask::kR | rhi::ColorWriteMask::kG | rhi::ColorWriteMask::kB);
                     if (!pipeline)
                     {
                         return std::unexpected(pipeline.error());
                     }
                     ctx.encoder().setPipeline(*pipeline);
                     ctx.encoder().setPushConstants(asBytes(push));
                     ctx.encoder().draw(3);
                     return {};
                 })
        .writeColor(scene, rhi::LoadOp::kLoad); // kLoad : lit la scène laissée par l'effet précédent
    return {};
}

// ── PostEffects ─────────────────────────────────────────────────────────────────────────────────────────────────────

bool PostEffects::hdrActive() const noexcept
{
    const PostEffect *effects[] = {autoExposure, bloom, tonemap, colorGrading, vignette};
    return std::any_of(std::begin(effects), std::end(effects), [](const PostEffect *e) { return e != nullptr && e->enabled() && e->needsHdr(); });
}

core::Result<> PostEffects::sync(SceneRenderer &renderer) const
{
    return renderer.setSceneColorFormat(hdrActive() ? rhi::Format::kRGBA16Float : rhi::Format::kRGBA8Unorm);
}

void PostEffects::disableAll() noexcept
{
    for (PostEffect *e : std::array<PostEffect *, 5>{autoExposure, bloom, tonemap, colorGrading, vignette})
    {
        if (e != nullptr)
        {
            e->setEnabled(false);
        }
    }
}

core::Result<PostEffects> addPostEffects(SceneRenderer &renderer, rhi::Device &device, ShaderLibrary &library, PipelineCache &pipelines,
                                         const std::filesystem::path &shaderDirectory)
{
    auto context = PostContext::create(device, library, pipelines, shaderDirectory);
    if (!context)
    {
        return std::unexpected(context.error());
    }
    // Tout est créé avant d'enregistrer quoi que ce soit : un échec ne laisse aucun effet à moitié installé.
    std::unique_ptr<AutoExposureFeature> autoExposure;
    if (AutoExposureFeature::supportedBy(device.capabilities())) // capacités lues (règle 11) : sinon pas d'exposition automatique, exposition unitaire
    {
        auto created = AutoExposureFeature::create(*context);
        if (!created)
        {
            return std::unexpected(created.error());
        }
        autoExposure = std::move(*created);
    }
    auto bloom = BloomFeature::create(*context);
    if (!bloom)
    {
        return std::unexpected(bloom.error());
    }
    auto tonemap = TonemapFeature::create(*context);
    if (!tonemap)
    {
        return std::unexpected(tonemap.error());
    }
    auto grading = ColorGradingFeature::create(*context);
    if (!grading)
    {
        return std::unexpected(grading.error());
    }
    auto vignette = VignetteFeature::create(*context);
    if (!vignette)
    {
        return std::unexpected(vignette.error());
    }

    PostEffects effects;
    std::vector<std::string> added;
    // Enregistre un effet désactivé ; en cas de refus, retire ceux déjà enregistrés.
    auto add = [&]<typename T>(std::unique_ptr<T> feature, T *&slot) -> core::Result<>
    {
        if (feature == nullptr)
        {
            return {};
        }
        feature->setEnabled(false);
        const std::string name = feature->name();
        auto registered = renderer.addFeature(std::move(feature));
        if (!registered)
        {
            for (const std::string &n : added)
            {
                renderer.removeFeature(n);
            }
            return std::unexpected(registered.error());
        }
        added.push_back(name);
        slot = static_cast<T *>(*registered);
        return {};
    };
    if (auto r = add(std::move(autoExposure), effects.autoExposure); !r)
    {
        return std::unexpected(r.error());
    }
    if (auto r = add(std::move(*bloom), effects.bloom); !r)
    {
        return std::unexpected(r.error());
    }
    if (auto r = add(std::move(*tonemap), effects.tonemap); !r)
    {
        return std::unexpected(r.error());
    }
    if (auto r = add(std::move(*grading), effects.colorGrading); !r)
    {
        return std::unexpected(r.error());
    }
    if (auto r = add(std::move(*vignette), effects.vignette); !r)
    {
        return std::unexpected(r.error());
    }
    return effects;
}

} // namespace orosmo::renderer
