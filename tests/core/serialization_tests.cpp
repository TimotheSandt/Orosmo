#include <doctest/doctest.h>

#include <engine/core/serialization.hpp>

#include <string>
#include <vector>

using namespace engine::core;

namespace
{
enum class Mode : int
{
    kFast = 0,
    kPretty = 2
};

struct Layer
{
    std::string name = "base";
    float weight = 1.0f;
};

struct TerrainParams
{
    float frequency = 1.0f;
    int octaves = 4;
    double seed = 0.0;
    bool wireframe = false;
    std::string label = "terrain";
    Vec3 origin{0.f, 0.f, 0.f};
    Mode mode = Mode::kFast;
    std::vector<Layer> layers;
    std::uint32_t resolution = 128;
};
} // namespace

template <> struct engine::core::Reflect<Layer>
{
    static constexpr auto fields()
    {
        return std::make_tuple(field("name", &Layer::name), field("weight", &Layer::weight, range(0.0, 1.0)));
    }
    static constexpr int kVersion = 1;
};

template <> struct engine::core::Reflect<TerrainParams>
{
    static constexpr int kVersion = 2;
    static constexpr auto fields()
    {
        return std::make_tuple(field("frequency", &TerrainParams::frequency, range(0.0, 16.0), "base noise frequency"),
                               field("octaves", &TerrainParams::octaves, range(1, 16)),
                               field("seed", &TerrainParams::seed), field("wireframe", &TerrainParams::wireframe),
                               field("label", &TerrainParams::label), field("origin", &TerrainParams::origin),
                               field("mode", &TerrainParams::mode), field("layers", &TerrainParams::layers),
                               field("resolution", &TerrainParams::resolution));
    }
};

