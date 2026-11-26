// Construction de maillages (A75.5, M8-L11) : sommets et triangles décrits par position, normale, uv et couleur, tangentes calculées
// pour la disposition PBR (`kPbrVertexAttributeSizes`), boîte et plan prêts à l'emploi. Produit une `MeshData` (ou directement un
// `Mesh`) ; ne touche pas au GPU avant `create`. Les faces avant sont dans le sens anti-horaire (glTF, `Mesh::frontFace`).
#pragma once

#include "orosmo/core/error.hpp"
#include "orosmo/renderer/mesh.hpp"
#include "orosmo/rhi/device.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace orosmo::renderer
{

// Disposition des sommets produite par le constructeur.
enum class MeshLayout : std::uint8_t
{
    kColored, // position 3, normale 3, couleur 3 : programme par défaut de la scène (mesh.vert / mesh.frag)
    kPbr,     // position 3, normale 3, uv 2, tangente 4 (w = signe), couleur 4 : mesh_pbr.vert (`kPbrVertexAttributeSizes`)
};

struct MeshVertex
{
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec2 uv{0.0f};
    glm::vec4 color{1.0f};
};

class MeshBuilder
{
  public:
    explicit MeshBuilder(MeshLayout layout = MeshLayout::kPbr) : layout_(layout) {}

    MeshBuilder &setName(std::string name)
    {
        name_ = std::move(name);
        return *this;
    }
    // Ajoute un sommet ; renvoie son index.
    std::uint32_t addVertex(const MeshVertex &vertex);
    MeshBuilder &addTriangle(std::uint32_t a, std::uint32_t b, std::uint32_t c);
    // Quadrilatère a b c d (anti-horaire vu de l'avant) : triangles (a, b, c) et (a, c, d).
    MeshBuilder &addQuad(std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d);
    // Boîte de demi-côtés `halfExtents` centrée en `center` : 24 sommets (normales et uv par face), faces tournées vers l'extérieur.
    MeshBuilder &addBox(const glm::vec3 &halfExtents, const glm::vec4 &color = glm::vec4(1.0f), const glm::vec3 &center = glm::vec3(0.0f));
    // Plan horizontal (normale +Y) de taille `size` (X, Z) centré en `center`, uv de 0 à `uvScale`.
    MeshBuilder &addPlane(const glm::vec2 &size, const glm::vec4 &color = glm::vec4(1.0f), const glm::vec3 &center = glm::vec3(0.0f),
                          const glm::vec2 &uvScale = glm::vec2(1.0f));

    MeshLayout layout() const noexcept { return layout_; }
    std::size_t vertexCount() const noexcept { return vertices_.size(); }
    std::size_t indexCount() const noexcept { return indices_.size(); }
    const std::vector<MeshVertex> &vertices() const noexcept { return vertices_; }
    const std::vector<std::uint32_t> &indices() const noexcept { return indices_; }

    // Données entrelacées de la disposition choisie (tangentes calculées pour kPbr). kInvalidArgument sans triangle, si le nombre
    // d'index n'est pas un multiple de 3 ou si un index dépasse les sommets.
    core::Result<MeshData> build() const;
    // `build`, puis `Mesh::create`.
    core::Result<Mesh> create(rhi::Device &device) const;

    // Tangentes par sommet (xyz unitaire orthogonal à la normale, w = ±1 : signe de la bitangente, glTF) accumulées sur les
    // triangles puis orthonormalisées (Gram-Schmidt). Un sommet sans direction d'uv exploitable reçoit une tangente perpendiculaire
    // quelconque à sa normale (déterministe). Tailles : positions, normales et uv de même longueur.
    static std::vector<glm::vec4> computeTangents(std::span<const glm::vec3> positions, std::span<const glm::vec3> normals,
                                                  std::span<const glm::vec2> uvs, std::span<const std::uint32_t> indices);

  private:
    MeshLayout layout_;
    std::string name_ = "mesh";
    std::vector<MeshVertex> vertices_;
    std::vector<std::uint32_t> indices_;
};

} // namespace orosmo::renderer
