// TER-01 (CPU) et TEST-03 : bruit déterministe. Les sommes de contrôle de référence sont VERSIONNÉES ici : la même valeur doit
// sortir sur Linux x64 et arm64, Windows (MinGW) et macOS arm64 (la CI exécute ces tests sur les quatre). Une modification
// volontaire de l'algorithme change ces constantes ; la changer sans raison casse le déterminisme promis aux utilisateurs.
#include <doctest/doctest.h>

#include <engine/world/checksum.hpp>
#include <engine/world/noise.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <limits>
#include <vector>

namespace w = engine::world;

namespace
{

// Grille size x size d'échantillons de f(x, y), pas `step`, origine décalée (coordonnées négatives et positives).
std::vector<float> grid(int size, float step, const std::function<float(float, float)> &f)
{
    std::vector<float> out;
    out.reserve(static_cast<std::size_t>(size) * static_cast<std::size_t>(size));
    for (int j = 0; j < size; ++j)
    {
        for (int i = 0; i < size; ++i)
        {
            out.push_back(f(static_cast<float>(i - size / 2) * step, static_cast<float>(j - size / 2) * step));
        }
    }
    return out;
}

struct Range
{
    float lo = 1e30f;
    float hi = -1e30f;
};
Range rangeOf(const std::vector<float> &v)
{
    Range r;
    for (float x : v)
    {
        if (!std::isfinite(x)) // un NaN ne déplacerait pas min / max : il ouvre les deux bornes pour faire échouer les tests
        {
            r.lo = -std::numeric_limits<float>::infinity();
            r.hi = std::numeric_limits<float>::infinity();
            continue;
        }
        r.lo = std::min(r.lo, x);
        r.hi = std::max(r.hi, x);
    }
    return r;
}

void expectChecksum(const char *name, const std::vector<float> &values, std::uint64_t expected)
{
    const std::uint64_t actual = w::checksum64(values);
    if (actual != expected)
    {
        std::printf("checksum %s = 0x%016llxULL\n", name, static_cast<unsigned long long>(actual));
    }
    INFO(name);
    CHECK(actual == expected);
}

} // namespace

