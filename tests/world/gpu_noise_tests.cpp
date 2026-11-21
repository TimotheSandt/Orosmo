// TER-01 (GPU) : le bruit calculé en compute donne les mêmes hauteurs que le processeur, dans la tolérance fixée (kGpuTolerance).
// Mesuré sur Mesa (Intel Arc, 2026-11-19) : écart max 0,5 mm pour 150 m d'amplitude (tolérance : 15 mm), un tiers des
// échantillons identiques au bit près.
#include <doctest/doctest.h>

#include "support/test_support.hpp"
#include "rhi/gpu_fixture.hpp"

#include <orosmo/world/gpu_noise.hpp>
#include <orosmo/world/terrain.hpp>

#include <algorithm>
#include <cmath>

namespace w = orosmo::world;

TEST_SUITE("world/gpu")
{
    TEST_CASE("TER-01: GPU and CPU heights agree within the fixed tolerance, for every basis, with and without warping")
    {
        Gpu gpu;
        SKIP_TEST_IF_NULL(gpu.device.get(), "OpenGL 4.3 context");
        auto noise = w::GpuNoise::create(*gpu.device);
        INFO((noise ? std::string() : noise.error().message));
        REQUIRE(noise.has_value());
        struct Case
        {
            w::NoiseBasis basis;
            float warp;
            w::TileKey key;
        };
        for (const Case c : {Case{w::NoiseBasis::kPerlin, 0.0f, {0, 0, 0}}, Case{w::NoiseBasis::kPerlin, 40.0f, {-3, 2, 1}},
                             Case{w::NoiseBasis::kSimplex, 25.0f, {5, -7, 2}}, Case{w::NoiseBasis::kWorley, 0.0f, {1, 1, 0}}})
        {
            w::TerrainParams p;
            p.tileResolution = 32;
            p.sampleSpacing = 2.0f;
            p.heightScale = 150.0f;
            p.relief = {c.basis, 6, 1.0f / 200.0f, 2.0f, 0.5f};
            p.warp = {c.warp, 1.0f / 300.0f, 3};
            const w::Heightmap cpu = w::generateTile(p, 42, c.key);
            auto heights = (*noise)->generateTile(p, 42, c.key);
            REQUIRE(heights.has_value());
            REQUIRE(heights->size() == cpu.heights.size());
            float worst = 0.0f;
            std::size_t identical = 0;
            for (std::size_t i = 0; i < cpu.heights.size(); ++i)
            {
                worst = std::max(worst, std::fabs((*heights)[i] - cpu.heights[i]));
                identical += (*heights)[i] == cpu.heights[i] ? 1 : 0;
            }
            INFO("basis ", static_cast<int>(c.basis), " warp ", c.warp, " : écart max ", worst, " m, identiques ", identical, "/", cpu.heights.size());
            CHECK(worst <= w::kGpuTolerance * std::max(1.0f, p.heightScale));
            auto again = (*noise)->generateTile(p, 42, c.key);
            REQUIRE(again.has_value());
            CHECK(*again == *heights); // déterministe d'un appel à l'autre
        }
        gpu.expectNoValidationErrors();
    }

    TEST_CASE("TER-01: offscreen generations in series reuse one result buffer (no growth of the deferred-destruction queue)")
    {
        Gpu gpu;
        SKIP_TEST_IF_NULL(gpu.device.get(), "OpenGL 4.3 context");
        auto noise = w::GpuNoise::create(*gpu.device);
        REQUIRE(noise.has_value());
        w::TerrainParams p;
        p.tileResolution = 32;
        REQUIRE((*noise)->generateTile(p, 1, {0, 0, 0}).has_value());
        const auto before = gpu.device->stats();
        for (int i = 0; i < 20; ++i) // sans present() : rien ne vide la file de destruction différée
        {
            REQUIRE((*noise)->generateTile(p, 1, {i, 0, 0}).has_value());
        }
        const auto after = gpu.device->stats();
        CHECK(after.buffers == before.buffers);
        CHECK(after.bindGroups == before.bindGroups);
        CHECK(after.pendingDestruction == before.pendingDestruction);
        // Une tuile plus grande agrandit le tampon une fois, puis le réutilise.
        p.tileResolution = 64;
        REQUIRE((*noise)->generateTile(p, 1, {0, 0, 0}).has_value());
        REQUIRE((*noise)->generateTile(p, 1, {1, 0, 0}).has_value());
        CHECK(gpu.device->stats().buffers == before.buffers);
        // lod = 32 : 2^lod en double, pas de décalage d'entier indéfini ; mêmes hauteurs que le processeur.
        w::TerrainParams far;
        far.tileResolution = 4;
        far.sampleSpacing = 0.0625f;
        far.relief = {w::NoiseBasis::kPerlin, 1, 1.0f / 8192.0f, 2.0f, 0.5f};
        far.warp = {0.0f, 1.0f / 300.0f, 1};
        const w::TileKey key{0, 0, 32};
        const auto heights = (*noise)->generateTile(far, 5, key);
        REQUIRE(heights.has_value());
        const w::Heightmap cpu = w::generateTile(far, 5, key);
        for (std::size_t i = 0; i < cpu.heights.size(); ++i)
        {
            CHECK(std::fabs((*heights)[i] - cpu.heights[i]) <= w::kGpuTolerance * std::max(1.0f, far.heightScale));
        }
        gpu.expectNoValidationErrors();
    }
}
