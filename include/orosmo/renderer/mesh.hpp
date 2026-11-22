// Maillage : géométrie (sommets, index, instances optionnelles), transformation, textures liées, constantes de shader. Se dessine
// par `SceneRenderer::drawMesh` ; ne garde aucun état global et ne se « lie » pas.
#pragma once

#include "orosmo/core/error.hpp"
#include "orosmo/renderer/aov.hpp"
#include "orosmo/renderer/gpu_buffer.hpp"
#include "orosmo/renderer/shader_library.hpp"
#include "orosmo/rhi/device.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <span>
#include <string>
#include <vector>

namespace orosmo::renderer
{

struct MeshData
{
    std::vector<float> vertices;
    std::vector<std::uint32_t> indices;
    // Taille en flottants (1 à 4) de chaque attribut ; emplacement 0, 1, 2... dans l'ordre. Le pas est leur somme.
    std::vector<std::uint32_t> attributeSizes;
    // Attributs par instance (emplacements à la suite des attributs de sommet) ; vide : pas d'instanciation.
    std::vector<float> instances;
    std::vector<std::uint32_t> instanceAttributeSizes;
    std::string name = "mesh";
};

// Texture liée au groupe 1 du maillage : handles non possédés (leur propriétaire doit les garder vivants).
struct TextureBinding
{
    rhi::TextureHandle texture;
    rhi::SamplerHandle sampler;
};

// Historique pour les vecteurs de mouvement (REN-21) : `frame` = numéro de l'image (0 : pas d'historique, le modèle précédent
// est le courant), `previousOrigin` = origine du rendu relatif à la caméra de l'image précédente.
struct TransformHistory
{
    std::uint64_t frame = 0;
    glm::vec3 previousOrigin{0.0f};
};

class Mesh
{
  public:
    Mesh() = default;
    ~Mesh() { reset(); }
    Mesh(const Mesh &) = delete;
    Mesh &operator=(const Mesh &) = delete;
    Mesh(Mesh &&other) noexcept { swap(other); }
    Mesh &operator=(Mesh &&other) noexcept
    {
        if (this != &other)
        {
            reset();
            swap(other);
        }
        return *this;
    }

    static core::Result<Mesh> create(rhi::Device &device, const MeshData &data);

    void setProgram(std::shared_ptr<ShaderProgram> program) { program_ = std::move(program); }
    // Textures aux liaisons 0, 1, 2... du groupe 1 (dans l'ordre).
    core::Result<> setTextures(std::span<const TextureBinding> textures);
    // Groupe 1 fourni par l'appelant (non possédé : il doit vivre jusqu'au dernier dessin), à la place des textures ; ex. le groupe
    // des récepteurs d'ombres (ShadowFeature::receiverGroup). Un groupe nul retire le groupe 1.
    void setMaterialGroup(rhi::BindGroupHandle group) noexcept;
    // Octets du bloc push_constant du programme (le premier entier est réservé au drapeau fil de fer, voir mesh.frag).
    void setPushConstants(std::span<const std::byte> bytes) { pushConstants_.assign(bytes.begin(), bytes.end()); }

    void setPosition(const glm::vec3 &position) { position_ = position; transformDirty_ = true; }
    void setRotation(const glm::vec3 &degrees) { rotation_ = degrees; transformDirty_ = true; }
    void setScale(const glm::vec3 &scale) { scale_ = scale; transformDirty_ = true; }
    const glm::vec3 &position() const noexcept { return position_; }
    const glm::vec3 &rotation() const noexcept { return rotation_; }
    const glm::vec3 &scale() const noexcept { return scale_; }
    glm::mat4 modelMatrix() const;
    // Envoie la matrice au GPU si la transformation a changé.
    // `origin` : rendu relatif à la caméra (REN-02) : la translation envoyée est position - origin (origin = position de la caméra).
    core::Result<> uploadTransform(const glm::vec3 &origin = glm::vec3(0.0f), const TransformHistory &history = {});