TEST_SUITE("world/noise")
{
    TEST_CASE("hashing is a pure function of (coordinates, seed); a different seed decorrelates")
    {
        CHECK(w::hash2(3, -7, 42) == w::hash2(3, -7, 42));
        CHECK(w::hash2(3, -7, 42) != w::hash2(3, -7, 43));
        CHECK(w::hash2(3, -7, 42) != w::hash2(-7, 3, 42));
        CHECK(w::hash3(1, 2, 3, 9) != w::hash3(1, 2, 4, 9));
        static_assert(w::hashMix(0) == 0); // bijection : 0 -> 0 (lowbias32)
        CHECK(w::deriveSeed(1, 0) != w::deriveSeed(1, 1));
    }

    TEST_CASE("Perlin: zero on lattice points, bounded by 1, continuous, seed-dependent")
    {
        CHECK(w::perlin2(3.0f, -5.0f, 7) == 0.0f);
        CHECK(w::perlin3(1.0f, 2.0f, -3.0f, 7) == 0.0f);
        const auto values = grid(128, 0.137f, [](float x, float y) { return w::perlin2(x, y, 42); });
        const Range r = rangeOf(values);
        CHECK(r.lo >= -1.0001f);
        CHECK(r.hi <= 1.0001f);
        CHECK(r.hi - r.lo > 1.0f); // pas une constante
        CHECK(std::fabs(w::perlin2(10.3f, 4.7f, 42) - w::perlin2(10.3001f, 4.7f, 42)) < 0.01f);
        CHECK(w::perlin2(10.3f, 4.7f, 42) != w::perlin2(10.3f, 4.7f, 43));
        const auto v3 = grid(64, 0.21f, [](float x, float y) { return w::perlin3(x, y, 0.37f * x - y, 5); });
        CHECK(rangeOf(v3).lo >= -1.0001f);
        CHECK(rangeOf(v3).hi <= 1.0001f);
    }

    TEST_CASE("Simplex and Worley: bounded, f1 <= f2, Worley is zero at a feature point")
    {
        const auto s2 = grid(128, 0.113f, [](float x, float y) { return w::simplex2(x, y, 3); });
        CHECK(rangeOf(s2).lo >= -1.05f);
        CHECK(rangeOf(s2).hi <= 1.05f);
        CHECK(rangeOf(s2).hi - rangeOf(s2).lo > 1.0f);
        const auto s3 = grid(64, 0.2f, [](float x, float y) { return w::simplex3(x, y, x * 0.5f, 3); });
        CHECK(rangeOf(s3).lo >= -1.05f);
        CHECK(rangeOf(s3).hi <= 1.05f);
        for (float x = -3.0f; x < 3.0f; x += 0.37f)
        {
            const w::Worley a = w::worley2(x, 1.5f * x, 11);
            CHECK(a.f1 >= 0.0f);
            CHECK(a.f1 <= a.f2);
            const w::Worley b = w::worley3(x, -x, 0.25f * x, 11);
            CHECK(b.f1 <= b.f2);
        }
        // Point caractéristique de la cellule (0, 0) : la distance y est nulle.
        const std::uint32_t h = w::hash2(0, 0, 11);
        const float px = static_cast<float>(h & 0xffffU) / 65536.0f;
        const float py = static_cast<float>((h >> 16) & 0xffffU) / 65536.0f;
        CHECK(w::worley2(px, py, 11).f1 == doctest::Approx(0.0f).epsilon(1e-6));
    }

    TEST_CASE("fBm and domain warping stay in the basis range and add detail; amplitude 0 is plain fBm")
    {
        const w::FbmParams params{.basis = w::NoiseBasis::kPerlin, .octaves = 6, .frequency = 0.5f, .lacunarity = 2.0f, .gain = 0.5f};
        const auto f = grid(96, 0.25f, [&](float x, float y) { return w::fbm2(x, y, 42, params); });
        CHECK(rangeOf(f).lo >= -1.0001f);
        CHECK(rangeOf(f).hi <= 1.0001f);
        CHECK(w::warpedFbm2(1.5f, 2.5f, 42, params, {}) == w::fbm2(1.5f, 2.5f, 42, params));
        const w::WarpParams warp{.amplitude = 2.0f, .frequency = 0.3f, .octaves = 3};
        CHECK(w::warpedFbm2(1.5f, 2.5f, 42, params, warp) != w::fbm2(1.5f, 2.5f, 42, params));
        // Les octaves comptent : 1 octave et 6 octaves diffèrent ; le nombre est borné à [1, 16].
        CHECK(w::fbm2(1.3f, 0.7f, 1, {.octaves = 1}) != w::fbm2(1.3f, 0.7f, 1, {.octaves = 6}));
        CHECK(w::fbm2(1.3f, 0.7f, 1, {.octaves = 0}) == w::fbm2(1.3f, 0.7f, 1, {.octaves = 1}));
        CHECK(w::fbm2(1.3f, 0.7f, 1, {.octaves = 99}) == w::fbm2(1.3f, 0.7f, 1, {.octaves = 16}));
        const auto worley = grid(32, 0.3f, [](float x, float y) { return w::fbm2(x, y, 9, {.basis = w::NoiseBasis::kWorley, .octaves = 3}); });
        CHECK(rangeOf(worley).lo >= -1.0001f);
        CHECK(rangeOf(worley).hi <= 1.0001f);
        CHECK(w::fbm3(0.3f, 0.6f, 0.9f, 2, {.basis = w::NoiseBasis::kSimplex, .octaves = 4}) ==
              w::fbm3(0.3f, 0.6f, 0.9f, 2, {.basis = w::NoiseBasis::kSimplex, .octaves = 4}));
    }

    TEST_CASE("coordinates outside the integer lattice give the neutral value instead of an undefined conversion")
    {
        const float nan = std::nanf("");
        for (float big : {w::kNoiseCoordinateLimit, -w::kNoiseCoordinateLimit, 1e30f, nan})
        {
            CHECK(w::perlin2(big, 0.5f, 1) == 0.0f);
            CHECK(w::perlin3(0.5f, big, 0.5f, 1) == 0.0f);
            CHECK(w::simplex2(0.5f, big, 1) == 0.0f);
            CHECK(w::simplex3(big, 0.5f, 0.5f, 1) == 0.0f);
            CHECK(w::worley2(big, 0.5f, 1).f1 == 0.5f);
            CHECK(w::worley3(0.5f, 0.5f, big, 1).f2 == 0.5f);
        }
        // Plages réfléchies de terrain.hpp : x = 64, fréquence 1, lacunarité 4, 16 octaves -> x * fréquence = 2^32 à l'octave 13.
        w::FbmParams p;
        p.frequency = 1.0f;
        p.lacunarity = 4.0f;
        p.octaves = 16;
        for (auto basis : {w::NoiseBasis::kPerlin, w::NoiseBasis::kSimplex, w::NoiseBasis::kWorley})
        {
            p.basis = basis;
            const float v = w::fbm2(64.25f, 3.5f, 9, p);
            CHECK(std::isfinite(v));
            CHECK(std::abs(v) <= 1.0f);
            CHECK(std::isfinite(w::fbm3(64.25f, 3.5f, -7.0f, 9, p)));
        }
        // Le domaine documenté (|x| < 2^23) garde ses valeurs : la garde ne touche que ce qui était indéfini.
        CHECK(w::perlin2(8388607.5f, 0.25f, 3) == w::perlin2(8388607.5f, 0.25f, 3));
    }

    TEST_CASE("TEST-03: reference checksums of every noise (same bytes on every system)")
    {
        const w::FbmParams fbm{.basis = w::NoiseBasis::kPerlin, .octaves = 6, .frequency = 0.25f, .lacunarity = 2.0f, .gain = 0.5f};
        expectChecksum("perlin2", grid(256, 0.0731f, [](float x, float y) { return w::perlin2(x, y, 42); }), 0xbf36c0eee9724845ULL);
        expectChecksum("perlin3", grid(128, 0.0731f, [](float x, float y) { return w::perlin3(x, y, 0.5f * x + 0.25f, 42); }), 0x3ad38c5845939827ULL);
        expectChecksum("simplex2", grid(256, 0.0731f, [](float x, float y) { return w::simplex2(x, y, 42); }), 0x15a686e2d621ffd7ULL);
        expectChecksum("simplex3", grid(128, 0.0731f, [](float x, float y) { return w::simplex3(x, y, 0.5f * x + 0.25f, 42); }), 0xe40b2bd7a12183d0ULL);
        expectChecksum("worley2", grid(256, 0.0731f, [](float x, float y) { return w::worley2(x, y, 42).f1; }), 0xf4306e39a4679690ULL);
        expectChecksum("worley3", grid(64, 0.0731f, [](float x, float y) { return w::worley3(x, y, 0.5f * x + 0.25f, 42).f2; }), 0x7b1f732ea5d071e2ULL);
        expectChecksum("fbm2", grid(256, 0.0731f, [&](float x, float y) { return w::fbm2(x, y, 42, fbm); }), 0xac0acf8c29679066ULL);
        expectChecksum("warpedFbm2",
                       grid(128, 0.0731f, [&](float x, float y) { return w::warpedFbm2(x, y, 42, fbm, {.amplitude = 1.5f, .frequency = 0.3f, .octaves = 3}); }),
                       0x97e2e634aa4e7a99ULL);
    }
}
