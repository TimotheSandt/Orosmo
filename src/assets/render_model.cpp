#include "orosmo/assets/render_model.hpp"

#include "orosmo/core/log.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>

namespace orosmo::assets
{

using core::ErrorCode;
using core::makeError;

renderer::BlendMode blendModeForAlpha(AlphaMode mode) noexcept
{
    switch (mode)
    {
        case AlphaMode::kMask: return renderer::BlendMode::kMask;
        case AlphaMode::kBlend: return renderer::BlendMode::kBlend;
        default: return renderer::BlendMode::kOpaque;
    }
}

namespace
{

rhi::AddressMode addressOf(WrapMode mode) noexcept
{
    switch (mode)
    {
        case WrapMode::kClampToEdge: return rhi::AddressMode::kClampToEdge;
        case WrapMode::kMirroredRepeat: return rhi::AddressMode::kMirroredRepeat;
        default: return rhi::AddressMode::kRepeat;
    }
}

template <typename T> bool inBounds(int index, const std::vector<T> &items) noexcept
{
    return index >= 0 && static_cast<std::size_t>(index) < items.size();
}

// Identité d'une image incluse : ses octets (FNV-1a 64 bits) et sa taille. Deux modèles de même `sourceKey` dont l'image N
// diffère n'ont pas la même clé ; deux références à la même image la partagent.
std::string embeddedImageKey(const std::string &sourceKey, int image, std::span<const std::byte> bytes)
{
    std::uint64_t hash = 14695981039346656037ull;
    for (const std::byte b : bytes)
    {
        hash = (hash ^ std::to_integer<std::uint64_t>(b)) * 1099511628211ull;
    }
    char digest[17];
    static constexpr char kHex[] = "0123456789abcdef";
    for (int i = 15; i >= 0; --i, hash >>= 4)
    {
        digest[i] = kHex[hash & 0xF];
    }
    digest[16] = '\0';
    return sourceKey + "#image" + std::to_string(image) + "#" + std::to_string(bytes.size()) + "-" + digest;
}

core::Result<std::shared_ptr<const renderer::Texture>> textureFor(const Model &model, const TextureRef &ref, TextureRole role, TextureCache &cache,
                                                                  const std::string &sourceKey)
{
    if (!ref.valid())
    {
        return std::shared_ptr<const renderer::Texture>();
    }
    // `pbrMaterialDesc` est publique : le modèle n'est pas forcément passé par `Model::validate`.
    if (!inBounds(ref.texture, model.textures))
    {
        return makeError(ErrorCode::kOutOfRange, "glTF: " + sourceKey + ": texture " + std::to_string(ref.texture) + " does not exist");
    }
    const TextureData &tex = model.textures[static_cast<std::size_t>(ref.texture)];
    if (tex.image < 0)
    {
        return std::shared_ptr<const renderer::Texture>(); // texture sans image utilisable (ex. KTX2 seul, lot G)
    }
    if (!inBounds(tex.image, model.images))
    {
        return makeError(ErrorCode::kOutOfRange, "glTF: " + sourceKey + ": image " + std::to_string(tex.image) + " does not exist");
    }
    if (tex.sampler >= 0 && !inBounds(tex.sampler, model.samplers))
    {
        return makeError(ErrorCode::kOutOfRange, "glTF: " + sourceKey + ": sampler " + std::to_string(tex.sampler) + " does not exist");
    }
    if (ref.texCoord != 0)
    {
        LOG_WARNING("glTF: ", sourceKey, ": texture uses TEXCOORD_", ref.texCoord, ", read with TEXCOORD_0 (one UV set in PBR meshes)");
    }
    TextureRequest request;
    request.role = role;
    if (tex.sampler >= 0)
    {
        const SamplerData &s = model.samplers[static_cast<std::size_t>(tex.sampler)];
        request.address = addressOf(s.wrapS); // un seul mode pour U et V (TextureCache)
        request.filter = s.nearest ? renderer::FilterMode::kNearest : renderer::FilterMode::kLinear;
    }
    const ImageData &image = model.images[static_cast<std::size_t>(tex.image)];
    core::Result<std::shared_ptr<renderer::Texture>> loaded =
        !image.encoded.empty() ? cache.loadFromMemory(embeddedImageKey(sourceKey, tex.image, image.encoded), image.encoded, request)
                               : cache.load(image.path, request);
    if (!loaded)
    {
        return std::unexpected(loaded.error());
    }
    return std::shared_ptr<const renderer::Texture>(*loaded);
}

} // namespace

core::Result<renderer::PbrMaterialDesc> pbrMaterialDesc(const Model &model, const MaterialData &material, TextureCache &textures,
                                                       const std::string &sourceKey)
{
    renderer::PbrMaterialDesc desc;
    desc.name = material.name.empty() ? sourceKey + ".material" : material.name;
    desc.params.baseColorFactor = material.baseColorFactor;
    desc.params.metallic = material.metallicFactor;
    desc.params.roughness = material.roughnessFactor;
    desc.params.normalScale = material.normal.scale;
    desc.params.occlusionStrength = material.occlusion.scale;
    desc.params.emissive = material.emissiveFactor * material.emissiveStrength;
    desc.params.alphaCutoff = material.alphaCutoff;
    desc.blend = blendModeForAlpha(material.alphaMode);
    desc.doubleSided = material.doubleSided;
    desc.unlit = material.unlit;
    const std::pair<const TextureRef *, TextureRole> maps[renderer::kPbrMapCount] = {
        {&material.baseColor, TextureRole::kColor}, {&material.metallicRoughness, TextureRole::kData}, {&material.normal, TextureRole::kNormal},
        {&material.occlusion, TextureRole::kData},  {&material.emissive, TextureRole::kColor},
    };
    for (std::size_t i = 0; i < renderer::kPbrMapCount; ++i)
    {
        auto texture = textureFor(model, *maps[i].first, maps[i].second, textures, sourceKey);
        if (!texture)
        {
            return std::unexpected(texture.error());
        }
        desc.maps[i] = std::move(*texture);
    }
    return desc;
}

std::optional<MeshPrimitive> flatShaded(const MeshPrimitive &p)
{
    if (!p.normals.empty() && p.normals.size() == p.positions.size())
    {
        return std::nullopt;
    }
    const std::size_t n = p.positions.size();
    MeshPrimitive out;
    out.material = p.material;
    out.boundsMin = p.boundsMin;
    out.boundsMax = p.boundsMax;
    std::vector<std::uint32_t> corners; // sommets source des triangles gardés (un triangle hors bornes est ignoré)
    corners.reserve(p.indices.size());
    for (std::size_t t = 0; t + 2 < p.indices.size(); t += 3)
    {
        if (p.indices[t] < n && p.indices[t + 1] < n && p.indices[t + 2] < n)
        {
            corners.insert(corners.end(), {p.indices[t], p.indices[t + 1], p.indices[t + 2]});
        }
    }
    const std::size_t count = corners.size();
    auto copy = [&](const auto &from, auto &to)
    {
        if (from.size() == n)
        {
            to.reserve(count);
            for (const std::uint32_t c : corners)
            {
                to.push_back(from[c]);
            }
        }
    };
    copy(p.positions, out.positions);
    copy(p.uv0, out.uv0);
    copy(p.uv1, out.uv1);
    copy(p.colors, out.colors);
    copy(p.joints, out.joints);
    copy(p.weights, out.weights);
    out.normals.reserve(count);
    out.indices.reserve(count);
    for (std::size_t t = 0; t < count; t += 3)
    {
        const core::Vec3 &a = out.positions[t];
        const core::Vec3 face = glm::cross(out.positions[t + 1] - a, out.positions[t + 2] - a);
        const core::Vec3 normal = glm::dot(face, face) > 0.0f ? glm::normalize(face) : core::Vec3(0.0f, 1.0f, 0.0f);
        out.normals.insert(out.normals.end(), {normal, normal, normal});
    }
    for (std::size_t i = 0; i < count; ++i)
    {
        out.indices.push_back(static_cast<std::uint32_t>(i));
    }
    return out;
}

std::vector<float> pbrVertices(const MeshPrimitive &p)
{
    const std::size_t n = p.positions.size();
    std::vector<core::Vec3> normals = p.normals;
    if (normals.size() != n)
    {
        normals.assign(n, core::Vec3(0.0f));
        for (std::size_t t = 0; t + 2 < p.indices.size(); t += 3)
        {
            const auto a = p.indices[t], b = p.indices[t + 1], c = p.indices[t + 2];
            const core::Vec3 face = glm::cross(p.positions[b] - p.positions[a], p.positions[c] - p.positions[a]); // pondérée par l'aire
            normals[a] += face;
            normals[b] += face;
            normals[c] += face;
        }
        for (auto &v : normals)
        {
            v = glm::dot(v, v) > 0.0f ? glm::normalize(v) : core::Vec3(0.0f, 1.0f, 0.0f);
        }
    }
    std::vector<core::Vec4> tangents = p.tangents;
    if (tangents.size() != n)
    {
        tangents.assign(n, core::Vec4(1.0f, 0.0f, 0.0f, 1.0f));
        if (p.uv0.size() == n)
        {
            std::vector<core::Vec3> tan(n, core::Vec3(0.0f));
            std::vector<core::Vec3> bitan(n, core::Vec3(0.0f));
            for (std::size_t t = 0; t + 2 < p.indices.size(); t += 3)
            {
                const auto a = p.indices[t], b = p.indices[t + 1], c = p.indices[t + 2];
                const core::Vec3 e1 = p.positions[b] - p.positions[a];
                const core::Vec3 e2 = p.positions[c] - p.positions[a];
                const core::Vec2 d1 = p.uv0[b] - p.uv0[a];
                const core::Vec2 d2 = p.uv0[c] - p.uv0[a];
                const float det = d1.x * d2.y - d2.x * d1.y;
                if (std::abs(det) < 1e-12f)
                {
                    continue;
                }
                const float r = 1.0f / det;
                const core::Vec3 sdir = (e1 * d2.y - e2 * d1.y) * r;
                const core::Vec3 tdir = (e2 * d1.x - e1 * d2.x) * r;
                for (auto v : {a, b, c})
                {
                    tan[v] += sdir;
                    bitan[v] += tdir;
                }
            }
            for (std::size_t v = 0; v < n; ++v)
            {
                const core::Vec3 nv = normals[v];
                core::Vec3 t = tan[v] - nv * glm::dot(nv, tan[v]);
                if (glm::dot(t, t) < 1e-12f)
                {
                    continue;
                }
                t = glm::normalize(t);
                const float w = glm::dot(glm::cross(nv, t), bitan[v]) < 0.0f ? -1.0f : 1.0f;
                tangents[v] = core::Vec4(t, w);
            }
        }
    }
    std::vector<float> out;
    out.reserve(n * renderer::kPbrVertexFloats);
    for (std::size_t v = 0; v < n; ++v)
    {
        const core::Vec3 &pos = p.positions[v];
        const core::Vec2 uv = p.uv0.size() == n ? p.uv0[v] : core::Vec2(0.0f);
        const core::Vec4 color = p.colors.size() == n ? p.colors[v] : core::Vec4(1.0f);
        out.insert(out.end(), {pos.x, pos.y, pos.z, normals[v].x, normals[v].y, normals[v].z, uv.x, uv.y, tangents[v].x, tangents[v].y,
                               tangents[v].z, tangents[v].w, color.r, color.g, color.b, color.a});
    }
    return out;
}

core::Result<RenderModel> RenderModel::create(rhi::Device &device, const Model &model, TextureCache &textures, renderer::PbrShaderCache &shaders,
                                              std::shared_ptr<const renderer::PbrDefaults> defaults, const RenderModelOptions &options)
{
    if (auto valid = model.validate(); !valid)
    {
        return std::unexpected(valid.error());
    }
    RenderModel out;
    // Matériaux : un par matériau du fichier, plus le matériau par défaut de glTF (blanc, métal 1, rugosité 1) en dernier.
    for (const MaterialData &material : model.materials)
    {
        auto desc = pbrMaterialDesc(model, material, textures, options.sourceKey);
        if (!desc)
        {
            return std::unexpected(desc.error());
        }
        auto created = renderer::PbrMaterial::create(device, std::move(*desc), defaults, options.shadows);
        if (!created)
        {
            return std::unexpected(created.error());
        }
        out.materials_.push_back(std::move(*created));
    }
    renderer::PbrMaterialDesc fallback;
    fallback.name = options.sourceKey + ".default";
    auto defaultMaterial = renderer::PbrMaterial::create(device, fallback, defaults, options.shadows);
    if (!defaultMaterial)
    {
        return std::unexpected(defaultMaterial.error());
    }
    out.materials_.push_back(std::move(*defaultMaterial));

    std::vector<std::vector<std::size_t>> meshPrimitives(model.meshes.size());
    for (std::size_t m = 0; m < model.meshes.size(); ++m)
    {
        for (std::size_t p = 0; p < model.meshes[m].primitives.size(); ++p)
        {
            const std::optional<MeshPrimitive> flat = flatShaded(model.meshes[m].primitives[p]);
            const MeshPrimitive &prim = flat ? *flat : model.meshes[m].primitives[p];
            if (prim.indices.empty())
            {
                continue;
            }
            renderer::MeshData data;
            data.vertices = pbrVertices(prim);
            data.indices = prim.indices;
            data.attributeSizes.assign(renderer::kPbrVertexAttributeSizes.begin(), renderer::kPbrVertexAttributeSizes.end());
            data.name = options.sourceKey + "." + model.meshes[m].name + "." + std::to_string(p);
            if (options.skinnable && !prim.joints.empty())
            {
                data.extraVertexUsage = rhi::BufferUsage::kStorage; // écrit par le skinning GPU (ANIM-01)
            }
            auto mesh = renderer::Mesh::create(device, data);
            if (!mesh)
            {
                return std::unexpected(mesh.error());
            }
            const auto &material = prim.material >= 0 ? out.materials_[static_cast<std::size_t>(prim.material)] : out.materials_.back();
            auto program = shaders.program(material->features(), options.extraDefines);
            if (!program)
            {
                return std::unexpected(program.error());
            }
            mesh->setProgram(*program);
            mesh->setMaterialGroup(material->group());
            mesh->setAovMaterial({material->blend(), {}});
            mesh->setCullMode(material->doubleSided() ? rhi::CullMode::kNone : rhi::CullMode::kBack);
            meshPrimitives[m].push_back(out.meshes_.size());
            out.sources_.push_back({m, p});
            out.meshes_.push_back(std::make_unique<renderer::Mesh>(std::move(*mesh)));
        }
    }

    // Nœuds dessinés : ceux de la scène choisie (parcours en profondeur), ou tous si le fichier n'a pas de scène.
    const int sceneIndex = options.scene >= 0 ? options.scene : model.defaultScene;
    if (sceneIndex >= static_cast<int>(model.scenes.size()))
    {
        return makeError(ErrorCode::kOutOfRange, "RenderModel: scene " + std::to_string(sceneIndex) + " does not exist");
    }
    std::vector<int> nodes;
    if (sceneIndex >= 0)
    {
        std::vector<int> stack(model.scenes[static_cast<std::size_t>(sceneIndex)].roots.rbegin(),
                               model.scenes[static_cast<std::size_t>(sceneIndex)].roots.rend());
        while (!stack.empty())
        {
            const int n = stack.back();
            stack.pop_back();
            nodes.push_back(n);
            const auto &children = model.nodes[static_cast<std::size_t>(n)].children;
            stack.insert(stack.end(), children.rbegin(), children.rend());
        }
    }
    else
    {
        for (std::size_t n = 0; n < model.nodes.size(); ++n)
        {
            nodes.push_back(static_cast<int>(n));
        }
    }
    for (int n : nodes)
    {
        const int mesh = model.nodes[static_cast<std::size_t>(n)].mesh;
        if (mesh < 0)
        {
            continue;
        }
        for (std::size_t gpu : meshPrimitives[static_cast<std::size_t>(mesh)])
        {
            out.items_.push_back({n, gpu});
        }
    }
    out.world_ = model.worldMatrices();
    return out;
}

core::Result<> RenderModel::setNodeMatrices(std::vector<glm::mat4> world)
{
    if (world.size() != world_.size())
    {
        return makeError(ErrorCode::kInvalidArgument, "RenderModel::setNodeMatrices: expected " + std::to_string(world_.size()) + " matrices");
    }
    world_ = std::move(world);
    return {};
}

core::Result<> RenderModel::draw(renderer::SceneRenderer &scene, const renderer::Camera &camera)
{
    for (const Item &item : items_)
    {
        renderer::Mesh &mesh = *meshes_[item.mesh];
        mesh.setModelMatrix(root_ * world_[static_cast<std::size_t>(item.node)]);
        if (auto r = scene.drawMesh(mesh, camera); !r)
        {
            return r;
        }
    }
    return {};
}

} // namespace orosmo::assets
