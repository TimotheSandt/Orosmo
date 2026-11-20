// TER-11 (export, chaque format relu), TER-13 (lancer de rayon cohérent avec le maillage et avec le rendu).
#include <doctest/doctest.h>

#include "support/temp_dir.hpp"
#include "support/test_support.hpp"
#include "renderer/repo_paths.hpp"
#include "rhi/gpu_fixture.hpp"

#include <orosmo/core/tasks.hpp>
#include <orosmo/renderer/light.hpp>
#include <orosmo/renderer/scene_renderer.hpp>
#include <orosmo/renderer/terrain_renderer.hpp>
#include <orosmo/world/checksum.hpp>
#include <orosmo/world/export.hpp>
#include <orosmo/world/lod.hpp>
#include <orosmo/world/raycast.hpp>

#include <nlohmann/json.hpp>
#include <stb_image.h>

#include <algorithm>
#include <clocale>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <string_view>

namespace w = orosmo::world;
namespace rd = orosmo::renderer;
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
        if (c >= 'A' && c <= 'Z')
        {
            return c - 'A';
        }
        if (c >= 'a' && c <= 'z')
        {
            return c - 'a' + 26;
        }
        if (c >= '0' && c <= '9')
        {
            return c - '0' + 52;
        }
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
        orosmo::core::TaskSystem tasks(3);
        CHECK(w::generateRegion(p, 9, -40, 7, 64, 48, &tasks).heights == w::generateRegion(p, 9, -40, 7, 64, 48).heights);

        // Dernier indice global hors de std::int64_t : région vide (rien d'alloué ni calculé), refusée par les exports.
        constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
        const w::HeightField overflow = w::generateRegion(p, 9, kMax - 1, 0, 3, 2, &tasks);
        CHECK(overflow.width == 0);
        CHECK(overflow.heights.empty());
        CHECK(w::generateRegion(p, 9, 0, kMax, 2, 2).heights.empty());
        CHECK(w::generateRegion(p, 9, kMax - 2, kMax - 1, 3, 2).heights.size() == 6); // dernier indice = kMax : accepté
        tests::TempDir dir("engine_export");
        CHECK((tests::errorOf(w::writeHeightExr(dir.path / "o.exr", overflow)) == orosmo::core::ErrorCode::kInvalidArgument));
    }

    TEST_CASE("PNG 16 bits: reopened by an independent decoder (stb), same quantized heights, same bytes at each export")
    {
        tests::TempDir dir("engine_export");
        const w::HeightField field = w::generateRegion(params(), 42, 0, 0, 33, 21);
        const float lo = field.minHeight(), hi = field.maxHeight();
        REQUIRE(w::writeHeightPng16(dir.path / "h.png", field, lo, hi).has_value());
        int width = 0, height = 0, channels = 0;
        const std::unique_ptr<stbi_us, void (*)(void *)> owned(stbi_load_16((dir.path / "h.png").string().c_str(), &width, &height, &channels, 1),
                                                                stbi_image_free);
        const stbi_us *pixels = owned.get();
        REQUIRE(pixels != nullptr);
        REQUIRE(width == 33); // la boucle lit 33 x 21 pixels : s'arrêter si l'image n'a pas cette taille
        REQUIRE(height == 21);
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
        CHECK((tests::errorOf(w::readHeightExr(dir.path / "absent.exr")) == orosmo::core::ErrorCode::kIo));
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
        CHECK((tests::errorOf(readBack(truncated)) == orosmo::core::ErrorCode::kParse));

        auto huge = valid; // fenêtre de 2^32 colonnes : aucune allocation, et pas de débordement de xMax - xMin + 1
        const std::size_t window = payloadOf("dataWindow") + 4;
        put32(huge, window, 0x80000000U);
        put32(huge, window + 8, 0x7fffffffU);
        CHECK((tests::errorOf(readBack(huge)) == orosmo::core::ErrorCode::kParse));

        auto outside = valid; // offset au-delà du fichier : refusé avant la lecture de la rangée
        put32(outside, table, 0xffffff00U);
        put32(outside, table + 4, 0xffffffffU);
        CHECK((tests::errorOf(readBack(outside)) == orosmo::core::ErrorCode::kParse));

        auto duplicate = valid; // deux entrées de la table vers la même rangée : l'autre rangée ne serait jamais remplie
        std::copy_n(valid.begin() + static_cast<std::ptrdiff_t>(table), 8, duplicate.begin() + static_cast<std::ptrdiff_t>(table + 8));
        CHECK((tests::errorOf(readBack(duplicate)) == orosmo::core::ErrorCode::kParse));

        // Erreur de lecture APRÈS l'ouverture (un dossier s'ouvre sous Linux, sa lecture échoue en EISDIR ; ailleurs l'ouverture
        // échoue) : kIo, pas d'exception.
        CHECK((tests::errorOf(w::readHeightExr(dir.path)) == orosmo::core::ErrorCode::kIo));
    }

    TEST_CASE("OBJ and glTF: reopened, vertices, normals and triangles match the height field")
    {
        tests::TempDir dir("engine_export");
        const w::HeightField field = w::generateRegion(params(), 5, 0, 0, 9, 7);
        REQUIRE(w::writeObj(dir.path / "t.obj", field).has_value());
        std::ifstream obj(dir.path / "t.obj");
        std::string line;
        std::vector<float> ys;
        std::vector<unsigned> faceIndices; // indices de sommet (base 1) de chaque face, dans l'ordre du fichier
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
            if (tag == "f")
            {
                ++faces;
                std::string corner;
                while (s >> corner)
                {
                    faceIndices.push_back(static_cast<unsigned>(std::stoul(corner.substr(0, corner.find('/')))));
                }
            }
        }
        REQUIRE(ys.size() == field.heights.size());
        for (std::size_t i = 0; i < ys.size(); ++i)
        {
            CHECK(ys[i] == doctest::Approx(field.heights[i]).epsilon(1e-5));
        }
        CHECK(normals == ys.size());
        CHECK(faces == 2u * 8u * 6u);
        // Chaque face désigne les bons sommets : deux triangles par cellule, anti-horaires vus du dessus.
        std::vector<unsigned> expected;
        for (unsigned j = 0; j + 1 < field.height; ++j)
        {
            for (unsigned i = 0; i + 1 < field.width; ++i)
            {
                const unsigned a = j * field.width + i + 1, b = a + 1, c = a + field.width, d = c + 1;
                expected.insert(expected.end(), {a, c, b, b, c, d});
            }
        }
        CHECK(faceIndices == expected);

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
        REQUIRE(buffer.size() >= (5 * 3 + 1) * 4 + 4);
        float y5 = 0;
        std::memcpy(&y5, buffer.data() + (5 * 3 + 1) * 4, 4); // POSITION du sommet 5, composante y
        CHECK(y5 == field.heights[5]);
        CHECK(doc["accessors"][0]["max"][1].get<float>() == doctest::Approx(field.maxHeight()));

        // Indices 32 bits (bufferViews[2]) : la même triangulation que l'OBJ, en base 0.
        auto floatAt = [&](std::size_t offset)
        {
            float v = 0;
            std::memcpy(&v, buffer.data() + offset, 4);
            return v;
        };
        const auto indicesView = doc["bufferViews"][2];
        const auto indicesOffset = indicesView["byteOffset"].get<std::size_t>();
        REQUIRE(indicesView["byteLength"].get<std::size_t>() == expected.size() * 4);
        REQUIRE(indicesOffset + expected.size() * 4 <= buffer.size());
        std::vector<unsigned> gltfIndices;
        for (std::size_t k = 0; k < expected.size(); ++k)
        {
            std::uint32_t v = 0;
            for (std::size_t b = 0; b < 4; ++b) // petit-boutiste (glTF)
                v |= static_cast<std::uint32_t>(buffer[indicesOffset + k * 4 + b]) << (8 * b);
            gltfIndices.push_back(v + 1);
        }
        CHECK(gltfIndices == expected);
        // NORMAL (bufferViews[1]) : différences centrées (décentrées au bord) recalculées ici en double, puis normalisées.
        const auto normalsOffset = doc["bufferViews"][1]["byteOffset"].get<std::size_t>();
        REQUIRE(doc["bufferViews"][1]["byteLength"].get<std::size_t>() == field.heights.size() * 12);
        for (std::uint32_t j = 0; j < field.height; ++j)
        {
            for (std::uint32_t i = 0; i < field.width; ++i)
            {
                const std::uint32_t il = i > 0 ? i - 1 : i, ir = std::min(i + 1, field.width - 1);
                const std::uint32_t jd = j > 0 ? j - 1 : j, ju = std::min(j + 1, field.height - 1);
                const double sx = -(static_cast<double>(field.at(ir, j)) - field.at(il, j)) / ((ir - il) * static_cast<double>(field.spacing));
                const double sz = -(static_cast<double>(field.at(i, ju)) - field.at(i, jd)) / ((ju - jd) * static_cast<double>(field.spacing));
                const double len = std::sqrt(sx * sx + 1.0 + sz * sz);
                const std::size_t at = normalsOffset + (static_cast<std::size_t>(j) * field.width + i) * 12;
                CHECK(floatAt(at) == doctest::Approx(sx / len).epsilon(1e-5));
                CHECK(floatAt(at + 4) == doctest::Approx(1.0 / len).epsilon(1e-5));
                CHECK(floatAt(at + 8) == doctest::Approx(sz / len).epsilon(1e-5));
            }
        }

        // Bornes de POSITION : des valeurs réellement présentes, même au-delà de l'ancienne valeur initiale (1e30).
        w::HeightField high;
        high.width = 2;
        high.height = 2;
        high.heights = {2e30f, 3e30f, 4e30f, 5e30f};
        REQUIRE(w::writeGltf(dir.path / "high.gltf", high).has_value());
        std::ifstream highIn(dir.path / "high.gltf");
        const nlohmann::json highDoc = nlohmann::json::parse(highIn);
        CHECK(highDoc["accessors"][0]["min"][1].get<float>() == 2e30f);
        CHECK(highDoc["accessors"][0]["max"][1].get<float>() == 5e30f);
        CHECK(highDoc["accessors"][0]["min"][0].get<float>() == 0.0f);
        CHECK(highDoc["accessors"][0]["max"][0].get<float>() == 1.0f);
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
        // Socle qui ne passe pas sous la surface : pas un solide, refusé.
        CHECK((tests::errorOf(w::writeStl(dir.path / "flat.stl", field, 0.0f)) == orosmo::core::ErrorCode::kInvalidArgument));
        CHECK((tests::errorOf(w::writeStl(dir.path / "nan.stl", field, std::nanf(""))) == orosmo::core::ErrorCode::kInvalidArgument));
    }

    TEST_CASE("exported positions come from the global sample index, like the heights; OBJ numbers ignore the C locale")
    {
        w::TerrainParams p = params();
        p.sampleSpacing = 0.125f;
        const w::HeightField field = w::generateRegion(p, 1, 33554433, 0, 3, 2);
        // originX (float) + 2 * 0.125 = 4194304 ; l'échantillon global 33554435 est en 4194304.5 (float), là où sa hauteur est prise.
        CHECK(field.xAt(2) == static_cast<float>(33554435.0 * 0.125));
        CHECK(field.xAt(2) == 4194304.5f);
        tests::TempDir dir("engine_export");
        const w::HeightField small = w::generateRegion(params(), 5, 0, 0, 3, 3);
        REQUIRE(w::writeObj(dir.path / "c.obj", small).has_value());
        const char *previous = std::setlocale(LC_NUMERIC, nullptr);
        const std::string saved = previous != nullptr ? previous : "C";
        bool comma = false;
        for (const char *name : {"fr_FR.UTF-8", "de_DE.UTF-8", "fr_FR.utf8", "de_DE.utf8"})
        {
            if (std::setlocale(LC_NUMERIC, name) != nullptr)
            {
                comma = true;
                break;
            }
        }
        if (!comma)
        {
            MESSAGE("no locale with a decimal comma on this system: only the C locale output is checked");
        }
        const auto written = w::writeObj(dir.path / "l.obj", small);
        std::setlocale(LC_NUMERIC, saved.c_str());
        REQUIRE(written.has_value());
        CHECK(readBytes(dir.path / "l.obj") == readBytes(dir.path / "c.obj"));
        // Grille dont le stockage ne correspond pas à la forme annoncée : biomeMap ne lit pas hors du vecteur.
        w::HeightField bad;
        bad.width = 2;
        bad.height = 2;
        bad.heights = {1.0f};
        CHECK(w::biomeMap(bad, {}).empty());
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
        // /dev/full s'ouvre, mais l'écriture échoue (ENOSPC) : l'erreur est propagée (stbi_write_png l'ignorait).
        if (std::error_code ec; fs::exists("/dev/full", ec))
        {
            CHECK((tests::errorOf(w::writeBiomePng("/dev/full", field, b)) == orosmo::core::ErrorCode::kIo));
        }
    }

    TEST_CASE("TEST-03: reference checksum of an exported region")
    {
        // Valeur d'or fixée à l'écriture de TER-11 (M3, lot B) par cette même fonction sur Linux x64, puis retrouvée à l'identique
        // par la CI Linux arm64 et Windows (MinGW). Elle couvre generateRegion (graine 42, 64 x 64, params() ci-dessus) : la
        // changer n'est permis que pour une modification VOLONTAIRE de l'algorithme de bruit ou de génération (voir noise_tests).
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
        const auto below = w::raycastTerrain(p, 11, {x, -500.0f, z}, {1.0f, 0.0f, 0.0f}, 10.0f); // départ sous la surface
        REQUIRE(below.has_value());
        CHECK(below->distance == 0.0f);
        for (int k = 0; k < 20; ++k) // rayons obliques : le point touché est sur la surface
        {
            const orosmo::core::Vec3 o{static_cast<float>(k) * 3.1f, 120.0f, -static_cast<float>(k) * 1.7f};
            auto h = w::raycastTerrain(p, 11, o, {0.4f, -1.0f, 0.3f}, 1000.0f);
            REQUIRE(h.has_value());
            CHECK(h->position.y == doctest::Approx(w::meshHeightAt(p, 11, h->position.x, h->position.z)).epsilon(1e-3));
        }
        // maxDistance infinie : la borne du rayon vaut DBL_MAX, (b - a) * fa débordait (impact à l'infini).
        w::TerrainParams flat = p;
        flat.heightScale = 0.0f;
        flat.heightOffset = 0.0f;
        const auto unbounded = w::raycastTerrain(flat, 1, {0.5f, 2.0f, 0.5f}, {0.0f, -1.0f, 0.0f}, std::numeric_limits<float>::infinity());
        REQUIRE(unbounded.has_value());
        CHECK(unbounded->distance == doctest::Approx(2.0f));
        CHECK(unbounded->position.y == doctest::Approx(0.0f));
    }
}

