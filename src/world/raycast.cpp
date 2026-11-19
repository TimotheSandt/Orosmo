#include "engine/world/raycast.hpp"

#include <cmath>

namespace engine::world
{

float meshHeightAt(const TerrainParams &params, std::uint32_t seed, float x, float z) noexcept
{
    // Échantillons globaux du niveau 0 aux coins de la cellule : mêmes positions et mêmes valeurs que les tuiles.
    const double s = static_cast<double>(params.sampleSpacing);
    const auto gi = static_cast<std::int64_t>(std::floor(static_cast<double>(x) / s));
    const auto gj = static_cast<std::int64_t>(std::floor(static_cast<double>(z) / s));
    auto position = [&](std::int64_t g) { return static_cast<float>(static_cast<double>(g) * s); };
    const float x0 = position(gi), x1 = position(gi + 1), z0 = position(gj), z1 = position(gj + 1);
    const float ha = heightAt(params, seed, x0, z0); // a = (i, j)
    const float hb = heightAt(params, seed, x1, z0); // b = (i + 1, j)
    const float hc = heightAt(params, seed, x0, z1); // c = (i, j + 1)
    const float hd = heightAt(params, seed, x1, z1); // d = (i + 1, j + 1)
    const float u = (x - x0) / (x1 - x0);
    const float v = (z - z0) / (z1 - z0);
    // Diagonale b-c (triangles a-c-b et b-c-d) : u + v <= 1 dans le premier.
    if (u + v <= 1.0f)
    {
        return ha + u * (hb - ha) + v * (hc - ha);
    }
    return hd + (1.0f - u) * (hc - hd) + (1.0f - v) * (hb - hd);
}

std::optional<RayHit> raycastTerrain(const TerrainParams &params, std::uint32_t seed, core::Vec3 origin, core::Vec3 direction, float maxDistance)
{
    const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
    if (!(length > 0.0f) || !(maxDistance > 0.0f))
    {
        return std::nullopt;
    }
    direction /= length;
    auto above = [&](float t)
    {
        const core::Vec3 p = origin + direction * t;
        return p.y - meshHeightAt(params, seed, p.x, p.z);
    };
    if (above(0.0f) <= 0.0f)
    {
        return RayHit{origin, 0.0f};
    }
    const float step = 0.25f * params.sampleSpacing;
    float previous = 0.0f;
    for (float t = step; previous < maxDistance; t += step)
    {
        const float current = std::min(t, maxDistance);
        if (above(current) <= 0.0f)
        {
            float lo = previous, hi = current;
            for (int i = 0; i < 32; ++i)
            {
                const float mid = 0.5f * (lo + hi);
                (above(mid) > 0.0f ? lo : hi) = mid;
            }
            return RayHit{origin + direction * hi, hi};
        }
        previous = current;
    }
    return std::nullopt;
}

} // namespace engine::world
