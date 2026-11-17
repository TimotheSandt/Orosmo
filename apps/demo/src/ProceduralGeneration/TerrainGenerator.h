#pragma once

#include "Grid.h"
#include "Noise.h"
#include "Renderer3D.h"

#include <functional>
#include <cmath>
#include <memory>

class TerrainGenerator
{
  public:
    // Seed of a generator built without one: fixed, never the clock (rule 9). The application picks the real seed.
    static constexpr int kDefaultSeed = 1;

    TerrainGenerator() : noise(kDefaultSeed) {}
    TerrainGenerator(int seed);
    TerrainGenerator(float sizeX, float sizeZ, int resX, int resZ, int seed);
    ~TerrainGenerator();

    void Destroy();

    void init(float sizeX, float sizeZ, int resX, int resZ);
    void Render(const Renderer3D &renderer3D, Camera &camera);

    // Terrain Generation
    void GenerateFlatTerrain();

    void GenerateRandomTerrain(float height = 1.0f);

    void GeneratePerlinTerrain(float scale, float height, int octaves, float persistence, float lacunarity);
    void GenerateFractalTerrain(float scale, float height, int octaves, float persistence, float lacunarity);

    // Terrain Modification
    void GenerateCrater(float depth, float radius, glm::vec3 center);

    Grid &GetGrid() { return grid; }
    const Grid &GetGrid() const { return grid; }
    Mesh &GetMesh() { return grid.GetMesh(); }

    void SetNoiseSeed(int seed) { noise.SetSeed(seed); }

  private:
    Grid grid;
    Noise noise;
};
