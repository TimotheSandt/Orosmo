#include "orosmo/anim/skinning.hpp"

#include "orosmo/renderer/material.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
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

core::Vec3 safeNormalize(const core::Vec3 &v)
{
    const float l = glm::length(v);
    return l > 1e-12f ? v / l : v;
}
} // namespace

std::vector<core::Mat4> jointPalette(const assets::SkinData &skin, std::span<const core::Mat4> world, int meshNode)
{
    const core::Mat4 toMesh = meshNode >= 0 && static_cast<std::size_t>(meshNode) < world.size() ? glm::inverse(world[static_cast<std::size_t>(meshNode)])
                                                                                                 : core::Mat4(1.0f);
    std::vector<core::Mat4> palette(skin.joints.size(), core::Mat4(1.0f));
    for (std::size_t j = 0; j < skin.joints.size(); ++j)
    {
        const int node = skin.joints[j];
        const core::Mat4 jointWorld = node >= 0 && static_cast<std::size_t>(node) < world.size() ? world[static_cast<std::size_t>(node)] : core::Mat4(1.0f);
        const core::Mat4 inverseBind = j < skin.inverseBindMatrices.size() ? skin.inverseBindMatrices[j] : core::Mat4(1.0f);
        palette[j] = toMesh * jointWorld * inverseBind;
    }
    return palette;
}

core::Result<std::vector<float>> skinVertices(std::span<const float> rest, std::span<const glm::u16vec4> joints, std::span<const core::Vec4> weights,
                                              std::span<const core::Mat4> palette)
{
    constexpr std::size_t kStride = renderer::kPbrVertexFloats;
    const std::size_t count = joints.size();
    if (rest.size() != count * kStride || weights.size() != count)
    {
        return makeError(ErrorCode::kInvalidArgument, "skinVertices: vertex, joint and weight counts differ");
    }
    std::vector<float> out(rest.begin(), rest.end());
    for (std::size_t v = 0; v < count; ++v)
    {
        core::Mat4 m(0.0f);
        for (int k = 0; k < 4; ++k)
        {
            const std::size_t j = joints[v][k];
            if (j >= palette.size())
            {
                return makeError(ErrorCode::kInvalidArgument, "skinVertices: joint index " + std::to_string(j) + " beyond the palette");
            }
            m += palette[j] * weights[v][k];
        }
        const float *r = rest.data() + v * kStride;
        float *o = out.data() + v * kStride;
        const core::Vec3 p = core::Vec3(m * core::Vec4(r[0], r[1], r[2], 1.0f));
        const core::Vec3 n = safeNormalize(core::Mat3(m) * core::Vec3(r[3], r[4], r[5]));
        const core::Vec3 t = safeNormalize(core::Mat3(m) * core::Vec3(r[8], r[9], r[10]));
        o[0] = p.x;
        o[1] = p.y;
        o[2] = p.z;
        o[3] = n.x;
        o[4] = n.y;
        o[5] = n.z;
        o[8] = t.x;
        o[9] = t.y;
        o[10] = t.z;
    }
    return out;
}

bool SkinnedModel::supportedBy(const rhi::Capabilities &capabilities) noexcept
{
    return capabilities.compute && capabilities.maxStorageBufferBindings >= 4;
}

SkinnedModel::~SkinnedModel()
{
    if (device_ != nullptr)
    {
        for (Entry &e : entries_)
        {
            device_->destroy(e.group);
        }
    }
}

