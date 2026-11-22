#include "orosmo/renderer/shadows.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <bit>
#include <cmath>

namespace orosmo::renderer
{

ShadowBehavior resolveShadowBehavior(LightMobility mobility, ShadowMode mode) noexcept
{
    ShadowBehavior b;
    b.mayMove = mobility == LightMobility::kDynamic;
    b.mayRecolor = mobility != LightMobility::kStatic;
    switch (mode)
    {
    case ShadowMode::kNone:
        b.status = ShadowStatus::kNone;
        break;
    case ShadowMode::kBaked:
        // Pas de lightmaps ni de sondes cuites (REN-16) : une lumière fixe retombe sur le cache temps réel (la géométrie fixe y est
        // dessinée une fois, ce qui tient lieu de cuisson) ; une lumière mobile ne se cuit pas : temps réel sans cache.
        b.realtime = true;
        b.cacheStatic = mobility != LightMobility::kDynamic;
        b.status = mobility == LightMobility::kDynamic ? ShadowStatus::kInvalidCombination : ShadowStatus::kBakedFallback;
        break;
    case ShadowMode::kDynamic:
        b.realtime = true;
        b.status = ShadowStatus::kRealtime;
        break;
    case ShadowMode::kCached:
        b.realtime = true;
        b.cacheStatic = true;
        b.status = ShadowStatus::kRealtime;
        break;
    }
    return b;
}

std::vector<float> cascadeSplits(float nearPlane, float farPlane, std::uint32_t count, float lambda)
{
    std::vector<float> splits;
    if (count == 0)
    {
        return splits;
    }
    nearPlane = std::max(nearPlane, 1e-4f);
    farPlane = std::max(farPlane, nearPlane * 1.001f);
    lambda = std::clamp(lambda, 0.0f, 1.0f);
    splits.reserve(count);
    for (std::uint32_t i = 1; i <= count; ++i)
    {
        const float p = static_cast<float>(i) / static_cast<float>(count);
        const float logSplit = nearPlane * std::pow(farPlane / nearPlane, p);
        const float uniformSplit = nearPlane + (farPlane - nearPlane) * p;
        splits.push_back(i == count ? farPlane : lambda * logSplit + (1.0f - lambda) * uniformSplit);
    }
    return splits;
}

CameraShape cameraShapeFrom(const glm::mat4 &view, const glm::mat4 &projection, const glm::vec3 &origin) noexcept
{
    CameraShape c;
    const glm::mat4 invView = glm::inverse(view);
    c.right = glm::normalize(glm::vec3(invView[0]));
    c.up = glm::normalize(glm::vec3(invView[1]));
    c.forward = -glm::normalize(glm::vec3(invView[2]));
    c.position = glm::vec3(invView[3]) + origin;
    // Perspective : w_clip = -z_vue (projection[2][3] = -1). Le jitter (projection[2][0..1] ou [3][0..1]) n'entre pas ici.
    c.orthographic = projection[2][3] == 0.0f;
    c.halfX = projection[0][0] != 0.0f ? 1.0f / std::abs(projection[0][0]) : 1.0f;
    c.halfY = projection[1][1] != 0.0f ? 1.0f / std::abs(projection[1][1]) : 1.0f;
    // Plan proche : profondeur 1 en reverse-Z. z_ndc = (A z + B) / w ; perspective w = -z, orthographique w = 1.
    const float a = projection[2][2];
    const float b = projection[3][2];
    if (c.orthographic)
    {
        c.nearPlane = a != 0.0f ? -(1.0f - b) / a : 0.1f; // A z + B = 1
    }
    else
    {
        c.nearPlane = (a + 1.0f) != 0.0f ? b / (a + 1.0f) : 0.1f; // (A z + B) / -z = 1  =>  z = -B / (A + 1)
    }
    if (!std::isfinite(c.nearPlane) || c.nearPlane <= 0.0f)
    {
        c.nearPlane = 0.1f;
    }
    return c;
}

std::array<glm::vec3, 8> sliceCorners(const CameraShape &camera, float nearDistance, float farDistance) noexcept
{
    std::array<glm::vec3, 8> corners{};
    const float distances[2] = {nearDistance, farDistance};
    std::size_t i = 0;
    for (const float d : distances)
    {
        const float hx = camera.orthographic ? camera.halfX : camera.halfX * d;
        const float hy = camera.orthographic ? camera.halfY : camera.halfY * d;
        const glm::vec3 center = camera.position + camera.forward * d;
        for (const glm::vec2 s : {glm::vec2(-1, -1), glm::vec2(1, -1), glm::vec2(1, 1), glm::vec2(-1, 1)})
        {
            corners[i++] = center + camera.right * (s.x * hx) + camera.up * (s.y * hy);
        }
    }
    return corners;
}

namespace
{
glm::vec3 stableUp(const glm::vec3 &direction) noexcept
{
    return std::abs(direction.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
}
} // namespace

ShadowViewFit fitCascade(const CameraShape &camera, float nearDistance, float farDistance, const glm::vec3 &toLight, std::uint32_t resolution,
                         float depthExtension) noexcept
{
    ShadowViewFit fit;
    resolution = std::max(resolution, 1u);
    // Sphère englobant la tranche, de rayon indépendant de l'orientation de la caméra. Perspective : centre à la distance c le long
    // de l'axe, équidistant des coins proches et lointains : (c - n)^2 + n^2 k^2 = (f - c)^2 + f^2 k^2, k^2 = tx^2 + ty^2.
    const float n = nearDistance;
    const float f = std::max(farDistance, nearDistance + 1e-3f);
    float centerDistance = 0.0f;
    float radius = 0.0f;
    if (camera.orthographic)
    {
        centerDistance = 0.5f * (n + f);
        const float h = 0.5f * (f - n);
        radius = std::sqrt(h * h + camera.halfX * camera.halfX + camera.halfY * camera.halfY);
    }
    else
    {
        const float k2 = camera.halfX * camera.halfX + camera.halfY * camera.halfY;
        centerDistance = 0.5f * (f + n) * (1.0f + k2);
        if (centerDistance >= f)
        {
            centerDistance = f;
            radius = f * std::sqrt(k2);
        }
        else
        {
            const float dn = centerDistance - n;
            radius = std::sqrt(dn * dn + n * n * k2);
        }
    }
    // Arrondi du rayon au 1/16 de mètre supérieur : il reste constant même si le champ varie d'un epsilon d'une image à l'autre.
    radius = std::ceil(radius * 16.0f) / 16.0f;
    const glm::vec3 center = camera.position + camera.forward * centerDistance;

    const glm::vec3 lightDir = glm::length(toLight) > 0.0f ? glm::normalize(toLight) : glm::vec3(0.0f, 1.0f, 0.0f);
    // Repère de la lumière : rotation seule (origine du monde), regard dans le sens de propagation (-toLight).
    const glm::mat4 rotation = glm::lookAtRH(glm::vec3(0.0f), -lightDir, stableUp(lightDir));
    glm::vec3 c = glm::vec3(rotation * glm::vec4(center, 1.0f));
    const float texel = 2.0f * radius / static_cast<float>(resolution);
    // Ancrage sur la grille de texels (x, y) et de profondeur (z) du repère de la lumière.
    c.x = std::floor(c.x / texel) * texel;
    c.y = std::floor(c.y / texel) * texel;
    c.z = std::floor(c.z / texel) * texel;
    const float nearPlane = -c.z - radius - std::max(depthExtension, 0.0f); // le repère regarde vers -z
    const float farPlane = -c.z + radius;
    fit.view = rotation;
    fit.projection = glm::orthoRH_ZO(c.x - radius, c.x + radius, c.y - radius, c.y + radius, nearPlane, farPlane);
    fit.viewProjection = fit.projection * fit.view;
    fit.center = center;
    fit.radius = radius;
    fit.texelWorld = texel;
    fit.depthRange = farPlane - nearPlane;
    return fit;
}

ShadowViewFit fitSpot(const glm::vec3 &position, const glm::vec3 &direction, float halfAngle, float nearPlane, float farPlane,
                      std::uint32_t resolution) noexcept
{
    ShadowViewFit fit;
    const glm::vec3 dir = glm::length(direction) > 0.0f ? glm::normalize(direction) : glm::vec3(0.0f, -1.0f, 0.0f);
    halfAngle = std::clamp(halfAngle, glm::radians(1.0f), glm::radians(85.0f));
    nearPlane = std::max(nearPlane, 1e-3f);
    farPlane = std::max(farPlane, nearPlane * 2.0f);
    fit.view = glm::lookAtRH(position, position + dir, stableUp(dir));
    fit.projection = glm::perspectiveRH_ZO(2.0f * halfAngle, 1.0f, nearPlane, farPlane);
    fit.viewProjection = fit.projection * fit.view;
    fit.center = position;
    fit.radius = farPlane;
    fit.texelWorld = 2.0f * std::tan(halfAngle) / static_cast<float>(std::max(resolution, 1u));
    fit.depthRange = farPlane - nearPlane;
    return fit;
}

glm::mat4 atlasTileMatrix(const AtlasTile &tile, std::uint32_t atlasSize) noexcept
{
    const float s = static_cast<float>(tile.size) / static_cast<float>(atlasSize);
    glm::mat4 m(1.0f);
    m[0][0] = 0.5f * s;
    m[1][1] = -0.5f * s; // NDC y = +1 en haut, v = 0 en haut
    m[3][0] = (static_cast<float>(tile.x) + 0.5f * static_cast<float>(tile.size)) / static_cast<float>(atlasSize);
    m[3][1] = (static_cast<float>(tile.y) + 0.5f * static_cast<float>(tile.size)) / static_cast<float>(atlasSize);
    return m;
}

std::uint32_t nextPowerOfTwo(std::uint32_t value) noexcept { return value <= 1 ? 1u : std::bit_ceil(value); }

ShadowAtlasAllocator::ShadowAtlasAllocator(std::uint32_t atlasSize, std::uint32_t minTile) { reset(atlasSize, minTile); }

void ShadowAtlasAllocator::reset(std::uint32_t atlasSize, std::uint32_t minTile)
{
    atlasSize_ = nextPowerOfTwo(std::max(atlasSize, 1u));
    minTile_ = std::min(nextPowerOfTwo(std::max(minTile, 1u)), atlasSize_);
    free_.assign(static_cast<std::size_t>(std::countr_zero(atlasSize_) - std::countr_zero(minTile_)) + 1, {});
    free_[0].push_back({0, 0, atlasSize_});
}

std::uint32_t ShadowAtlasAllocator::levelOf(std::uint32_t size) const noexcept
{
    return static_cast<std::uint32_t>(std::countr_zero(atlasSize_) - std::countr_zero(size));
}

namespace
{
bool tileBefore(const AtlasTile &a, const AtlasTile &b) noexcept { return a.y != b.y ? a.y < b.y : a.x < b.x; }
void insertSorted(std::vector<AtlasTile> &list, const AtlasTile &tile)
{
    list.insert(std::lower_bound(list.begin(), list.end(), tile, tileBefore), tile);
}
} // namespace

std::optional<AtlasTile> ShadowAtlasAllocator::allocate(std::uint32_t size)
{
    const std::uint32_t s = std::max(nextPowerOfTwo(std::max(size, 1u)), minTile_);
    if (s > atlasSize_)
    {
        return std::nullopt;
    }
    const std::uint32_t level = levelOf(s);
    // Niveau libre le plus proche au-dessus (tuiles plus grandes), découpé ensuite en quatre jusqu'à la taille voulue.
    std::int64_t from = static_cast<std::int64_t>(level);
    while (from >= 0 && free_[static_cast<std::size_t>(from)].empty())
    {
        --from;
    }
    if (from < 0)
    {
        return std::nullopt;
    }
    AtlasTile tile = free_[static_cast<std::size_t>(from)].front();
    free_[static_cast<std::size_t>(from)].erase(free_[static_cast<std::size_t>(from)].begin());
    for (auto l = static_cast<std::uint32_t>(from); l < level; ++l)
    {
        const std::uint32_t half = tile.size / 2;
        insertSorted(free_[l + 1], {tile.x + half, tile.y, half});
        insertSorted(free_[l + 1], {tile.x, tile.y + half, half});
        insertSorted(free_[l + 1], {tile.x + half, tile.y + half, half});
        tile.size = half;
    }
    return tile;
}

void ShadowAtlasAllocator::release(const AtlasTile &tile)
{
    if (tile.size < minTile_ || tile.size > atlasSize_ || !std::has_single_bit(tile.size))
    {
        return;
    }
    // Déjà libre (elle-même ou une tuile libre plus grande qui la contient, après fusion) : sans effet.
    for (std::uint32_t l = 0; l <= levelOf(tile.size); ++l)
    {
        for (const AtlasTile &f : free_[l])
        {
            if (tile.x >= f.x && tile.y >= f.y && tile.x + tile.size <= f.x + f.size && tile.y + tile.size <= f.y + f.size)
            {
                return;
            }
        }
    }
    AtlasTile current = tile;
    std::uint32_t level = levelOf(current.size);
    while (true)
    {
        auto &list = free_[level];
        if (level == 0)
        {
            insertSorted(list, current);
            return;
        }
        const std::uint32_t parent = current.size * 2;
        const AtlasTile p{current.x - current.x % parent, current.y - current.y % parent, parent};
        const std::uint32_t h = current.size;
        const std::array<AtlasTile, 4> siblings{AtlasTile{p.x, p.y, h}, AtlasTile{p.x + h, p.y, h}, AtlasTile{p.x, p.y + h, h},
                                                AtlasTile{p.x + h, p.y + h, h}};
        bool allFree = true;
        for (const AtlasTile &s : siblings)
        {
            if (s != current && std::find(list.begin(), list.end(), s) == list.end())
            {
                allFree = false;
            }
        }
        if (!allFree)
        {
            insertSorted(list, current);
            return;
        }
        std::erase_if(list, [&](const AtlasTile &t) { return t != current && std::find(siblings.begin(), siblings.end(), t) != siblings.end(); });
        current = p;
        --level;
    }
}

std::uint64_t ShadowAtlasAllocator::freeArea() const noexcept
{
    std::uint64_t area = 0;
    for (const auto &list : free_)
    {
        for (const AtlasTile &t : list)
        {
            area += static_cast<std::uint64_t>(t.size) * t.size;
        }
    }
    return area;
}

void ShadowHash::bytes(std::span<const std::byte> data) noexcept
{
    for (const std::byte b : data)
    {
        state_ ^= static_cast<std::uint64_t>(b);
        state_ *= 1099511628211ull;
    }
}

} // namespace orosmo::renderer
