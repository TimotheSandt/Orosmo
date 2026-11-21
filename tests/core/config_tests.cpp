#include <doctest/doctest.h>

#include <orosmo/core/config.hpp>

#include <filesystem>
#include <fstream>

using namespace orosmo::core;
namespace fs = std::filesystem;

namespace
{
Config makeConfig()
{
    Config c;
    c.declare("window.width", 1280, "window width in pixels");
    c.declare("terrain.octaves", 4, "noise octaves");
    c.declare("terrain.name", std::string("default"), "terrain name");
    c.declare("debug", false, "debug overlay");
    c.declare("terrain.frequency", 1.5, "frequency");
    return c;
}
struct TempFile
{
    fs::path path;
    explicit TempFile(const std::string &name) : path(fs::temp_directory_path() / ("engine_cfg_" + name)) { fs::remove(path); }
    ~TempFile() { fs::remove(path); }
    void write(const std::string &text) const { std::ofstream(path, std::ios::trunc) << text; }
};
struct Terrain
{
    int octaves = 1;
    std::string name = "x";
    double frequency = 0.0;
};
} // namespace

template <> struct orosmo::core::Reflect<Terrain>
{
    [[maybe_unused]] static constexpr int kVersion = 1;
    static constexpr auto fields()
    {
        return std::make_tuple(field("octaves", &Terrain::octaves), field("name", &Terrain::name),
                               field("frequency", &Terrain::frequency));
    }
};

