#include "engine/renderer/pipeline_cache.hpp"

#include <type_traits>

namespace engine::renderer
{

namespace
{
template <typename T> void put(std::string &key, const T &value)
{
    static_assert(std::is_trivially_copyable_v<T>);
    key.append(reinterpret_cast<const char *>(&value), sizeof(T));
}
template <typename T> void putAll(std::string &key, const std::vector<T> &values)
{
    put(key, values.size());
    for (const T &v : values)
    {
        put(key, v);
    }
}
// Les structures du RHI ont des octets de remplissage : on sérialise champ par champ, jamais l'objet entier.
void putBlend(std::string &key, const rhi::BlendState &b)
{
    put(key, b.enabled);
    put(key, b.srcColor);
    put(key, b.dstColor);
    put(key, b.colorOp);
    put(key, b.srcAlpha);
    put(key, b.dstAlpha);
    put(key, b.alphaOp);
}
} // namespace

void PipelineCache::dropStale(const ShaderProgram &program)
{
    for (auto it = entries_.begin(); it != entries_.end();)
    {
        if (it->second.programId == program.id() && it->second.revision != program.revision())
        {
            device_.destroy(it->second.pipeline);
            it = entries_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

core::Result<rhi::PipelineHandle> PipelineCache::graphics(const ShaderProgram &program, const GraphicsState &state)
{
    if (program.isCompute())
    {
        return core::makeError(core::ErrorCode::kInvalidArgument, "PipelineCache::graphics: '" + program.name() + "' is a compute program");
    }
    dropStale(program);
    std::string key = "g";
    put(key, program.id());
    put(key, program.revision());
    put(key, state.vertexBuffers.size());
    for (const auto &b : state.vertexBuffers)
    {
        put(key, b.binding);
        put(key, b.stride);
        put(key, b.stepRate);
    }
    put(key, state.vertexAttributes.size());
    for (const auto &a : state.vertexAttributes)
    {
        put(key, a.location);
        put(key, a.binding);
        put(key, a.format);
        put(key, a.offset);
    }
    put(key, state.topology);
    put(key, state.raster.fill);
    put(key, state.raster.cull);
    put(key, state.raster.frontFace);
    put(key, state.depth.test);
    put(key, state.depth.write);
    put(key, state.depth.compare);
    put(key, state.colorTargets.size());
    for (const auto &t : state.colorTargets)
    {
        put(key, t.format);
        putBlend(key, t.blend);
        put(key, t.writeMask);
    }
    put(key, state.depthFormat);

    if (auto it = entries_.find(key); it != entries_.end())
    {
        ++hits_;
        return it->second.pipeline;
    }
    ++misses_;
    rhi::GraphicsPipelineDesc desc;
    desc.vertex = program.vertex();
    desc.fragment = program.fragment();
    desc.vertexBuffers = state.vertexBuffers;
    desc.vertexAttributes = state.vertexAttributes;
    desc.topology = state.topology;
    desc.raster = state.raster;
    desc.depth = state.depth;
    desc.colorTargets = state.colorTargets;
    desc.depthFormat = state.depthFormat;
    desc.debugName = state.debugName.empty() ? program.name() : state.debugName;
    auto pipeline = device_.createGraphicsPipeline(desc);
    if (!pipeline)
    {
        return std::unexpected(pipeline.error());
    }
    entries_.emplace(std::move(key), Entry{*pipeline, program.id(), program.revision()});
    return *pipeline;
}

core::Result<rhi::PipelineHandle> PipelineCache::compute(const ShaderProgram &program)
{
    if (!program.isCompute())
    {
        return core::makeError(core::ErrorCode::kInvalidArgument, "PipelineCache::compute: '" + program.name() + "' is not a compute program");
    }
    dropStale(program);
    std::string key = "c";
    put(key, program.id());
    put(key, program.revision());
    if (auto it = entries_.find(key); it != entries_.end())
    {
        ++hits_;
        return it->second.pipeline;
    }
    ++misses_;
    rhi::ComputePipelineDesc desc;
    desc.compute = program.compute();
    desc.debugName = program.name();
    auto pipeline = device_.createComputePipeline(desc);
    if (!pipeline)
    {
        return std::unexpected(pipeline.error());
    }
    entries_.emplace(std::move(key), Entry{*pipeline, program.id(), program.revision()});
    return *pipeline;
}

void PipelineCache::clear()
{
    for (auto &[key, entry] : entries_)
    {
        device_.destroy(entry.pipeline);
    }
    entries_.clear();
}

} // namespace engine::renderer
