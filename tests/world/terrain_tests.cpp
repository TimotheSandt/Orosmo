// TER-02 (tuiles), TER-03 (niveaux de détail sans fissure), TER-04 (streaming sous budget), TEST-03 (somme de contrôle d'une tuile).
#include <doctest/doctest.h>

#include <orosmo/core/tasks.hpp>
#include <orosmo/world/checksum.hpp>
#include <orosmo/world/lod.hpp>
#include <orosmo/world/streaming.hpp>
#include <orosmo/world/terrain.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <mutex>
#include <set>
#include <tuple>

namespace w = orosmo::world;

namespace
{

w::TerrainParams smallParams()
{
    w::TerrainParams p;
    p.tileResolution = 16;
    p.sampleSpacing = 2.0f;
    p.heightScale = 80.0f;
    p.relief.frequency = 1.0f / 256.0f;
    p.warp = {30.0f, 1.0f / 300.0f, 2};
    return p;
}

struct Vertex
{
    float x, y, z;
};
Vertex vertexAt(const w::TileMesh &mesh, std::uint32_t n, std::uint32_t i, std::uint32_t j)
{
    const std::size_t base = (static_cast<std::size_t>(j) * n + i) * w::TileMesh::kFloatsPerVertex;
    return {mesh.vertices[base], mesh.vertices[base + 1], mesh.vertices[base + 2]};
}

// Hauteur du bord de `mesh` (tuile grossière) au point (x, z) situé sur ce bord : interpolation linéaire entre ses deux sommets.
float edgeHeight(const w::TileMesh &mesh, std::uint32_t n, float x, float z)
{
    // Cherche le segment de bord (4 bords) qui contient le point.
    for (int edge = 0; edge < 4; ++edge)
    {
        for (std::uint32_t t = 0; t + 1 < n; ++t)
        {
            auto at = [&](std::uint32_t s)
            {
                switch (edge)
                {
                    case 0: return vertexAt(mesh, n, 0, s);
                    case 1: return vertexAt(mesh, n, n - 1, s);
                    case 2: return vertexAt(mesh, n, s, 0);
                    default: return vertexAt(mesh, n, s, n - 1);
                }
            };
            const Vertex a = at(t);
            const Vertex b = at(t + 1);
            const bool alongZ = a.x == b.x;
            if (alongZ && x == a.x && z >= a.z && z <= b.z)
            {
                return a.y + (b.y - a.y) * (z - a.z) / (b.z - a.z);
            }
            if (!alongZ && z == a.z && x >= a.x && x <= b.x)
            {
                return a.y + (b.y - a.y) * (x - a.x) / (b.x - a.x);
            }
        }
    }
    return std::nanf("");
}

} // namespace

