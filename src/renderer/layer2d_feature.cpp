#include "orosmo/renderer/layer2d_feature.hpp"

namespace orosmo::renderer
{

using core::ErrorCode;
using core::makeError;

core::Result<std::unique_ptr<Layer2DFeature>> Layer2DFeature::create(std::string name, Layer2DMode mode, rhi::Device &device, ShaderLibrary &library,
                                                                     PipelineCache &pipelines, const std::filesystem::path &shaderDirectory)
{
    auto renderer = Renderer2D::create(device, library, pipelines, shaderDirectory);
    if (!renderer)
    {
        return std::unexpected(renderer.error());
    }
    std::unique_ptr<Layer2DFeature> feature(new Layer2DFeature());
    feature->name_ = std::move(name);
    feature->mode_ = mode;
    feature->device_ = &device;
    feature->renderer_ = std::move(*renderer);
    return feature;
}

core::Result<> Layer2DFeature::prepare(const FrameData &frame)
{
    viewProj_ = frame.viewProj;
    // Rendu relatif à la caméra (REN-02) : `viewProj` attend des positions relatives à la caméra. Origine de l'espace des shaders
    // en coordonnées monde : caméra en rendu relatif, 0 sinon (FrameData : cameraWorld - cameraRelativeOrigin).
    // En double : la partie basse de la caméra (cameraWorldLow) se perdrait dans une somme en float loin de l'origine.
    shaderOrigin_ = glm::dvec3(glm::vec3(frame.cameraWorldHigh)) + glm::dvec3(glm::vec3(frame.cameraWorldLow)) - glm::dvec3(glm::vec3(frame.cameraRelativeOrigin));
    return {};
}

std::optional<std::uint64_t> Layer2DFeature::passSignature() const
{
    std::uint64_t signature = mixPassSignature(0, static_cast<std::uint64_t>(mode_));
    if (mode_ == Layer2DMode::kTexture)
    {
        signature = mixPassSignature(signature, texture());
        signature = mixPassSignature(signature, target_.valid() ? (std::uint64_t{target_.width()} << 32) | target_.height() : 0);
    }
    return signature;
}

core::Result<> Layer2DFeature::setTextureSize(std::uint32_t width, std::uint32_t height)
{
    if (width == 0 || height == 0)
    {
        return makeError(ErrorCode::kInvalidArgument, "Layer2DFeature::setTextureSize: empty size");
    }
    if (target_.valid())
    {
        return target_.resize(width, height);
    }
    auto created = RenderTarget::create(*device_, {.size = {width, height}, .colorFormats = {rhi::Format::kRGBA8Unorm}, .depth = false, .debugName = name_});
    if (!created)
    {
        return std::unexpected(created.error());
    }
    target_ = std::move(*created);
    return {};
}

core::Result<> Layer2DFeature::declarePasses(RenderGraphBuilder &builder)
{
    RenderGraph &graph = builder.graph();
    const RenderGraphBuilder::Resources &known = builder.resources();
    switch (mode_)
    {
    case Layer2DMode::kWorld:
    {
        if (!known.sceneDepth.valid())
        {
            return makeError(ErrorCode::kInvalidArgument, "Layer2DFeature(world): the scene has no depth");
        }
        const RgTexture color = known.sceneColor;
        const RgTexture depth = known.sceneDepth;
        graph
            .addPass(name_, PassType::kGeneric,
                     [this, color, depth](RenderPassContext &ctx) -> core::Result<>
                     {
                         const RgTextureDesc &desc = ctx.desc(color);
                         Render2DOptions options;
                         options.loadExisting = true;
                         options.depth = ctx.texture(depth);
                         options.depthFormat = ctx.desc(depth).format;
                         // Le plan est ramené côté CPU dans le repère des maillages (translation - origine) : aucune coordonnée
                         // monde absolue n'est envoyée au shader.
                         glm::mat4 plane = plane_;
                         plane[3] = glm::vec4(glm::vec3(glm::dvec3(plane[3]) - shaderOrigin_), plane[3].w);
                         options.viewProjection = viewProj_ * plane;
                         ++passesRun_;
                         return renderer_->render(ctx.encoder(), ctx.texture(color), desc.format, desc.size, options);
                     })
            .read(depth)
            .write(color);
        break;
    }
    case Layer2DMode::kOverlay:
    {
        if (!known.output.valid())
        {
            return makeError(ErrorCode::kInvalidArgument, "Layer2DFeature(overlay): the frame has no output texture");
        }
        const RgTexture output = known.output;
        graph
            .addPass(name_, PassType::kGeneric,
                     [this, output](RenderPassContext &ctx) -> core::Result<>
                     {
                         const RgTextureDesc &desc = ctx.desc(output);
                         Render2DOptions options;
                         options.loadExisting = true;
                         ++passesRun_;
                         return renderer_->render(ctx.encoder(), ctx.texture(output), desc.format, desc.size, options);
                     })
            .read(output)
            .write(output);
        break;
    }
    case Layer2DMode::kTexture:
    {
        if (!target_.valid())
        {
            return makeError(ErrorCode::kInvalidArgument, "Layer2DFeature(texture): setTextureSize was not called");
        }
        const RgTexture texture = graph.importTexture(name_ + ".texture", target_.color(0), {.format = rhi::Format::kRGBA8Unorm, .size = {target_.width(), target_.height()}});
        graph
            .addPass(name_, PassType::kGeneric,
                     [this](RenderPassContext &ctx) -> core::Result<>
                     {
                         Render2DOptions options;
                         options.loadExisting = false;
                         options.clearColor = clear_;
                         ++passesRun_;
                         return renderer_->render(ctx.encoder(), target_.color(0), rhi::Format::kRGBA8Unorm, {target_.width(), target_.height()}, options);
                     })
            .write(texture);
        builder.addSceneInput(texture); // la scène lit la texture : cette passe s'exécute avant elle et n'est jamais écartée
        break;
    }
    }
    return {};
}

} // namespace orosmo::renderer
