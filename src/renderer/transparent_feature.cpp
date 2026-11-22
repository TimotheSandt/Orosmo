#include "orosmo/renderer/transparent_feature.hpp"

#include <algorithm>
#include <cstring>

namespace orosmo::renderer
{

using core::ErrorCode;
using core::makeError;

namespace
{

// 0 : écrit la profondeur ; 1 : mélangé, trié ; 2 : additif.
int groupOf(BlendMode mode) noexcept
{
    if (mode == BlendMode::kAdditive)
    {
        return 2;
    }
    return isTransparent(mode) ? 1 : 0;
}

struct Push
{
    std::int32_t wireframe = 0;
    float alpha = 1.0f;
    float cutoff = 0.5f;
    std::int32_t mode = 0;
};

} // namespace

std::vector<std::uint32_t> sortTransparents(std::span<const TransparentFeature::Entry> entries, const glm::vec3 &camera)
{
    std::vector<std::uint32_t> order(entries.size());
    for (std::uint32_t i = 0; i < order.size(); ++i)
    {
        order[i] = i;
    }
    const auto distance = [&](std::uint32_t i) { return glm::dot(entries[i].center - camera, entries[i].center - camera); };
    std::ranges::stable_sort(order,
                             [&](std::uint32_t a, std::uint32_t b)
                             {
                                 const int ga = groupOf(entries[a].material.mode);
                                 const int gb = groupOf(entries[b].material.mode);
                                 if (ga != gb)
                                 {
                                     return ga < gb;
                                 }
                                 if (ga == 0)
                                 {
                                     return false; // profondeur écrite : l'ordre de dépôt suffit
                                 }
                                 if (entries[a].material.priority != entries[b].material.priority)
                                 {
                                     return entries[a].material.priority < entries[b].material.priority;
                                 }
                                 return distance(a) > distance(b); // l'arrière d'abord
                             });
    return order;
}

core::Result<std::unique_ptr<TransparentFeature>> TransparentFeature::create(rhi::Device &, ShaderLibrary &library, PipelineCache &pipelines,
                                                                             const std::filesystem::path &shaderDirectory)
{
    auto program = library.loadGraphics(shaderDirectory / "mesh.vert", shaderDirectory / "mesh.frag", {{"OROSMO_TRANSPARENT", "1"}});
    if (!program)
    {
        return std::unexpected(program.error());
    }
    std::unique_ptr<TransparentFeature> feature(new TransparentFeature());
    feature->pipelines_ = &pipelines;
    feature->program_ = *program;
    return feature;
}

core::Result<> TransparentFeature::prepare(const FrameData &frame)
{
    cameraPosition_ = glm::vec3(frame.cameraWorldHigh) + glm::vec3(frame.cameraWorldLow);
    return {};
}

core::Result<std::uint32_t> TransparentFeature::submit(SceneRenderer &scene, Mesh &mesh, const TransparentMaterial &material)
{
    if (!mesh.valid())
    {
        return makeError(ErrorCode::kInvalidArgument, "TransparentFeature::submit: empty mesh");
    }
    auto info = scene.sceneDrawInfo();
    if (!info)
    {
        return std::unexpected(info.error());
    }
    if (auto r = mesh.uploadTransform(info->cameraOrigin); !r)
    {
        return std::unexpected(r.error());
    }
    // Transformation de CE dépôt, copiée maintenant dans une tranche de l'image (groupe 2 à décalage dynamique) : elle reste celle
    // du dépôt jusqu'à l'exécution différée à endFrame, même si le maillage est déplacé ou redéposé entre-temps.
    auto slice = scene.recordModelSlice(mesh);
    if (!slice)
    {
        return std::unexpected(slice.error());
    }
    const BlendModeState state = blendModeState(material.mode);
    GraphicsState gs;
    gs.vertexBuffers = mesh.vertexBuffers();
    gs.vertexAttributes = mesh.vertexAttributes();
    gs.raster.cull = rhi::CullMode::kNone;
    gs.depth = {true, state.depthWrite, rhi::CompareOp::kGreater}; // reverse-Z
    gs.depthFormat = info->depthFormat;
    gs.colorTargets.clear(); // le défaut de GraphicsState contient déjà une cible
    for (std::size_t i = 0; i < info->colorFormats.size(); ++i)
    {
        rhi::ColorTargetState t;
        t.format = info->colorFormats[i];
        t.writeMask = i == 0 ? rhi::ColorWriteMask::kAll : rhi::ColorWriteMask::kNone; // vecteurs de mouvement intacts
        if (i == 0)
        {
            t.blend = state.blend;
        }
        gs.colorTargets.push_back(t);
    }
    gs.debugName = "transparent." + std::string(blendModeName(material.mode));
    auto pipeline = pipelines_->graphics(*program_, gs);
    if (!pipeline)
    {
        return std::unexpected(pipeline.error());
    }
    Entry e;
    e.pipeline = *pipeline;
    e.modelOffset = *slice;
    e.textureGroup = mesh.textureGroup();
    e.vertexBuffer = mesh.vertexBuffer();
    e.indexBuffer = mesh.indexBuffer();
    e.indexCount = mesh.indexCount();
    e.material = material;
    e.center = mesh.position() + material.centerOffset;
    e.sequence = static_cast<std::uint32_t>(entries_.size());
    entries_.push_back(e);
    return e.sequence;
}

core::Result<> TransparentFeature::declarePasses(RenderGraphBuilder &builder)
{
    const RenderGraphBuilder::Resources &r = builder.resources();
    if (entries_.empty() || !r.sceneDepth.valid())
    {
        entries_.clear();
        lastOrder_.clear();
        stats_ = {};
        return {};
    }
    // Mêmes cibles que la passe « scene » (couleur puis chaque AOV actif, dans l'ordre de l'énumération : AovLayout) : les AOV
    // sont chargés et laissés intacts (masque nul du pipeline). Toutes doivent y être, sinon le pipeline (construit sur
    // `sceneDrawInfo().colorFormats`) ne correspond plus à la passe (ex. TAA : mouvement + profondeur linéaire, REN-19).
    auto pass = builder.graph().addPass(kPassName, PassType::kRaster, [this](RenderPassContext &ctx) { return play(ctx); });
    pass.writeColor(r.sceneColor, rhi::LoadOp::kLoad);
    for (const RgTexture aov : r.aovs)
    {
        if (aov.valid())
        {
            pass.writeColor(aov, rhi::LoadOp::kLoad);
        }
    }
    pass.writeDepth(r.sceneDepth, rhi::LoadOp::kLoad);
    return {};
}

void TransparentFeature::frameEnded() noexcept
{
    entries_.clear(); // dépôts d'une image qui a échoué avant la passe : jamais rejoués à l'image suivante
}

core::Result<> TransparentFeature::play(RenderPassContext &ctx)
{
    rhi::CommandEncoder &e = ctx.encoder();
    const std::vector<Entry> entries = std::move(entries_);
    entries_.clear();
    lastOrder_ = sortTransparents(entries, cameraPosition_);
    stats_ = {};
    stats_.submitted = static_cast<std::uint32_t>(entries.size());
    for (const std::uint32_t index : lastOrder_)
    {
        const Entry &entry = entries[index];
        switch (groupOf(entry.material.mode))
        {
        case 0: ++stats_.opaque; break;
        case 1: ++stats_.blended; break;
        default: ++stats_.additive; break;
        }
        Push push;
        push.alpha = entry.material.alpha;
        push.cutoff = entry.material.cutoff;
        push.mode = static_cast<std::int32_t>(entry.material.mode);
        e.setPipeline(entry.pipeline);
        e.setBindGroup(0, ctx.frame()->group);
        if (!entry.textureGroup.isNull())
        {
            e.setBindGroup(1, entry.textureGroup);
        }
        const std::uint32_t modelOffset = entry.modelOffset;
        e.setBindGroup(2, ctx.frame()->modelGroup, std::span(&modelOffset, 1));
        e.setPushConstants(std::as_bytes(std::span(&push, 1)));
        e.setVertexBuffer(0, entry.vertexBuffer);
        e.setIndexBuffer(entry.indexBuffer, rhi::IndexType::kUint32);
        e.drawIndexed(entry.indexCount, 1);
        ++stats_.drawCalls;
    }
    return {};
}

} // namespace orosmo::renderer
