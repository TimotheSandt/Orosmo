#include "engine/renderer/fullscreen_blit.hpp"

namespace engine::renderer
{

namespace
{
constexpr std::size_t kMaxCachedGroups = 64;
std::uint64_t pack(std::uint32_t index, std::uint32_t generation) { return (static_cast<std::uint64_t>(generation) << 32) | index; }
} // namespace

core::Result<std::unique_ptr<FullscreenBlit>> FullscreenBlit::create(rhi::Device &device, ShaderLibrary &library, PipelineCache &pipelines,
                                                                     const std::filesystem::path &shaderDirectory)
{
    std::unique_ptr<FullscreenBlit> blit(new FullscreenBlit(device, pipelines));
    auto program = library.loadGraphics(shaderDirectory / "blit.vert", shaderDirectory / "blit.frag");
    if (!program)
    {
        return std::unexpected(program.error());
    }
    blit->program_ = *program;
    rhi::SamplerDesc sd;
    sd.mipFilter = std::nullopt;
    sd.addressU = rhi::AddressMode::kClampToEdge;
    sd.addressV = rhi::AddressMode::kClampToEdge;
    sd.debugName = "blit.linear";
    auto linear = device.createSampler(sd);
    if (!linear)
    {
        return std::unexpected(linear.error());
    }
    blit->linear_ = *linear;
    sd.minFilter = rhi::Filter::kNearest;
    sd.magFilter = rhi::Filter::kNearest;
    sd.debugName = "blit.nearest";
    auto nearest = device.createSampler(sd);
    if (!nearest)
    {
        return std::unexpected(nearest.error());
    }
    blit->nearest_ = *nearest;
    return blit;
}

FullscreenBlit::~FullscreenBlit()
{
    for (const auto &[key, group] : groups_)
    {
        device_.destroy(group);
    }
    device_.destroy(linear_);
    device_.destroy(nearest_);
}

core::Result<rhi::BindGroupHandle> FullscreenBlit::groupFor(rhi::TextureHandle source, rhi::SamplerHandle sampler)
{
    const auto key = std::make_pair(pack(source.index, source.generation), pack(sampler.index, sampler.generation));
    if (auto it = groups_.find(key); it != groups_.end())
    {
        return it->second;
    }
    if (groups_.size() >= kMaxCachedGroups)
    {
        // Les textures redimensionnées laissent des entrées périmées : on repart de zéro plutôt que de les suivre une à une.
        for (const auto &[k, group] : groups_)
        {
            device_.destroy(group);
        }
        groups_.clear();
    }
    rhi::BindGroupDesc desc;
    desc.entries.push_back({.binding = 0, .type = rhi::BindingType::kSampledTexture, .texture = source, .sampler = sampler});
    desc.debugName = "blit.source";
    auto group = device_.createBindGroup(desc);
    if (!group)
    {
        return std::unexpected(group.error());
    }
    groups_.emplace(key, *group);
    return *group;
}

core::Result<> FullscreenBlit::draw(rhi::CommandEncoder &encoder, rhi::TextureHandle source, rhi::SamplerHandle sampler, rhi::Format destinationFormat)
{
    GraphicsState state;
    state.raster.cull = rhi::CullMode::kNone;
    state.colorTargets.assign(1, rhi::ColorTargetState{.format = destinationFormat});
    state.debugName = "blit";
    auto pipeline = pipelines_.graphics(*program_, state);
    if (!pipeline)
    {
        return std::unexpected(pipeline.error());
    }
    auto group = groupFor(source, sampler);
    if (!group)
    {
        return std::unexpected(group.error());
    }
    encoder.setPipeline(*pipeline);
    encoder.setBindGroup(1, *group);
    encoder.draw(3);
    return {};
}

core::Result<> FullscreenBlit::copy(rhi::CommandEncoder &encoder, rhi::TextureHandle source, rhi::SamplerHandle sampler, rhi::TextureHandle destination,
                                    rhi::Format destinationFormat, const std::string &passName)
{
    rhi::RenderPassDesc pass;
    pass.colors.push_back({.texture = destination, .load = rhi::LoadOp::kDontCare});
    pass.debugName = passName;
    encoder.beginRenderPass(pass);
    auto r = draw(encoder, source, sampler, destinationFormat);
    encoder.endRenderPass();
    return r;
}

} // namespace engine::renderer
