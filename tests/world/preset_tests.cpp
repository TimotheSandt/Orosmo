// TER-12 : les préréglages livrés avec le visualiseur (apps/terrain_viewer/presets/*.json) sont des documents TerrainParams valides
// (réflexion et sérialisation CORE-12, plages vérifiées), distincts, et un préréglage enregistré se relit à l'identique.
#include <doctest/doctest.h>

#include "renderer/repo_paths.hpp"
#include "support/temp_dir.hpp"

#include <engine/core/serialization.hpp>
#include <engine/world/terrain.hpp>

#include <fstream>
#include <sstream>

namespace w = engine::world;

TEST_SUITE("world/presets")
{
    TEST_CASE("the shipped presets load, stay in range and differ from each other")
    {
        const auto dir = tests::repoRoot(__FILE__) / "apps" / "terrain_viewer" / "presets";
        std::vector<w::TerrainParams> all;
        for (const char *name : {"hills", "mountains", "islands", "dunes"})
        {
            CAPTURE(name);
            std::ifstream in(dir / (std::string(name) + ".json"), std::ios::binary);
            REQUIRE(in.good());
            std::ostringstream text;
            text << in.rdbuf();
            auto json = engine::core::parseJson(text.str());
            REQUIRE(json.has_value());
            w::TerrainParams p;
            auto loaded = engine::core::loadDocument(p, *json);
            INFO((loaded ? std::string() : loaded.error().message));
            REQUIRE(loaded.has_value());
            all.push_back(p);
        }
        for (std::size_t i = 0; i < all.size(); ++i)
            for (std::size_t j = i + 1; j < all.size(); ++j)
                CHECK(engine::core::saveDocument(all[i]) != engine::core::saveDocument(all[j]));
        CHECK(all[1].relief.basis == w::NoiseBasis::kSimplex);
        CHECK(all[3].relief.basis == w::NoiseBasis::kWorley);
    }

    TEST_CASE("a saved preset reads back identically")
    {
        tests::TempDir dir("presets");
        w::TerrainParams p;
        p.relief.octaves = 9;
        p.heightScale = 333.5f;
        p.warp.amplitude = 12.25f;
        dir.write("custom.json", engine::core::saveDocument(p).dump(2));
        std::ifstream in(dir.path / "custom.json", std::ios::binary);
        std::ostringstream text;
        text << in.rdbuf();
        w::TerrainParams back;
        REQUIRE(engine::core::loadDocument(back, *engine::core::parseJson(text.str())).has_value());
        CHECK(engine::core::saveDocument(back) == engine::core::saveDocument(p));
        CHECK(w::heightAt(back, 3, 10.0f, 20.0f) == w::heightAt(p, 3, 10.0f, 20.0f));
    }
}
