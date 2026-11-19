// Projet minimal (docs/USING_THE_ENGINE.md) : une tuile de terrain générée par le moteur, sans fenêtre ni GPU.
#include <engine/world/checksum.hpp>
#include <engine/world/terrain.hpp>

#include <cstdio>

int main()
{
    engine::world::TerrainParams params;   // paramètres par défaut (réfléchis : sérialisables, affichables par engine::ui)
    params.heightScale = 80.0f;
    const engine::world::Heightmap tile = engine::world::generateTile(params, /*seed=*/42, {.x = 0, .z = 0, .lod = 0});
    std::printf("%zu samples, height at the centre = %.3f m, checksum %016llx\n", tile.heights.size(),
                static_cast<double>(tile.heights[tile.heights.size() / 2]),
                static_cast<unsigned long long>(engine::world::checksum64(tile.heights)));
    return 0;
}