TEST_SUITE("world/render")
{
    TEST_CASE("TER-13: the FIRST crossing along the ray, even over a crest thinner than the old fixed step; degenerate inputs")
    {
        w::TerrainParams p = params();
        p.sampleSpacing = 1.0f;
        p.heightScale = 1.0f;
        p.relief.frequency = 0.5f;
        p.relief.octaves = 1;
        p.warp.amplitude = 0.0f;
        // Rayons presque horizontaux, au ras de la surface : aucune traversée ne doit précéder le point rendu (contrôle brutal par
        // 4000 échantillons le long du rayon), et un rayon manqué ne doit traverser nulle part.
        int hits = 0;
        for (int k = 0; k < 200; ++k)
        {
            const float x = -20.0f + 0.37f * static_cast<float>(k), z = 3.0f - 0.11f * static_cast<float>(k);
            const orosmo::core::Vec3 o{x, w::meshHeightAt(p, 7, x, z) + 0.05f, z};
            const orosmo::core::Vec3 d{std::cos(0.7f * static_cast<float>(k)), -0.01f, std::sin(0.7f * static_cast<float>(k))};
            const float range = 6.0f;
            const auto h = w::raycastTerrain(p, 7, o, d, range);
            const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            const float end = h ? h->distance : range;
            bool crossedBefore = false;
            for (int i = 1; i < 4000; ++i)
            {
                const float t = end * static_cast<float>(i) / 4000.0f;
                const orosmo::core::Vec3 q = o + d * (t / len);
                crossedBefore = crossedBefore || q.y < w::meshHeightAt(p, 7, q.x, q.z) - 1e-4f;
            }
            CHECK_FALSE(crossedBefore);
            if (h)
            {
                ++hits;
                CHECK(h->position.y == doctest::Approx(w::meshHeightAt(p, 7, h->position.x, h->position.z)).epsilon(1e-4));
            }
        }
        CHECK(hits > 20);

        // Progression garantie : à 262144 m, un pas de 0.015625 m ne changeait plus t en float (boucle sans fin).
        w::TerrainParams fine = params();
        fine.sampleSpacing = 0.0625f;
        const auto down = w::raycastTerrain(fine, 3, {0.0f, 300000.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 400000.0f);
        REQUIRE(down.has_value());
        CHECK(down->position.y == doctest::Approx(w::meshHeightAt(fine, 3, 0.0f, 0.0f)).epsilon(1e-4));
        // Cellule dont les deux bords tombent sur le même float : hauteur finie.
        CHECK(std::isfinite(w::meshHeightAt(fine, 3, 1048576.0f, 0.0f)));

        // Direction finie dont le carré de la norme déborde en float.
        w::TerrainParams flat = params();
        flat.heightScale = 0.0f;
        flat.heightOffset = 0.0f;
        const auto huge = w::raycastTerrain(flat, 1, {0.0f, 1.0f, 0.0f}, {0.0f, -1e20f, 0.0f}, 10.0f);
        REQUIRE(huge.has_value());
        CHECK(huge->distance == doctest::Approx(1.0f));
        // Espacement invalide (appel direct, hors des bornes réfléchies) : pas de conversion indéfinie.
        w::TerrainParams zero = params();
        zero.sampleSpacing = 0.0f;
        CHECK(std::isnan(w::meshHeightAt(zero, 1, 1.0f, 1.0f)));
        CHECK_FALSE(w::raycastTerrain(zero, 1, {0.0f, 10.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 100.0f).has_value());
    }

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
