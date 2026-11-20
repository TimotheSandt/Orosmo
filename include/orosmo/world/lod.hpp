// TER-03 : niveaux de détail par quadtree, SANS FISSURE. La sélection découpe un nœud tant que la caméra est plus proche que
// `distanceFactor` fois sa taille, puis équilibre l'arbre (deux feuilles voisines diffèrent d'un niveau au plus). Le maillage d'une
// feuille « coud » chaque bord voisin d'une feuille plus grossière : ses sommets impairs y prennent la moyenne de leurs voisins
// pairs, c'est-à-dire exactement la droite du bord grossier ; les sommets pairs sont les mêmes échantillons des deux côtés.
#pragma once

#include "orosmo/world/terrain.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace orosmo::world
{

struct LodConfig
{
    std::uint32_t rootLod = 5;     // niveau des tuiles racines (les plus grandes)
    std::int32_t rootRadius = 2;   // tuiles racines de part et d'autre de celle de la caméra
    float distanceFactor = 2.0f;   // un nœud se découpe si distance(caméra, nœud) < facteur * taille du nœud (distance de vue)
};

// Feuilles à afficher pour une caméra en (x, z) : équilibrées, sans recouvrement, couvrant les racines autour de la caméra.
std::vector<TileKey> selectTiles(const TerrainParams &params, const LodConfig &config, float cameraX, float cameraZ);

enum class Edge : std::uint8_t
{
    kWest,  // x minimal
    kEast,  // x maximal
    kSouth, // z minimal
    kNorth, // z maximal
};

// Pour chaque bord (ordre de `Edge`) : vrai si la feuille voisine de ce côté est plus grossière (niveau + 1) : bord à coudre.
using CoarserNeighbors = std::array<bool, 4>;
CoarserNeighbors coarserNeighbors(const std::vector<TileKey> &leaves, const TileKey &leaf);

// Maillage d'une tuile : sommets (position x, y, z, normale x, y, z ; mètres, repère monde) et triangles.
struct TileMesh
{
    TileKey key{};
    std::vector<float> vertices; // 6 flottants par sommet
    std::vector<std::uint32_t> indices;
    static constexpr std::uint32_t kFloatsPerVertex = 6;
    std::size_t vertexCount() const noexcept { return vertices.size() / kFloatsPerVertex; }
};

TileMesh buildTileMesh(const TerrainParams &params, std::uint32_t seed, const Heightmap &heights, const CoarserNeighbors &stitch);

} // namespace orosmo::world