TEST_SUITE("core/config")
{
    TEST_CASE("defaults are readable, typed, and unknown keys are reported")
    {
        Config c = makeConfig();
        CHECK(*c.get<int>("window.width") == 1280);
        CHECK(*c.get<std::string>("terrain.name") == "default");
        CHECK(*c.get<bool>("debug") == false);
        CHECK((c.get<int>("nope").error().code == ErrorCode::kNotFound));
        CHECK((c.get<int>("terrain.name").error().code == ErrorCode::kParse));
        CHECK(c.getOr<int>("nope", 7) == 7);
    }

    TEST_CASE("layers: default < file < command line")
    {
        Config c = makeConfig();
        TempFile f("layers.json");
        f.write(R"({"terrain":{"octaves":6,"name":"alps"},"window":{"width":800}})");
        REQUIRE(c.loadFile(f.path).has_value());
        CHECK(*c.get<int>("terrain.octaves") == 6);
        CHECK(*c.get<int>("window.width") == 800);
        auto positional = c.parseArgs({"--window.width", "1920", "scene.json", "--terrain.octaves=9"});
        REQUIRE(positional.has_value());
        CHECK(*positional == std::vector<std::string>{"scene.json"});
        CHECK(*c.get<int>("window.width") == 1920);
        CHECK(*c.get<int>("terrain.octaves") == 9);
        CHECK(*c.get<std::string>("terrain.name") == "alps");
        CHECK(*c.get<double>("terrain.frequency") == 1.5);
    }

    TEST_CASE("command line: boolean flags, --no-flag, type errors, missing value")
    {
        Config c = makeConfig();
        REQUIRE(c.parseArgs({"--debug"}).has_value());
        CHECK(*c.get<bool>("debug"));
        REQUIRE(c.parseArgs({"--no-debug"}).has_value());
        CHECK_FALSE(*c.get<bool>("debug"));
        auto bad = c.parseArgs({"--window.width", "wide"});
        REQUIRE_FALSE(bad.has_value());
        CHECK((bad.error().code == ErrorCode::kParse));
        auto missing = c.parseArgs({"--terrain.octaves"});
        REQUIRE_FALSE(missing.has_value());
        CHECK((missing.error().code == ErrorCode::kInvalidArgument));
        REQUIRE(c.parseArgs({"--terrain.name", "123"}).has_value()); // chaîne déclarée : pas converti en nombre
        CHECK(*c.get<std::string>("terrain.name") == "123");
        REQUIRE(c.parseArgs({"--terrain.frequency", "2"}).has_value()); // entier accepté pour un nombre
        CHECK(*c.get<double>("terrain.frequency") == 2.0);
    }

    TEST_CASE("hot reload reports exactly the keys whose effective value changed")
    {
        Config c = makeConfig();
        TempFile f("reload.json");
        f.write(R"({"terrain":{"octaves":6}})");
        auto first = c.loadFile(f.path);
        REQUIRE(first.has_value());
        CHECK(*first == std::vector<std::string>{"terrain.octaves"});
        f.write(R"({"terrain":{"octaves":6,"name":"dunes"},"window":{"width":1280}})");
        auto second = c.loadFile(f.path);
        REQUIRE(second.has_value());
        CHECK(*second == std::vector<std::string>{"terrain.name"}); // width = valeur par défaut : inchangée
        f.write("{}");
        auto third = c.loadFile(f.path);
        REQUIRE(third.has_value());
        CHECK(*third == std::vector<std::string>{"terrain.name", "terrain.octaves"}); // retour aux défauts
    }

    TEST_CASE("a broken file keeps the previous values")
    {
        Config c = makeConfig();
        TempFile f("broken.json");
        f.write(R"({"terrain":{"octaves":6}})");
        REQUIRE(c.loadFile(f.path).has_value());
        f.write("{ not json");
        CHECK_FALSE(c.loadFile(f.path).has_value());
        CHECK(*c.get<int>("terrain.octaves") == 6);
        CHECK_FALSE(c.loadFile(fs::temp_directory_path() / "engine_cfg_does_not_exist").has_value());
        f.write("[1]");
        CHECK_FALSE(c.loadFile(f.path).has_value());
    }

    TEST_CASE("save then load round trip, and applyTo fills a reflected struct")
    {
        Config a = makeConfig();
        a.set("terrain.octaves", 11);
        a.set("terrain.name", "ridge");
        TempFile f("save.json");
        REQUIRE(a.saveFile(f.path).has_value());
        Config b = makeConfig();
        REQUIRE(b.loadFile(f.path).has_value());
        Terrain t;
        REQUIRE(b.applyTo(t, "terrain").has_value());
        CHECK(t.octaves == 11);
        CHECK(t.name == "ridge");
        CHECK(t.frequency == 1.5);
    }

    TEST_CASE("describe lists every declared key with its default and description")
    {
        Config c = makeConfig();
        const std::string help = c.describe();
        CHECK(help.find("--window.width (default 1280)") != std::string::npos);
        CHECK(help.find("window width in pixels") != std::string::npos);
        CHECK(help.find("--debug (default false)") != std::string::npos);
    }

    TEST_CASE("get() of an object reads the same merged view as applyTo()")
    {
        Config c = makeConfig();
        TempFile f("merged.json");
        f.write(R"({"terrain":{"octaves":6}})"); // le fichier ne dit rien de terrain.name ni terrain.frequency
        REQUIRE(c.loadFile(f.path).has_value());
        auto terrain = c.get("terrain");
        REQUIRE(terrain.has_value());
        CHECK((*terrain)["octaves"] == 6);
        CHECK((*terrain)["name"] == "default"); // vient des défauts, comme pour applyTo
        Terrain t;
        REQUIRE(c.applyTo(t, "terrain").has_value());
        CHECK(t.name == "default");
        CHECK(t.octaves == 6);
    }

    TEST_CASE("command line: --no-x is only for booleans, a float is refused for an integer setting")
    {
        Config c = makeConfig();
        auto r = c.parseArgs({"--no-window.width"});
        REQUIRE_FALSE(r.has_value());
        CHECK((r.error().code == ErrorCode::kInvalidArgument));
        CHECK(*c.get<int>("window.width") == 1280); // rien n'a été écrit

        r = c.parseArgs({"--window.width=2.5"});
        REQUIRE_FALSE(r.has_value());
        CHECK((r.error().code == ErrorCode::kParse));
        CHECK(*c.get<int>("window.width") == 1280);
        REQUIRE(c.parseArgs({"--terrain.frequency=3"}).has_value()); // entier pour un flottant : accepté
    }

#ifdef __linux__
    TEST_CASE("saveFile reports a write that fails when the buffer is flushed")
    {
        Config c = makeConfig();
        auto r = c.saveFile("/dev/full"); // ouvre sans erreur, échoue (ENOSPC) au vidage / à la fermeture
        REQUIRE_FALSE(r.has_value());
        CHECK((r.error().code == ErrorCode::kIo));
    }
#endif
}
