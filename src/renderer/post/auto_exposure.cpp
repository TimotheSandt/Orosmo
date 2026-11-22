// REN-18 : exposition automatique. Une passe générique en trois dispatchs compute : remise à zéro de l'histogramme, histogramme
// de la luminance de la scène (256 cases en log2, tuiles 16×16 en mémoire partagée), moyenne géométrique + exposition visée +
// adaptation. Le tampon d'exposition persiste d'une image à l'autre (adaptation) ; il est importé dans le graphe sous le nom
// `post.exposure`, que le tonemapping lit s'il le trouve.
#include "orosmo/renderer/post_effects.hpp"

#include <glm/glm.hpp>

#include <cmath>
#include <cstring>

namespace orosmo::renderer
{

using core::ErrorCode;
using core::makeError;

namespace
{
constexpr std::uint64_t kHistogramBytes = kHistogramBins * sizeof(std::uint32_t);
constexpr std::uint64_t kExposureBytes = sizeof(glm::vec4);
constexpr std::uint32_t kTile = 16;

template <typename T> std::span<const std::byte> asBytes(const T &value) { return std::as_bytes(std::span(&value, 1)); }
} // namespace

bool AutoExposureFeature::supportedBy(const rhi::Capabilities &capabilities) noexcept
{
    return capabilities.compute && capabilities.maxComputeInvocations >= 256 && capabilities.maxComputeWorkGroupSize[0] >= 256 &&
           capabilities.maxComputeWorkGroupSize[1] >= 16;
}

core::Result<std::unique_ptr<AutoExposureFeature>> AutoExposureFeature::create(std::shared_ptr<PostContext> context)
{
    if (context == nullptr)
    {
        return makeError(ErrorCode::kInvalidArgument, "AutoExposureFeature::create: null context");
    }
    if (!supportedBy(context->device().capabilities()))
    {
        return makeError(ErrorCode::kUnsupported, "AutoExposureFeature::create: the device lacks compute or 256-invocation work groups");
    }
    std::unique_ptr<AutoExposureFeature> feature(new AutoExposureFeature(std::move(context)));
    PostContext &ctx = *feature->context_;
    for (auto [file, slot] : {std::pair{"histogram_clear.comp", &feature->clear_}, std::pair{"histogram.comp", &feature->histogram_},
                              std::pair{"exposure.comp", &feature->average_}})
    {
        auto program = ctx.computeProgram(file);
        if (!program)
        {
            return std::unexpected(program.error());
        }
        *slot = *program;
    }
    rhi::BufferDesc desc;
    desc.size = kHistogramBytes;
    desc.usage = rhi::BufferUsage::kStorage | rhi::BufferUsage::kCopySrc | rhi::BufferUsage::kCopyDst;
    desc.debugName = AutoExposureFeature::kHistogramBuffer;
    const std::vector<std::byte> zeros(kHistogramBytes, std::byte{0});
    auto histogram = ctx.device().createBuffer(desc, zeros);
    if (!histogram)
    {
        return std::unexpected(histogram.error());
    }
    feature->histogramBuffer_ = *histogram;
    desc.size = kExposureBytes;
    desc.usage = rhi::BufferUsage::kStorage | rhi::BufferUsage::kUniform | rhi::BufferUsage::kCopySrc | rhi::BufferUsage::kCopyDst;
    desc.debugName = AutoExposureFeature::kExposureBuffer;
    auto exposure = ctx.device().createBuffer(desc, std::span(zeros).first(kExposureBytes)); // w = 0 : pas encore d'exposition
    if (!exposure)
    {
        return std::unexpected(exposure.error());
    }
    feature->exposureBuffer_ = *exposure;
    return feature;
}

AutoExposureFeature::~AutoExposureFeature()
{
    context_->device().destroy(histogramBuffer_);
    context_->device().destroy(exposureBuffer_);
}

core::Result<> AutoExposureFeature::prepare(const FrameData &frame)
{
    deltaSeconds_ = frame.time.y;
    return {};
}

core::Result<> AutoExposureFeature::resetAdaptation()
{
    const glm::vec4 zero(0.0f);
    return context_->device().updateBuffer(exposureBuffer_, 0, asBytes(zero));
}

core::Result<std::vector<std::uint32_t>> AutoExposureFeature::readHistogram() const
{
    auto raw = context_->device().readBuffer(histogramBuffer_, 0, kHistogramBytes);
    if (!raw)
    {
        return std::unexpected(raw.error());
    }
    std::vector<std::uint32_t> bins(kHistogramBins);
    std::memcpy(bins.data(), raw->data(), kHistogramBytes);
    return bins;
}

core::Result<ExposureState> AutoExposureFeature::readExposure() const
{
    auto raw = context_->device().readBuffer(exposureBuffer_, 0, kExposureBytes);
    if (!raw)
    {
        return std::unexpected(raw.error());
    }
    glm::vec4 v;
    std::memcpy(&v, raw->data(), sizeof v);
    return ExposureState{.exposure = v.x, .averageLuminance = v.y, .target = v.z, .valid = v.w > 0.5f};
}

core::Result<> AutoExposureFeature::declare(RenderGraphBuilder &builder)
{
    RenderGraph &graph = builder.graph();
    const RgTexture scene = builder.resources().sceneColor;
    const rhi::Extent2D size = builder.resources().renderSize;
    const RgBuffer histogram = graph.importBuffer(kHistogramBuffer, histogramBuffer_, {.size = kHistogramBytes});
    const RgBuffer exposure = graph.importBuffer(kExposureBuffer, exposureBuffer_, {.size = kExposureBytes});
    const AutoExposureSettings s = settings_;
    const float maxLog2 = std::max(s.maxLog2, s.minLog2 + 1e-3f);
    const glm::vec4 histogramPush(s.minLog2, maxLog2, static_cast<float>(size.width), static_cast<float>(size.height));
    struct AveragePush
    {
        glm::vec4 range;
        glm::vec4 exposure;
    } averagePush{{s.minLog2, maxLog2, adaptationFactor(deltaSeconds_, s.adaptationSpeed), 0.0f},
                  {s.key, std::exp2(s.minEv), std::exp2(std::max(s.maxEv, s.minEv)), 0.0f}};
    graph
        .addPass(kName, PassType::kGeneric,
                 [this, scene, size, histogramPush, averagePush](RenderPassContext &ctx) -> core::Result<>
                 {
                     PipelineCache &pipelines = context_->pipelines();
                     auto clear = pipelines.compute(*clear_);
                     auto build = pipelines.compute(*histogram_);
                     auto average = pipelines.compute(*average_);
                     for (const auto *p : {&clear, &build, &average})
                     {
                         if (!*p)
                         {
                             return std::unexpected(p->error());
                         }
                     }
                     rhi::BindGroupDesc desc;
                     desc.entries.push_back(
                         {.binding = 0, .type = rhi::BindingType::kSampledTexture, .texture = ctx.texture(scene), .sampler = context_->nearestSampler()});
                     desc.entries.push_back({.binding = 1, .type = rhi::BindingType::kStorageBuffer, .buffer = histogramBuffer_});
                     desc.entries.push_back({.binding = 2, .type = rhi::BindingType::kStorageBuffer, .buffer = exposureBuffer_});
                     desc.debugName = "post.auto-exposure";
                     auto group = frameGroup(desc);
                     if (!group)
                     {
                         return std::unexpected(group.error());
                     }
                     rhi::CommandEncoder &e = ctx.encoder();
                     e.bindComputePipeline(*clear);
                     e.setBindGroup(0, *group);
                     e.dispatch(1, 1, 1);
                     e.bindComputePipeline(*build);
                     e.setBindGroup(0, *group);
                     e.setPushConstants(asBytes(histogramPush));
                     e.dispatch((size.width + kTile - 1) / kTile, (size.height + kTile - 1) / kTile, 1);
                     e.bindComputePipeline(*average);
                     e.setBindGroup(0, *group);
                     e.setPushConstants(asBytes(averagePush));
                     e.dispatch(1, 1, 1);
                     return {};
                 })
        .read(scene)
        .write(histogram)
        .write(exposure);
    return {};
}

} // namespace orosmo::renderer
