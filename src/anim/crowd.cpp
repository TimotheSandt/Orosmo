#include "orosmo/anim/crowd.hpp"

#include "orosmo/renderer/instance_renderer.hpp"
#include "orosmo/renderer/material.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <optional>
#include <string>

namespace orosmo::anim
{

using core::ErrorCode;
using core::makeError;

namespace
{
struct GpuInfluence
{
    std::uint32_t joints[4];
    float weights[4];
};
static_assert(sizeof(GpuInfluence) == 32);

// Groupe des palettes : 2 (le 0 est l'image, le 1 le matériau PBR). Emplacement plat OpenGL : 2 × 8 + 0.
constexpr std::uint32_t kPaletteGroup = 2;
constexpr std::uint32_t kPaletteFlatBinding = kPaletteGroup * 8;
} // namespace

bool CrowdRenderer::supportedBy(const rhi::Capabilities &capabilities) noexcept
{
    return capabilities.maxStorageBufferBindings > kPaletteFlatBinding;
}

CrowdRenderer::~CrowdRenderer()
{
    device_.destroy(paletteGroup_);
}

core::Result<std::unique_ptr<CrowdRenderer>> CrowdRenderer::create(rhi::Device &device, renderer::ShaderLibrary &library,
                                                                   renderer::PipelineCache &pipelines, const std::filesystem::path &shaderDirectory,
                                                                   const assets::Model &model, const assets::RenderModel &render,
                                                                   const CrowdOptions &options)
{
    if (!supportedBy(device.capabilities()))
    {
        return makeError(ErrorCode::kUnsupported, "CrowdRenderer: storage buffers read by the vertex shader at group 2 are required");
    }
    std::unique_ptr<CrowdRenderer> out(new CrowdRenderer(device, pipelines));
    out->parents_ = nodeParents(model);
    out->skins_ = model.skins;
    for (const assets::SkinData &skin : model.skins)
    {
        out->skinOffsets_.push_back(out->jointCount_);
        out->jointCount_ += static_cast<std::uint32_t>(skin.joints.size());
    }

    std::vector<char> seen(render.meshCount(), 0);
    for (const assets::RenderModel::Item &item : render.items())
    {
        const int skin = model.nodes[static_cast<std::size_t>(item.node)].skin;
        if (skin < 0 || seen[item.mesh] != 0)
        {
            continue;
        }
        const assets::RenderModel::MeshSource &src = render.meshSource(item.mesh);
        const std::optional<assets::MeshPrimitive> flat = assets::flatShaded(model.meshes[src.mesh].primitives[src.primitive]);
        const assets::MeshPrimitive &prim = flat ? *flat : model.meshes[src.mesh].primitives[src.primitive]; // comme RenderModel
        if (prim.joints.empty())
        {
            continue;
        }
        seen[item.mesh] = 1;
        const assets::SkinData &skinData = model.skins[static_cast<std::size_t>(skin)];
        const std::string name = "crowd." + model.meshes[src.mesh].name + "." + std::to_string(src.primitive);
        std::vector<GpuInfluence> influences(prim.joints.size());
        for (std::size_t v = 0; v < prim.joints.size(); ++v)
        {
            for (int k = 0; k < 4; ++k)
            {
                if (prim.joints[v][k] >= skinData.joints.size())
                {
                    return makeError(ErrorCode::kInvalidArgument, "CrowdRenderer: " + name + " vertex " + std::to_string(v) + " uses joint " +
                                                                      std::to_string(prim.joints[v][k]) + " beyond its skin");
                }
                influences[v].joints[k] = out->skinOffsets_[static_cast<std::size_t>(skin)] + prim.joints[v][k];
                influences[v].weights[k] = prim.weights[v][k];
            }
        }
        const auto &materials = render.materials();
        const auto &material = prim.material >= 0 ? materials[static_cast<std::size_t>(prim.material)] : materials.back();
        std::vector<renderer::Define> defines = renderer::pbrDefines(material->features());
        defines.insert(defines.end(), options.extraDefines.begin(), options.extraDefines.end());
        auto program = library.loadGraphics(shaderDirectory / "mesh_pbr_crowd.vert", shaderDirectory / "mesh_pbr.frag", defines);
        if (!program)
        {
            return std::unexpected(program.error());
        }
        rhi::BufferDesc d;
        d.size = influences.size() * sizeof(GpuInfluence);
        d.usage = rhi::BufferUsage::kVertex;
        d.debugName = name + ".influences";
        auto buffer = renderer::GpuBuffer::create(device, d, std::as_bytes(std::span(influences)));
        if (!buffer)
        {
            return std::unexpected(buffer.error());
        }
        Batch batch;
        batch.mesh = &render.mesh(item.mesh);
        batch.program = *program;
        batch.material = material->group();
        batch.cull = material->doubleSided() ? rhi::CullMode::kNone : rhi::CullMode::kBack;
        batch.influences = std::move(*buffer);
        out->batches_.push_back(std::move(batch));
    }
    if (out->batches_.empty())
    {
        return makeError(ErrorCode::kInvalidArgument, "CrowdRenderer: the model draws no skinned primitive");
    }
    return out;
}

core::Result<> CrowdRenderer::agentPalette(const Pose &pose, std::span<core::Mat4> out) const
{
    if (out.size() != jointCount_)
    {
        return makeError(ErrorCode::kInvalidArgument, "CrowdRenderer::agentPalette: expected " + std::to_string(jointCount_) + " matrices");
    }
    auto world = worldMatrices(parents_, pose);
    if (!world)
    {
        return std::unexpected(world.error());
    }
    for (std::size_t s = 0; s < skins_.size(); ++s)
    {
        const std::vector<core::Mat4> palette = jointPalette(skins_[s], *world, -1);
        std::copy(palette.begin(), palette.end(), out.begin() + skinOffsets_[s]);
    }
    return {};
}

core::Result<> CrowdRenderer::setAgents(std::span<const core::Mat4> worlds, std::span<const Pose> poses)
{
    if (worlds.size() != poses.size())
    {
        return makeError(ErrorCode::kInvalidArgument, "CrowdRenderer::setAgents: one pose per agent");
    }
    palettes_.resize(worlds.size() * jointCount_);
    for (std::size_t a = 0; a < worlds.size(); ++a)
    {
        if (auto r = agentPalette(poses[a], std::span(palettes_).subspan(a * jointCount_, jointCount_)); !r)
        {
            return r;
        }
    }
    return setAgentPalettes(worlds, palettes_);
}

core::Result<> CrowdRenderer::ensureBuffer(renderer::GpuBuffer &buffer, std::uint64_t bytes, rhi::BufferUsage usage, const char *name, bool &recreated)
{
    recreated = false;
    if (buffer.valid() && buffer.size() >= bytes)
    {
        return {};
    }
    // Croissance par puissances de deux : pas de recréation à chaque image quand le nombre d'agents varie.
    rhi::BufferDesc desc;
    desc.size = std::bit_ceil(std::max<std::uint64_t>(bytes, 4096));
    desc.usage = usage | rhi::BufferUsage::kCopyDst;
    desc.debugName = name;
    auto created = renderer::GpuBuffer::create(device_, desc);
    if (!created)
    {
        return std::unexpected(created.error());
    }
    buffer = std::move(*created);
    recreated = true;
    return {};
}

core::Result<> CrowdRenderer::setAgentPalettes(std::span<const core::Mat4> worlds, std::span<const core::Mat4> palettes)
{
    if (palettes.size() != worlds.size() * jointCount_)
    {
        return makeError(ErrorCode::kInvalidArgument, "CrowdRenderer::setAgentPalettes: expected " + std::to_string(worlds.size() * jointCount_) +
                                                          " palette matrices");
    }
    agents_ = worlds.size();
    worlds_.assign(worlds.begin(), worlds.end());
    if (agents_ == 0)
    {
        return {};
    }
    bool recreated = false;
    if (auto r = ensureBuffer(paletteBuffer_, palettes.size_bytes(), rhi::BufferUsage::kStorage, "crowd.palettes", recreated); !r)
    {
        return r;
    }
    if (recreated || paletteGroup_.isNull())
    {
        device_.destroy(paletteGroup_);
        rhi::BindGroupDesc gd;
        gd.debugName = "crowd.palettes";
        gd.entries.push_back({.binding = 0, .type = rhi::BindingType::kStorageBuffer, .buffer = paletteBuffer_.handle()});
        auto group = device_.createBindGroup(gd);
        if (!group)
        {
            return std::unexpected(group.error());
        }
        paletteGroup_ = *group;
    }
    return paletteBuffer_.update(0, std::as_bytes(palettes));
}

core::Result<CrowdDrawStats> CrowdRenderer::draw(renderer::SceneRenderer &scene, bool wireframe)
{
    CrowdDrawStats stats;
    auto info = scene.sceneDrawInfo();
    if (!info)
    {
        return std::unexpected(info.error());
    }
    if (agents_ == 0)
    {
        return stats;
    }
    // Instances : matrices relatives à la caméra (REN-02), premier os de la palette de chaque agent.
    scratch_.resize(agents_);
    for (std::size_t a = 0; a < agents_; ++a)
    {
        core::Mat4 m = worlds_[a];
        m[3] -= glm::vec4(info->cameraOrigin, 0.0f);
        for (int c = 0; c < 4; ++c)
        {
            scratch_[a].model[c] = m[c];
        }
        scratch_[a].info = glm::uvec4(static_cast<std::uint32_t>(a) * jointCount_, 0, 0, 0);
    }
    bool recreated = false;
    if (auto r = ensureBuffer(instanceBuffer_, scratch_.size() * sizeof(CrowdGpuInstance), rhi::BufferUsage::kVertex, "crowd.instances", recreated); !r)
    {
        return std::unexpected(r.error());
    }
    if (auto r = instanceBuffer_.update(0, std::as_bytes(std::span(scratch_))); !r)
    {
        return std::unexpected(r.error());
    }
    stats.paletteBytes = static_cast<std::uint64_t>(agents_) * jointCount_ * sizeof(core::Mat4);

    struct Command
    {
        rhi::PipelineHandle pipeline;
        rhi::BindGroupHandle material;
        std::vector<std::byte> pushConstants;
        rhi::BufferHandle vertices;
        rhi::BufferHandle influences;
        rhi::BufferHandle indices;
        std::uint32_t indexCount = 0;
    };
    std::vector<Command> commands;
    for (int pass = 0; pass < (wireframe ? 2 : 1); ++pass)
    {
        const bool lines = pass == 1;
        for (const Batch &batch : batches_)
        {
            // Base commune avec le dessin instancié (cull, profondeur reverse-Z, cibles), puis nos trois liaisons de sommets.
            renderer::GraphicsState state = renderer::instancedGraphicsState(*batch.mesh, batch.cull, *info, lines);
            state.vertexBuffers = batch.mesh->vertexBuffers();
            state.vertexAttributes = batch.mesh->vertexAttributes();
            state.vertexBuffers.push_back({.binding = 1, .stride = sizeof(GpuInfluence), .stepRate = rhi::VertexStepRate::kVertex});
            state.vertexBuffers.push_back({.binding = 2, .stride = sizeof(CrowdGpuInstance), .stepRate = rhi::VertexStepRate::kInstance});
            state.vertexAttributes.push_back({.location = 5, .binding = 1, .format = rhi::VertexFormat::kUint4, .offset = 0});
            state.vertexAttributes.push_back({.location = 6, .binding = 1, .format = rhi::VertexFormat::kFloat4, .offset = 16});
            for (std::uint32_t c = 0; c < 4; ++c)
            {
                state.vertexAttributes.push_back({.location = 8 + c, .binding = 2, .format = rhi::VertexFormat::kFloat4, .offset = c * 16});
            }
            state.vertexAttributes.push_back({.location = 12, .binding = 2, .format = rhi::VertexFormat::kUint4, .offset = 64});
            state.debugName = batch.program->name() + ".crowd" + (lines ? ".lines" : "");
            auto pipeline = pipelines_.graphics(*batch.program, state);
            if (!pipeline)
            {
                return std::unexpected(pipeline.error());
            }
            Command c;
            c.pipeline = *pipeline;
            c.material = batch.material;
            c.pushConstants = renderer::instancedPushConstants(pipelines_, *pipeline, renderer::InstanceMaterial{}, lines);
            c.vertices = batch.mesh->vertexBuffer();
            c.influences = batch.influences.handle();
            c.indices = batch.mesh->indexBuffer();
            c.indexCount = batch.mesh->indexCount();
            commands.push_back(std::move(c));
            ++stats.drawCalls;
            stats.instances += static_cast<std::uint32_t>(agents_);
        }
    }
    const rhi::BufferHandle instances = instanceBuffer_.handle();
    const rhi::BindGroupHandle palettes = paletteGroup_;
    const auto count = static_cast<std::uint32_t>(agents_);
    auto record = scene.drawCustom(
        [commands = std::move(commands), instances, palettes, count](rhi::CommandEncoder &e, rhi::BindGroupHandle frameGroup) -> core::Result<>
        {
            for (const Command &c : commands)
            {
                e.setPipeline(c.pipeline);
                e.setBindGroup(0, frameGroup);
                e.setBindGroup(1, c.material);
                e.setBindGroup(kPaletteGroup, palettes);
                if (!c.pushConstants.empty())
                {
                    e.setPushConstants(c.pushConstants);
                }
                e.setVertexBuffer(0, c.vertices);
                e.setVertexBuffer(1, c.influences);
                e.setVertexBuffer(2, instances);
                e.setIndexBuffer(c.indices, rhi::IndexType::kUint32);
                e.drawIndexed(c.indexCount, count, 0, 0, 0);
            }
            return {};
        });
    if (!record)
    {
        return std::unexpected(record.error());
    }
    return stats;
}

} // namespace orosmo::anim
