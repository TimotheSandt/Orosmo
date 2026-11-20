// TER-11 (export, chaque format relu), TER-13 (lancer de rayon cohérent avec le maillage et avec le rendu).
#include <doctest/doctest.h>

#include "support/temp_dir.hpp"
#include "support/test_support.hpp"
#include "renderer/repo_paths.hpp"
#include "rhi/gpu_fixture.hpp"

#include <engine/core/tasks.hpp>
#include <engine/renderer/light.hpp>
#include <engine/renderer/scene_renderer.hpp>
#include <engine/renderer/terrain_renderer.hpp>
#include <engine/world/checksum.hpp>
#include <engine/world/export.hpp>
#include <engine/world/lod.hpp>
#include <engine/world/raycast.hpp>

#include <nlohmann/json.hpp>
#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string_view>

namespace w = engine::world;
namespace rd = engine::renderer;
namespace fs = std::filesystem;

namespace
{

w::TerrainParams params()
{
    w::TerrainParams p;
    p.tileResolution = 16;
    p.sampleSpacing = 2.0f;
    p.heightScale = 60.0f;
    p.relief.frequency = 1.0f / 128.0f;
    p.warp = {20.0f, 1.0f / 200.0f, 2};
    return p;
}

std::vector<std::uint8_t> readBytes(const fs::path &path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::vector<std::uint8_t> decodeBase64(const std::string &text)
{
    auto value = [](char c) -> int
    {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        return c == '+' ? 62 : (c == '/' ? 63 : -1);
    };
    std::vector<std::uint8_t> out;
    std::uint32_t buffer = 0;
    int bits = 0;
    for (char c : text)
    {
        const int v = value(c);
        if (v < 0)
            continue;
        buffer = (buffer << 6) | static_cast<std::uint32_t>(v);
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>(buffer >> bits));
        }
    }
    return out;
}

} // namespace

