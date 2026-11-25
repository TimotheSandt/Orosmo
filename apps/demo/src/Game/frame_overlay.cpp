#include "frame_overlay.hpp"

#include <orosmo/renderer/render_graph.hpp>

namespace demo
{

namespace rd = orosmo::renderer;
namespace rhi = orosmo::rhi;

FrameComposition chooseFrameComposition(const FrameCompositionQuery &query) noexcept
{
    const bool sameSize = query.view == query.backbuffer && query.view.width > 0 && query.view.height > 0;
    if (query.keepComposed || !sameSize || query.backbufferFormat != rhi::Format::kRGBA8Unorm)
    {
        return FrameComposition::kComposedTarget;
    }
    return FrameComposition::kBackbuffer;
}

void FrameOverlay::beginFrame(rhi::TextureHandle backbuffer, FrameComposition composition, bool ui) noexcept
{
    backbuffer_ = backbuffer;
    composition_ = composition;
    ui_ = ui;
    uiDone = false;
    presented = false;
}

orosmo::core::Result<> FrameOverlay::declarePasses(rd::RenderGraphBuilder &builder)
{
    rd::RenderGraph &graph = builder.graph();
    const rd::RgTexture output = builder.resources().output; // le backbuffer ou la cible « composed » (FrameComposition)
    if (ui_)
    {
        graph
            .addPass("ui", rd::PassType::kGeneric,
                     [this, output](rd::RenderPassContext &ctx) -> orosmo::core::Result<>
                     {
                         drawUi_(ctx.encoder(), ctx.texture(output));
                         uiDone = true;
                         return {};
                     })
            .read(output)
            .write(output);
    }
    if (composition_ == FrameComposition::kComposedTarget)
    {
        // Taille sans importance ici (ce n'est pas un attachement déclaré) : la copie étire l'image sur tout le backbuffer.
        const rd::RgTexture backbuffer = graph.importTexture("backbuffer", backbuffer_, {.format = rhi::Format::kRGBA8Unorm, .size = {1, 1}});
        graph
            .addPass("present", rd::PassType::kGeneric,
                     [this, output](rd::RenderPassContext &ctx) -> orosmo::core::Result<>
                     {
                         present_(ctx.encoder(), ctx.texture(output));
                         presented = true;
                         return {};
                     })
            .read(output)
            .write(backbuffer);
    }
    return {};
}

} // namespace demo
