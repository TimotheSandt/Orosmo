// REN-21, REN-41, REN-35 : sorties auxiliaires et modes d'affichage de debug de `SceneRenderer` (séparé de scene_renderer.cpp).
#include "orosmo/renderer/scene_renderer.hpp"

#include <bit>
#include <cmath>

namespace orosmo::renderer
{

using core::ErrorCode;
using core::makeError;

namespace
{

// Plugin des modes « normales » et « mouvement » : lit l'AOV et remplace la couleur de la scène par sa visualisation.
class DebugViewFeature final : public RenderFeature
{
  public:
    DebugViewFeature(rhi::Device &device, PipelineCache &pipelines, std::shared_ptr<ShaderProgram> program, Aov aov, rhi::SamplerHandle sampler)
        : device_(device), pipelines_(pipelines), program_(std::move(program)), aov_(aov), sampler_(sampler)
    {
    }
    ~DebugViewFeature() override
    {
        device_.destroy(group_);
        device_.destroy(sampler_);
    }

    std::string name() const override { return "debug.view"; }
    AovSet requiredAovs() const override { return {aov_}; }
    // A61 C : une seule passe, toujours la même ; son groupe suit la texture lue à l'exécution.
    std::optional<std::uint64_t> passSignature() const override { return static_cast<std::uint64_t>(aov_); }

    core::Result<> declarePasses(RenderGraphBuilder &builder) override
    {
        const RgTexture source = builder.aov(aov_);
        if (!source.valid())
        {
            return makeError(ErrorCode::kInvalidArgument, "debug view: the AOV '" + std::string(aovInfo(aov_).name) + "' is not active");
        }
        const RgTexture sceneColor = builder.resources().sceneColor;
        builder.graph()
            .addPass("debug.view", PassType::kRaster,
                     [this, source, sceneColor](RenderPassContext &ctx) -> core::Result<>
                     {
                         // Le groupe vit jusqu'à ce que la texture change (redimensionnement) ou que le plugin disparaisse : le détruire ici
                         // le libérerait pendant l'enregistrement, avant le submit de l'encodeur (une soumission synchrone entre-temps le
                         // verrait libéré : VUID-vkFreeDescriptorSets-pDescriptorSets-00309). Destruction retardée par le RHI ensuite.
                         const rhi::TextureHandle texture = ctx.texture(source);
                         if (group_.isNull() || texture != groupTexture_)
                         {
                             rhi::BindGroupDesc desc;
                             desc.entries.push_back({.binding = 0, .type = rhi::BindingType::kSampledTexture, .texture = texture, .sampler = sampler_});
                             desc.debugName = "debug.view.source";
                             auto created = ctx.device().createBindGroup(desc);
                             if (!created)
                             {
                                 return std::unexpected(created.error());
                             }
                             ctx.device().destroy(group_); // l'ancien n'est plus utilisé par l'image en cours d'enregistrement
                             group_ = *created;
                             groupTexture_ = texture;
                         }
                         GraphicsState state;
                         state.raster.cull = rhi::CullMode::kNone;
                         state.colorTargets.assign(1, rhi::ColorTargetState{.format = ctx.desc(sceneColor).format});
                         state.debugName = "debug.view";
                         auto pipeline = pipelines_.graphics(*program_, state);
                         if (!pipeline)
                         {
                             return std::unexpected(pipeline.error());
                         }
                         ctx.encoder().setPipeline(*pipeline);
                         ctx.encoder().setBindGroup(1, group_);
                         ctx.encoder().draw(3);
                         return {};
                     })
            .read(source)
            .writeColor(builder.resources().sceneColor, rhi::LoadOp::kDontCare);
        return {};
    }

