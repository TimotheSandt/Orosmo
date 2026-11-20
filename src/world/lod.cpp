#include "orosmo/world/lod.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace orosmo::world
{

namespace
{

// Floor de a / 2^k pour un entier signé (vers -infini).
std::int32_t floorShift(std::int32_t a, std::uint32_t k) noexcept { return static_cast<std::int32_t>(std::floor(std::ldexp(static_cast<double>(a), -static_cast<int>(k)))); }

// Distance horizontale de (cx, cz) au carré de la tuile (0 dedans).
double distanceTo(const TerrainParams &params, const TileKey &key, double cx, double cz) noexcept
{
    const double size = tileSize(params, key.lod);
    const double x0 = static_cast<double>(key.x) * size;
    const double z0 = static_cast<double>(key.z) * size;
    const double dx = std::max({x0 - cx, 0.0, cx - (x0 + size)});
    const double dz = std::max({z0 - cz, 0.0, cz - (z0 + size)});
    return std::sqrt(dx * dx + dz * dz);
}

using LeafSet = std::unordered_set<TileKey, TileKeyHash>;

// Feuille qui contient la cellule (x, z) du niveau `lod` (elle peut être plus fine, égale ou plus grossière) ; nullptr si
// aucune feuille n'est à ce niveau ou au-dessus (bord de la zone, ou région découpée plus finement).
const TileKey *coveringLeafAtOrAbove(const LeafSet &leaves, std::int32_t x, std::int32_t z, std::uint32_t lod, std::uint32_t maxLod)
{
    for (std::uint32_t l = lod; l <= maxLod; ++l)
    {
        const TileKey candidate{floorShift(x, l - lod), floorShift(z, l - lod), l};
        if (auto it = leaves.find(candidate); it != leaves.end())
        {
            return &*it;
        }
    }
    return nullptr;
}

constexpr std::array<std::array<std::int32_t, 2>, 4> kDirections = {{{-1, 0}, {1, 0}, {0, -1}, {0, 1}}}; // ordre de Edge

} // namespace

std::vector<TileKey> selectTiles(const TerrainParams &params, const LodConfig &config, float cameraX, float cameraZ)
{
    const double rootSize = tileSize(params, config.rootLod);
    const auto cx = static_cast<std::int32_t>(std::floor(static_cast<double>(cameraX) / rootSize));
    const auto cz = static_cast<std::int32_t>(std::floor(static_cast<double>(cameraZ) / rootSize));
    std::vector<TileKey> work;
    for (std::int32_t z = cz - config.rootRadius; z <= cz + config.rootRadius; ++z)
    {
        for (std::int32_t x = cx - config.rootRadius; x <= cx + config.rootRadius; ++x)
        {
            work.push_back({x, z, config.rootLod});
        }
    }
    LeafSet leaves;
    auto split = [&](const TileKey &k, std::vector<TileKey> &into)
    {
        for (std::int32_t dz = 0; dz < 2; ++dz)
        {
            for (std::int32_t dx = 0; dx < 2; ++dx)
            {
                into.push_back({k.x * 2 + dx, k.z * 2 + dz, k.lod - 1});
            }
        }
    };
    // 1. Découpe selon la distance.
    while (!work.empty())
    {
        const TileKey k = work.back();
        work.pop_back();
        if (k.lod > 0 && distanceTo(params, k, cameraX, cameraZ) < static_cast<double>(config.distanceFactor) * tileSize(params, k.lod))
        {
            split(k, work);
        }
        else
        {
            leaves.insert(k);
        }
    }
    // 2. Équilibrage : une feuille dont une voisine est plus grossière de 2 niveaux ou plus fait découper cette voisine.
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (const TileKey &leaf : std::vector<TileKey>(leaves.begin(), leaves.end()))
        {
            if (!leaves.contains(leaf))
            {
                continue;
            }
            for (const auto &d : kDirections)
            {
                const TileKey *n = coveringLeafAtOrAbove(leaves, leaf.x + d[0], leaf.z + d[1], leaf.lod, config.rootLod);
                if (n != nullptr && n->lod > leaf.lod + 1)
                {
                    const TileKey coarse = *n;
                    leaves.erase(coarse);
                    std::vector<TileKey> children;
                    split(coarse, children);
                    leaves.insert(children.begin(), children.end());
                    changed = true;
                }
            }
        }
    }
    std::vector<TileKey> out(leaves.begin(), leaves.end());
    std::ranges::sort(out, [](const TileKey &a, const TileKey &b)
                      { return a.lod != b.lod ? a.lod > b.lod : (a.z != b.z ? a.z < b.z : a.x < b.x); });
    return out;
}

CoarserNeighbors coarserNeighbors(const std::vector<TileKey> &leaves, const TileKey &leaf)
{
    LeafSet set(leaves.begin(), leaves.end());
    std::uint32_t maxLod = leaf.lod;
    for (const TileKey &k : leaves)
    {
        maxLod = std::max(maxLod, k.lod);
    }
    CoarserNeighbors out{};
    for (std::size_t e = 0; e < 4; ++e)
    {
        const TileKey *n = coveringLeafAtOrAbove(set, leaf.x + kDirections[e][0], leaf.z + kDirections[e][1], leaf.lod, maxLod);
        out[e] = n != nullptr && n->lod > leaf.lod;
    }
    return out;
}

