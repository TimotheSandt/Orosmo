// TER-01 (GPU) : le bruit calculé en compute donne les mêmes hauteurs que le processeur, dans la tolérance fixée (kGpuTolerance).
// Mesuré sur Mesa (Intel Arc, 2026-11-19) : écart max 0,5 mm pour 150 m d'amplitude (tolérance : 15 mm), un tiers des
// échantillons identiques au bit près.
#include <doctest/doctest.h>

#include "support/test_support.hpp"
#include "rhi/gpu_fixture.hpp"

#include <engine/world/gpu_noise.hpp>
#include <engine/world/terrain.hpp>

#include <algorithm>
#include <cmath>

namespace w = engine::world;

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
}
