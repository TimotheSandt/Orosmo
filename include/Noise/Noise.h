#pragma once

#include "Random.h"
#include "Utilities.h"

class Noise
{
  public:
    Noise() = delete; // no hidden clock seed (rule 9): the application chooses the seed
    explicit Noise(uint64_t seed);
    ~Noise() = default;

    void SetSeed(uint64_t seed);
    uint64_t GetSeed();

    float WhiteNoise(float x);
    float WhiteNoise(float x, float y);
    float WhiteNoise(float x, float y, float z);
    float WhiteNoise(float x, float y, float z, float w);

    float SmoothNoise(float x, float scale);
    float SmoothNoise(float x, float y, float scale);
    float SmoothNoise(float x, float y, float z, float scale);
    float SmoothNoise(float x, float y, float z, float w, float scale);

    float FractalNoise(float x, float scale, int octaves, float persistence, float lacunarity);
    float FractalNoise(float x, float y, float scale, int octaves, float persistence, float lacunarity);
    float FractalNoise(float x, float y, float z, float scale, int octaves, float persistence, float lacunarity);
    float FractalNoise(float x, float y, float z, float w, float scale, int octaves, float persistence, float lacunarity);

    float PerlinNoise(float x, float scale, int octaves, float persistence, float lacunarity);
    float PerlinNoise(float x, float y, float scale, int octaves, float persistence, float lacunarity);
    float PerlinNoise(float x, float y, float z, float scale, int octaves, float persistence, float lacunarity);
    float PerlinNoise(float x, float y, float z, float w, float scale, int octaves, float persistence, float lacunarity);

  private:
    uint64_t seed;
};
