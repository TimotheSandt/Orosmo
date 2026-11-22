#include "orosmo/renderer/scene_view.hpp"

#include <algorithm>

namespace orosmo::renderer
{

using core::ErrorCode;
using core::makeError;

core::Result<std::unique_ptr<SceneView>> SceneView::create(rhi::Device &device, ShaderLibrary &library, PipelineCache &pipelines,
                                                           const std::filesystem::path &shaderDirectory, std::uint32_t width, std::uint32_t height)
{
    if (width == 0 || height == 0)
    {
        return makeError(ErrorCode::kInvalidArgument, "SceneView::create: empty size");
    }
    auto renderer = SceneRenderer::create(device, library, pipelines, {.shaderDirectory = shaderDirectory});
    if (!renderer)
    {
        return std::unexpected(renderer.error());
    }
    auto target = RenderTarget::create(device, {.size = {width, height}, .colorFormats = {rhi::Format::kRGBA8Unorm}, .depth = false, .debugName = "scene-view"});
    if (!target)
    {
        return std::unexpected(target.error());
    }
    std::unique_ptr<SceneView> view(new SceneView());
    view->renderer_ = std::move(*renderer);
    view->target_ = std::move(*target);
    view->renderer_->setOutputResolution(static_cast<int>(width), static_cast<int>(height));
    view->camera_.setAspect(width, height);
    return view;
}

core::Result<> SceneView::resize(std::uint32_t width, std::uint32_t height)
{
    if (width == 0 || height == 0)
    {
        return makeError(ErrorCode::kInvalidArgument, "SceneView::resize: empty size");
    }
    if (auto r = target_.resize(width, height); !r)
    {
        return r;
    }
    renderer_->setOutputResolution(static_cast<int>(width), static_cast<int>(height));
    camera_.setAspect(width, height);
    return {};
}

core::Result<std::unique_ptr<ViewsFeature>> ViewsFeature::create(rhi::Device &device, ShaderLibrary &library, PipelineCache &pipelines,
                                                                 const std::filesystem::path &shaderDirectory)
{
    auto blit = FullscreenBlit::create(device, library, pipelines, shaderDirectory);
    if (!blit)
    {
        return std::unexpected(blit.error());
    }
    std::unique_ptr<ViewsFeature> feature(new ViewsFeature());
    feature->blit_ = std::move(*blit);
    return feature;
}

core::Result<> ViewsFeature::declarePasses(RenderGraphBuilder &builder)
{
    const RgTexture output = builder.resources().output;
    if (placements_.empty())
    {
        return {};
    }
    if (!output.valid())
    {
        placements_.clear();
        return makeError(ErrorCode::kInvalidArgument, "ViewsFeature: the frame has no output texture");
    }
    builder.graph()
        .addPass("views", PassType::kGeneric,
                 [this, output](RenderPassContext &ctx) -> core::Result<>
                 {
                     const std::vector<ViewPlacement> placements = std::move(placements_);
                     placements_.clear();
                     const RgTextureDesc &desc = ctx.desc(output);
                     rhi::CommandEncoder &e = ctx.encoder();
                     rhi::RenderPassDesc pass;
                     pass.colors.push_back({.texture = ctx.texture(output), .load = rhi::LoadOp::kLoad});
                     pass.debugName = "views";
                     e.beginRenderPass(pass);
                     core::Result<> status;
                     for (const ViewPlacement &p : placements)
                     {
                         // Intersection avec la destination (le ciseau ne peut pas déborder).
                         const std::int64_t x0 = std::max<std::int64_t>(p.rect.x, 0);
                         const std::int64_t y0 = std::max<std::int64_t>(p.rect.y, 0);
                         const std::int64_t x1 = std::min<std::int64_t>(static_cast<std::int64_t>(p.rect.x) + p.rect.width, desc.size.width);
                         const std::int64_t y1 = std::min<std::int64_t>(static_cast<std::int64_t>(p.rect.y) + p.rect.height, desc.size.height);
                         if (p.rect.width == 0 || p.rect.height == 0 || x1 <= x0 || y1 <= y0 || p.texture.isNull())
                         {
                             continue;
                         }
                         e.setViewport({static_cast<float>(p.rect.x), static_cast<float>(p.rect.y), static_cast<float>(p.rect.width),
                                        static_cast<float>(p.rect.height), 0.0f, 1.0f});
                         e.setScissor({static_cast<std::int32_t>(x0), static_cast<std::int32_t>(y0), static_cast<std::uint32_t>(x1 - x0),
                                       static_cast<std::uint32_t>(y1 - y0)});
                         if (auto r = blit_->draw(e, p.texture, p.sampler, desc.format); !r)
                         {
                             status = r;
                             break;
                         }
                     }
                     e.endRenderPass();
                     return status;
                 })
        .read(output)
        .write(output);
    return {};
}

} // namespace orosmo::renderer