  private:
    rhi::Device &device_;
    PipelineCache &pipelines_;
    std::shared_ptr<ShaderProgram> program_;
    Aov aov_;
    rhi::SamplerHandle sampler_;
    rhi::BindGroupHandle group_;
    rhi::TextureHandle groupTexture_;
};

} // namespace

AovSet SceneRenderer::activeAovs() const
{
    AovSet set = aovDeclared_;
    if (motionVectorsEnabled_)
    {
        set.add(Aov::kMotion);
    }
    for (const auto &feature : features_)
    {
        set = set | feature->requiredAovs();
    }
    return set;
}

core::Result<> SceneRenderer::declareAov(Aov aov)
{
    if (static_cast<std::size_t>(aov) >= kAovCount)
    {
        return makeError(ErrorCode::kInvalidArgument, "SceneRenderer::declareAov: unknown AOV");
    }
    AovSet wanted = activeAovs();
    wanted.add(aov);
    if (wanted.count() + 1 > device_.capabilities().maxColorAttachments)
    {
        return makeError(ErrorCode::kUnsupported, "SceneRenderer::declareAov: '" + std::string(aovInfo(aov).name) + "' needs " +
                                                      std::to_string(wanted.count() + 1) + " color attachments, the device has " +
                                                      std::to_string(device_.capabilities().maxColorAttachments));
    }
    aovDeclared_.add(aov);
    return {};
}

core::Result<> SceneRenderer::setDebugView(DebugView view)
{
    if (view == debugView_)
    {
        return {};
    }
    if (frameOpen_ || executing_)
    {
        // Revue PR #37 : l'ancien plugin `debug.view` resterait (retrait différé) et le nouveau serait refusé ; rien n'est changé.
        return makeError(ErrorCode::kBusy, "SceneRenderer::setDebugView: refused during a frame (between beginFrame and the end of endFrame)");
    }
    std::shared_ptr<ShaderProgram> program;
    std::shared_ptr<ShaderProgram> texturedProgram;
    std::unique_ptr<RenderFeature> feature;
    switch (view)
    {
    case DebugView::kNormals:
    case DebugView::kMotion:
    {
        const bool normals = view == DebugView::kNormals;
        auto loaded = library_.loadGraphics(shaderDirectory_ / "blit.vert", shaderDirectory_ / "debug_view_aov.frag", {{"OROSMO_VIEW", normals ? "1" : "2"}});
        if (!loaded)
        {
            return std::unexpected(loaded.error());
        }
        rhi::SamplerDesc sd;
        sd.minFilter = rhi::Filter::kNearest;
        sd.magFilter = rhi::Filter::kNearest;
        sd.mipFilter = std::nullopt;
        sd.addressU = rhi::AddressMode::kClampToEdge;
        sd.addressV = rhi::AddressMode::kClampToEdge;
        sd.debugName = "debug.view";
        auto sampler = device_.createSampler(sd);
        if (!sampler)
        {
            return std::unexpected(sampler.error());
        }
        feature = std::make_unique<DebugViewFeature>(device_, pipelines_, *loaded, normals ? Aov::kNormals : Aov::kMotion, *sampler);
        break;
    }
    case DebugView::kAlbedo:
    case DebugView::kOverdraw:
    case DebugView::kWireframe:
    {
        const char *mode = view == DebugView::kAlbedo ? "1" : (view == DebugView::kOverdraw ? "2" : "3");
        auto loaded = library_.loadGraphics(shaderDirectory_ / "mesh.vert", shaderDirectory_ / "debug_view.frag", {{"OROSMO_DEBUG_VIEW", mode}});
        if (!loaded)
        {
            return std::unexpected(loaded.error());
        }
        program = *loaded;
        // Variante des maillages texturés (mesh_textured.vert : uv à l'emplacement 2) : l'albédo est le texel, pas les uv.
        auto textured = library_.loadGraphics(shaderDirectory_ / "mesh_textured.vert", shaderDirectory_ / "debug_view.frag",
                                              {{"OROSMO_DEBUG_VIEW", mode}, {"OROSMO_TEXTURED", "1"}});
        if (!textured)
        {
            return std::unexpected(textured.error());
        }
        texturedProgram = *textured;
        break;
    }
    case DebugView::kNone:
    case DebugView::kLod:
        break;
    }
    removeFeature("debug.view");
    debugViewProgram_ = std::move(program);
    debugViewTexturedProgram_ = std::move(texturedProgram);
    debugView_ = DebugView::kNone;
    if (feature)
    {
        if (auto added = addFeature(std::move(feature)); !added)
        {
            return std::unexpected(added.error());
        }
    }
    debugView_ = view;
    return {};
}

void SceneRenderer::configureMaterialState(GraphicsState &state, BlendMode blend, bool lines) const
{
    if (debugView_ == DebugView::kOverdraw && !lines)
    {
        // Chaque fragment s'additionne, sans test de profondeur : ce qu'on compte, c'est ce qui serait dessiné.
        state.depth = {false, false, rhi::CompareOp::kAlways};
        state.colorTargets[0].blend = {true,
                                       rhi::BlendFactor::kOne,
                                       rhi::BlendFactor::kOne,
                                       rhi::BlendOp::kAdd,
                                       rhi::BlendFactor::kOne,
                                       rhi::BlendFactor::kOne,
                                       rhi::BlendOp::kAdd};
        return;
    }
    if (blend != BlendMode::kOpaque)
    {
        const BlendModeState mode = blendModeState(blend);
        state.colorTargets[0].blend = mode.blend; // les AOV ne sont jamais mélangés : un transparent qui en demande les remplace
        state.depth.write = state.depth.write && mode.depthWrite;
    }
}

std::string_view debugViewName(DebugView view) noexcept
{
    switch (view)
    {
    case DebugView::kNone: return "none";
    case DebugView::kNormals: return "normals";
    case DebugView::kAlbedo: return "albedo";
    case DebugView::kOverdraw: return "overdraw";
    case DebugView::kLod: return "lod";
    case DebugView::kWireframe: return "wireframe";
    case DebugView::kMotion: return "motion";
    }
    return "none";
}

std::optional<DebugView> debugViewFromName(std::string_view name) noexcept
{
    for (const DebugView view : {DebugView::kNone, DebugView::kNormals, DebugView::kAlbedo, DebugView::kOverdraw, DebugView::kLod,
                                 DebugView::kWireframe, DebugView::kMotion})
    {
        if (debugViewName(view) == name)
        {
            return view;
        }
    }
    return std::nullopt;
}

glm::vec3 lodDebugColor(std::uint32_t level) noexcept
{
    static constexpr glm::vec3 kColors[] = {{0.90f, 0.15f, 0.15f}, {0.95f, 0.55f, 0.10f}, {0.95f, 0.90f, 0.15f}, {0.25f, 0.80f, 0.25f},
                                            {0.15f, 0.80f, 0.85f}, {0.20f, 0.35f, 0.90f}, {0.65f, 0.25f, 0.85f}};
    return kColors[level % (sizeof(kColors) / sizeof(kColors[0]))];
}

void FrustumFreeze::drawFrozen(DebugDraw &draw, const glm::vec4 &color, float farDepth) const
{
    if (!frozen_)
    {
        return;
    }
    const glm::mat4 inverse = glm::inverse(viewProj_);
    glm::vec3 corners[2][4]; // [plan][coin] : 0 proche, 1 lointain
    const float depths[2] = {1.0f, farDepth};
    const glm::vec2 xy[4] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    for (int plane = 0; plane < 2; ++plane)
    {
        for (int c = 0; c < 4; ++c)
        {
            const glm::vec4 world = inverse * glm::vec4(xy[c], depths[plane], 1.0f);
            corners[plane][c] = glm::vec3(world) / world.w;
        }
    }
    for (int c = 0; c < 4; ++c)
    {
        draw.line(corners[0][c], corners[0][(c + 1) % 4], color);
        draw.line(corners[1][c], corners[1][(c + 1) % 4], color);
        draw.line(corners[0][c], corners[1][c], color);
    }
}

} // namespace orosmo::renderer