    // Identifiant écrit dans l'AOV `kObjectId` (REN-21) ; 0 = aucun objet (valeur d'effacement).
    void setObjectId(std::uint32_t id) noexcept
    {
        objectId_ = id;
        transformDirty_ = true; // l'identifiant voyage dans le bloc du modèle
    }
    std::uint32_t objectId() const noexcept { return objectId_; }
    // Matériau vu des AOV (REN-41) : mode de fusion (appliqué au pipeline : mélange sur la couleur, profondeur non écrite pour les
    // transparents) et AOV demandés explicitement ; un transparent n'écrit AUCUN AOV par défaut.
    void setAovMaterial(const AovMaterial &material) noexcept { aovMaterial_ = material; }
    const AovMaterial &aovMaterial() const noexcept { return aovMaterial_; }

    // Écrit `data` à partir du flottant `offsetFloats` ; ne change ni la taille ni les index.
    core::Result<> updateVertices(std::span<const float> data, std::size_t offsetFloats = 0);
    core::Result<> updateInstances(std::span<const float> data, std::size_t offsetFloats = 0);

    void reset() noexcept;
    bool valid() const noexcept { return device_ != nullptr && vertexBuffer_.valid(); }
    const std::shared_ptr<ShaderProgram> &program() const noexcept { return program_; }
    std::uint32_t indexCount() const noexcept { return indexCount_; }
    std::uint32_t instanceCount() const noexcept { return instanceCount_; }
    bool instanced() const noexcept { return instanceCount_ > 1 || !instanceSizes_.empty(); }
    const std::vector<rhi::VertexBufferLayout> &vertexBuffers() const noexcept { return layouts_; }
    const std::vector<rhi::VertexAttribute> &vertexAttributes() const noexcept { return attributes_; }
    rhi::BufferHandle vertexBuffer() const noexcept { return vertexBuffer_.handle(); }
    std::uint64_t vertexBufferSize() const noexcept { return vertexBuffer_.size(); } // octets (copie dans un pool, REN-29)
    rhi::BufferHandle instanceBuffer() const noexcept { return instanceBuffer_.handle(); }
    rhi::BufferHandle indexBuffer() const noexcept { return indexBuffer_.handle(); }
    rhi::BindGroupHandle modelGroup() const noexcept { return modelGroup_; }
    rhi::BindGroupHandle textureGroup() const noexcept { return textureGroup_; }
    const std::vector<std::byte> &pushConstants() const noexcept { return pushConstants_; }

  private:
    void swap(Mesh &other) noexcept;

    rhi::Device *device_ = nullptr;
    GpuBuffer vertexBuffer_;
    GpuBuffer indexBuffer_;
    GpuBuffer instanceBuffer_;
    GpuBuffer modelBuffer_;
    rhi::BindGroupHandle modelGroup_;
    rhi::BindGroupHandle textureGroup_;
    bool ownsTextureGroup_ = true; // faux : groupe donné par setMaterialGroup
    std::vector<rhi::VertexBufferLayout> layouts_;
    std::vector<rhi::VertexAttribute> attributes_;
    std::vector<std::uint32_t> instanceSizes_;
    std::uint32_t indexCount_ = 0;
    std::uint32_t instanceCount_ = 1;
    std::shared_ptr<ShaderProgram> program_;
    std::vector<std::byte> pushConstants_;
    glm::vec3 position_{0.0f};
    glm::vec3 rotation_{0.0f};
    glm::vec3 scale_{1.0f};
    bool transformDirty_ = true;
    glm::vec3 uploadedOrigin_{0.0f};
    glm::mat4 uploadedPreviousWorld_{1.0f};
    glm::vec3 uploadedPreviousOrigin_{0.0f};
    glm::mat4 previousWorld_{1.0f}; // modèle (monde) de l'image précédente
    glm::mat4 lastWorld_{1.0f};     // modèle (monde) vu par le dernier uploadTransform
    bool hasLastWorld_ = false;
    std::uint64_t lastFrame_ = 0;
    std::uint32_t objectId_ = 0;
    AovMaterial aovMaterial_;
};

} // namespace orosmo::renderer
