// Copie d'une texture sur toute la cible d'une passe par un triangle plein écran (remplace les anciens blits de framebuffer :
// le RHI n'a pas de commande de blit). Filtrage = celui de l'échantillonneur donné.
#pragma once

#include "orosmo/core/error.hpp"
#include "orosmo/renderer/pipeline_cache.hpp"
#include "orosmo/renderer/shader_library.hpp"
#include "orosmo/rhi/device.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <utility>

namespace orosmo::renderer
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

    // À l'intérieur d'une passe dont l'unique cible couleur a le format `destinationFormat`. Respecte le viewport et le
    // découpage (ciseau) ACTIFS de la passe : seule la zone qu'ils laissent est remplie. `premultiplied` : compose une source à
    // couleur prémultipliée par-dessus la cible (1, 1 - a) au lieu de la remplacer.
    core::Result<> draw(rhi::CommandEncoder &encoder, rhi::TextureHandle source, rhi::SamplerHandle sampler, rhi::Format destinationFormat,
                        bool premultiplied = false);
    // Passe complète : ouvre une passe sur `destination` (contenu précédent ignoré, viewport et ciseau = toute la cible), copie,
    // la referme.
    core::Result<> copy(rhi::CommandEncoder &encoder, rhi::TextureHandle source, rhi::SamplerHandle sampler, rhi::TextureHandle destination,
                        rhi::Format destinationFormat, const std::string &passName = "blit");
    // Passe complète qui GARDE le contenu de `destination` et y compose `source` (couleur prémultipliée, ex. une interface
    // rendue sur fond transparent), étirée à toute la cible.
    core::Result<> compose(rhi::CommandEncoder &encoder, rhi::TextureHandle source, rhi::SamplerHandle sampler, rhi::TextureHandle destination,
                           rhi::Format destinationFormat, const std::string &passName = "compose");

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

} // namespace orosmo::renderer
