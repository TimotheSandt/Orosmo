// Cache de pipelines en mémoire : un pipeline par (programme, état fixe), construit pour la révision courante du programme. Après
// un rechargement à chaud (révision plus récente), le pipeline est reconstruit à la demande ; si la nouvelle révision ne se lie
// pas (étages incompatibles), le dernier pipeline utilisable est gardé et l'échec est conservé dans `lastError()`.
#pragma once

#include "orosmo/core/error.hpp"
#include "orosmo/renderer/shader_library.hpp"
#include "orosmo/rhi/device.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace orosmo::renderer
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
    // Dernier échec de reconstruction après un rechargement (le pipeline précédent a été rendu à la place).
    const std::optional<core::Error> &lastError() const noexcept { return lastError_; }
    // Taille des push constants de la révision qui a CONSTRUIT ce pipeline (après un repli sur l'ancien pipeline, la réflexion
    // courante du programme décrit une autre révision : c'est celle-ci qu'il faut suivre pour envoyer les constantes). 0 si
    // le handle n'est pas dans le cache.
    std::uint32_t pushConstantSize(rhi::PipelineHandle pipeline) const noexcept;

  private:
    struct Entry
    {
        rhi::PipelineHandle pipeline;
        std::uint64_t revision = 0;       // révision du programme pour laquelle `pipeline` a été construit
        std::uint64_t failedRevision = 0; // révision qui n'a pas pu être construite (pas de nouvel essai pour elle)
        std::uint32_t pushConstantSize = 0; // de la révision `revision`
    };
    template <typename Build> core::Result<rhi::PipelineHandle> lookup(std::string key, const ShaderProgram &program, Build &&build);

    rhi::Device &device_;
    std::map<std::string, Entry> entries_;
    std::size_t hits_ = 0;
    std::size_t misses_ = 0;
    std::optional<core::Error> lastError_;
};

} // namespace orosmo::renderer
