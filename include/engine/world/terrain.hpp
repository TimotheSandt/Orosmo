// TER-02 : terrain par tuiles. La hauteur est une fonction pure de (graine, position, paramètres) ; une tuile échantillonne cette
// fonction sur une grille. Les positions d'échantillons sont calculées depuis des indices ENTIERS globaux : deux tuiles voisines
// (ou deux niveaux de détail) qui partagent un échantillon le calculent au même endroit, donc avec la même valeur exacte.
#pragma once

#include "engine/core/reflection.hpp"
#include "engine/world/noise.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace engine::world
{

struct TerrainParams
{
    FbmParams relief{NoiseBasis::kPerlin, 6, 1.0f / 512.0f, 2.0f, 0.5f};
    WarpParams warp{0.0f, 1.0f / 1024.0f, 3};
    float heightScale = 120.0f;  // amplitude du relief, mètres
    float heightOffset = 0.0f;   // ajouté après l'échelle, mètres
    float sampleSpacing = 1.0f;  // distance entre deux échantillons au niveau de détail 0, mètres
    std::uint32_t tileResolution = 64; // quadrilatères par côté de tuile (échantillons : résolution + 1)
};

// Hauteur (mètres) au point (x, z) du monde.
float heightAt(const TerrainParams &params, std::uint32_t seed, float x, float z) noexcept;

// Tuile (x, z) du niveau de détail `lod` : elle couvre [x, x + 1) * taille, taille = résolution * espacement * 2^lod.
struct TileKey
{
    std::int32_t x = 0;
    std::int32_t z = 0;
    std::uint32_t lod = 0;
    friend bool operator==(const TileKey &, const TileKey &) = default;
};

struct TileKeyHash
{
    std::size_t operator()(const TileKey &k) const noexcept
    {
        return static_cast<std::size_t>(hash3(k.x, k.z, static_cast<std::int32_t>(k.lod), 0x5eedU));
    }
};

double tileSize(const TerrainParams &params, std::uint32_t lod) noexcept;
double sampleSpacing(const TerrainParams &params, std::uint32_t lod) noexcept;

// Grille de hauteurs d'une tuile : (résolution + 1)^2 échantillons, rangée par rangée (z croissant), x croissant dans la rangée.
struct Heightmap
{
    TileKey key{};
    std::uint32_t resolution = 0;
    std::vector<float> heights;

    std::uint32_t samplesPerSide() const noexcept { return resolution + 1; }
    float at(std::uint32_t i, std::uint32_t j) const noexcept { return heights[static_cast<std::size_t>(j) * samplesPerSide() + i]; }
    std::size_t memoryBytes() const noexcept { return heights.size() * sizeof(float); }
};

// Position monde (mètres) de l'échantillon (i, j) de la tuile : depuis l'indice global entier, en double puis arrondie en float.
float sampleX(const TerrainParams &params, const TileKey &key, std::uint32_t i) noexcept;
float sampleZ(const TerrainParams &params, const TileKey &key, std::uint32_t j) noexcept;

// Génère la tuile (déterministe, sans état : appelable depuis n'importe quel thread).
Heightmap generateTile(const TerrainParams &params, std::uint32_t seed, const TileKey &key);

} // namespace engine::world

// Réflexion (CORE-12) : les paramètres du terrain s'affichent et se sauvegardent automatiquement (UI-12, préréglages).
template <> struct engine::core::Reflect<engine::world::FbmParams>
{
    static constexpr int kVersion = 1;
    static constexpr auto fields()
    {
        using P = engine::world::FbmParams;
        return std::make_tuple(field("basis", &P::basis, range(0, 2), "0 Perlin, 1 Simplex, 2 Worley"),
                               field("octaves", &P::octaves, range(1, 16), "nombre d'octaves"),
                               field("frequency", &P::frequency, range(0.0001, 1.0), "fréquence de la première octave (1/m)"),
                               field("lacunarity", &P::lacunarity, range(1.0, 4.0), "multiplicateur de fréquence"),
                               field("gain", &P::gain, range(0.0, 1.0), "multiplicateur d'amplitude"));
    }
};

template <> struct engine::core::Reflect<engine::world::WarpParams>
{
    static constexpr int kVersion = 1;
    static constexpr auto fields()
    {
        using P = engine::world::WarpParams;
        return std::make_tuple(field("amplitude", &P::amplitude, range(0.0, 2000.0), "déplacement maximal (m)"),
                               field("frequency", &P::frequency, range(0.0001, 1.0), "fréquence du champ de déplacement (1/m)"),
                               field("octaves", &P::octaves, range(1, 8), "octaves du champ"));
    }
};

template <> struct engine::core::Reflect<engine::world::TerrainParams>
{
    static constexpr int kVersion = 1;
    static constexpr auto fields()
    {
        using P = engine::world::TerrainParams;
        return std::make_tuple(field("relief", &P::relief, {}, "bruit du relief"), field("warp", &P::warp, {}, "distorsion de domaine"),
                               field("heightScale", &P::heightScale, range(0.0, 5000.0), "amplitude (m)"),
                               field("heightOffset", &P::heightOffset, range(-5000.0, 5000.0), "décalage (m)"),
                               field("sampleSpacing", &P::sampleSpacing, range(0.0625, 64.0), "espacement au niveau 0 (m)"),
                               field("tileResolution", &P::tileResolution, range(4, 256), "quadrilatères par côté de tuile"));
    }
};
