#include "engine/renderer/mesh.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <numeric>
#include <string>

namespace engine::renderer
{

using core::ErrorCode;
using core::makeError;

namespace
{
core::Result<rhi::VertexFormat> formatFor(std::uint32_t floats)
{
    switch (floats)
    {
        case 1: return rhi::VertexFormat::kFloat1;
        case 2: return rhi::VertexFormat::kFloat2;
        case 3: return rhi::VertexFormat::kFloat3;
        case 4: return rhi::VertexFormat::kFloat4;
    }
    return makeError(ErrorCode::kInvalidArgument, "Mesh: an attribute has " + std::to_string(floats) + " floats; only 1 to 4 are supported");
}

core::Result<> addAttributes(const std::vector<std::uint32_t> &sizes, std::uint32_t binding, std::uint32_t firstLocation,
                             std::vector<rhi::VertexAttribute> &attributes, std::uint32_t &strideBytes)
{
    std::uint32_t offsetFloats = 0;
    for (std::size_t i = 0; i < sizes.size(); ++i)
    {
        auto format = formatFor(sizes[i]);
        if (!format)
        {
            return std::unexpected(format.error());
        }
        attributes.push_back({.location = firstLocation + static_cast<std::uint32_t>(i), .binding = binding, .format = *format,
                              .offset = offsetFloats * 4});
        offsetFloats += sizes[i];
    }
    strideBytes = offsetFloats * 4;
    return {};
}
} // namespace

core::Result<Mesh> Mesh::create(rhi::Device &device, const MeshData &data)
{
    if (data.vertices.empty() || data.indices.empty() || data.attributeSizes.empty())
    {
        return makeError(ErrorCode::kInvalidArgument, "Mesh::create: vertices, indices and attribute sizes are required (" + data.name + ")");
    }
    Mesh mesh;
    std::uint32_t vertexStride = 0;
    if (auto r = addAttributes(data.attributeSizes, 0, 0, mesh.attributes_, vertexStride); !r)
    {
        return std::unexpected(r.error());
    }
    if (data.vertices.size() % (vertexStride / 4) != 0)
    {
        return makeError(ErrorCode::kInvalidArgument, "Mesh::create: the vertex array is not a whole number of vertices (" + data.name + ")");
    }
    mesh.layouts_.push_back({.binding = 0, .stride = vertexStride, .stepRate = rhi::VertexStepRate::kVertex});
    // Chaque index doit désigner un sommet existant : sinon le dessin lirait hors du tampon de sommets.
    const std::size_t vertexCount = data.vertices.size() / (vertexStride / 4);
    if (auto bad = std::ranges::find_if(data.indices, [&](std::uint32_t i) { return i >= vertexCount; }); bad != data.indices.end())
    {
        return makeError(ErrorCode::kOutOfRange, "Mesh::create: index " + std::to_string(*bad) + " refers to vertex " + std::to_string(*bad) +
                                                     " but there are only " + std::to_string(vertexCount) + " vertices (" + data.name + ")");
    }

    std::uint32_t instanceStride = 0;
    if (!data.instanceAttributeSizes.empty())
    {
        if (auto r = addAttributes(data.instanceAttributeSizes, 1, static_cast<std::uint32_t>(data.attributeSizes.size()), mesh.attributes_, instanceStride); !r)
        {
            return std::unexpected(r.error());
        }
        if (data.instances.empty() || data.instances.size() % (instanceStride / 4) != 0)
        {
            return makeError(ErrorCode::kInvalidArgument, "Mesh::create: instance data is empty or not a whole number of instances (" + data.name + ")");
        }
        mesh.layouts_.push_back({.binding = 1, .stride = instanceStride, .stepRate = rhi::VertexStepRate::kInstance});
        mesh.instanceSizes_ = data.instanceAttributeSizes;
        mesh.instanceCount_ = static_cast<std::uint32_t>(data.instances.size() / (instanceStride / 4));
    }

    auto makeBuffer = [&](rhi::BufferUsage usage, std::span<const std::byte> bytes, const char *suffix) -> core::Result<GpuBuffer>
    {
        rhi::BufferDesc d;
        d.size = bytes.size();
        d.usage = usage | rhi::BufferUsage::kCopyDst | rhi::BufferUsage::kCopySrc;
        d.debugName = data.name + suffix;
        return GpuBuffer::create(device, d, bytes);
    };
    auto vb = makeBuffer(rhi::BufferUsage::kVertex, std::as_bytes(std::span(data.vertices)), ".vertices");
    auto ib = makeBuffer(rhi::BufferUsage::kIndex, std::as_bytes(std::span(data.indices)), ".indices");
    if (!vb || !ib)
    {
        return std::unexpected(!vb ? vb.error() : ib.error());
    }
    mesh.vertexBuffer_ = std::move(*vb);
    mesh.indexBuffer_ = std::move(*ib);
    if (!data.instanceAttributeSizes.empty())
    {
        auto inst = makeBuffer(rhi::BufferUsage::kVertex, std::as_bytes(std::span(data.instances)), ".instances");
        if (!inst)
        {
            return std::unexpected(inst.error());
        }
        mesh.instanceBuffer_ = std::move(*inst);
    }
    // Bloc ModelBlock de mesh.vert (std140) : matrice du modèle puis matrice des normales (inverse transposée, en mat4).
    rhi::BufferDesc md;
    md.size = 2 * sizeof(glm::mat4);
    md.usage = rhi::BufferUsage::kUniform | rhi::BufferUsage::kCopyDst;
    md.debugName = data.name + ".model";
    auto model = GpuBuffer::create(device, md);
    if (!model)
    {
        return std::unexpected(model.error());
    }
    mesh.modelBuffer_ = std::move(*model);
    rhi::BindGroupDesc bg;
    bg.entries.push_back({.binding = 0, .type = rhi::BindingType::kUniformBuffer, .buffer = mesh.modelBuffer_.handle(), .size = 2 * sizeof(glm::mat4)});
    bg.debugName = data.name + ".modelGroup";
    auto group = device.createBindGroup(bg);
    if (!group)
    {
        return std::unexpected(group.error());
    }
    mesh.modelGroup_ = *group;
    mesh.indexCount_ = static_cast<std::uint32_t>(data.indices.size());
    mesh.device_ = &device;
    if (auto r = mesh.uploadTransform(); !r)
    {
        return std::unexpected(r.error());
    }
    return mesh;
}

glm::mat4 Mesh::modelMatrix() const
{
    glm::mat4 model(1.0f);
    model = glm::translate(model, position_);
    model = glm::rotate(model, glm::radians(rotation_.x), glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::rotate(model, glm::radians(rotation_.y), glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, glm::radians(rotation_.z), glm::vec3(0.0f, 0.0f, 1.0f));
    return glm::scale(model, scale_);
}

core::Result<> Mesh::uploadTransform()
{
    if (!valid())
    {
        return makeError(ErrorCode::kInvalidArgument, "Mesh::uploadTransform: empty mesh");
    }
    if (!transformDirty_)
    {
        return {};
    }
    // Les normales se transforment par l'inverse transposée de la partie 3x3 (juste aussi avec une échelle non uniforme) ; calculée
    // une fois par objet ici plutôt qu'à chaque sommet. Matrice non inversible (échelle nulle) : identité, la normale est inutile.
    std::array<glm::mat4, 2> block{modelMatrix(), glm::mat4(1.0f)};
    const glm::mat3 linear(block[0]);
    if (glm::determinant(linear) != 0.0f)
    {
        block[1] = glm::mat4(glm::transpose(glm::inverse(linear)));
    }
    if (auto r = modelBuffer_.update(0, std::as_bytes(std::span(glm::value_ptr(block[0]), 32))); !r)
    {
        return r;
    }
    transformDirty_ = false;
    return {};
}

core::Result<> Mesh::setTextures(std::span<const TextureBinding> textures)
{
    if (device_ == nullptr)
    {
        return makeError(ErrorCode::kInvalidArgument, "Mesh::setTextures: empty mesh");
    }
    rhi::BindGroupHandle fresh;
    if (!textures.empty())
    {
        rhi::BindGroupDesc bg;
        for (std::size_t i = 0; i < textures.size(); ++i)
        {
            bg.entries.push_back({.binding = static_cast<std::uint32_t>(i), .type = rhi::BindingType::kSampledTexture, .texture = textures[i].texture,
                                  .sampler = textures[i].sampler});
        }
        bg.debugName = "mesh.textures";
        auto group = device_->createBindGroup(bg);
        if (!group)
        {
            return std::unexpected(group.error());
        }
        fresh = *group;
    }
    device_->destroy(textureGroup_);
    textureGroup_ = fresh;
    return {};
}

core::Result<> Mesh::updateVertices(std::span<const float> data, std::size_t offsetFloats)
{
    if (!valid())
    {
        return makeError(ErrorCode::kInvalidArgument, "Mesh::updateVertices: empty mesh");
    }
    // Décalage vérifié AVANT la conversion en octets (une multiplication qui déborde reviendrait au début du tampon).
    if (offsetFloats > vertexBuffer_.size() / sizeof(float))
    {
        return makeError(ErrorCode::kOutOfRange, "Mesh::updateVertices: offset beyond the vertex buffer");
    }
    return vertexBuffer_.update(offsetFloats * sizeof(float), std::as_bytes(data));
}

core::Result<> Mesh::updateInstances(std::span<const float> data, std::size_t offsetFloats)
{
    if (!instanceBuffer_.valid())
    {
        return makeError(ErrorCode::kInvalidArgument, "Mesh::updateInstances: the mesh has no instance data");
    }
    if (offsetFloats > instanceBuffer_.size() / sizeof(float))
    {
        return makeError(ErrorCode::kOutOfRange, "Mesh::updateInstances: offset beyond the instance buffer");
    }
    return instanceBuffer_.update(offsetFloats * sizeof(float), std::as_bytes(data));
}

void Mesh::reset() noexcept
{
    if (device_ != nullptr)
    {
        device_->destroy(modelGroup_);
        device_->destroy(textureGroup_);
    }
    vertexBuffer_.reset();
    indexBuffer_.reset();
    instanceBuffer_.reset();
    modelBuffer_.reset();
    modelGroup_ = {};
    textureGroup_ = {};
    layouts_.clear();
    attributes_.clear();
    instanceSizes_.clear();
    indexCount_ = 0;
    instanceCount_ = 1;
    program_.reset();
    pushConstants_.clear();
    device_ = nullptr;
}

void Mesh::swap(Mesh &other) noexcept
{
    std::swap(device_, other.device_);
    std::swap(vertexBuffer_, other.vertexBuffer_);
    std::swap(indexBuffer_, other.indexBuffer_);
    std::swap(instanceBuffer_, other.instanceBuffer_);
    std::swap(modelBuffer_, other.modelBuffer_);
    std::swap(modelGroup_, other.modelGroup_);
    std::swap(textureGroup_, other.textureGroup_);
    std::swap(layouts_, other.layouts_);
    std::swap(attributes_, other.attributes_);
    std::swap(instanceSizes_, other.instanceSizes_);
    std::swap(indexCount_, other.indexCount_);
    std::swap(instanceCount_, other.instanceCount_);
    std::swap(program_, other.program_);
    std::swap(pushConstants_, other.pushConstants_);
    std::swap(position_, other.position_);
    std::swap(rotation_, other.rotation_);
    std::swap(scale_, other.scale_);
    std::swap(transformDirty_, other.transformDirty_);
}

} // namespace engine::renderer
