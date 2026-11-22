// R2D-06 : plusieurs vues. Une `SceneView` est une caméra 3D avec son propre `SceneRenderer`, qui rend vers une texture à elle (taille
// de la zone d'affichage). Les vues se placent ensuite dans des zones de la destination (écran partagé, incrustation) par le plugin
// `ViewsFeature`, ou s'affichent dans l'interface (`CanvasRenderer::registerExternalImage` : vue 3D incrustée dans un widget).
// Les lumières (`SceneRenderer::setLights`) et les maillages peuvent être partagés entre vues ; chaque vue a sa caméra, son échelle de
// rendu et ses plugins (`renderer().addFeature`).
//
// Ordre sur l'encodeur : les vues sont rendues (`beginFrame` ... `endFrame` de la vue) AVANT l'`endFrame` de l'image principale, qui
// compose leurs textures (passe « views »).
#pragma once

#include "orosmo/core/error.hpp"
#include "orosmo/renderer/camera.hpp"
#include "orosmo/renderer/fullscreen_blit.hpp"
#include "orosmo/renderer/pipeline_cache.hpp"
#include "orosmo/renderer/render_feature.hpp"
#include "orosmo/renderer/render_target.hpp"
#include "orosmo/renderer/scene_renderer.hpp"
#include "orosmo/renderer/shader_library.hpp"

#include <filesystem>
#include <memory>
#include <vector>

namespace orosmo::renderer
{

// Zone de la destination, en pixels, origine en haut à gauche.
struct ViewRect
{
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    friend bool operator==(const ViewRect &, const ViewRect &) = default;
};

class SceneView
{
  public:
    static core::Result<std::unique_ptr<SceneView>> create(rhi::Device &device, ShaderLibrary &library, PipelineCache &pipelines,
                                                           const std::filesystem::path &shaderDirectory, std::uint32_t width, std::uint32_t height);
    SceneRenderer &renderer() noexcept { return *renderer_; }
    Camera &camera() noexcept { return camera_; }

    // Nouvelle taille de la zone : texture recréée, proportions de la caméra mises à jour. Le contenu est perdu.
    core::Result<> resize(std::uint32_t width, std::uint32_t height);
    std::uint32_t width() const noexcept { return target_.width(); }
    std::uint32_t height() const noexcept { return target_.height(); }

    // Rend la vue : beginFrame, setCamera, `draw(renderer, camera)` (dépôt des dessins : drawMesh...), endFrame vers la texture de la vue.
    template <typename Draw> core::Result<> render(rhi::CommandEncoder &encoder, const FrameOptions &options, Draw &&draw)
    {
        if (auto r = renderer_->beginFrame(encoder, options); !r)
        {
            return r;
        }
        if (auto r = renderer_->setCamera(camera_); !r)
        {
            return r;
        }
        if (auto r = draw(*renderer_, camera_); !r)
        {
            return r;
        }
        return renderer_->endFrame({target_.color(0), target_.info().colorFormats[0], {target_.width(), target_.height()}});
    }

    rhi::TextureHandle texture() const noexcept { return target_.color(0); }
    rhi::SamplerHandle sampler() const noexcept { return target_.sampler(); }

  private:
    SceneView() = default;
    std::unique_ptr<SceneRenderer> renderer_;
    RenderTarget target_;
    Camera camera_;
};

// Une vue (texture) placée dans une zone de la destination.
struct ViewPlacement
{
    rhi::TextureHandle texture;
    rhi::SamplerHandle sampler;
    ViewRect rect;
};

// Plugin : compose les vues placées dans la sortie de l'image (après la mise à l'échelle). Les placements valent pour UNE image
// (vidés par la passe). Une zone qui dépasse la sortie est découpée par la destination ; une zone vide est ignorée.
class ViewsFeature final : public RenderFeature
{
  public:
    static constexpr const char *kName = "views";
    static core::Result<std::unique_ptr<ViewsFeature>> create(rhi::Device &device, ShaderLibrary &library, PipelineCache &pipelines,
                                                              const std::filesystem::path &shaderDirectory);
    std::string name() const override { return kName; }
    core::Result<> declarePasses(RenderGraphBuilder &builder) override;

    void place(const ViewPlacement &placement) { placements_.push_back(placement); }
    void place(const SceneView &view, const ViewRect &rect) { placements_.push_back({view.texture(), view.sampler(), rect}); }
    std::size_t placementCount() const noexcept { return placements_.size(); }

  private:
    ViewsFeature() = default;
    std::unique_ptr<FullscreenBlit> blit_;
    std::vector<ViewPlacement> placements_;
};

} // namespace orosmo::renderer
