// REN-19 : anticrénelage temporel. Une passe raster `aa.taa.resolve` (deux sorties : couleur accumulée et information) puis
// `aa.taa.copy` (recopie dans la scène). L'historique (couleur RGBA16F + information RGBA16F, double tampon) appartient au
// plugin et est importé dans le graphe à chaque image ; il n'est « l'image précédente » que si la résolution a été exécutée à
// l'image précédente, à la même taille (sinon il est ignoré, comme après `resetHistory`).
#include "orosmo/renderer/antialiasing.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <cstring>

namespace orosmo::renderer
{

using core::ErrorCode;
using core::makeError;

namespace
{
constexpr rhi::Format kHistoryFormat = rhi::Format::kRGBA16Float;

template <typename T> std::span<const std::byte> asBytes(const T &value) { return std::as_bytes(std::span(&value, 1)); }
} // namespace

core::Result<std::unique_ptr<TaaFeature>> TaaFeature::create(std::shared_ptr<PostContext> context)
{
    if (context == nullptr)
    {
        return makeError(ErrorCode::kInvalidArgument, "TaaFeature::create: null context");
    }
    std::unique_ptr<TaaFeature> feature(new TaaFeature(std::move(context)));
    auto program = feature->context_->fullscreenProgram("taa_resolve.frag");
    if (!program)
    {
        return std::unexpected(program.error());
    }
    feature->program_ = *program;
    return feature;
}

TaaFeature::~TaaFeature() { destroyTargets(); }

void TaaFeature::destroyTargets() noexcept
{
    for (auto *set : {&color_, &info_})
    {
        for (rhi::TextureHandle &texture : *set)
        {
            if (!texture.isNull())
            {
                context_->device().destroy(texture);
            }
            texture = {};
        }
    }
    size_ = {};
    historyValid_ = false;
}

core::Result<> TaaFeature::ensureTargets(rhi::Extent2D size)
{
    if (size.width == size_.width && size.height == size_.height && !color_[0].isNull())
    {
        return {};
    }
    destroyTargets();
    rhi::TextureDesc desc;
    desc.format = kHistoryFormat;
    desc.size = {size.width, size.height, 1};
    desc.usage = rhi::TextureUsage::kSampled | rhi::TextureUsage::kRenderTarget | rhi::TextureUsage::kCopySrc;
    for (std::size_t i = 0; i < 2; ++i)
    {
        desc.debugName = i == 0 ? "aa.taa.history0" : "aa.taa.history1";
        auto color = context_->device().createTexture(desc);
        if (!color)
        {
            destroyTargets();
            return std::unexpected(color.error());
        }
        color_[i] = *color;
        desc.debugName = i == 0 ? "aa.taa.info0" : "aa.taa.info1";
        auto info = context_->device().createTexture(desc);
        if (!info)
        {
            destroyTargets();
            return std::unexpected(info.error());
        }
        info_[i] = *info;
    }
    size_ = size;
    return {};
}

core::Result<> TaaFeature::prepare(const FrameData &frame)
{
    (void)frame;
    ++prepared_;
    return {};
}

core::Result<> TaaFeature::declare(RenderGraphBuilder &builder)
{
    const RgTexture motion = builder.aov(Aov::kMotion);
    const RgTexture depth = builder.aov(Aov::kLinearDepth);
    if (!motion.valid() || !depth.valid())
    {
        return makeError(ErrorCode::kUnsupported, "TaaFeature: the scene pass has no motion vectors or linear depth (requiredAovs)");
    }
    const rhi::Extent2D size = builder.resources().renderSize;
    if (auto r = ensureTargets(size); !r)
    {
        return r;
    }
    // Historique utilisable : écrit à l'image précédente (aucune image sautée), à cette taille, sans demande de réinitialisation.
    const bool reset = resetRequested_ || !historyValid_ || lastResolved_ + 1 != prepared_;
    const std::uint32_t previous = last_;
    const std::uint32_t next = last_ ^ 1u;
    historyValid_ = false; // seule une résolution exécutée avec succès le revalide

    RenderGraph &graph = builder.graph();
    const RgTexture scene = builder.resources().sceneColor;
    const RgTextureDesc historyDesc{.format = kHistoryFormat, .size = size};
    const RgTexture previousColor = graph.importTexture(kHistoryTexture, color_[previous], historyDesc);
    const RgTexture previousInfo = graph.importTexture(kInfoTexture, info_[previous], historyDesc);
    const RgTexture nextColor = graph.importTexture("aa.taa.history.next", color_[next], historyDesc);
    const RgTexture nextInfo = graph.importTexture("aa.taa.info.next", info_[next], historyDesc);
    struct Push
    {
        glm::vec4 params;
        glm::vec4 flags;
    } push{{static_cast<float>(std::clamp(settings_.maxAccumulatedFrames, 2u, 64u)), std::max(settings_.clipGamma, 0.0f),
            std::max(settings_.depthTolerance, 0.0f), std::max(settings_.motionTolerancePixels, 0.0f)},
           {reset ? 1.0f : 0.0f, settings_.clampHistory ? 1.0f : 0.0f, 0.0f, 0.0f}};
    const std::uint64_t frame = prepared_;
    graph
        .addPass("aa.taa.resolve", PassType::kRaster,
                 [this, scene, motion, depth, previousColor, previousInfo, push, next, frame](RenderPassContext &ctx) -> core::Result<>
                 {
                     GraphicsState state;
                     state.raster.cull = rhi::CullMode::kNone;
                     state.colorTargets.assign(2, rhi::ColorTargetState{.format = kHistoryFormat});
                     state.debugName = program_->name();
                     auto pipeline = context_->pipelines().graphics(*program_, state);
                     if (!pipeline)
                     {
                         return std::unexpected(pipeline.error());
                     }
                     const rhi::SamplerHandle nearest = context_->nearestSampler();
                     rhi::BindGroupDesc desc;
                     desc.entries.push_back({.binding = 0, .type = rhi::BindingType::kSampledTexture, .texture = ctx.texture(scene), .sampler = nearest});
                     desc.entries.push_back({.binding = 1, .type = rhi::BindingType::kSampledTexture, .texture = ctx.texture(motion), .sampler = nearest});
                     desc.entries.push_back({.binding = 2, .type = rhi::BindingType::kSampledTexture, .texture = ctx.texture(depth), .sampler = nearest});
                     desc.entries.push_back({.binding = 3,
                                             .type = rhi::BindingType::kSampledTexture,
                                             .texture = ctx.texture(previousColor),
                                             .sampler = context_->linearSampler()});
                     desc.entries.push_back(
                         {.binding = 4, .type = rhi::BindingType::kSampledTexture, .texture = ctx.texture(previousInfo), .sampler = nearest});
                     desc.debugName = "aa.taa.resolve";
                     auto group = frameGroup(desc);
                     if (!group)
                     {
                         return std::unexpected(group.error());
                     }
                     ctx.encoder().setPipeline(*pipeline);
                     ctx.encoder().setBindGroup(1, *group);
                     ctx.encoder().setPushConstants(asBytes(push));
                     ctx.encoder().draw(3);
                     historyValid_ = true;
                     last_ = next;
                     lastResolved_ = frame;
                     resetRequested_ = false; // transmise à cette image
                     return {};
                 })
        .read(scene)
        .read(motion)
        .read(depth)
        .read(previousColor)
        .read(previousInfo)
        .writeColor(nextColor, rhi::LoadOp::kDontCare)
        .writeColor(nextInfo, rhi::LoadOp::kDontCare);
    declareCopyToScene(builder, "aa.taa.copy", nextColor);
    return {};
}

core::Result<std::vector<std::array<float, 4>>> TaaFeature::readHistoryInfo() const
{
    if (!historyValid_)
    {
        return makeError(ErrorCode::kNotFound, "TaaFeature::readHistoryInfo: no history yet");
    }
    auto raw = context_->device().readTexture(info_[last_]);
    if (!raw)
    {
        return std::unexpected(raw.error());
    }
    std::vector<std::array<float, 4>> out(raw->size() / 8);
    for (std::size_t i = 0; i < out.size(); ++i)
    {
        std::uint16_t h[4];
        std::memcpy(h, raw->data() + i * 8, sizeof h);
        out[i] = {glm::unpackHalf1x16(h[0]), glm::unpackHalf1x16(h[1]), glm::unpackHalf1x16(h[2]), glm::unpackHalf1x16(h[3])};
    }
    return out;
}

} // namespace orosmo::renderer
