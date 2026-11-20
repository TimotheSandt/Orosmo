#include "orosmo/world/terrain.hpp"

#include <cmath>

namespace orosmo::world
{

float heightAt(const TerrainParams &params, std::uint32_t seed, float x, float z) noexcept
{
    return params.heightOffset + params.heightScale * warpedFbm2(x, z, seed, params.relief, params.warp);
}

double sampleSpacing(const TerrainParams &params, std::uint32_t lod) noexcept
{
    return static_cast<double>(params.sampleSpacing) * std::ldexp(1.0, static_cast<int>(lod));
}

double tileSize(const TerrainParams &params, std::uint32_t lod) noexcept { return sampleSpacing(params, lod) * static_cast<double>(params.tileResolution); }

namespace
{
// Indice global entier de l'échantillon, puis position : exactement la même pour toutes les tuiles qui partagent cet échantillon
// (un échantillon du niveau l+1 est l'échantillon pair du niveau l au même endroit : 2 * indice * espacement_l).
float samplePosition(const TerrainParams &params, std::int32_t tile, std::uint32_t lod, std::uint32_t i) noexcept
{
    const std::int64_t global = static_cast<std::int64_t>(tile) * static_cast<std::int64_t>(params.tileResolution) + static_cast<std::int64_t>(i);
    const std::int64_t finest = global * (std::int64_t{1} << lod); // indice au niveau 0
    return static_cast<float>(static_cast<double>(finest) * static_cast<double>(params.sampleSpacing));
}
} // namespace

float sampleX(const TerrainParams &params, const TileKey &key, std::uint32_t i) noexcept { return samplePosition(params, key.x, key.lod, i); }
float sampleZ(const TerrainParams &params, const TileKey &key, std::uint32_t j) noexcept { return samplePosition(params, key.z, key.lod, j); }

Heightmap generateTile(const TerrainParams &params, std::uint32_t seed, const TileKey &key)
{
    Heightmap map;
    map.key = key;
    map.resolution = params.tileResolution;
    const std::uint32_t n = map.samplesPerSide();
    map.heights.resize(static_cast<std::size_t>(n) * n);
    for (std::uint32_t j = 0; j < n; ++j)
    {
        const float z = sampleZ(params, key, j);
        for (std::uint32_t i = 0; i < n; ++i)
        {
            map.heights[static_cast<std::size_t>(j) * n + i] = heightAt(params, seed, sampleX(params, key, i), z);
        }
    }
    return map;
}

} // namespace orosmo::world
