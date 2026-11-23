// REN-30 : culling sur le CPU, sans aucune dépendance graphique. Boîtes englobantes alignées (`Aabb`), pyramide de vue extraite
// d'une matrice (`Frustum`, reverse-Z et plan lointain infini compris), taille projetée à l'écran (rejet des petits objets) et
// hiérarchie de volumes englobants (`Bvh`) parcourue contre la pyramide : un nœud entièrement dedans accepte tout son contenu sans
// autre test, un nœud entièrement dehors rejette tout son contenu.
#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace orosmo::renderer
{

struct Aabb
{
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};

    glm::vec3 center() const noexcept { return (min + max) * 0.5f; }
    glm::vec3 extents() const noexcept { return (max - min) * 0.5f; } // demi-tailles
    float radius() const noexcept { return glm::length(extents()); } // rayon de la sphère englobante centrée
    bool valid() const noexcept { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }
    // Boîte vide (min > max) : neutre pour `merge`.
    static Aabb empty() noexcept;
    void merge(const Aabb &other) noexcept;
    // Boîte englobant la boîte transformée (méthode d'Arvo : exacte pour les huit coins).
    Aabb transformed(const glm::mat4 &transform) const noexcept;
};

enum class CullResult : std::uint8_t
{
    kOutside,
    kIntersects,
    kInside,
};

// Pyramide de vue : plans (a, b, c, d) normalisés, intérieur où a·x + b·y + c·z + d >= 0. Extraite de la matrice vue-projection
// (Gribb-Hartmann) pour une profondeur [0, 1] : gauche, droite, bas, haut, puis les plans de profondeur z = 0 et z = w. Un plan
// dégénéré (le plan lointain d'une projection reverse-Z infinie) est omis : rien n'est rejeté au loin.
class Frustum
{
  public:
    Frustum() = default;
    static Frustum fromMatrix(const glm::mat4 &viewProjection) noexcept;

    CullResult test(const Aabb &box) const noexcept;
    bool contains(const glm::vec3 &point) const noexcept;
    std::span<const glm::vec4> planes() const noexcept { return {planes_.data(), count_}; }

  private:
    std::array<glm::vec4, 6> planes_{};
    std::size_t count_ = 0;
};

// Ce dont le culling a besoin d'une vue : la pyramide, la position de l'œil (distance des LOD) et l'échelle de projection pour
// la taille à l'écran.
struct CullView
{
    Frustum frustum;
    glm::vec3 eye{0.0f};
    // Direction de visée (unitaire) : la taille à l'écran se mesure à la profondeur de vue, plus prudente que la distance pour un
    // objet hors de l'axe. Nulle (vue construite à la main) : distance euclidienne.
    glm::vec3 forward{0.0f};
    // |proj[1][1]| : 1 / tan(fov / 2) en perspective, 2 / hauteur visible en orthographique.
    float projectionScale = 1.0f;
    bool orthographic = false;
    float viewportHeight = 1.0f; // pixels
    // Rayon projeté en dessous duquel un objet est rejeté (pixels) ; 0 : pas de rejet des petits objets.
    float minScreenRadius = 0.0f;
    // Distance au-delà de laquelle tout est rejeté (mètres, infini par défaut) : borne commune à tous les objets.
    float maxDistance = 0.0f; // 0 : pas de borne

    static CullView fromMatrices(const glm::mat4 &view, const glm::mat4 &projection, float viewportHeight, bool orthographic = false);
};

// Rayon projeté à l'écran (pixels) de la sphère englobant `box`, vue depuis `view`, à la profondeur de vue de son centre (borne
// prudente : jamais plus petite que le rayon apparent à la distance euclidienne). Infini si la sphère touche le plan de l'œil.
float projectedRadiusPixels(const Aabb &box, const CullView &view) noexcept;

struct CullStats
{
    std::uint32_t nodesVisited = 0;
    std::uint32_t objectsTested = 0;  // objets testés un par un (ceux d'un nœud entièrement dedans ne le sont pas contre les plans)
    std::uint32_t visible = 0;
    std::uint32_t frustumCulled = 0;  // objets rejetés par la pyramide (un nœud rejeté compte tout son contenu)
    std::uint32_t smallCulled = 0;    // objets rejetés parce que trop petits à l'écran
    std::uint32_t distanceCulled = 0; // objets rejetés au-delà de maxDistance

    CullStats &operator+=(const CullStats &other) noexcept;
};

// Hiérarchie de volumes englobants sur une liste de boîtes (index = position dans la liste). Construction par coupure médiane sur
// l'axe le plus long (déterministe : même entrée, même arbre). `refit` recalcule les boîtes sans changer la topologie (objets
// mobiles) ; une construction neuve est préférable quand les objets se sont beaucoup déplacés.
class Bvh
{
  public:
    static constexpr std::uint32_t kLeafSize = 4;

    void build(std::span<const Aabb> boxes);
    void refit(std::span<const Aabb> boxes);
    void clear() noexcept;

    // Ajoute à `visible` les index des boîtes visibles (pyramide, distance, taille à l'écran), dans l'ordre de l'arbre.
    CullStats query(const CullView &view, std::span<const Aabb> boxes, std::vector<std::uint32_t> &visible) const;

    std::size_t nodeCount() const noexcept { return nodes_.size(); }
    std::size_t objectCount() const noexcept { return indices_.size(); }
    Aabb bounds() const noexcept;

  private:
    struct Node
    {
        Aabb box;
        std::uint32_t first = 0; // feuille : premier index dans indices_ ; nœud interne : enfant gauche (le droit suit le sous-arbre)
        std::uint32_t count = 0; // feuille : nombre d'objets ; 0 : nœud interne
        std::uint32_t right = 0; // nœud interne : index de l'enfant droit
        std::uint32_t total = 0; // objets du sous-arbre
    };
    std::uint32_t buildNode(std::span<const Aabb> boxes, std::uint32_t first, std::uint32_t count);
    void acceptAll(const Node &node, const CullView &view, std::span<const Aabb> boxes, std::vector<std::uint32_t> &visible, CullStats &stats) const;

    std::vector<Node> nodes_;
    std::vector<std::uint32_t> indices_;
};

// Culling sans hiérarchie (référence des tests et petites listes) : mêmes règles que Bvh::query, objet par objet.
CullStats cullLinear(const CullView &view, std::span<const Aabb> boxes, std::vector<std::uint32_t> &visible);

} // namespace orosmo::renderer
