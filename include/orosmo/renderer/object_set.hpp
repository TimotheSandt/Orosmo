// REN-28/30/31 : ensemble d'objets génériques à dessiner par instances, sans dépendance graphique. Un objet = des niveaux de détail
// (identifiants de maillage, le 0 le plus détaillé), un matériau, une boîte locale, une transformation et une couleur. `prepare`
// rejette les objets hors de la vue (BVH contre la pyramide), trop petits ou trop lointains, choisit le LOD par distance (fondu
// tramé dans les bandes de transition), puis range les instances visibles par (maillage, matériau) dans un InstanceBatchBuilder.
// Les identifiants de maillage et de matériau sont ceux d'InstanceRenderer (instance_renderer.hpp) ; ils ne sont pas vérifiés ici.
#pragma once

#include "orosmo/core/error.hpp"
#include "orosmo/renderer/culling.hpp"
#include "orosmo/renderer/instance_batch.hpp"
#include "orosmo/renderer/lod.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace orosmo::renderer
{

struct ObjectId
{
    std::uint32_t index = 0xFFFFFFFFu;
    std::uint32_t generation = 0;
    bool operator==(const ObjectId &) const = default;
};

struct ObjectDesc
{
    std::vector<std::uint32_t> lodMeshes; // au moins un ; au plus 255
    std::uint32_t material = 0;
    std::uint32_t lodSettings = 0; // identifiant rendu par ObjectSet::addLodSettings ; 0 : un seul niveau, jamais rejeté au loin
    Aabb localBounds;               // dans le repère du maillage
    glm::mat4 transform{1.0f};
    glm::vec4 color{1.0f};
};

struct VisibilityOptions
{
    bool useBvh = true;     // faux : test linéaire objet par objet (référence, mesures)
    bool frustumCulling = true;
    bool lod = true;        // faux : toujours le niveau 0
    bool lodColors = false; // REN-35 : une couleur par niveau de détail (debug_view.hpp : lodDebugColor)
};

struct VisibilityStats
{
    CullStats cull;
    std::uint32_t objects = 0;    // objets vivants
    std::uint32_t instances = 0;  // instances produites (un objet en fondu en produit deux)
    std::uint32_t crossFades = 0; // objets dessinés en fondu entre deux niveaux
    std::uint32_t lodCulled = 0;  // rejetés au-delà de leur cullDistance
};

class ObjectSet
{
  public:
    ObjectSet();

    // Réglages de LOD partagés par plusieurs objets ; l'identifiant 0 (un seul niveau) existe toujours.
    std::uint32_t addLodSettings(LodSettings settings);
    const LodSettings &lodSettings(std::uint32_t id) const noexcept;

    core::Result<ObjectId> add(const ObjectDesc &desc);
    core::Result<> remove(ObjectId id);
    core::Result<> setTransform(ObjectId id, const glm::mat4 &transform);
    core::Result<> setColor(ObjectId id, const glm::vec4 &color);
    bool contains(ObjectId id) const noexcept;
    std::size_t size() const noexcept { return objects_.size(); }
    void clear() noexcept;

    // Vide `out`, y range les instances visibles et appelle out.build(). La BVH est reconstruite après des ajouts ou retraits et
    // réajustée (refit) après des déplacements.
    VisibilityStats prepare(const CullView &view, InstanceBatchBuilder &out, const VisibilityOptions &options = {});

  private:
    struct Object
    {
        std::vector<std::uint32_t> lodMeshes;
        std::uint32_t material = 0;
        std::uint32_t lodSettings = 0;
        Aabb localBounds;
        glm::mat4 transform{1.0f};
        glm::vec4 color{1.0f};
        std::uint32_t slot = 0; // index dans slots_ (pour le retrait par échange)
    };
    struct Slot
    {
        std::uint32_t dense = 0xFFFFFFFFu; // index dans objects_, ou libre
        std::uint32_t generation = 0;
    };
    Object *find(ObjectId id) noexcept;

    std::vector<Object> objects_;     // denses
    std::vector<Aabb> worldBounds_;   // même index que objects_
    std::vector<Slot> slots_;
    std::vector<std::uint32_t> freeSlots_;
    std::vector<LodSettings> lodSettings_;
    Bvh bvh_;
    bool structureDirty_ = true;
    bool boundsDirty_ = false;
    std::vector<std::uint32_t> visible_;
};

} // namespace orosmo::renderer