TEST_SUITE("world/export")
{
    TEST_CASE("generateRegion samples the same positions as the tiles; threads do not change the result")
    {
        const w::TerrainParams p = params();
        const w::HeightField field = w::generateRegion(p, 9, 16, 32, 17, 17);
        const w::Heightmap tile = w::generateTile(p, 9, {1, 2, 0});
        CHECK(field.heights == tile.heights);
        CHECK(field.originX == 32.0f);
        CHECK(field.originZ == 64.0f);
        engine::core::TaskSystem tasks(3);
        CHECK(w::generateRegion(p, 9, -40, 7, 64, 48, &tasks).heights == w::generateRegion(p, 9, -40, 7, 64, 48).heights);
    }

    TEST_CASE("PNG 16 bits: reopened by an independent decoder (stb), same quantized heights, same bytes at each export")
    {
        tests::TempDir dir("engine_export");
        const w::HeightField field = w::generateRegion(params(), 42, 0, 0, 33, 21);
        const float lo = field.minHeight(), hi = field.maxHeight();
        REQUIRE(w::writeHeightPng16(dir.path / "h.png", field, lo, hi).has_value());
        int width = 0, height = 0, channels = 0;
        stbi_us *pixels = stbi_load_16((dir.path / "h.png").string().c_str(), &width, &height, &channels, 1);
        REQUIRE(pixels != nullptr);
        CHECK(width == 33);
        CHECK(height == 21);
        CHECK(channels == 1);
        bool sawMin = false, sawMax = false;
        for (std::uint32_t j = 0; j < field.height; ++j)
        {
            for (std::uint32_t i = 0; i < field.width; ++i)
            {
                const std::uint16_t v = pixels[j * field.width + i];
                const double expected = (static_cast<double>(field.at(i, j)) - lo) * 65535.0 / (static_cast<double>(hi) - lo);
                CHECK(std::abs(static_cast<double>(v) - expected) <= 0.5 + 1e-6);
                sawMin = sawMin || v == 0;
                sawMax = sawMax || v == 65535;
            }
        }
        stbi_image_free(pixels);
        CHECK(sawMin);
        CHECK(sawMax);
        REQUIRE(w::writeHeightPng16(dir.path / "again.png", field, lo, hi).has_value());
        CHECK(readBytes(dir.path / "h.png") == readBytes(dir.path / "again.png"));
        CHECK_FALSE(w::writeHeightPng16(dir.path / "bad.png", field, 1.0f, 1.0f).has_value());
    }

    TEST_CASE("EXR float: reopened, the heights are exactly the generated ones")
    {
        tests::TempDir dir("engine_export");
        const w::HeightField field = w::generateRegion(params(), 3, -10, 4, 40, 25);
        REQUIRE(w::writeHeightExr(dir.path / "h.exr", field).has_value());
        auto back = w::readHeightExr(dir.path / "h.exr");
        REQUIRE(back.has_value());
        CHECK(back->width == 40);
        CHECK(back->height == 25);
        CHECK(back->heights == field.heights);
        const auto bytes = readBytes(dir.path / "h.exr");
        REQUIRE(bytes.size() > 8);
        CHECK(bytes[0] == 0x76);
        CHECK(bytes[1] == 0x2f);
        CHECK(bytes[2] == 0x31);
        CHECK(bytes[3] == 0x01);
        CHECK((tests::errorOf(w::readHeightExr(dir.path / "absent.exr")) == engine::core::ErrorCode::kIo));
    }

    TEST_CASE("EXR from an untrusted file: truncated attributes, oversized windows, bad offsets and duplicate rows are errors")
    {
        tests::TempDir dir("engine_export");
        const w::HeightField field = w::generateRegion(params(), 3, 0, 0, 3, 2);
        REQUIRE(w::writeHeightExr(dir.path / "h.exr", field).has_value());
        const auto valid = readBytes(dir.path / "h.exr");
        auto payloadOf = [&](std::string_view name) // position de la taille de l'attribut (après « nom\0type\0 »)
        {
            const auto it = std::search(valid.begin(), valid.end(), name.begin(), name.end());
            REQUIRE(it != valid.end());
            auto p = static_cast<std::size_t>(it - valid.begin()) + name.size() + 1;
            while (valid[p] != 0)
                ++p;
            return p + 1;
        };
        auto put32 = [](std::vector<std::uint8_t> &b, std::size_t at, std::uint32_t v)
        {
            for (int i = 0; i < 4; ++i)
                b[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(v >> (8 * i));
        };
        auto readBack = [&](const std::vector<std::uint8_t> &bytes)
        {
            const fs::path path = dir.path / "bad.exr";
            std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            return w::readHeightExr(path);
        };
        REQUIRE(readBack(valid).has_value());
        const std::size_t table = valid.size() - 2 * (8 + 8 + 3 * 4); // en-tête, table (8 octets par rangée), rangées

        auto truncated = valid; // attribut « channels » réduit à 2 octets : le nom du canal n'y est plus terminé
        put32(truncated, payloadOf("channels"), 2);
        CHECK((tests::errorOf(readBack(truncated)) == engine::core::ErrorCode::kParse));

        auto huge = valid; // fenêtre de 2^32 colonnes : aucune allocation, et pas de débordement de xMax - xMin + 1
        const std::size_t window = payloadOf("dataWindow") + 4;
        put32(huge, window, 0x80000000U);
        put32(huge, window + 8, 0x7fffffffU);
        CHECK((tests::errorOf(readBack(huge)) == engine::core::ErrorCode::kParse));

        auto outside = valid; // offset au-delà du fichier : refusé avant la lecture de la rangée
        put32(outside, table, 0xffffff00U);
        put32(outside, table + 4, 0xffffffffU);
        CHECK((tests::errorOf(readBack(outside)) == engine::core::ErrorCode::kParse));

        auto duplicate = valid; // deux entrées de la table vers la même rangée : l'autre rangée ne serait jamais remplie
        std::copy_n(valid.begin() + static_cast<std::ptrdiff_t>(table), 8, duplicate.begin() + static_cast<std::ptrdiff_t>(table + 8));
        CHECK((tests::errorOf(readBack(duplicate)) == engine::core::ErrorCode::kParse));
    }

    TEST_CASE("OBJ and glTF: reopened, vertices, normals and triangles match the height field")
    {
        tests::TempDir dir("engine_export");
        const w::HeightField field = w::generateRegion(params(), 5, 0, 0, 9, 7);
        REQUIRE(w::writeObj(dir.path / "t.obj", field).has_value());
        std::ifstream obj(dir.path / "t.obj");
        std::string line;
        std::vector<float> ys;
        std::size_t normals = 0, faces = 0;
        while (std::getline(obj, line))
        {
            std::istringstream s(line);
            std::string tag;
            s >> tag;
            if (tag == "v")
            {
                float x = 0, y = 0, z = 0;
                s >> x >> y >> z;
                ys.push_back(y);
            }
            normals += tag == "vn" ? 1 : 0;
            faces += tag == "f" ? 1 : 0;
        }
        REQUIRE(ys.size() == field.heights.size());
        for (std::size_t i = 0; i < ys.size(); ++i)
        {
            CHECK(ys[i] == doctest::Approx(field.heights[i]).epsilon(1e-5));
        }
        CHECK(normals == ys.size());
        CHECK(faces == 2u * 8u * 6u);

        REQUIRE(w::writeGltf(dir.path / "t.gltf", field).has_value());
        std::ifstream in(dir.path / "t.gltf");
        const nlohmann::json doc = nlohmann::json::parse(in);
        CHECK(doc["asset"]["version"] == "2.0");
        const std::string uri = doc["buffers"][0]["uri"];
        const std::string prefix = "data:application/octet-stream;base64,";
        REQUIRE(uri.rfind(prefix, 0) == 0);
        const auto buffer = decodeBase64(uri.substr(prefix.size()));
        CHECK(buffer.size() == doc["buffers"][0]["byteLength"].get<std::size_t>());
        CHECK(doc["accessors"][0]["count"] == field.heights.size());
        CHECK(doc["accessors"][2]["count"] == 2u * 8u * 6u * 3u);
        float y5 = 0;
        std::memcpy(&y5, buffer.data() + (5 * 3 + 1) * 4, 4); // POSITION du sommet 5, composante y
        CHECK(y5 == field.heights[5]);
        CHECK(doc["accessors"][0]["max"][1].get<float>() == doctest::Approx(field.maxHeight()));
    }

    TEST_CASE("STL: a closed, consistently oriented solid (every edge shared by two triangles in opposite directions), positive volume")
    {
        tests::TempDir dir("engine_export");
        const w::HeightField field = w::generateRegion(params(), 8, 0, 0, 12, 9);
        REQUIRE(w::writeStl(dir.path / "t.stl", field, 5.0f).has_value());
        const auto bytes = readBytes(dir.path / "t.stl");
        REQUIRE(bytes.size() >= 84);
        std::uint32_t count = 0;
        std::memcpy(&count, bytes.data() + 80, 4);
        CHECK(count == 4u * 11u * 8u + 4u * (11u + 8u)); // surface + fond + jupes
        REQUIRE(bytes.size() == 84u + 50u * count);
        using V = std::array<float, 3>;
        std::map<std::pair<V, V>, int> directed;
        double volume = 0.0;
        for (std::uint32_t t = 0; t < count; ++t)
        {
            V p[3];
            std::memcpy(p, bytes.data() + 84 + 50 * t + 12, 36);
            for (int e = 0; e < 3; ++e)
            {
                ++directed[{p[e], p[(e + 1) % 3]}];
            }
            volume += (p[0][0] * (p[1][1] * p[2][2] - p[1][2] * p[2][1]) - p[0][1] * (p[1][0] * p[2][2] - p[1][2] * p[2][0]) +
                       p[0][2] * (p[1][0] * p[2][1] - p[1][1] * p[2][0])) / 6.0;
        }
        bool closed = true;
        for (const auto &[edge, n] : directed)
        {
            closed = closed && n == 1 && directed.contains({edge.second, edge.first});
        }
        CHECK(closed);
        CHECK(volume > 0.0); // normales vers l'extérieur
    }

    TEST_CASE("biomes: classification by height and slope, and a reopened RGB map")
    {
        const w::BiomeParams b{.seaLevel = 0.0f, .sandHeight = 2.0f, .snowHeight = 50.0f, .rockSlope = 0.4f};
        CHECK((w::classify(-1.0f, 0.0f, b) == w::Biome::kWater));
        CHECK((w::classify(1.0f, 0.0f, b) == w::Biome::kSand));
        CHECK((w::classify(10.0f, 0.0f, b) == w::Biome::kGrass));
        CHECK((w::classify(10.0f, 0.6f, b) == w::Biome::kRock));
        CHECK((w::classify(80.0f, 0.1f, b) == w::Biome::kSnow));
        tests::TempDir dir("engine_export");
        const w::HeightField field = w::generateRegion(params(), 4, 0, 0, 20, 20);
        REQUIRE(w::writeBiomePng(dir.path / "b.png", field, b).has_value());
        int width = 0, height = 0, channels = 0;
        unsigned char *rgb = stbi_load((dir.path / "b.png").string().c_str(), &width, &height, &channels, 3);
        REQUIRE(rgb != nullptr);
        CHECK(width == 20);
        const auto map = w::biomeMap(field, b);
        const bool water = map[0] == w::Biome::kWater;
        CHECK((rgb[2] > rgb[0]) == water); // eau : bleu dominant
        stbi_image_free(rgb);
    }

    TEST_CASE("TEST-03: reference checksum of an exported region")
    {
        const w::HeightField field = w::generateRegion(params(), 42, 0, 0, 64, 64);
        const std::uint64_t sum = w::checksum64(field.heights);
        if (sum != 0x535edcc509c49bd1ULL)
        {
            std::printf("checksum region = 0x%016llxULL\n", static_cast<unsigned long long>(sum));
        }
        CHECK(sum == 0x535edcc509c49bd1ULL);
    }
}

TEST_SUITE("world/raycast")
{
    TEST_CASE("TER-13: the mesh height matches the samples; a vertical ray hits it; an upward ray misses")
    {
        const w::TerrainParams p = params();
        const w::Heightmap tile = w::generateTile(p, 11, {0, 0, 0});
        for (std::uint32_t j = 0; j <= 16; j += 5)
        {
            for (std::uint32_t i = 0; i <= 16; i += 3)
            {
                CHECK(w::meshHeightAt(p, 11, w::sampleX(p, tile.key, i), w::sampleZ(p, tile.key, j)) == doctest::Approx(tile.at(i, j)).epsilon(1e-6));
            }
        }
        const float x = 7.3f, z = 13.9f;
        auto hit = w::raycastTerrain(p, 11, {x, 500.0f, z}, {0.0f, -1.0f, 0.0f}, 2000.0f);
        REQUIRE(hit.has_value());
        CHECK(hit->position.y == doctest::Approx(w::meshHeightAt(p, 11, x, z)).epsilon(1e-4));
        CHECK_FALSE(w::raycastTerrain(p, 11, {x, 500.0f, z}, {0.0f, 1.0f, 0.0f}, 2000.0f).has_value());
        CHECK(w::raycastTerrain(p, 11, {x, -500.0f, z}, {1.0f, 0.0f, 0.0f}, 10.0f)->distance == 0.0f); // départ sous la surface
        for (int k = 0; k < 20; ++k) // rayons obliques : le point touché est sur la surface
        {
            const engine::core::Vec3 o{static_cast<float>(k) * 3.1f, 120.0f, -static_cast<float>(k) * 1.7f};
            auto h = w::raycastTerrain(p, 11, o, {0.4f, -1.0f, 0.3f}, 1000.0f);
            REQUIRE(h.has_value());
            CHECK(h->position.y == doctest::Approx(w::meshHeightAt(p, 11, h->position.x, h->position.z)).epsilon(1e-3));
        }
    }
}

TEST_SUITE("world/render")
{
    TEST_CASE("TER-13: consistent with rendering: the rendered depth at the image centre gives the same point as the ray")
    {
        Gpu gpu;
        SKIP_TEST_IF_NULL(gpu.device.get(), "OpenGL 4.3 context");
        const w::TerrainParams p = params();
        const w::TileKey key{0, 0, 0};
        const w::TileMesh mesh = w::buildTileMesh(p, 11, w::generateTile(p, 11, key), {});
        std::vector<float> local = mesh.vertices;
        rd::ShaderLibrary library(*gpu.device);
        rd::PipelineCache pipelines(*gpu.device);
        const fs::path shaders = tests::repoRoot(__FILE__) / "shaders" / "rhi";
        auto scene = tests::valueOf(rd::SceneRenderer::create(*gpu.device, library, pipelines, {.shaderDirectory = shaders}));
        auto terrain = tests::valueOf(rd::TerrainRenderer::create(*gpu.device, library, shaders));
        REQUIRE(terrain->updateTile({.id = 1, .origin = {0, 0, 0}, .vertices = local, .indices = mesh.indices}).has_value());
        auto lights = tests::valueOf(rd::LightManager::create(*gpu.device));
        REQUIRE(lights.upload().has_value());
        scene->setLights(&lights);
        scene->setDepthAsTextureEnabled(true);
        scene->setOutputResolution(33, 33);
        rd::Camera camera;
        camera.setAspect(33, 33);
        camera.setNearPlane(0.5f);
        camera.setPosition({16.0f, 90.0f, 40.0f});
        camera.setOrientation(glm::normalize(glm::vec3(0.0f, -90.0f, -24.0f)));
        const TextureHandle out = gpu.target(Format::kRGBA8Unorm, {33, 33}, "out");
        gpu.run(
            [&](CommandEncoder &e)
            {
                REQUIRE(scene->beginFrame(e).has_value());
                REQUIRE(scene->setCamera(camera).has_value());
                REQUIRE(terrain->draw(*scene, camera).has_value());
                REQUIRE(scene->endFrame({out, Format::kRGBA8Unorm, {33, 33}}).has_value());
            });
        // Profondeur rendue au pixel central (16, 16), reprojetée dans le monde.
        auto depthBytes = gpu->readTexture(scene->sceneTarget().depth(), {.offset = {16, 16, 0}, .size = {1, 1, 1}});
        REQUIRE(depthBytes.has_value());
        float depth = 0.0f;
        std::memcpy(&depth, depthBytes->data(), 4);
        REQUIRE(depth > 0.0f); // le terrain est touché (reverse-Z : 0 = rien)
        const glm::mat4 inverse = glm::inverse(camera.matrix());
        const glm::vec4 world = inverse * glm::vec4(0.0f, 0.0f, depth, 1.0f); // centre de l'image : NDC (0, 0)
        const glm::vec3 rendered = glm::vec3(world) / world.w;
        auto hit = w::raycastTerrain(p, 11, camera.position(), camera.orientation(), 500.0f);
        REQUIRE(hit.has_value());
        INFO("rendu ", rendered.x, ",", rendered.y, ",", rendered.z, " rayon ", hit->position.x, ",", hit->position.y, ",", hit->position.z);
        CHECK(glm::length(rendered - hit->position) < 0.05f);
        gpu.expectNoValidationErrors();
    }
}
