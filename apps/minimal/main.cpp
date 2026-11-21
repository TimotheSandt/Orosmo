// Projet minimal (docs/USING_THE_ENGINE.md) : une tuile de terrain générée par le moteur, sans fenêtre ni GPU.
#include <orosmo/world/checksum.hpp>
#include <orosmo/world/terrain.hpp>

#include <cstdio>

int main()
{
    orosmo::world::TerrainParams params;   // paramètres par défaut (réfléchis : sérialisables, affichables par orosmo::ui)
    params.heightScale = 80.0f;
    const orosmo::world::Heightmap tile = orosmo::world::generateTile(params, /*seed=*/42, {.x = 0, .z = 0, .lod = 0});
    std::printf("%zu samples, height at the centre = %.3f m, checksum %016llx\n", tile.heights.size(),
                static_cast<double>(tile.heights[tile.heights.size() / 2]),
                static_cast<unsigned long long>(orosmo::world::checksum64(tile.heights)));
    return 0;
}
