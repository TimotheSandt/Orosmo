#include "orosmo/renderer/mesh_builder.hpp"

#include "orosmo/renderer/material.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace orosmo::renderer
{

using core::ErrorCode;
using core::makeError;

std::uint32_t MeshBuilder::addVertex(const MeshVertex &vertex)
{
    vertices_.push_back(vertex);
    return static_cast<std::uint32_t>(vertices_.size() - 1);
}

MeshBuilder &MeshBuilder::addTriangle(std::uint32_t a, std::uint32_t b, std::uint32_t c)
{
    indices_.insert(indices_.end(), {a, b, c});
    return *this;
}

MeshBuilder &MeshBuilder::addQuad(std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d)
{
    indices_.insert(indices_.end(), {a, b, c, a, c, d});
    return *this;
}

MeshBuilder &MeshBuilder::addBox(const glm::vec3 &halfExtents, const glm::vec4 &color, const glm::vec3 &center)
{
    std::array<glm::vec4, 6> colors;
    colors.fill(color);
    return addBox(halfExtents, colors, center);
}

MeshBuilder &MeshBuilder::addBox(const glm::vec3 &halfExtents, const std::array<glm::vec4, 6> &faceColors, const glm::vec3 &center)
{
    // Une face par normale : `u` et `v` couvrent la face, u × v = normale (sommets anti-horaires vus de l'extérieur).
    struct Face
    {
        glm::vec3 normal;
        glm::vec3 u;
        glm::vec3 v;
    };
    static const std::array<Face, 6> kFaces{{
        {{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}},
        {{-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
        {{0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
        {{0.0f, -1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
        {{0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
        {{0.0f, 0.0f, -1.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
    }};
    for (std::size_t f = 0; f < kFaces.size(); ++f)
    {
        const Face &face = kFaces[f];
        const glm::vec4 &color = faceColors[f];
        const glm::vec3 n = face.normal * halfExtents;
        const glm::vec3 u = face.u * halfExtents;
        const glm::vec3 v = face.v * halfExtents;
        const std::uint32_t a = addVertex({center + n - u - v, face.normal, {0.0f, 1.0f}, color});
        const std::uint32_t b = addVertex({center + n + u - v, face.normal, {1.0f, 1.0f}, color});
        const std::uint32_t c = addVertex({center + n + u + v, face.normal, {1.0f, 0.0f}, color});
        const std::uint32_t d = addVertex({center + n - u + v, face.normal, {0.0f, 0.0f}, color});
        addQuad(a, b, c, d);
    }
    return *this;
}

MeshBuilder &MeshBuilder::addPlane(const glm::vec2 &size, const glm::vec4 &color, const glm::vec3 &center, const glm::vec2 &uvScale)
{
    const glm::vec3 normal{0.0f, 1.0f, 0.0f};
    const float x = size.x * 0.5f;
    const float z = size.y * 0.5f;
    // Vu d'au-dessus (+Y), le sens anti-horaire va de +X vers -Z.
    const std::uint32_t a = addVertex({center + glm::vec3(-x, 0.0f, z), normal, {0.0f, uvScale.y}, color});
    const std::uint32_t b = addVertex({center + glm::vec3(x, 0.0f, z), normal, {uvScale.x, uvScale.y}, color});
    const std::uint32_t c = addVertex({center + glm::vec3(x, 0.0f, -z), normal, {uvScale.x, 0.0f}, color});
    const std::uint32_t d = addVertex({center + glm::vec3(-x, 0.0f, -z), normal, {0.0f, 0.0f}, color});
    return addQuad(a, b, c, d);
}

std::vector<glm::vec4> MeshBuilder::computeTangents(std::span<const glm::vec3> positions, std::span<const glm::vec3> normals,
                                                    std::span<const glm::vec2> uvs, std::span<const std::uint32_t> indices)
{
    const std::size_t count = positions.size();
    std::vector<glm::vec3> tangents(count, glm::vec3(0.0f));
    std::vector<glm::vec3> bitangents(count, glm::vec3(0.0f));
    if (normals.size() != count || uvs.size() != count)
    {
        return std::vector<glm::vec4>(count, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    }
    for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
    {
        const std::uint32_t i0 = indices[i];
        const std::uint32_t i1 = indices[i + 1];
        const std::uint32_t i2 = indices[i + 2];
        if (i0 >= count || i1 >= count || i2 >= count)
        {
            continue;
        }
        const glm::vec3 e1 = positions[i1] - positions[i0];
        const glm::vec3 e2 = positions[i2] - positions[i0];
        const glm::vec2 d1 = uvs[i1] - uvs[i0];
        const glm::vec2 d2 = uvs[i2] - uvs[i0];
        const float det = d1.x * d2.y - d2.x * d1.y;
        if (std::fabs(det) < 1e-12f)
        {
            continue; // uv dégénérés : ce triangle ne donne pas de direction
        }
        const float r = 1.0f / det;
        const glm::vec3 t = (e1 * d2.y - e2 * d1.y) * r;
        const glm::vec3 b = (e2 * d1.x - e1 * d2.x) * r;
        for (const std::uint32_t v : {i0, i1, i2})
        {
            tangents[v] += t;
            bitangents[v] += b;
        }
    }
    std::vector<glm::vec4> result(count);
    for (std::size_t v = 0; v < count; ++v)
    {
        const glm::vec3 n = glm::length(normals[v]) > 0.0f ? glm::normalize(normals[v]) : glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 t = tangents[v] - n * glm::dot(n, tangents[v]); // Gram-Schmidt
        if (glm::length(t) < 1e-6f)
        {
            // Perpendiculaire quelconque, choisie de façon stable : l'axe le moins aligné avec la normale.
            const glm::vec3 axis = std::fabs(n.x) < 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
            t = axis - n * glm::dot(n, axis);
        }
        // Mise à l'échelle par la plus grande composante avant la normalisation : dot(t, t) ne déborde pas en float (revue PR #37).
        const float largest = std::max({std::fabs(t.x), std::fabs(t.y), std::fabs(t.z)});
        t = glm::normalize(t / largest);
        const float w = glm::dot(glm::cross(n, t), bitangents[v]) < 0.0f ? -1.0f : 1.0f;
        result[v] = glm::vec4(t, w);
    }
    return result;
}

core::Result<MeshData> MeshBuilder::build() const
{
    if (indices_.empty() || indices_.size() % 3 != 0)
    {
        return makeError(ErrorCode::kInvalidArgument, "MeshBuilder::build: '" + name_ + "' needs whole triangles (" +
                                                          std::to_string(indices_.size()) + " indices)");
    }
    for (const std::uint32_t index : indices_)
    {
        if (index >= vertices_.size())
        {
            return makeError(ErrorCode::kInvalidArgument, "MeshBuilder::build: '" + name_ + "' index " + std::to_string(index) +
                                                              " is out of range (" + std::to_string(vertices_.size()) + " vertices)");
        }
    }
    MeshData data;
    data.name = name_;
    data.indices = indices_;
    if (layout_ == MeshLayout::kColored)
    {
        data.attributeSizes = {3, 3, 3};
        data.vertices.reserve(vertices_.size() * 9);
        for (const MeshVertex &v : vertices_)
        {
            data.vertices.insert(data.vertices.end(), {v.position.x, v.position.y, v.position.z, v.normal.x, v.normal.y, v.normal.z, v.color.r,
                                                       v.color.g, v.color.b});
        }
        return data;
    }
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> uvs;
    positions.reserve(vertices_.size());
    normals.reserve(vertices_.size());
    uvs.reserve(vertices_.size());
    for (const MeshVertex &v : vertices_)
    {
        positions.push_back(v.position);
        normals.push_back(v.normal);
        uvs.push_back(v.uv);
    }
    const std::vector<glm::vec4> tangents = computeTangents(positions, normals, uvs, indices_);
    data.attributeSizes.assign(kPbrVertexAttributeSizes.begin(), kPbrVertexAttributeSizes.end());
    data.vertices.reserve(vertices_.size() * kPbrVertexFloats);
    for (std::size_t i = 0; i < vertices_.size(); ++i)
    {
        const MeshVertex &v = vertices_[i];
        const glm::vec4 &t = tangents[i];
        data.vertices.insert(data.vertices.end(), {v.position.x, v.position.y, v.position.z, v.normal.x, v.normal.y, v.normal.z, v.uv.x, v.uv.y,
                                                   t.x, t.y, t.z, t.w, v.color.r, v.color.g, v.color.b, v.color.a});
    }
    return data;
}

core::Result<Mesh> MeshBuilder::create(rhi::Device &device) const
{
    auto data = build();
    if (!data)
    {
        return std::unexpected(data.error());
    }
    return Mesh::create(device, *data);
}

} // namespace orosmo::renderer
