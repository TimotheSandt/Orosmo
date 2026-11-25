// REN-28 : regroupement des instances, sans dépendance graphique. Chaque instance (matrice du modèle, couleur, seuil de tramage du
// LOD) est rangée sous sa clé (maillage, matériau) ; `build` les trie par clé (ordre stable : celui des ajouts dans une clé) et
// produit un tableau contigu au format GPU plus la liste des lots : un lot = une clé = UN draw call instancié.
#pragma once

#include <glm/glm.hpp>

#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace orosmo::renderer
{

// Disposition d'une instance dans le tampon de sommets (pas d'instance), lue par shaders/rhi/mesh_instanced.vert aux emplacements
// kInstanceFirstLocation à kInstanceFirstLocation + 7 : quatre colonnes du modèle, trois colonnes de la matrice des normales
// (inverse transposée, w de la première = seuil de tramage du LOD, voir lod.hpp), couleur multipliée à celle des sommets.
struct GpuInstance
{
    glm::vec4 model[4];
    glm::vec4 normal[3];
    glm::vec4 color;
};
static_assert(sizeof(GpuInstance) == 128, "GpuInstance : 8 vec4 sans remplissage");
static_assert(offsetof(GpuInstance, model) == 0 && offsetof(GpuInstance, normal) == 64 && offsetof(GpuInstance, color) == 112,
              "GpuInstance : offsets lus par mesh_instanced.vert et écrits par gpu_cull.comp");

// Emplacements des attributs d'instance : après ceux du maillage (8 au plus), jusqu'au minimum garanti de 16 (GL et Vulkan).
inline constexpr std::uint32_t kInstanceFirstLocation = 8;
inline constexpr std::uint32_t kInstanceAttributeCount = 8;

struct InstanceKey
{
    std::uint32_t mesh = 0;
    std::uint32_t material = 0;
    auto operator<=>(const InstanceKey &) const = default;
};

struct InstanceBatch
{
    InstanceKey key;
    std::uint32_t firstInstance = 0; // index dans instances()
    std::uint32_t instanceCount = 0;
};

// Instance prête pour le GPU : matrice des normales calculée ici (une fois par instance, pas par sommet).
GpuInstance makeGpuInstance(const glm::mat4 &model, const glm::vec4 &color = glm::vec4(1.0f), float dither = 0.0f) noexcept;

class InstanceBatchBuilder
{
  public:
    void clear() noexcept;
    void reserve(std::size_t instances);
    void add(InstanceKey key, const GpuInstance &instance);
    void add(InstanceKey key, const glm::mat4 &model, const glm::vec4 &color = glm::vec4(1.0f), float dither = 0.0f)
    {
        add(key, makeGpuInstance(model, color, dither));
    }
    // Trie par clé et forme les lots. Après `build`, `instances()` est dans l'ordre des lots ; un nouvel `add` invalide le résultat
    // (il faut rappeler `build`).
    void build();

    bool built() const noexcept { return built_; }
    std::size_t size() const noexcept { return keys_.size(); }
    std::span<const GpuInstance> instances() const noexcept { return sorted_; }
    std::span<const InstanceBatch> batches() const noexcept { return batches_; }

  private:
    std::vector<InstanceKey> keys_;
    std::vector<GpuInstance> pending_;
    std::vector<std::uint32_t> order_;
    std::vector<GpuInstance> sorted_;
    std::vector<InstanceBatch> batches_;
    bool built_ = false;
};

} // namespace orosmo::renderer
