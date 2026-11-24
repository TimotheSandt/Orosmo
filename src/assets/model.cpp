#include "orosmo/assets/model.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <string>

namespace orosmo::assets
{

using core::ErrorCode;
using core::makeError;

core::Mat4 NodeData::localMatrix() const
{
    return glm::translate(core::Mat4(1.0f), translation) * glm::mat4_cast(rotation) * glm::scale(core::Mat4(1.0f), scale);
}

std::vector<core::Mat4> Model::worldMatrices() const
{
    std::vector<core::Mat4> world(nodes.size(), core::Mat4(1.0f));
    std::vector<char> done(nodes.size(), 0);
    // Parcours itératif depuis chaque nœud vers sa racine : pas de récursion (hiérarchies profondes), chaque nœud calculé une fois.
    std::vector<int> chain;
    for (std::size_t i = 0; i < nodes.size(); ++i)
    {
        chain.clear();
        int n = static_cast<int>(i);
        while (n >= 0 && done[static_cast<std::size_t>(n)] == 0 && chain.size() <= nodes.size())
        {
            chain.push_back(n);
            n = nodes[static_cast<std::size_t>(n)].parent;
        }
        for (auto it = chain.rbegin(); it != chain.rend(); ++it)
        {
            const auto idx = static_cast<std::size_t>(*it);
            const int parent = nodes[idx].parent;
            world[idx] = parent >= 0 ? world[static_cast<std::size_t>(parent)] * nodes[idx].localMatrix() : nodes[idx].localMatrix();
            done[idx] = 1;
        }
    }
    return world;
}

ModelStats Model::stats() const
{
    ModelStats s;
    for (const auto &mesh : meshes)
    {
        for (const auto &prim : mesh.primitives)
        {
            s.vertices += prim.positions.size();
            s.triangles += prim.indices.size() / 3;
            ++s.primitives;
        }
    }
    return s;
}

namespace
{

bool inRange(int index, std::size_t size) noexcept { return index >= -1 && index < static_cast<int>(size); }

} // namespace

core::Result<> Model::validate() const
{
    auto fail = [](const std::string &what) { return makeError(ErrorCode::kParse, "Model: " + what); };
    for (std::size_t m = 0; m < meshes.size(); ++m)
    {
        for (const auto &prim : meshes[m].primitives)
        {
            const std::size_t n = prim.positions.size();
            const auto badLength = [n](std::size_t size) { return size != 0 && size != n; };
            if (badLength(prim.normals.size()) || badLength(prim.tangents.size()) || badLength(prim.uv0.size()) ||
                badLength(prim.uv1.size()) || badLength(prim.colors.size()) || badLength(prim.joints.size()) ||
                badLength(prim.weights.size()) || prim.joints.size() != prim.weights.size())
            {
                return fail("mesh " + std::to_string(m) + ": attribute lengths differ from POSITION");
            }
            if (prim.indices.size() % 3 != 0)
            {
                return fail("mesh " + std::to_string(m) + ": index count is not a multiple of 3");
            }
            for (std::uint32_t idx : prim.indices)
            {
                if (idx >= n)
                {
                    return fail("mesh " + std::to_string(m) + ": index " + std::to_string(idx) + " out of range");
                }
            }
            if (!inRange(prim.material, materials.size()))
            {
                return fail("mesh " + std::to_string(m) + ": material out of range");
            }
        }
    }
    for (const auto &mat : materials)
    {
        for (const TextureRef *ref : {&mat.baseColor, &mat.metallicRoughness, &mat.normal, &mat.occlusion, &mat.emissive})
        {
            if (!inRange(ref->texture, textures.size()))
            {
                return fail("material " + mat.name + ": texture out of range");
            }
        }
    }
    for (const auto &tex : textures)
    {
        if (!inRange(tex.image, images.size()) || !inRange(tex.sampler, samplers.size()))
        {
            return fail("texture: image or sampler out of range");
        }
    }
    for (std::size_t i = 0; i < nodes.size(); ++i)
    {
        const NodeData &n = nodes[i];
        if (!inRange(n.parent, nodes.size()) || !inRange(n.mesh, meshes.size()) || !inRange(n.skin, skins.size()) ||
            !inRange(n.camera, cameras.size()) || !inRange(n.light, lights.size()))
        {
            return fail("node " + std::to_string(i) + ": reference out of range");
        }
        for (int c : n.children)
        {
            if (c < 0 || c >= static_cast<int>(nodes.size()) || nodes[static_cast<std::size_t>(c)].parent != static_cast<int>(i))
            {
                return fail("node " + std::to_string(i) + ": child " + std::to_string(c) + " is out of range or has another parent");
            }
        }
    }
    // Pas de cycle : en remontant les parents, on sort en au plus nodes.size() pas.
    for (std::size_t i = 0; i < nodes.size(); ++i)
    {
        int n = static_cast<int>(i);
        std::size_t steps = 0;
        while (n >= 0 && steps <= nodes.size())
        {
            n = nodes[static_cast<std::size_t>(n)].parent;
            ++steps;
        }
        if (n >= 0)
        {
            return fail("node hierarchy has a cycle");
        }
    }
    for (const auto &skin : skins)
    {
        if (skin.inverseBindMatrices.size() != skin.joints.size() || !inRange(skin.skeleton, nodes.size()))
        {
            return fail("skin " + skin.name + ": joints and inverse bind matrices differ");
        }
        for (int j : skin.joints)
        {
            if (j < 0 || j >= static_cast<int>(nodes.size()))
            {
                return fail("skin " + skin.name + ": joint out of range");
            }
        }
    }
    for (const auto &anim : animations)
    {
        for (const auto &ch : anim.channels)
        {
            const std::size_t groups = ch.interpolation == Interpolation::kCubicSpline ? 3 : 1;
            if (ch.node < 0 || ch.node >= static_cast<int>(nodes.size()) || ch.times.empty() ||
                ch.values.size() != ch.times.size() * groups * ch.componentCount)
            {
                return fail("animation " + anim.name + ": channel node or key count is wrong");
            }
        }
    }
    for (const auto &scene : scenes)
    {
        for (int r : scene.roots)
        {
            if (r < 0 || r >= static_cast<int>(nodes.size()))
            {
                return fail("scene " + scene.name + ": root out of range");
            }
        }
    }
    if (!inRange(defaultScene, scenes.size()))
    {
        return fail("default scene out of range");
    }
    return {};
}

} // namespace orosmo::assets