TEST_SUITE("core/serialization")
{
    TEST_CASE("reflection describes names, types, ranges")
    {
        auto d = describeFields<TerrainParams>();
        REQUIRE(d.size() == 9);
        CHECK(d[0].name == "frequency");
        CHECK((d[0].type == FieldType::kFloat));
        CHECK(d[0].range.bounded);
        CHECK(d[0].range.max == 16.0);
        CHECK(d[0].description == "base noise frequency");
        CHECK((d[1].type == FieldType::kInt));
        CHECK((d[4].type == FieldType::kString));
        CHECK((d[5].type == FieldType::kVec3));
        CHECK((d[6].type == FieldType::kEnum));
        CHECK((d[7].type == FieldType::kArray));
        CHECK((d[8].type == FieldType::kUInt));
    }

    TEST_CASE("forEachField visits and mutates the fields")
    {
        TerrainParams p;
        int count = 0;
        forEachField(p, [&](const char *name, auto &value, const Range &) {
            ++count;
            if (std::string(name) == "octaves")
            {
                if constexpr (std::is_same_v<std::remove_cvref_t<decltype(value)>, int>)
                    value = 9;
            }
        });
        CHECK(count == 9);
        CHECK(p.octaves == 9);
    }

    TEST_CASE("JSON round trip is exact")
    {
        TerrainParams a;
        a.frequency = 2.5f;
        a.octaves = 7;
        a.seed = 123456789012.0;
        a.wireframe = true;
        a.label = "ünï";
        a.origin = Vec3(1.f, -2.f, 3.5f);
        a.mode = Mode::kPretty;
        a.layers = {{"rock", 0.25f}, {"sand", 1.0f}};
        a.resolution = 4096;
        const std::string text = dumpJson(saveDocument(a));
        auto parsed = parseJson(text);
        REQUIRE(parsed.has_value());
        TerrainParams b;
        REQUIRE(loadDocument(b, *parsed).has_value());
        CHECK(b.frequency == a.frequency);
        CHECK(b.octaves == 7);
        CHECK(b.seed == a.seed);
        CHECK(b.wireframe);
        CHECK(b.label == "ünï");
        CHECK(b.origin == a.origin);
        CHECK((b.mode == Mode::kPretty));
        REQUIRE(b.layers.size() == 2);
        CHECK(b.layers[0].name == "rock");
        CHECK(b.layers[1].weight == 1.0f);
        CHECK(b.resolution == 4096u);
    }

    TEST_CASE("binary (MessagePack) round trip is exact and smaller than the text")
    {
        TerrainParams a;
        a.octaves = 11;
        a.layers = {{"rock", 0.5f}};
        const Json doc = saveDocument(a);
        const auto bytes = toBinary(doc);
        CHECK(bytes.size() < dumpJson(doc, -1).size());
        auto back = fromBinary(bytes);
        REQUIRE(back.has_value());
        CHECK(*back == doc);
        TerrainParams b;
        REQUIRE(loadDocument(b, *back).has_value());
        CHECK(b.octaves == 11);
        CHECK(b.layers[0].name == "rock");
        const std::vector<std::uint8_t> truncated(bytes.begin(), bytes.begin() + 3);
        CHECK_FALSE(fromBinary(truncated).has_value());
    }

    TEST_CASE("missing fields keep their default, unknown fields are ignored")
    {
        TerrainParams p;
        auto doc = parseJson(R"({"version":2,"data":{"octaves":3,"future":true}})");
        REQUIRE(doc.has_value());
        REQUIRE(loadDocument(p, *doc).has_value());
        CHECK(p.octaves == 3);
        CHECK(p.frequency == 1.0f);
        CHECK(p.label == "terrain");
    }

    TEST_CASE("errors name the field: wrong type, out of range, malformed document")
    {
        TerrainParams p;
        auto r = loadDocument(p, *parseJson(R"({"version":2,"data":{"octaves":"many"}})"));
        REQUIRE_FALSE(r.has_value());
        CHECK((r.error().code == ErrorCode::kParse));
        CHECK(r.error().message.find("octaves") != std::string::npos);

        r = loadDocument(p, *parseJson(R"({"version":2,"data":{"octaves":99}})"));
        REQUIRE_FALSE(r.has_value());
        CHECK((r.error().code == ErrorCode::kOutOfRange));

        r = loadDocument(p, *parseJson(R"({"version":2,"data":{"layers":[{"weight":3.0}]}})"));
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().message.find("layers[0].weight") != std::string::npos);

        r = loadDocument(p, *parseJson(R"({"version":2,"data":{"resolution":-5}})"));
        CHECK_FALSE(r.has_value());

        CHECK_FALSE(parseJson("{not json").has_value());
        CHECK_FALSE(loadDocument(p, *parseJson("[1,2]")).has_value());
    }

    TEST_CASE("versioning: migration from an older version, refusal of a newer one")
    {
        // Version 1 appelait le champ « freq » ; la version 2 l'appelle « frequency ».
        MigrationTable migrations;
        migrations[1] = [](Json &data) -> Result<> {
            if (data.contains("freq"))
            {
                data["frequency"] = data["freq"];
                data.erase("freq");
            }
            return {};
        };
        TerrainParams p;
        auto old = parseJson(R"({"version":1,"data":{"freq":8.0}})");
        REQUIRE(loadDocument(p, *old, migrations).has_value());
        CHECK(p.frequency == 8.0f);

        auto noMigration = loadDocument(p, *old);
        REQUIRE_FALSE(noMigration.has_value());
        CHECK((noMigration.error().code == ErrorCode::kVersionMismatch));

        auto future = loadDocument(p, *parseJson(R"({"version":3,"data":{}})"), migrations);
        REQUIRE_FALSE(future.has_value());
        CHECK((future.error().code == ErrorCode::kVersionMismatch));
    }

    TEST_CASE("integers: representable values are accepted whatever their JSON representation, the others are refused")
    {
        // Entier JSON stocké « signé » (littéral int) mais positif : représentable dans un champ non signé.
        TerrainParams p;
        Json doc = {{"version", 2}, {"data", {{"resolution", 128}}}};
        REQUIRE(loadDocument(p, doc).has_value());
        CHECK(p.resolution == 128u);

        int i = 0;
        auto r = fromJsonValue(i, *parseJson("18446744073709551615"), "x"); // 2^64-1 : ne tient pas dans int
        REQUIRE_FALSE(r.has_value());
        CHECK((r.error().code == ErrorCode::kOutOfRange));
        r = fromJsonValue(i, *parseJson("2147483648"), "x"); // INT_MAX + 1
        CHECK_FALSE(r.has_value());
        r = fromJsonValue(i, *parseJson("-2147483648"), "x");
        REQUIRE(r.has_value());
        CHECK(i == INT32_MIN);

        std::uint32_t u = 0;
        CHECK_FALSE(fromJsonValue(u, *parseJson("4294967296"), "x").has_value());
        CHECK_FALSE(fromJsonValue(u, *parseJson("-1"), "x").has_value());
        REQUIRE(fromJsonValue(u, *parseJson("4294967295"), "x").has_value());
        CHECK(u == 4294967295u);
    }

    TEST_CASE("a document version outside int is refused instead of being truncated")
    {
        TerrainParams p;
        // 4294967298 = 2^32 + 2 : tronqué sur 32 bits, il vaudrait 2 (la version courante) et passerait.
        auto r = loadDocument(p, *parseJson(R"({"version":4294967298,"data":{"octaves":3}})"));
        REQUIRE_FALSE(r.has_value());
        CHECK((r.error().code == ErrorCode::kVersionMismatch));
        CHECK(p.octaves == 4); // rien n'a été chargé
        r = loadDocument(p, *parseJson(R"({"version":-1,"data":{}})"));
        CHECK_FALSE(r.has_value());
    }
}
