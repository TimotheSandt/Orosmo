// Copie d'une texture sur toute la cible d'une passe par un triangle plein écran (remplace les anciens blits de framebuffer :
// le RHI n'a pas de commande de blit). Filtrage = celui de l'échantillonneur donné.
#pragma once

#include "engine/core/error.hpp"
#include "engine/renderer/pipeline_cache.hpp"
#include "engine/renderer/shader_library.hpp"
#include "engine/rhi/device.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <utility>

namespace engine::renderer
{

class FullscreenBlit
{
  public:
    // `shaderDirectory` contient `blit.vert` et `blit.frag` (shaders/rhi/).
    static core::Result<std::unique_ptr<FullscreenBlit>> create(rhi::Device &device, ShaderLibrary &library, PipelineCache &pipelines,
                                                                const std::filesystem::path &shaderDirectory);
    ~FullscreenBlit();
    FullscreenBlit(const FullscreenBlit &) = delete;
    FullscreenBlit &operator=(const FullscreenBlit &) = delete;

    // À l'intérieur d'une passe dont l'unique cible couleur a le format `destinationFormat`.
    core::Result<> draw(rhi::CommandEncoder &encoder, rhi::TextureHandle source, rhi::SamplerHandle sampler, rhi::Format destinationFormat);
    // Passe complète : ouvre une passe sur `destination` (contenu précédent ignoré), copie, la referme.
    core::Result<> copy(rhi::CommandEncoder &encoder, rhi::TextureHandle source, rhi::SamplerHandle sampler, rhi::TextureHandle destination,
                        rhi::Format destinationFormat, const std::string &passName = "blit");

    rhi::SamplerHandle linearSampler() const noexcept { return linear_; }
    rhi::SamplerHandle nearestSampler() const noexcept { return nearest_; }

  private:
    FullscreenBlit(rhi::Device &device, PipelineCache &pipelines) : device_(device), pipelines_(pipelines) {}
    core::Result<rhi::BindGroupHandle> groupFor(rhi::TextureHandle source, rhi::SamplerHandle sampler);

    rhi::Device &device_;
    PipelineCache &pipelines_;
    std::shared_ptr<ShaderProgram> program_;
    rhi::SamplerHandle linear_;
    rhi::SamplerHandle nearest_;
    std::map<std::pair<std::uint64_t, std::uint64_t>, rhi::BindGroupHandle> groups_;
};

} // namespace engine::renderer