TEST_SUITE("world/terrain")
{
    TEST_CASE("TER-02: a tile is a pure function of (seed, key, params); neighbours and LOD levels share exact samples")
    {
        const w::TerrainParams p = smallParams();
        const w::Heightmap a = w::generateTile(p, 7, {0, 0, 0});
        const w::Heightmap again = w::generateTile(p, 7, {0, 0, 0});
        CHECK(a.heights == again.heights);
        CHECK(a.heights.size() == 17u * 17u);
        CHECK(w::generateTile(p, 8, {0, 0, 0}).heights != a.heights);
        // Voisine à l'est : sa colonne 0 est la colonne 16 de `a`, octet pour octet.
        const w::Heightmap east = w::generateTile(p, 7, {1, 0, 0});
        for (std::uint32_t j = 0; j <= 16; ++j)
        {
            CHECK(east.at(0, j) == a.at(16, j));
        }
        // Niveau 1 : l'échantillon (i, j) est l'échantillon pair (2i, 2j) des tuiles du niveau 0 au même endroit.
        const w::Heightmap coarse = w::generateTile(p, 7, {0, 0, 1});
        for (std::uint32_t j = 0; j <= 8; ++j)
        {
            for (std::uint32_t i = 0; i <= 8; ++i)
            {
                CHECK(coarse.at(i, j) == a.at(2 * i, 2 * j));
            }
        }
        CHECK(w::sampleX(p, {-3, 0, 2}, 5) == doctest::Approx(-3 * 16 * 2 * 4 + 5 * 2 * 4));
        // Niveau élevé : global * 2^lod dépassait std::int64_t (2^31 * 2^32 avec 256 quadrilatères par tuile) ; en double, la
        // position est exacte, et l'échantillon partagé avec le niveau inférieur (indice pair) reste le même.
        w::TerrainParams wide = p;
        wide.tileResolution = 256;
        const double far = std::ldexp(8388608.0 * 256.0, 32) * static_cast<double>(wide.sampleSpacing);
        CHECK(w::sampleX(wide, {8388608, 0, 32}, 0) == static_cast<float>(far));
        CHECK(w::sampleX(wide, {8388608, 0, 32}, 0) == w::sampleX(wide, {16777216, 0, 31}, 0));
        CHECK(w::sampleX(wide, {0, 0, 32}, 0) == 0.0f);
        CHECK(w::sampleZ(wide, {0, -8388608, 32}, 0) == static_cast<float>(-far));
    }

    TEST_CASE("TEST-03: reference checksum of a generated tile")
    {
        const w::Heightmap tile = w::generateTile(smallParams(), 42, {-2, 3, 1});
        const std::uint64_t sum = w::checksum64(tile.heights);
        if (sum != 0xa414bb7320da7e83ULL)
        {
            std::printf("checksum tile = 0x%016llxULL\n", static_cast<unsigned long long>(sum));
        }
        CHECK(sum == 0xa414bb7320da7e83ULL);
    }

    TEST_CASE("TER-03: the selection covers the roots without overlap, is balanced and finer near the camera")
    {
        const w::TerrainParams p = smallParams();
        const w::LodConfig lod{.rootLod = 4, .rootRadius = 1, .distanceFactor = 1.5f};
        const auto leaves = w::selectTiles(p, lod, 10.0f, -20.0f);
        REQUIRE_FALSE(leaves.empty());
        // Aire totale = aire des 3x3 racines ; aucune paire ne se recouvre (une feuille ne contient pas une autre).
        double area = 0.0;
        for (const auto &k : leaves)
        {
            area += w::tileSize(p, k.lod) * w::tileSize(p, k.lod);
        }
        CHECK(area == doctest::Approx(9.0 * w::tileSize(p, 4) * w::tileSize(p, 4)));
        for (const auto &a : leaves)
        {
            for (const auto &b : leaves)
            {
                if (a == b || a.lod <= b.lod)
                {
                    continue;
                }
                const auto shift = a.lod - b.lod;
                CHECK_FALSE(((b.x >> shift) == a.x && (b.z >> shift) == a.z)); // b n'est pas dans a
            }
        }
        // La feuille sous la caméra est au niveau 0 ; une racine lointaine n'est pas découpée.
        const auto under = std::ranges::find_if(leaves, [&](const w::TileKey &k)
                                                {
                                                    const double s = w::tileSize(p, k.lod);
                                                    return 10.0 >= k.x * s && 10.0 < (k.x + 1) * s && -20.0 >= k.z * s && -20.0 < (k.z + 1) * s;
                                                });
        REQUIRE(under != leaves.end());
        CHECK(under->lod == 0u);
        // Équilibre : deux feuilles qui se touchent diffèrent d'un niveau au plus.
        for (const auto &a : leaves)
        {
            const auto stitch = w::coarserNeighbors(leaves, a);
            (void)stitch;
            for (const auto &b : leaves)
            {
                const double sa = w::tileSize(p, a.lod), sb = w::tileSize(p, b.lod);
                const double ax0 = a.x * sa, ax1 = ax0 + sa, az0 = a.z * sa, az1 = az0 + sa;
                const double bx0 = b.x * sb, bx1 = bx0 + sb, bz0 = b.z * sb, bz1 = bz0 + sb;
                const bool touchX = (ax1 == bx0 || bx1 == ax0) && az0 < bz1 && bz0 < az1;
                const bool touchZ = (az1 == bz0 || bz1 == az0) && ax0 < bx1 && bx0 < ax1;
                if (touchX || touchZ)
                {
                    CHECK(std::abs(static_cast<int>(a.lod) - static_cast<int>(b.lod)) <= 1);
                }
            }
        }
        // Distance de vue réglable : un facteur plus grand donne plus de feuilles.
        CHECK(w::selectTiles(p, {.rootLod = 4, .rootRadius = 1, .distanceFactor = 3.0f}, 10.0f, -20.0f).size() > leaves.size());

        // Entrées hors domaine : sélection vide, sans conversion indéfinie vers std::int32_t ni débordement des clés.
        const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
        CHECK(w::selectTiles(p, lod, nan, 0.0f).empty());
        CHECK(w::selectTiles(p, lod, 0.0f, inf).empty());
        CHECK(w::selectTiles(p, lod, -1e30f, 0.0f).empty());
        CHECK(w::selectTiles(p, {.rootLod = 31, .rootRadius = 1, .distanceFactor = 1.5f}, 0.0f, 0.0f).empty());
        CHECK(w::selectTiles(p, {.rootLod = 4, .rootRadius = -1, .distanceFactor = 1.5f}, 0.0f, 0.0f).empty());
        w::TerrainParams flatTiles = p;
        flatTiles.tileResolution = 0; // taille de racine nulle (appel direct, hors des bornes réfléchies)
        CHECK(w::selectTiles(flatTiles, lod, 0.0f, 0.0f).empty());
        // Loin mais représentable (racine 512 m : |clé| < 2^27 - 3) : sélection normale.
        CHECK_FALSE(w::selectTiles(p, lod, 1e10f, -1e10f).empty());
        CHECK(w::selectTiles(p, lod, 1e11f, 0.0f).empty()); // clé racine ~2^27,5 : niveau 0 hors de std::int32_t
    }

    TEST_CASE("TER-03: no crack: every vertex on an edge shared with a coarser leaf lies on the coarse edge")
    {
        // Résolutions paire et impaires (plage réfléchie 4..256) : avec une résolution impaire, la parité locale d'un sommet
        // n'est pas sa parité globale, et l'extrémité d'un bord peut être un sommet impair.
        for (const std::uint32_t resolution : {16U, 5U, 7U})
        {
        CAPTURE(resolution);
        w::TerrainParams p = smallParams();
        p.tileResolution = resolution;
        const auto leaves = w::selectTiles(p, {.rootLod = 3, .rootRadius = 1, .distanceFactor = 1.5f}, 0.0f, 0.0f);
        const std::uint32_t n = p.tileResolution + 1;
        std::size_t checkedEdges = 0;
        for (const auto &fine : leaves)
        {
            const auto stitch = w::coarserNeighbors(leaves, fine);
            const w::TileMesh fineMesh = w::buildTileMesh(p, 5, w::generateTile(p, 5, fine), stitch);
            for (std::size_t e = 0; e < 4; ++e)
            {
                if (!stitch[e])
                {
                    continue;
                }
                const std::int32_t dx = e == 0 ? -1 : (e == 1 ? 1 : 0);
                const std::int32_t dz = e == 2 ? -1 : (e == 3 ? 1 : 0);
                const w::TileKey coarse{(fine.x + dx) >> 1, (fine.z + dz) >> 1, fine.lod + 1};
                REQUIRE(std::ranges::find(leaves, coarse) != leaves.end());
                const w::TileMesh coarseMesh = w::buildTileMesh(p, 5, w::generateTile(p, 5, coarse), w::coarserNeighbors(leaves, coarse));
                for (std::uint32_t t = 0; t < n; ++t)
                {
                    const Vertex v = e == 0 ? vertexAt(fineMesh, n, 0, t)
                                   : e == 1 ? vertexAt(fineMesh, n, n - 1, t)
                                   : e == 2 ? vertexAt(fineMesh, n, t, 0)
                                            : vertexAt(fineMesh, n, t, n - 1);
                    const float onCoarse = edgeHeight(coarseMesh, n, v.x, v.z);
                    REQUIRE_FALSE(std::isnan(onCoarse));
                    CHECK(v.y == doctest::Approx(onCoarse).epsilon(1e-6)); // sur la droite du bord grossier : pas de fissure
                }
                ++checkedEdges;
            }
        }
        CHECK(checkedEdges > 0);
        }
    }

    TEST_CASE("TER-03: a tile mesh has (res+1)^2 vertices, 6 res^2 indices, unit normals facing up")
    {
        const w::TerrainParams p = smallParams();
        const w::TileMesh mesh = w::buildTileMesh(p, 1, w::generateTile(p, 1, {0, 0, 0}), {});
        CHECK(mesh.vertexCount() == 17u * 17u);
        CHECK(mesh.indices.size() == 6u * 16u * 16u);
        for (std::size_t v = 0; v < mesh.vertexCount(); ++v)
        {
            const float nx = mesh.vertices[v * 6 + 3], ny = mesh.vertices[v * 6 + 4], nz = mesh.vertices[v * 6 + 5];
            CHECK(std::sqrt(nx * nx + ny * ny + nz * nz) == doctest::Approx(1.0f).epsilon(1e-5));
            CHECK(ny > 0.0f);
        }
    }

    TEST_CASE("TER-04: streaming generates asynchronously (same bytes as synchronous), respects the memory budget, keeps the selection")
    {
        orosmo::core::TaskSystem tasks(2);
        const w::TerrainParams p = smallParams();
        const w::LodConfig lod{.rootLod = 3, .rootRadius = 1, .distanceFactor = 1.5f};
        const std::size_t tileBytes = [&]
        {
            const w::Heightmap h = w::generateTile(p, 3, {0, 0, 0});
            const w::TileMesh m = w::buildTileMesh(p, 3, h, {});
            return h.memoryBytes() + m.vertices.size() * sizeof(float) + m.indices.size() * sizeof(std::uint32_t);
        }();
        const std::size_t selectionSize = w::selectTiles(p, lod, 0.0f, 0.0f).size();
        w::TileStreamer streamer(p, 3, lod, {.memoryBudgetBytes = tileBytes * (selectionSize + 4), .maxJobsInFlight = 4}, tasks);
        streamer.update(0.0f, 0.0f);
        CHECK(streamer.stats().pendingTiles <= 4u);
        for (int i = 0; i < 64 && streamer.stats().residentTiles < selectionSize; ++i)
        {
            streamer.finishPending();
            streamer.update(0.0f, 0.0f);
        }
        streamer.finishPending();
        REQUIRE(streamer.stats().residentTiles >= selectionSize);
        for (const auto &key : streamer.selection())
        {
            const w::ResidentTile *t = streamer.find(key);
            REQUIRE(t != nullptr);
            CHECK(t->heights.heights == w::generateTile(p, 3, key).heights);
            CHECK(t->mesh.vertices == w::buildTileMesh(p, 3, t->heights, w::coarserNeighbors(streamer.selection(), key)).vertices);
        }
        // La caméra s'éloigne : d'autres tuiles sont demandées, les anciennes partent quand le budget est dépassé.
        for (int step = 1; step <= 6; ++step)
        {
            for (int i = 0; i < 64; ++i)
            {
                streamer.update(static_cast<float>(step) * 400.0f, 0.0f);
                streamer.finishPending();
                if (std::ranges::all_of(streamer.selection(), [&](const w::TileKey &k) { return streamer.find(k) != nullptr; }))
                {
                    break;
                }
            }
            streamer.update(static_cast<float>(step) * 400.0f, 0.0f);
            const w::StreamingStats s = streamer.stats();
            if (!s.overBudget)
            {
                CHECK(s.memoryBytes <= tileBytes * (selectionSize + 4)); // toutes les tuiles ont la même taille : budget exact
            }
            for (const auto &key : streamer.selection())
            {
                CHECK(streamer.find(key) != nullptr); // la sélection n'est jamais évincée
            }
        }
        CHECK(streamer.stats().evictions > 0);
        CHECK(streamer.stats().generated > selectionSize);
    }

    TEST_CASE("TER-04: finishPending applies the budget to the tiles it integrates, and re-stitches tiles whose neighbours changed")
    {
        orosmo::core::TaskSystem tasks(1);
        const w::TerrainParams p = smallParams();
        const std::size_t tileBytes = [&]
        {
            const w::Heightmap h = w::generateTile(p, 3, {0, 0, 0});
            const w::TileMesh m = w::buildTileMesh(p, 3, h, {});
            return h.memoryBytes() + m.vertices.size() * sizeof(float) + m.indices.size() * sizeof(std::uint32_t);
        }();
        // Une seule tuile sélectionnée (racine sans subdivision), budget d'une tuile : passer de A à B ne doit pas en garder deux.
        const w::LodConfig single{.rootLod = 0, .rootRadius = 0, .distanceFactor = 0.0f};
        w::TileStreamer streamer(p, 3, single, {.memoryBudgetBytes = tileBytes, .maxJobsInFlight = 4}, tasks);
        streamer.update(0.0f, 0.0f);
        streamer.finishPending();
        REQUIRE(streamer.stats().residentTiles == 1);
        streamer.update(1000.0f, 0.0f); // B lancée ; A encore résidente (le cache est au budget)
        streamer.finishPending();       // B intégrée : A doit partir sans attendre un autre update()
        CHECK(streamer.stats().residentTiles == 1);
        CHECK(streamer.stats().memoryBytes <= tileBytes);
        CHECK_FALSE(streamer.stats().overBudget);
        REQUIRE(streamer.selection().size() == 1);
        CHECK(streamer.find(streamer.selection()[0]) != nullptr);

        // Travaux lancés pour une sélection, terminés après le passage à une autre : la couture suit la sélection courante.
        std::mutex gate;
        gate.lock();
        tasks.submit([&] { const std::lock_guard<std::mutex> hold(gate); }); // bloque le seul thread de travail
        const w::LodConfig lod{.rootLod = 3, .rootRadius = 1, .distanceFactor = 1.5f};
        w::TileStreamer moving(p, 3, lod, {.memoryBudgetBytes = tileBytes * 1000, .maxJobsInFlight = 1000}, tasks);
        moving.update(0.0f, 0.0f);
        moving.update(37.0f, 21.0f); // les travaux de la première sélection ne sont pas commencés
        gate.unlock();
        moving.finishPending();
        std::size_t checked = 0;
        for (const auto &key : moving.selection())
        {
            if (const w::ResidentTile *t = moving.find(key))
            {
                CHECK(t->mesh.vertices == w::buildTileMesh(p, 3, t->heights, w::coarserNeighbors(moving.selection(), key)).vertices);
                ++checked;
            }
        }
        CHECK(checked > 0);
    }

    TEST_CASE("TER-04: a selection larger than the budget is reported, not evicted")
    {
        orosmo::core::TaskSystem tasks(1);
        const w::TerrainParams p = smallParams();
        w::TileStreamer streamer(p, 1, {.rootLod = 3, .rootRadius = 1, .distanceFactor = 1.5f}, {.memoryBudgetBytes = 1, .maxJobsInFlight = 64}, tasks);
        for (int i = 0; i < 16; ++i) // la sélection peut dépasser le nombre de générations simultanées : plusieurs tours
        {
            streamer.update(0.0f, 0.0f);
            streamer.finishPending();
        }
        streamer.update(0.0f, 0.0f);
        CHECK(streamer.stats().overBudget);
        CHECK(streamer.stats().residentTiles == streamer.selection().size());
    }
}
