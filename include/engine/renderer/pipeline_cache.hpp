// Cache de pipelines en mémoire : un pipeline par (programme, révision, état fixe). Un rechargement à chaud (révision plus
// récente) détruit les pipelines périmés du programme et en recrée à la demande.
#pragma once

#include "engine/core/error.hpp"
#include "engine/renderer/shader_library.hpp"
#include "engine/rhi/device.hpp"

#include <map>
#include <string>
#include <vector>

namespace engine::renderer
{

struct GraphicsState
{
    std::vector<rhi::VertexBufferLayout> vertexBuffers;
    std::vector<rhi::VertexAttribute> vertexAttributes;
    rhi::PrimitiveTopology topology = rhi::PrimitiveTopology::kTriangleList;
    rhi::RasterState raster{};
    rhi::DepthState depth{};
    std::vector<rhi::ColorTargetState> colorTargets{rhi::ColorTargetState{}};
    rhi::Format depthFormat = rhi::Format::kUndefined;
    std::string debugName;
};

class PipelineCache
{
  public:
    explicit PipelineCache(rhi::Device &device) : device_(device) {}
    ~PipelineCache() { clear(); }
    PipelineCache(const PipelineCache &) = delete;
    PipelineCache &operator=(const PipelineCache &) = delete;

    core::Result<rhi::PipelineHandle> graphics(const ShaderProgram &program, const GraphicsState &state);
    core::Result<rhi::PipelineHandle> compute(const ShaderProgram &program);
    // Détruit tous les pipelines (les handles rendus deviennent périmés).
    void clear();

    std::size_t size() const noexcept { return entries_.size(); }
    std::size_t hits() const noexcept { return hits_; }
    std::size_t misses() const noexcept { return misses_; }

  private:
    struct Entry
    {
        rhi::PipelineHandle pipeline;
        std::uint64_t programId = 0;
        std::uint64_t revision = 0;
    };
    void dropStale(const ShaderProgram &program);

    rhi::Device &device_;
    std::map<std::string, Entry> entries_;
    std::size_t hits_ = 0;
    std::size_t misses_ = 0;
};

} // namespace engine::renderer
