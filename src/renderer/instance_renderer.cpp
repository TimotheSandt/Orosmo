#include "orosmo/renderer/instance_renderer.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <string>

namespace orosmo::renderer
{

using core::ErrorCode;
using core::makeError;

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
    auto context = scene.passContext();
    if (!context)
    {
        return std::unexpected(context.error());
    }
    if (batches.batches().empty())
    {
        return stats;
    }
    if (auto r = upload(batches, context->cameraOrigin, stats); !r)
    {
        return std::unexpected(r.error());
    }

    rhi::CommandEncoder &e = *context->encoder;
    rhi::PipelineHandle current;
    for (int pass = 0; pass < (wireframe ? 2 : 1); ++pass)
    {
        const bool lines = pass == 1;
        for (const InstanceBatch &batch : batches.batches())
        {
            const Mesh &mesh = *meshes_[batch.key.mesh];
            const InstanceMaterial &material = materials_[batch.key.material];
            const ShaderProgram &program = material.program ? *material.program : *defaultProgram_;

            GraphicsState state;
            state.vertexBuffers = mesh.vertexBuffers();
            state.vertexBuffers.push_back({.binding = 1, .stride = sizeof(GpuInstance), .stepRate = rhi::VertexStepRate::kInstance});
            state.vertexAttributes = mesh.vertexAttributes();
            for (std::uint32_t i = 0; i < kInstanceAttributeCount; ++i)
            {
                state.vertexAttributes.push_back(
                    {.location = kInstanceFirstLocation + i, .binding = 1, .format = rhi::VertexFormat::kFloat4, .offset = i * 16});
            }
            state.raster.cull = material.cull;
            state.raster.fill = lines ? rhi::FillMode::kLine : rhi::FillMode::kFill;
            // Mêmes règles que SceneRenderer::draw : reverse-Z, le fil de fer passe à profondeur égale.
            state.depth = {true, true, lines ? rhi::CompareOp::kGreaterEqual : rhi::CompareOp::kGreater};
            state.depthFormat = context->depthFormat;
            state.colorTargets.clear();
            for (std::size_t i = 0; i < context->colorFormats.size(); ++i)
            {
                rhi::ColorTargetState t;
                t.format = context->colorFormats[i];
                t.writeMask = i == 0 ? rhi::ColorWriteMask::kAll : rhi::ColorWriteMask::kNone;
                state.colorTargets.push_back(t);
            }
            state.debugName = program.name() + ".instanced" + (lines ? ".lines" : "");
            auto pipeline = pipelines_.graphics(program, state);
            if (!pipeline)
            {
                return std::unexpected(pipeline.error());
            }
            if (*pipeline != current)
            {
                e.setPipeline(*pipeline);
                e.setBindGroup(0, context->frameGroup);
                current = *pipeline;
                ++stats.pipelineChanges;
            }
            if (!material.textures.isNull())
            {
                e.setBindGroup(1, material.textures);
            }
            if (const std::uint32_t pushSize = pipelines_.pushConstantSize(*pipeline); pushSize > 0)
            {
                std::vector<std::byte> push(pushSize, std::byte{0});
                if (const std::size_t n = std::min<std::size_t>(pushSize, material.pushConstants.size()); n > 0)
                {
                    std::memcpy(push.data(), material.pushConstants.data(), n);
                }
                if (pushSize >= sizeof(std::int32_t))
                {
                    const std::int32_t flag = lines ? 1 : 0; // convention mesh.frag : premier entier = fil de fer
                    std::memcpy(push.data(), &flag, sizeof flag);
                }
                e.setPushConstants(push);
            }
            e.setVertexBuffer(0, mesh.vertexBuffer());
            e.setVertexBuffer(1, instanceBuffer_.handle());
            e.setIndexBuffer(mesh.indexBuffer(), rhi::IndexType::kUint32);
            e.drawIndexed(mesh.indexCount(), batch.instanceCount, 0, 0, batch.firstInstance);
            ++stats.drawCalls;
            stats.instances += batch.instanceCount;
        }
    }
    return stats;
}

} // namespace orosmo::renderer
