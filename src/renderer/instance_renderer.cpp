#include "orosmo/renderer/instance_renderer.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <string>

namespace orosmo::renderer
{

using core::ErrorCode;
using core::makeError;

GraphicsState instancedGraphicsState(const Mesh &layoutOf, rhi::CullMode cull, const SceneDrawInfo &info, bool lines)
{
    GraphicsState state;
    state.vertexBuffers = layoutOf.vertexBuffers();
    state.vertexBuffers.push_back({.binding = 1, .stride = sizeof(GpuInstance), .stepRate = rhi::VertexStepRate::kInstance});
    state.vertexAttributes = layoutOf.vertexAttributes();
    for (std::uint32_t i = 0; i < kInstanceAttributeCount; ++i)
    {
        state.vertexAttributes.push_back({.location = kInstanceFirstLocation + i, .binding = 1, .format = rhi::VertexFormat::kFloat4, .offset = i * 16});
    }
    state.raster.cull = cull;
    state.raster.fill = lines ? rhi::FillMode::kLine : rhi::FillMode::kFill;
    // Mêmes règles que SceneRenderer::drawMesh : reverse-Z, le fil de fer passe à profondeur égale.
    state.depth = {true, true, lines ? rhi::CompareOp::kGreaterEqual : rhi::CompareOp::kGreater};
    state.depthFormat = info.depthFormat;
    state.colorTargets.clear();
    for (std::size_t i = 0; i < info.colorFormats.size(); ++i)
    {
        rhi::ColorTargetState t;
        t.format = info.colorFormats[i];
        t.writeMask = i == 0 ? rhi::ColorWriteMask::kAll : rhi::ColorWriteMask::kNone;
        state.colorTargets.push_back(t);
    }
    return state;
}

std::vector<std::byte> instancedPushConstants(PipelineCache &pipelines, rhi::PipelineHandle pipeline, const InstanceMaterial &material, bool lines)
{
    std::vector<std::byte> bytes;
    if (const std::uint32_t pushSize = pipelines.pushConstantSize(pipeline); pushSize > 0)
    {
        bytes.assign(pushSize, std::byte{0});
        if (const std::size_t n = std::min<std::size_t>(pushSize, material.pushConstants.size()); n > 0)
        {
            std::memcpy(bytes.data(), material.pushConstants.data(), n);
        }
        if (pushSize >= sizeof(std::int32_t))
        {
            const std::int32_t flag = lines ? 1 : 0; // convention mesh.frag : premier entier = fil de fer
            std::memcpy(bytes.data(), &flag, sizeof flag);
        }
    }
    return bytes;
}

core::Result<std::unique_ptr<InstanceRenderer>> InstanceRenderer::create(rhi::Device &device, ShaderLibrary &library, PipelineCache &pipelines,
                                                                         const std::filesystem::path &shaderDirectory)
{
    auto program = library.loadGraphics(shaderDirectory / "mesh_instanced.vert", shaderDirectory / "mesh.frag", {{"OROSMO_INSTANCED", "1"}});
    if (!program)
    {
        return std::unexpected(program.error());
    }
    std::unique_ptr<InstanceRenderer> renderer(new InstanceRenderer(device, pipelines));
    renderer->defaultProgram_ = *program;
    return renderer;
}

core::Result<std::uint32_t> InstanceRenderer::addMesh(const Mesh &mesh)
{
    if (!mesh.valid())
    {
        return makeError(ErrorCode::kInvalidArgument, "InstanceRenderer::addMesh: empty mesh");
    }
    if (mesh.instanced())
    {
        return makeError(ErrorCode::kInvalidArgument, "InstanceRenderer::addMesh: the mesh has its own instance data (binding 1 is the instance buffer)");
    }
    if (mesh.vertexAttributes().size() > kInstanceFirstLocation)
    {
        return makeError(ErrorCode::kInvalidArgument, "InstanceRenderer::addMesh: more than " + std::to_string(kInstanceFirstLocation) +
                                                          " vertex attributes (locations 8 to 15 hold the instance)");
    }
    meshes_.push_back(&mesh);
    return static_cast<std::uint32_t>(meshes_.size() - 1);
}

std::uint32_t InstanceRenderer::addMaterial(InstanceMaterial material)
{
    materials_.push_back(std::move(material));
    return static_cast<std::uint32_t>(materials_.size() - 1);
}

bool InstanceRenderer::setMaterial(std::uint32_t id, InstanceMaterial material)
{
    if (id >= materials_.size())
        return false;
    materials_[id] = std::move(material);
    return true;
}

core::Result<> InstanceRenderer::upload(const InstanceBatchBuilder &batches, const glm::vec3 &origin, InstanceDrawStats &stats)
{
    std::span<const GpuInstance> instances = batches.instances();
    if (origin != glm::vec3(0.0f))
    {
        // Rendu relatif à la caméra (REN-02) : translations par rapport à la caméra, calculées ici une fois par instance.
        scratch_.assign(instances.begin(), instances.end());
        for (GpuInstance &instance : scratch_)
        {
            instance.model[3] -= glm::vec4(origin, 0.0f);
        }
        instances = scratch_;
    }
    const std::uint64_t bytes = instances.size_bytes();
    if (!instanceBuffer_.valid() || instanceBuffer_.size() < bytes)
    {
        // Croissance par puissances de deux (au moins 64 instances) : pas de recréation à chaque image quand le nombre varie.
        const std::uint64_t size = std::bit_ceil(std::max<std::uint64_t>(bytes, 64 * sizeof(GpuInstance)));
        if (instanceBuffer_.valid())
        {
            if (auto r = instanceBuffer_.resize(size, false); !r)
            {
                return r;
            }
        }
        else
        {
            rhi::BufferDesc desc;
            desc.size = size;
            desc.usage = rhi::BufferUsage::kVertex | rhi::BufferUsage::kCopyDst;
            desc.debugName = "instances";
            auto buffer = GpuBuffer::create(device_, desc);
            if (!buffer)
            {
                return std::unexpected(buffer.error());
            }
            instanceBuffer_ = std::move(*buffer);
        }
    }
    stats.uploadedBytes = bytes;
    return instanceBuffer_.update(0, std::as_bytes(instances));
}

core::Result<InstanceDrawStats> InstanceRenderer::draw(SceneRenderer &scene, const InstanceBatchBuilder &batches, bool wireframe)
{
    InstanceDrawStats stats;
    if (!batches.built())
    {
        return makeError(ErrorCode::kInvalidArgument, "InstanceRenderer::draw: call InstanceBatchBuilder::build first");
    }
    // Clés vérifiées avant tout envoi ou dessin : un identifiant inconnu ne laisse pas de dessin partiel.
    for (const InstanceBatch &batch : batches.batches())
    {
        if (batch.key.mesh >= meshes_.size() || !meshes_[batch.key.mesh]->valid())
        {
            return makeError(ErrorCode::kNotFound, "InstanceRenderer::draw: unknown or released mesh " + std::to_string(batch.key.mesh));
        }
        if (batch.key.material >= materials_.size())
        {
            return makeError(ErrorCode::kNotFound, "InstanceRenderer::draw: unknown material " + std::to_string(batch.key.material));
        }
    }
    auto info = scene.sceneDrawInfo();
    if (!info)
    {
        return std::unexpected(info.error());
    }
    if (batches.batches().empty())
    {
        return stats;
    }
    if (auto r = upload(batches, info->cameraOrigin, stats); !r)
    {
        return std::unexpected(r.error());
    }

    // Pipelines, constantes et tampons résolus maintenant ; le rappel les joue dans la passe « scene » (endFrame).
    struct Command
    {
        rhi::PipelineHandle pipeline; // nul : même pipeline que la commande précédente
        rhi::BindGroupHandle textures;
        std::vector<std::byte> pushConstants;
        rhi::BufferHandle vertexBuffer;
        rhi::BufferHandle indexBuffer;
        std::uint32_t indexCount = 0;
        std::uint32_t instanceCount = 0;
        std::uint32_t firstInstance = 0;
    };
    std::vector<Command> commands;
    commands.reserve(batches.batches().size() * (wireframe ? 2 : 1));
    rhi::PipelineHandle current;
    for (int pass = 0; pass < (wireframe ? 2 : 1); ++pass)
    {
        const bool lines = pass == 1;
        for (const InstanceBatch &batch : batches.batches())
        {
            const Mesh &mesh = *meshes_[batch.key.mesh];
            const InstanceMaterial &material = materials_[batch.key.material];
            const ShaderProgram &program = material.program ? *material.program : *defaultProgram_;

            GraphicsState state = instancedGraphicsState(mesh, material.cull, *info, lines);
            state.debugName = program.name() + ".instanced" + (lines ? ".lines" : "");
            auto pipeline = pipelines_.graphics(program, state);
            if (!pipeline)
            {
                return std::unexpected(pipeline.error());
            }
            Command command;
            if (*pipeline != current)
            {
                command.pipeline = *pipeline;
                current = *pipeline;
                ++stats.pipelineChanges;
            }
            command.textures = material.textures;
            command.pushConstants = instancedPushConstants(pipelines_, *pipeline, material, lines);
            command.vertexBuffer = mesh.vertexBuffer();
            command.indexBuffer = mesh.indexBuffer();
            command.indexCount = mesh.indexCount();
            command.instanceCount = batch.instanceCount;
            command.firstInstance = batch.firstInstance;
            commands.push_back(std::move(command));
            ++stats.drawCalls;
            stats.instances += batch.instanceCount;
        }
    }

    const rhi::BufferHandle instances = instanceBuffer_.handle();
    auto record = scene.drawCustom(
        [commands = std::move(commands), instances](rhi::CommandEncoder &e, rhi::BindGroupHandle frameGroup) -> core::Result<>
        {
            for (const Command &c : commands)
            {
                if (!c.pipeline.isNull())
                {
                    e.setPipeline(c.pipeline);
                    e.setBindGroup(0, frameGroup);
                }
                if (!c.textures.isNull())
                {
                    e.setBindGroup(1, c.textures);
                }
                if (!c.pushConstants.empty())
                {
                    e.setPushConstants(c.pushConstants);
                }
                e.setVertexBuffer(0, c.vertexBuffer);
                e.setVertexBuffer(1, instances);
                e.setIndexBuffer(c.indexBuffer, rhi::IndexType::kUint32);
                e.drawIndexed(c.indexCount, c.instanceCount, 0, 0, c.firstInstance);
            }
            return {};
        });
    if (!record)
    {
        return std::unexpected(record.error());
    }
    return stats;
}

} // namespace orosmo::renderer
