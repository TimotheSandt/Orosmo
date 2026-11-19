#include "engine/world/noise.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace engine::world
{

namespace
{

struct Vec2f
{
    float x;
    float y;
};
struct Vec3f
{
    float x;
    float y;
    float z;
};

constexpr float kInvSqrt2 = 0.70710678118654752f;

// Perlin 2D : huit directions unitaires. Valeur maximale théorique sqrt(2)/2, ramenée à 1 par kPerlin2Scale.
constexpr std::array<Vec2f, 8> kGradients2 = {{{1.0f, 0.0f},
                                               {-1.0f, 0.0f},
                                               {0.0f, 1.0f},
                                               {0.0f, -1.0f},
                                               {kInvSqrt2, kInvSqrt2},
                                               {-kInvSqrt2, kInvSqrt2},
                                               {kInvSqrt2, -kInvSqrt2},
                                               {-kInvSqrt2, -kInvSqrt2}}};
constexpr float kPerlin2Scale = 1.41421356237309505f;

// Perlin 3D : les douze milieux d'arêtes du cube, normalisés. Maximum théorique sqrt(3)/2, ramené à 1.
constexpr std::array<Vec3f, 12> kGradients3 = {{{kInvSqrt2, kInvSqrt2, 0.0f},
                                                {-kInvSqrt2, kInvSqrt2, 0.0f},
                                                {kInvSqrt2, -kInvSqrt2, 0.0f},
                                                {-kInvSqrt2, -kInvSqrt2, 0.0f},
                                                {kInvSqrt2, 0.0f, kInvSqrt2},
                                                {-kInvSqrt2, 0.0f, kInvSqrt2},
                                                {kInvSqrt2, 0.0f, -kInvSqrt2},
                                                {-kInvSqrt2, 0.0f, -kInvSqrt2},
                                                {0.0f, kInvSqrt2, kInvSqrt2},
                                                {0.0f, -kInvSqrt2, kInvSqrt2},
                                                {0.0f, kInvSqrt2, -kInvSqrt2},
                                                {0.0f, -kInvSqrt2, -kInvSqrt2}}};
constexpr float kPerlin3Scale = 1.15470053837925153f;

// Simplex (Gustavson) : gradients classiques (arêtes du cube, non normalisés) et facteurs d'échelle de l'algorithme d'origine.
constexpr std::array<Vec3f, 12> kSimplexGradients = {{{1, 1, 0},
                                                      {-1, 1, 0},
                                                      {1, -1, 0},
                                                      {-1, -1, 0},
                                                      {1, 0, 1},
                                                      {-1, 0, 1},
                                                      {1, 0, -1},
                                                      {-1, 0, -1},
                                                      {0, 1, 1},
                                                      {0, -1, 1},
                                                      {0, 1, -1},
                                                      {0, -1, -1}}};
constexpr float kF2 = 0.36602540378443865f; // (sqrt(3) - 1) / 2
constexpr float kG2 = 0.21132486540518712f; // (3 - sqrt(3)) / 6
constexpr float kF3 = 1.0f / 3.0f;
constexpr float kG3 = 1.0f / 6.0f;

inline float fade(float t) noexcept { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
inline float lerp(float a, float b, float t) noexcept { return a + t * (b - a); }

inline std::int32_t floorToInt(float v) noexcept { return static_cast<std::int32_t>(std::floor(v)); }

inline float grad2(std::uint32_t h, float x, float y) noexcept
{
    const Vec2f g = kGradients2[h & 7U];
    return g.x * x + g.y * y;
}

inline float grad3(std::uint32_t h, float x, float y, float z) noexcept
{
    const Vec3f g = kGradients3[h % 12U];
    return g.x * x + g.y * y + g.z * z;
}

// Position dans [0, 1) tirée de 16 bits du hachage.
inline float unit16(std::uint32_t bits) noexcept { return static_cast<float>(bits & 0xffffU) * (1.0f / 65536.0f); }

float basis2(NoiseBasis basis, float x, float y, std::uint32_t seed) noexcept
{
    switch (basis)
    {
        case NoiseBasis::kSimplex: return simplex2(x, y, seed);
        case NoiseBasis::kWorley: return 1.0f - 2.0f * std::min(worley2(x, y, seed).f1, 1.0f);
        case NoiseBasis::kPerlin:
        default: return perlin2(x, y, seed);
    }
}

float basis3(NoiseBasis basis, float x, float y, float z, std::uint32_t seed) noexcept
{
    switch (basis)
    {
        case NoiseBasis::kSimplex: return simplex3(x, y, z, seed);
        case NoiseBasis::kWorley: return 1.0f - 2.0f * std::min(worley3(x, y, z, seed).f1, 1.0f);
        case NoiseBasis::kPerlin:
        default: return perlin3(x, y, z, seed);
    }
}

} // namespace

float perlin2(float x, float y, std::uint32_t seed) noexcept
{
    const std::int32_t ix = floorToInt(x);
    const std::int32_t iy = floorToInt(y);
    const float fx = x - static_cast<float>(ix);
    const float fy = y - static_cast<float>(iy);
    const float n00 = grad2(hash2(ix, iy, seed), fx, fy);
    const float n10 = grad2(hash2(ix + 1, iy, seed), fx - 1.0f, fy);
    const float n01 = grad2(hash2(ix, iy + 1, seed), fx, fy - 1.0f);
    const float n11 = grad2(hash2(ix + 1, iy + 1, seed), fx - 1.0f, fy - 1.0f);
    const float u = fade(fx);
    const float v = fade(fy);
    return lerp(lerp(n00, n10, u), lerp(n01, n11, u), v) * kPerlin2Scale;
}

float perlin3(float x, float y, float z, std::uint32_t seed) noexcept
{
    const std::int32_t ix = floorToInt(x);
    const std::int32_t iy = floorToInt(y);
    const std::int32_t iz = floorToInt(z);
    const float fx = x - static_cast<float>(ix);
    const float fy = y - static_cast<float>(iy);
    const float fz = z - static_cast<float>(iz);
    const float u = fade(fx);
    const float v = fade(fy);
    const float w = fade(fz);
    float layer[2];
    for (int dz = 0; dz < 2; ++dz)
    {
        const float rz = fz - static_cast<float>(dz);
        const float n00 = grad3(hash3(ix, iy, iz + dz, seed), fx, fy, rz);
        const float n10 = grad3(hash3(ix + 1, iy, iz + dz, seed), fx - 1.0f, fy, rz);
        const float n01 = grad3(hash3(ix, iy + 1, iz + dz, seed), fx, fy - 1.0f, rz);
        const float n11 = grad3(hash3(ix + 1, iy + 1, iz + dz, seed), fx - 1.0f, fy - 1.0f, rz);
        layer[dz] = lerp(lerp(n00, n10, u), lerp(n01, n11, u), v);
    }
    return lerp(layer[0], layer[1], w) * kPerlin3Scale;
}

float simplex2(float x, float y, std::uint32_t seed) noexcept
{
    const float s = (x + y) * kF2;
    const std::int32_t i = floorToInt(x + s);
    const std::int32_t j = floorToInt(y + s);
    const float t = static_cast<float>(i + j) * kG2;
    const float x0 = x - (static_cast<float>(i) - t);
    const float y0 = y - (static_cast<float>(j) - t);
    const std::int32_t i1 = x0 > y0 ? 1 : 0;
    const std::int32_t j1 = x0 > y0 ? 0 : 1;
    const float x1 = x0 - static_cast<float>(i1) + kG2;
    const float y1 = y0 - static_cast<float>(j1) + kG2;
    const float x2 = x0 - 1.0f + 2.0f * kG2;
    const float y2 = y0 - 1.0f + 2.0f * kG2;
    auto corner = [&](float cx, float cy, std::int32_t gi, std::int32_t gj)
    {
        float r = 0.5f - cx * cx - cy * cy;
        if (r <= 0.0f)
        {
            return 0.0f;
        }
        r *= r;
        const Vec3f g = kSimplexGradients[hash2(gi, gj, seed) % 12U];
        return r * r * (g.x * cx + g.y * cy);
    };
    return 70.0f * (corner(x0, y0, i, j) + corner(x1, y1, i + i1, j + j1) + corner(x2, y2, i + 1, j + 1));
}

float simplex3(float x, float y, float z, std::uint32_t seed) noexcept
{
    const float s = (x + y + z) * kF3;
    const std::int32_t i = floorToInt(x + s);
    const std::int32_t j = floorToInt(y + s);
    const std::int32_t k = floorToInt(z + s);
    const float t = static_cast<float>(i + j + k) * kG3;
    const float x0 = x - (static_cast<float>(i) - t);
    const float y0 = y - (static_cast<float>(j) - t);
    const float z0 = z - (static_cast<float>(k) - t);
    std::int32_t i1, j1, k1, i2, j2, k2;
    if (x0 >= y0)
    {
        if (y0 >= z0) { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }
        else if (x0 >= z0) { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 0; k2 = 1; }
        else { i1 = 0; j1 = 0; k1 = 1; i2 = 1; j2 = 0; k2 = 1; }
    }
    else
    {
        if (y0 < z0) { i1 = 0; j1 = 0; k1 = 1; i2 = 0; j2 = 1; k2 = 1; }
        else if (x0 < z0) { i1 = 0; j1 = 1; k1 = 0; i2 = 0; j2 = 1; k2 = 1; }
        else { i1 = 0; j1 = 1; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }
    }
    auto corner = [&](float cx, float cy, float cz, std::int32_t gi, std::int32_t gj, std::int32_t gk)
    {
        float r = 0.6f - cx * cx - cy * cy - cz * cz;
        if (r <= 0.0f)
        {
            return 0.0f;
        }
        r *= r;
        const Vec3f g = kSimplexGradients[hash3(gi, gj, gk, seed) % 12U];
        return r * r * (g.x * cx + g.y * cy + g.z * cz);
    };
    const float c0 = corner(x0, y0, z0, i, j, k);
    const float c1 = corner(x0 - static_cast<float>(i1) + kG3, y0 - static_cast<float>(j1) + kG3, z0 - static_cast<float>(k1) + kG3, i + i1, j + j1,
                            k + k1);
    const float c2 = corner(x0 - static_cast<float>(i2) + 2.0f * kG3, y0 - static_cast<float>(j2) + 2.0f * kG3,
                            z0 - static_cast<float>(k2) + 2.0f * kG3, i + i2, j + j2, k + k2);
    const float c3 = corner(x0 - 1.0f + 3.0f * kG3, y0 - 1.0f + 3.0f * kG3, z0 - 1.0f + 3.0f * kG3, i + 1, j + 1, k + 1);
    return 32.0f * (c0 + c1 + c2 + c3);
}

Worley worley2(float x, float y, std::uint32_t seed) noexcept
{
    const std::int32_t ix = floorToInt(x);
    const std::int32_t iy = floorToInt(y);
    const float fx = x - static_cast<float>(ix);
    const float fy = y - static_cast<float>(iy);
    float d1 = 1e30f;
    float d2 = 1e30f;
    for (std::int32_t dy = -1; dy <= 1; ++dy)
    {
        for (std::int32_t dx = -1; dx <= 1; ++dx)
        {
            const std::uint32_t h = hash2(ix + dx, iy + dy, seed);
            const float px = static_cast<float>(dx) + unit16(h) - fx;
            const float py = static_cast<float>(dy) + unit16(h >> 16) - fy;
            const float d = px * px + py * py;
            if (d < d1)
            {
                d2 = d1;
                d1 = d;
            }
            else if (d < d2)
            {
                d2 = d;
            }
        }
    }
    return {std::sqrt(d1), std::sqrt(d2)};
}

Worley worley3(float x, float y, float z, std::uint32_t seed) noexcept
{
    const std::int32_t ix = floorToInt(x);
    const std::int32_t iy = floorToInt(y);
    const std::int32_t iz = floorToInt(z);
    const float fx = x - static_cast<float>(ix);
    const float fy = y - static_cast<float>(iy);
    const float fz = z - static_cast<float>(iz);
    float d1 = 1e30f;
    float d2 = 1e30f;
    for (std::int32_t dz = -1; dz <= 1; ++dz)
    {
        for (std::int32_t dy = -1; dy <= 1; ++dy)
        {
            for (std::int32_t dx = -1; dx <= 1; ++dx)
            {
                const std::uint32_t h = hash3(ix + dx, iy + dy, iz + dz, seed);
                const std::uint32_t hz = hashMix(h ^ 0x68bc21ebU);
                const float px = static_cast<float>(dx) + unit16(h) - fx;
                const float py = static_cast<float>(dy) + unit16(h >> 16) - fy;
                const float pz = static_cast<float>(dz) + unit16(hz) - fz;
                const float d = px * px + py * py + pz * pz;
                if (d < d1)
                {
                    d2 = d1;
                    d1 = d;
                }
                else if (d < d2)
                {
                    d2 = d;
                }
            }
        }
    }
    return {std::sqrt(d1), std::sqrt(d2)};
}

float fbm2(float x, float y, std::uint32_t seed, const FbmParams &params) noexcept
{
    const int octaves = std::clamp(params.octaves, 1, 16);
    float sum = 0.0f;
    float norm = 0.0f;
    float amplitude = 1.0f;
    float frequency = params.frequency;
    for (int o = 0; o < octaves; ++o)
    {
        sum += amplitude * basis2(params.basis, x * frequency, y * frequency, deriveSeed(seed, static_cast<std::uint32_t>(o)));
        norm += amplitude;
        amplitude *= params.gain;
        frequency *= params.lacunarity;
    }
    return norm > 0.0f ? sum / norm : 0.0f;
}

float fbm3(float x, float y, float z, std::uint32_t seed, const FbmParams &params) noexcept
{
    const int octaves = std::clamp(params.octaves, 1, 16);
    float sum = 0.0f;
    float norm = 0.0f;
    float amplitude = 1.0f;
    float frequency = params.frequency;
    for (int o = 0; o < octaves; ++o)
    {
        sum += amplitude * basis3(params.basis, x * frequency, y * frequency, z * frequency, deriveSeed(seed, static_cast<std::uint32_t>(o)));
        norm += amplitude;
        amplitude *= params.gain;
        frequency *= params.lacunarity;
    }
    return norm > 0.0f ? sum / norm : 0.0f;
}

float warpedFbm2(float x, float y, std::uint32_t seed, const FbmParams &params, const WarpParams &warp) noexcept
{
    if (warp.amplitude == 0.0f)
    {
        return fbm2(x, y, seed, params);
    }
    const FbmParams field{NoiseBasis::kPerlin, warp.octaves, warp.frequency, 2.0f, 0.5f};
    const float qx = fbm2(x, y, deriveSeed(seed, 0x51edU), field);
    const float qy = fbm2(x, y, deriveSeed(seed, 0x7a3bU), field);
    return fbm2(x + warp.amplitude * qx, y + warp.amplitude * qy, seed, params);
}

} // namespace engine::world