core::Result<SkinnedModel> SkinnedModel::create(rhi::Device &device, renderer::ShaderLibrary &library, renderer::PipelineCache &pipelines,
                                                const std::filesystem::path &shaderDirectory, const assets::Model &model, assets::RenderModel &render)
{
    if (!supportedBy(device.capabilities()))
    {
        return makeError(ErrorCode::kUnsupported, "SkinnedModel: compute shaders and 4 storage buffer bindings are required");
    }
    auto program = library.loadCompute(shaderDirectory / "skin.comp");
    if (!program)
    {
        return std::unexpected(program.error());
    }
    SkinnedModel out;
    out.device_ = &device;
    out.pipelines_ = &pipelines;
    out.program_ = *program;
    out.render_ = &render;
    out.parents_ = nodeParents(model);
    out.skins_ = model.skins;

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
        Entry e;
        e.mesh = item.mesh;
        e.node = item.node;
        e.skin = skin;
        e.vertexCount = static_cast<std::uint32_t>(prim.positions.size());
        const std::string name = "skin." + model.meshes[src.mesh].name + "." + std::to_string(src.primitive);

        const std::vector<float> rest = assets::pbrVertices(prim);
        std::vector<GpuInfluence> influences(prim.joints.size());
        for (std::size_t v = 0; v < prim.joints.size(); ++v)
        {
            for (int k = 0; k < 4; ++k)
            {
                if (prim.joints[v][k] >= skinData.joints.size())
                {
                    return makeError(ErrorCode::kInvalidArgument, "SkinnedModel: " + name + " vertex " + std::to_string(v) + " uses joint " +
                                                                      std::to_string(prim.joints[v][k]) + " beyond its skin");
                }
                influences[v].joints[k] = prim.joints[v][k];
                influences[v].weights[k] = prim.weights[v][k];
            }
        }
        const renderer::Mesh &mesh = render.mesh(item.mesh);
        if (mesh.vertexBufferSize() != rest.size() * sizeof(float))
        {
            return makeError(ErrorCode::kInvalidArgument, "SkinnedModel: " + name + " vertex buffer does not match its primitive");
        }
        auto makeBuffer = [&](std::span<const std::byte> bytes, rhi::BufferUsage usage, const char *suffix) -> core::Result<renderer::GpuBuffer>
        {
            rhi::BufferDesc d;
            d.size = bytes.size();
            d.usage = rhi::BufferUsage::kStorage | usage;
            d.debugName = name + suffix;
            return renderer::GpuBuffer::create(device, d, bytes);
        };
        const std::vector<core::Mat4> identity(skinData.joints.size(), core::Mat4(1.0f));
        auto restBuffer = makeBuffer(std::as_bytes(std::span(rest)), rhi::BufferUsage::kNone, ".rest");
        auto influenceBuffer = makeBuffer(std::as_bytes(std::span(influences)), rhi::BufferUsage::kNone, ".influences");
        auto paletteBuffer = makeBuffer(std::as_bytes(std::span(identity)), rhi::BufferUsage::kCopyDst, ".palette");
        if (!restBuffer || !influenceBuffer || !paletteBuffer)
        {
            return std::unexpected(!restBuffer ? restBuffer.error() : !influenceBuffer ? influenceBuffer.error() : paletteBuffer.error());
        }
        e.rest = std::move(*restBuffer);
        e.influences = std::move(*influenceBuffer);
        e.palette = std::move(*paletteBuffer);
        rhi::BindGroupDesc gd;
        gd.debugName = name;
        gd.entries.push_back({.binding = 0, .type = rhi::BindingType::kStorageBuffer, .buffer = e.rest.handle()});
        gd.entries.push_back({.binding = 1, .type = rhi::BindingType::kStorageBuffer, .buffer = e.influences.handle()});
        gd.entries.push_back({.binding = 2, .type = rhi::BindingType::kStorageBuffer, .buffer = e.palette.handle()});
        gd.entries.push_back({.binding = 3, .type = rhi::BindingType::kStorageBuffer, .buffer = mesh.vertexBuffer()});
        auto group = device.createBindGroup(gd);
        if (!group)
        {
            return std::unexpected(group.error());
        }
        e.group = *group;
        out.entries_.push_back(std::move(e));
    }
    return out;
}

std::size_t SkinnedModel::skinnedVertexCount() const noexcept
{
    std::size_t n = 0;
    for (const Entry &e : entries_)
    {
        n += e.vertexCount;
    }
    return n;
}

core::Result<> SkinnedModel::setPose(const Pose &pose)
{
    auto world = worldMatrices(parents_, pose);
    if (!world)
    {
        return std::unexpected(world.error());
    }
    for (Entry &e : entries_)
    {
        const std::vector<core::Mat4> palette = jointPalette(skins_[static_cast<std::size_t>(e.skin)], *world, e.node);
        if (auto r = e.palette.update(0, std::as_bytes(std::span(palette))); !r)
        {
            return r;
        }
    }
    return render_->setNodeMatrices(std::move(*world));
}

core::Result<> SkinnedModel::dispatch(rhi::CommandEncoder &encoder)
{
    if (entries_.empty())
    {
        return {};
    }
    auto pipeline = pipelines_->compute(*program_);
    if (!pipeline)
    {
        return std::unexpected(pipeline.error());
    }
    encoder.beginDebugGroup("anim.skin");
    encoder.bindComputePipeline(*pipeline);
    for (const Entry &e : entries_)
    {
        encoder.setBindGroup(0, e.group);
        encoder.dispatch((e.vertexCount + kSkinWorkgroupSize - 1) / kSkinWorkgroupSize, 1, 1);
    }
    encoder.endDebugGroup();
    return {};
}

} // namespace orosmo::anim
