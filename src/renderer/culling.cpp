#include "orosmo/renderer/culling.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace orosmo::renderer
{

Aabb Aabb::empty() noexcept
{
    constexpr float kMax = std::numeric_limits<float>::max();
    return {glm::vec3(kMax), glm::vec3(-kMax)};
}

void Aabb::merge(const Aabb &other) noexcept
{
    min = glm::min(min, other.min);
    max = glm::max(max, other.max);
}

Aabb Aabb::transformed(const glm::mat4 &transform) const noexcept
{
    // Arvo : chaque axe de sortie est la translation plus la somme, sur les axes d'entrée, du min et du max des produits.
    Aabb out{glm::vec3(transform[3]), glm::vec3(transform[3])};
    for (int column = 0; column < 3; ++column)
    {
        for (int row = 0; row < 3; ++row)
        {
            const float a = transform[column][row] * min[column];
            const float b = transform[column][row] * max[column];
            out.min[row] += std::min(a, b);
            out.max[row] += std::max(a, b);
        }
    }
    return out;
}

Frustum Frustum::fromMatrix(const glm::mat4 &m) noexcept
{
    const auto row = [&](int r) { return glm::vec4(m[0][r], m[1][r], m[2][r], m[3][r]); };
    const glm::vec4 r0 = row(0);
    const glm::vec4 r1 = row(1);
    const glm::vec4 r2 = row(2);
    const glm::vec4 r3 = row(3);
    const std::array<glm::vec4, 6> raw{r3 + r0, r3 - r0, r3 + r1, r3 - r1, r2, r3 - r2};
    Frustum f;
    for (const glm::vec4 &p : raw)
    {
        const float length = glm::length(glm::vec3(p));
        // Plan dégénéré (z = 0 d'une projection reverse-Z infinie : la ligne de profondeur est constante) : omis.
        if (length > 1e-6f && std::isfinite(length))
        {
            f.planes_[f.count_++] = p / length;
        }
    }
    return f;
}

CullResult Frustum::test(const Aabb &box) const noexcept
{
    const glm::vec3 c = box.center();
    const glm::vec3 e = box.extents();
    CullResult result = CullResult::kInside;
    for (std::size_t i = 0; i < count_; ++i)
    {
        const glm::vec4 &p = planes_[i];
        const float distance = glm::dot(glm::vec3(p), c) + p.w;
        const float reach = glm::dot(glm::abs(glm::vec3(p)), e); // projection des demi-tailles sur la normale
        if (distance + reach < 0.0f)
        {
            return CullResult::kOutside;
        }
        if (distance - reach < 0.0f)
        {
            result = CullResult::kIntersects;
        }
    }
    return result;
}

bool Frustum::contains(const glm::vec3 &point) const noexcept
{
    for (std::size_t i = 0; i < count_; ++i)
    {
        if (glm::dot(glm::vec3(planes_[i]), point) + planes_[i].w < 0.0f)
        {
            return false;
        }
    }
    return true;
}

CullView CullView::fromMatrices(const glm::mat4 &view, const glm::mat4 &projection, float viewportHeight, bool orthographic)
{
    CullView v;
    v.frustum = Frustum::fromMatrix(projection * view);
    v.eye = glm::vec3(glm::inverse(view)[3]);
    v.projectionScale = std::abs(projection[1][1]);
    v.orthographic = orthographic;
    v.viewportHeight = viewportHeight;
    return v;
}

float projectedRadiusPixels(const Aabb &box, const CullView &view) noexcept
{
    const float radius = box.radius();
    if (view.orthographic)
    {
        return radius * view.projectionScale * view.viewportHeight * 0.5f;
    }
    const float distance = glm::length(box.center() - view.eye);
    if (distance <= radius)
    {
        return std::numeric_limits<float>::infinity();
    }
    return radius / distance * view.projectionScale * view.viewportHeight * 0.5f;
}

CullStats &CullStats::operator+=(const CullStats &o) noexcept
{
    nodesVisited += o.nodesVisited;
    objectsTested += o.objectsTested;
    visible += o.visible;
    frustumCulled += o.frustumCulled;
    smallCulled += o.smallCulled;
    distanceCulled += o.distanceCulled;
    return *this;
}

namespace
{
// Règles communes à un objet dont la pyramide est déjà réglée : distance, puis taille à l'écran.
bool acceptObject(const Aabb &box, const CullView &view, CullStats &stats)
{
    if (view.maxDistance > 0.0f)
    {
        // Distance du point de la boîte le plus proche de l'œil : une grande boîte proche n'est pas rejetée.
        const glm::vec3 nearest = glm::clamp(view.eye, box.min, box.max);
        if (glm::length(nearest - view.eye) > view.maxDistance)
        {
            ++stats.distanceCulled;
            return false;
        }
    }
    if (view.minScreenRadius > 0.0f && projectedRadiusPixels(box, view) < view.minScreenRadius)
    {
        ++stats.smallCulled;
        return false;
    }
    ++stats.visible;
    return true;
}
} // namespace

CullStats cullLinear(const CullView &view, std::span<const Aabb> boxes, std::vector<std::uint32_t> &visible)
{
    CullStats stats;
    for (std::uint32_t i = 0; i < boxes.size(); ++i)
    {
        ++stats.objectsTested;
        if (view.frustum.test(boxes[i]) == CullResult::kOutside)
        {
            ++stats.frustumCulled;
            continue;
        }
        if (acceptObject(boxes[i], view, stats))
        {
            visible.push_back(i);
        }
    }
    return stats;
}

void Bvh::clear() noexcept
{
    nodes_.clear();
    indices_.clear();
}

Aabb Bvh::bounds() const noexcept { return nodes_.empty() ? Aabb::empty() : nodes_.front().box; }

void Bvh::build(std::span<const Aabb> boxes)
{
    clear();
    if (boxes.empty())
    {
        return;
    }
    indices_.resize(boxes.size());
    std::iota(indices_.begin(), indices_.end(), 0u);
    nodes_.reserve(2 * boxes.size() / kLeafSize + 1);
    buildNode(boxes, 0, static_cast<std::uint32_t>(boxes.size()));
}

std::uint32_t Bvh::buildNode(std::span<const Aabb> boxes, std::uint32_t first, std::uint32_t count)
{
    const auto self = static_cast<std::uint32_t>(nodes_.size());
    nodes_.push_back({});
    Aabb box = Aabb::empty();
    Aabb centers = Aabb::empty();
    for (std::uint32_t i = first; i < first + count; ++i)
    {
        box.merge(boxes[indices_[i]]);
        const glm::vec3 c = boxes[indices_[i]].center();
        centers.merge({c, c});
    }
    nodes_[self].box = box;
    nodes_[self].total = count;
    if (count <= kLeafSize)
    {
        nodes_[self].first = first;
        nodes_[self].count = count;
        return self;
    }
    // Axe le plus long des centres, coupure à la médiane (nth_element : O(n) ; égalités départagées par l'index, déterministe).
    const glm::vec3 size = centers.max - centers.min;
    const int axis = size.x >= size.y && size.x >= size.z ? 0 : (size.y >= size.z ? 1 : 2);
    const std::uint32_t half = count / 2;
    auto begin = indices_.begin() + first;
    std::nth_element(begin, begin + half, begin + count,
                     [&](std::uint32_t a, std::uint32_t b)
                     {
                         const float ca = boxes[a].center()[axis];
                         const float cb = boxes[b].center()[axis];
                         return ca < cb || (ca == cb && a < b);
                     });
    const std::uint32_t left = buildNode(boxes, first, half);
    const std::uint32_t right = buildNode(boxes, first + half, count - half);
    nodes_[self].first = left;
    nodes_[self].right = right;
    nodes_[self].count = 0;
    return self;
}

void Bvh::refit(std::span<const Aabb> boxes)
{
    // Les enfants sont toujours après leur parent : un parcours à rebours voit les enfants d'abord.
    for (std::size_t n = nodes_.size(); n-- > 0;)
    {
        Node &node = nodes_[n];
        Aabb box = Aabb::empty();
        if (node.count > 0)
        {
            for (std::uint32_t i = node.first; i < node.first + node.count; ++i)
            {
                box.merge(boxes[indices_[i]]);
            }
        }
        else
        {
            box = nodes_[node.first].box;
            box.merge(nodes_[node.right].box);
        }
        node.box = box;
    }
}

void Bvh::acceptAll(const Node &node, const CullView &view, std::span<const Aabb> boxes, std::vector<std::uint32_t> &visible, CullStats &stats) const
{
    if (node.count > 0)
    {
        for (std::uint32_t i = node.first; i < node.first + node.count; ++i)
        {
            if (acceptObject(boxes[indices_[i]], view, stats))
            {
                visible.push_back(indices_[i]);
            }
        }
        return;
    }
    acceptAll(nodes_[node.first], view, boxes, visible, stats);
    acceptAll(nodes_[node.right], view, boxes, visible, stats);
}

CullStats Bvh::query(const CullView &view, std::span<const Aabb> boxes, std::vector<std::uint32_t> &visible) const
{
    CullStats stats;
    if (nodes_.empty())
    {
        return stats;
    }
    // Pile explicite : profondeur ~ log2(n / kLeafSize), sans récursion pour les nœuds coupés par la pyramide.
    std::uint32_t stack[64];
    std::size_t top = 0;
    stack[top++] = 0;
    while (top > 0)
    {
        const Node &node = nodes_[stack[--top]];
        ++stats.nodesVisited;
        const CullResult r = view.frustum.test(node.box);
        if (r == CullResult::kOutside)
        {
            stats.frustumCulled += node.total;
            continue;
        }
        if (r == CullResult::kInside)
        {
            acceptAll(node, view, boxes, visible, stats);
            continue;
        }
        if (node.count > 0)
        {
            for (std::uint32_t i = node.first; i < node.first + node.count; ++i)
            {
                const std::uint32_t object = indices_[i];
                ++stats.objectsTested;
                if (view.frustum.test(boxes[object]) == CullResult::kOutside)
                {
                    ++stats.frustumCulled;
                    continue;
                }
                if (acceptObject(boxes[object], view, stats))
                {
                    visible.push_back(object);
                }
            }
            continue;
        }
        stack[top++] = node.right;
        stack[top++] = node.first;
    }
    return stats;
}

} // namespace orosmo::renderer
