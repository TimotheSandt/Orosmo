#include "orosmo/renderer/mesh.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cstddef>
#include <array>
#include <cstring>
#include <numeric>
#include <string>

namespace orosmo::renderer
{

namespace
{
// Bloc ModelBlock de shaders/rhi/mesh.vert (std140) : trois mat4 de 64 octets (modèle, normales, modèle précédent) puis un uvec4.
struct ModelBlock
{
    glm::mat4 model;
    glm::mat4 normalMatrix;
    glm::mat4 prevModel;
    glm::uvec4 info; // x : identifiant d'objet
};
static_assert(sizeof(glm::mat4) == 64);
static_assert(offsetof(ModelBlock, model) == 0);
static_assert(offsetof(ModelBlock, normalMatrix) == 64);
static_assert(offsetof(ModelBlock, prevModel) == 128 && offsetof(ModelBlock, info) == 192);
static_assert(sizeof(ModelBlock) == 208 && sizeof(ModelBlock) == Mesh::kModelBlockSize);
} // namespace


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
    md.size = sizeof(ModelBlock);
    md.usage = rhi::BufferUsage::kUniform | rhi::BufferUsage::kCopyDst;
    md.debugName = data.name + ".model";
    auto model = GpuBuffer::create(device, md);
    if (!model)
    {
        return std::unexpected(model.error());
    }
    mesh.modelBuffer_ = std::move(*model);
    rhi::BindGroupDesc bg;
    bg.entries.push_back({.binding = 0, .type = rhi::BindingType::kUniformBuffer, .buffer = mesh.modelBuffer_.handle(), .size = sizeof(ModelBlock)});
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

core::Result<> Mesh::uploadTransform(const glm::vec3 &origin, const TransformHistory &history)
{
    if (!valid())
    {
        return makeError(ErrorCode::kInvalidArgument, "Mesh::uploadTransform: empty mesh");
    }
    if (auto r = prepareTransform(origin, history); !r)
    {
        return r;
    }
    if (!bufferStale_)
    {
        return {};
    }
    if (auto r = modelBuffer_.update(0, std::span<const std::byte>(modelBlock_)); !r) // une vue sur tout le bloc, pas sur une matrice
    {
        return r;
    }
    bufferStale_ = false;
    return {};
}

core::Result<> Mesh::prepareTransform(const glm::vec3 &origin, const TransformHistory &history)
{
    if (!valid())
    {
        return makeError(ErrorCode::kInvalidArgument, "Mesh::prepareTransform: empty mesh");
    }
    // REN-21 : modèle de l'image précédente (vecteurs de mouvement). Sans historique demandé, il égale le modèle courant et rien
    // ne change pour l'appelant : un maillage immobile n'est renvoyé que si l'origine ou la transformation change.
    bool tracked = false;
    glm::mat4 world = modelMatrix();
    if (history.frame != 0)
    {
        if (history.frame != lastFrame_)
        {
            // Le modèle suivi n'est « précédent » que pour l'image qui suit directement : après une interruption du suivi (maillage
            // non dessiné, AOV de mouvement retiré), le dernier vu est périmé et le modèle courant sert de repli (aucun mouvement).
            previousWorld_ = hasLastWorld_ && history.frame == lastFrame_ + 1 ? lastWorld_ : world;
            lastFrame_ = history.frame;
            tracked = true; // nouvelle image : le « précédent » change même si l'objet est immobile
        }
        lastWorld_ = world;
        hasLastWorld_ = true;
        tracked = tracked || previousWorld_ != uploadedPreviousWorld_ || history.previousOrigin != uploadedPreviousOrigin_;
    }
    const bool withHistory = history.frame != 0; // le bloc écrit avec historique a un prevModel distinct : à réécrire sans
    if (!transformDirty_ && origin == uploadedOrigin_ && !tracked && withHistory == uploadedWithHistory_)
    {
        return {};
    }
    // Les normales se transforment par l'inverse transposée de la partie 3x3 (juste aussi avec une échelle non uniforme) ; calculée
    // une fois par objet ici plutôt qu'à chaque sommet. Matrice non inversible (échelle nulle) : identité, la normale est inutile.
    ModelBlock block{world, glm::mat4(1.0f), world, glm::uvec4(objectId_, 0u, 0u, 0u)};
    block.model[3] -= glm::vec4(origin, 0.0f); // translation relative à `origin` (0 : repère monde)
    block.prevModel = block.model;
    if (history.frame != 0)
    {
        block.prevModel = previousWorld_;
        block.prevModel[3] -= glm::vec4(history.previousOrigin, 0.0f);
    }
    const glm::mat3 linear(block.model);
    if (glm::determinant(linear) != 0.0f)
    {
        block.normalMatrix = glm::mat4(glm::transpose(glm::inverse(linear)));
    }
    std::memcpy(modelBlock_.data(), &block, sizeof block);
    bufferStale_ = true;
    transformDirty_ = false;
    uploadedOrigin_ = origin;
    uploadedWithHistory_ = withHistory;
    uploadedPreviousWorld_ = history.frame != 0 ? previousWorld_ : world;
    uploadedPreviousOrigin_ = history.previousOrigin;
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
    if (ownsTextureGroup_)
    {
        device_->destroy(textureGroup_);
    }
    textureGroup_ = fresh;
    ownsTextureGroup_ = true;
    return {};
}

void Mesh::setMaterialGroup(rhi::BindGroupHandle group) noexcept
{
    if (device_ != nullptr && ownsTextureGroup_)
    {
        device_->destroy(textureGroup_);
    }
    textureGroup_ = group;
    ownsTextureGroup_ = false;
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
        if (ownsTextureGroup_)
        {
            device_->destroy(textureGroup_);
        }
    }
    vertexBuffer_.reset();
    indexBuffer_.reset();
    instanceBuffer_.reset();
    modelBuffer_.reset();
    modelGroup_ = {};
    textureGroup_ = {};
    ownsTextureGroup_ = true;
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
    std::swap(ownsTextureGroup_, other.ownsTextureGroup_);
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
    std::swap(bufferStale_, other.bufferStale_);
    std::swap(uploadedOrigin_, other.uploadedOrigin_); // va avec modelBuffer_ : l'origine des matrices déjà envoyées
    std::swap(uploadedWithHistory_, other.uploadedWithHistory_);
    std::swap(uploadedPreviousWorld_, other.uploadedPreviousWorld_);
    std::swap(uploadedPreviousOrigin_, other.uploadedPreviousOrigin_);
    std::swap(previousWorld_, other.previousWorld_);
    std::swap(lastWorld_, other.lastWorld_);
    std::swap(hasLastWorld_, other.hasLastWorld_);
    std::swap(lastFrame_, other.lastFrame_);
    std::swap(objectId_, other.objectId_);
    std::swap(aovMaterial_, other.aovMaterial_);
    std::swap(modelBlock_, other.modelBlock_);
}

} // namespace orosmo::renderer