TileMesh buildTileMesh(const TerrainParams &params, std::uint32_t seed, const Heightmap &heights, const CoarserNeighbors &stitch)
{
    TileMesh mesh;
    mesh.key = heights.key;
    const std::uint32_t n = heights.samplesPerSide();
    const auto spacing = static_cast<float>(sampleSpacing(params, heights.key.lod));
    std::vector<float> h = heights.heights;
    const std::vector<float> &original = heights.heights;
    // Couture : sur un bord voisin d'une feuille deux fois plus grossière, chaque sommet d'indice GLOBAL impair prend la moyenne
    // de ses deux voisins pairs (les échantillons du côté grossier sont les indices globaux pairs) : il tombe exactement sur
    // l'arête du côté grossier. L'indice global (et non local) traite aussi une résolution impaire, où la parité locale et la
    // parité globale diffèrent d'une tuile à l'autre ; un voisin hors de la tuile (extrémité impaire) est recalculé par heightAt.
    const auto res = static_cast<std::int64_t>(heights.resolution);
    auto alongPosition = [&](std::int32_t tile, std::int64_t t) // position d'un indice local, éventuellement -1 ou n
    {
        TileKey k = heights.key;
        k.x = tile;
        if (t < 0)
        {
            k.x = tile - 1;
            t += res;
        }
        else if (t > res)
        {
            k.x = tile + 1;
            t -= res;
        }
        return sampleX(params, k, static_cast<std::uint32_t>(t));
    };
    // index(t) : case du sommet t du bord ; tile : coordonnée de la tuile le long du bord ; height(t) : hauteur hors tuile.
    auto stitchLine = [&](auto index, std::int32_t tile, auto heightOutside)
    {
        const std::int64_t first = static_cast<std::int64_t>(tile) * res;
        for (std::uint32_t t = 0; t < n; ++t)
        {
            if (((first + t) & 1) == 0)
            {
                continue;
            }
            const float a = t > 0 ? original[index(t - 1)] : heightOutside(alongPosition(tile, -1));
            const float b = t + 1 < n ? original[index(t + 1)] : heightOutside(alongPosition(tile, static_cast<std::int64_t>(n)));
            h[index(t)] = 0.5f * (a + b);
        }
    };
    const float west = sampleX(params, heights.key, 0);
    const float east = sampleX(params, heights.key, n - 1);
    const float south = sampleZ(params, heights.key, 0);
    const float north = sampleZ(params, heights.key, n - 1);
    auto atX = [&](float x) { return [&params, seed, x](float z) { return heightAt(params, seed, x, z); }; };
    auto atZ = [&](float z) { return [&params, seed, z](float x) { return heightAt(params, seed, x, z); }; };
    if (stitch[static_cast<std::size_t>(Edge::kWest)]) stitchLine([&](std::uint32_t t) { return static_cast<std::size_t>(t) * n; }, heights.key.z, atX(west));
    if (stitch[static_cast<std::size_t>(Edge::kEast)]) stitchLine([&](std::uint32_t t) { return static_cast<std::size_t>(t) * n + (n - 1); }, heights.key.z, atX(east));
    if (stitch[static_cast<std::size_t>(Edge::kSouth)]) stitchLine([&](std::uint32_t t) { return static_cast<std::size_t>(t); }, heights.key.x, atZ(south));
    if (stitch[static_cast<std::size_t>(Edge::kNorth)]) stitchLine([&](std::uint32_t t) { return static_cast<std::size_t>(n - 1) * n + t; }, heights.key.x, atZ(north));

    mesh.vertices.reserve(static_cast<std::size_t>(n) * n * TileMesh::kFloatsPerVertex);
    for (std::uint32_t j = 0; j < n; ++j)
    {
        const float z = sampleZ(params, heights.key, j);
        for (std::uint32_t i = 0; i < n; ++i)
        {
            const float x = sampleX(params, heights.key, i);
            const float y = h[static_cast<std::size_t>(j) * n + i];
            // Normale par différences centrées sur la fonction de hauteur (aussi aux bords : pas besoin des tuiles voisines).
            const float hl = i > 0 ? h[static_cast<std::size_t>(j) * n + i - 1] : heightAt(params, seed, x - spacing, z);
            const float hr = i + 1 < n ? h[static_cast<std::size_t>(j) * n + i + 1] : heightAt(params, seed, x + spacing, z);
            const float hd = j > 0 ? h[static_cast<std::size_t>(j - 1) * n + i] : heightAt(params, seed, x, z - spacing);
            const float hu = j + 1 < n ? h[static_cast<std::size_t>(j + 1) * n + i] : heightAt(params, seed, x, z + spacing);
            float nx = hl - hr;
            float ny = 2.0f * spacing;
            float nz = hd - hu;
            const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            nx /= len;
            ny /= len;
            nz /= len;
            mesh.vertices.insert(mesh.vertices.end(), {x, y, z, nx, ny, nz});
        }
    }
    mesh.indices.reserve(static_cast<std::size_t>(n - 1) * (n - 1) * 6);
    for (std::uint32_t j = 0; j + 1 < n; ++j)
    {
        for (std::uint32_t i = 0; i + 1 < n; ++i)
        {
            const std::uint32_t a = j * n + i;
            const std::uint32_t b = a + 1;
            const std::uint32_t c = a + n;
            const std::uint32_t d = c + 1;
            // Sens antihoraire vu du dessus (Y vers le haut, main droite).
            mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});
        }
    }
    return mesh;
}

} // namespace orosmo::world
