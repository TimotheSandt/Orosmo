// ECS-03 (A79 lot L3) : sauvegarde et chargement de scène (JSON, MessagePack), versions, empreinte.
#include <doctest/doctest.h>

#include <orosmo/scene/fingerprint.hpp>
#include <orosmo/scene/scene_file.hpp>

#include <bit>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <system_error>
#include <vector>

using namespace orosmo;
using namespace orosmo::scene;

namespace
{
// Code d'erreur d'un résultat qui DOIT être une erreur : REQUIRE avant `.error()` (un succès inattendu est signalé, sans
// appeler `.error()` sur une valeur).
template <typename T> core::ErrorCode errorOf(const core::Result<T> &result)
{
    REQUIRE(!result);
    return result.error().code;
}

// Répertoire temporaire propre à ce cas (deux exécutions simultanées ne se gênent pas), supprimé à la sortie, même si un
// REQUIRE échoue.
struct TempDir
{
    std::filesystem::path path;
    TempDir()
    {
        const auto base = std::filesystem::temp_directory_path();
        const auto stamp = static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count());
        for (unsigned attempt = 0;; ++attempt)
        {
            path = base / ("orosmo-scene-" + std::to_string(stamp) + "-" + std::to_string(attempt));
            std::error_code ec;
            if (std::filesystem::create_directory(path, ec))
                return;
            REQUIRE(attempt < 1000);
        }
    }
    ~TempDir()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    TempDir(const TempDir &) = delete;
    TempDir &operator=(const TempDir &) = delete;
};

enum class Mood : std::int16_t
{
    kCalm = -3,
    kAngry = 7,
};

struct Stats
{
    std::uint8_t level = 1;
    std::int64_t gold = -5;
    double weight = 70.25;
};

struct Agent
{
    bool active = true;
    Mood mood = Mood::kCalm;
    float speed = 1.5f;
    core::Vec2 heading{1.f, 0.f};
    core::Vec4 color{0.1f, 0.2f, 0.3f, 1.f};
    core::Quat look{1.f, 0.f, 0.f, 0.f};
    Stats stats;
    Entity target;
    std::uint64_t big = 0;
};

// Version 1 d'un composant « Health » : un entier de points de vie.
struct HealthV1
{
    std::int32_t hp = 100;
};
// Version 2 : points de vie en réel et armure ; la migration renomme « hp » en « points ».
struct HealthV2
{
    float points = 50.f;
    float armor = 3.f;
};
} // namespace

template <> struct orosmo::core::Reflect<Stats>
{
    static constexpr auto fields()
    {
        return std::make_tuple(field("level", &Stats::level, range(0, 99)), field("gold", &Stats::gold), field("weight", &Stats::weight));
    }
};

template <> struct orosmo::core::Reflect<Agent>
{
    static constexpr int kVersion = 1;
    static constexpr auto fields()
    {
        return std::make_tuple(field("active", &Agent::active), field("mood", &Agent::mood), field("speed", &Agent::speed),
                               field("heading", &Agent::heading), field("color", &Agent::color), field("look", &Agent::look),
                               field("stats", &Agent::stats), field("target", &Agent::target), field("big", &Agent::big));
    }
};

template <> struct orosmo::core::Reflect<HealthV1>
{
    static constexpr int kVersion = 1;
    static constexpr auto fields() { return std::make_tuple(field("hp", &HealthV1::hp)); }
};

template <> struct orosmo::core::Reflect<HealthV2>
{
    static constexpr int kVersion = 2;
    static constexpr auto fields() { return std::make_tuple(field("points", &HealthV2::points), field("armor", &HealthV2::armor)); }
};

namespace
{
// Monde de test : hiérarchie, indices libres, générations non nulles, valeurs extrêmes.
World makeWorld(ComponentType<Agent> &agentType)
{
    World world;
    agentType = *world.registerComponent<Agent>("Agent");
    std::vector<Entity> e;
    for (int i = 0; i < 8; ++i)
        e.push_back(world.create());
    world.destroy(e[2]);
    world.destroy(e[5]);
    e[2] = world.create(); // indice 5 réutilisé, génération 1
    REQUIRE(world.setParent(e[1], e[0]));
    REQUIRE(world.setParent(e[3], e[0]));
    REQUIRE(world.setParent(e[4], e[1]));
    REQUIRE(world.setParent(e[2], e[0])); // ordre des enfants de e0 : 1, 3, (5)
    REQUIRE(world.setTransform(e[0], {.position = {1.0e7 + 0.123456789, -2.5, 3.0}, .rotation = {0.5, 0.5, 0.5, 0.5}, .scale = {2.0, 1.0, 1.0}}));
    REQUIRE(world.setTransform(e[1], {.position = {0.1, 0.2, 0.3}}));
    REQUIRE(world.setTransform(e[4], {.position = {-0.0, 1e-300, 4.9e-324}}));

    Agent a;
    a.speed = std::nextafter(0.1f, 1.f);
    a.mood = Mood::kAngry;
    a.heading = {-0.0f, std::numeric_limits<float>::denorm_min()};
    a.color = {std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), 1e-38f, 3.4e38f};
    a.stats = {99, std::numeric_limits<std::int64_t>::min(), 1.0 / 3.0};
    a.target = e[4];
    a.big = std::numeric_limits<std::uint64_t>::max();
    world.add(e[3], agentType, a);
    a.active = false;
    a.mood = Mood::kCalm;
    a.target = e[2];
    world.add(e[1], agentType, a); // ordre dense : e3 puis e1
    world.add(e[7], agentType);
    world.updateTransforms();
    return world;
}
} // namespace

TEST_SUITE("scene/file")
{
    TEST_CASE("Scene: JSON round trip is exact (entities, hierarchy, order, values, transforms)")
    {
        ComponentType<Agent> type;
        World original = makeWorld(type);
        const core::Json saved = saveScene(original);

        World loaded;
        REQUIRE(loaded.registerComponent<Agent>("Agent"));
        auto report = loadScene(loaded, saved);
        REQUIRE(report);
        CHECK(report->warnings.empty());
        CHECK(report->entities == original.entityCount());
        CHECK(report->components == 3 + 3); // 3 Agent, 3 Transform

        CHECK(loaded.entityTable() == original.entityTable());
        CHECK(fingerprint(loaded) == fingerprint(original));
        CHECK(core::dumpJson(saveScene(loaded)) == core::dumpJson(saved));
        for (const Entity e : original.entities())
        {
            CHECK(loaded.alive(e));
            CHECK(loaded.children(e) == original.children(e));
        }
        auto loadedType = *loaded.componentType<Agent>("Agent");
        const std::span<const Entity> order = loaded.componentEntities(loadedType.id);
        const std::span<const Entity> before = original.componentEntities(type.id);
        CHECK(std::equal(order.begin(), order.end(), before.begin(), before.end()));
        const Entity e3 = Entity{3, 0};
        const Agent *a = loaded.get(e3, loadedType);
        REQUIRE(a != nullptr);
        CHECK(std::memcmp(&a->speed, &original.get(e3, type)->speed, sizeof(float)) == 0);
        CHECK(a->stats.gold == std::numeric_limits<std::int64_t>::min());
        CHECK(a->big == std::numeric_limits<std::uint64_t>::max());
        CHECK(std::isinf(a->color.x));
        CHECK(std::signbit(a->heading.x));
        CHECK(a->mood == Mood::kAngry);
        CHECK(loaded.alive(a->target)); // référence d'entité gardée (même indice, même génération)
        // Cache monde recalculé après chargement.
        const Entity e4 = Entity{4, 0};
        CHECK(loaded.transform(e4)->worldPosition == original.transform(e4)->worldPosition);
        CHECK(loaded.dirtyTransformCount() == 0);
        // Les create suivants rendent les mêmes identifiants.
        CHECK(loaded.create() == original.create());
        CHECK(loaded.create() == original.create());
    }

    TEST_CASE("Scene: MessagePack round trip is exact and the same as JSON")
    {
        ComponentType<Agent> type;
        World original = makeWorld(type);
        const std::vector<std::uint8_t> bytes = saveSceneBinary(original);
        World loaded;
        REQUIRE(loaded.registerComponent<Agent>("Agent"));
        REQUIRE(loadSceneBinary(loaded, bytes));
        CHECK(fingerprint(loaded) == fingerprint(original));
        CHECK(saveSceneBinary(loaded) == bytes);
        CHECK(core::dumpJson(saveScene(loaded)) == core::dumpJson(saveScene(original)));
        CHECK(bytes.size() < core::dumpJson(saveScene(original), -1).size());

        const TempDir temp;
        const std::filesystem::path &dir = temp.path;
        for (const char *name : {"orosmo_scene_test.json", "orosmo_scene_test.oscene"})
        {
            const auto path = dir / name;
            REQUIRE(saveSceneFile(original, path));
            CHECK_FALSE(std::filesystem::exists(dir / (std::string(name) + ".tmp")));
            World fromFile;
            REQUIRE(fromFile.registerComponent<Agent>("Agent"));
            REQUIRE(loadSceneFile(fromFile, path));
            CHECK(fingerprint(fromFile) == fingerprint(original));
        }
        World other;
        CHECK((errorOf(loadSceneFile(other, dir / "orosmo_absent_scene.json")) == core::ErrorCode::kIo));
    }

    TEST_CASE("Scene: component migration v1 -> v2, missing field = default, unknown field and type ignored with a warning")
    {
        World v1;
        auto health1 = *v1.registerComponent<HealthV1>("Health");
        const Entity a = v1.create();
        const Entity b = v1.create();
        v1.add(a, health1, HealthV1{42});
        v1.add(b, health1, HealthV1{7});
        core::Json saved = saveScene(v1);
        // Champ ajouté à la main (inconnu de v2 après migration), et type absent du monde de destination.
        saved["data"]["components"][0]["values"][1]["mana"] = 3;
        saved["data"]["components"].push_back(core::Json{{"name", "Ghost"}, {"version", 1}, {"entities", {0}}, {"values", {core::Json::object()}}});

        World v2;
        auto health2 = *v2.registerComponent<HealthV2>("Health");
        SceneMigrations migrations;
        migrations.components["Health"][1] = [](core::Json &value) -> core::Result<> {
            value["points"] = value["hp"].get<double>();
            value.erase("hp");
            return {};
        };
        // Sans migration : refusé, monde inchangé (document entier comparé : entités, composants, hiérarchie).
        const Entity untouched = v2.create();
        const Entity child = v2.create();
        REQUIRE(v2.setParent(child, untouched));
        v2.add(child, health2, HealthV2{1.f, 2.f});
        const std::string before = core::dumpJson(saveScene(v2));
        CHECK((errorOf(loadScene(v2, saved)) == core::ErrorCode::kVersionMismatch));
        CHECK(core::dumpJson(saveScene(v2)) == before);
        CHECK(v2.alive(untouched));

        auto report = loadScene(v2, saved, migrations);
        REQUIRE(report);
        CHECK(v2.get(a, health2)->points == 42.f);
        CHECK(v2.get(a, health2)->armor == 3.f); // absent : valeur par défaut
        CHECK(v2.get(b, health2)->points == 7.f);
        REQUIRE(report->warnings.size() == 2);
        CHECK(report->warnings[0].find("mana") != std::string::npos);
        CHECK(report->warnings[1].find("Ghost") != std::string::npos);

        // Version plus récente que celle enregistrée : refusée.
        core::Json v2saved = saveScene(v2);
        World old;
        REQUIRE(old.registerComponent<HealthV1>("Health"));
        CHECK((errorOf(loadScene(old, v2saved)) == core::ErrorCode::kVersionMismatch));
    }

    TEST_CASE("Scene: format migration and invalid documents (world unchanged)")
    {
        ComponentType<Agent> type;
        World original = makeWorld(type);
        core::Json saved = saveScene(original);

        core::Json future = saved;
        future["version"] = kSceneFormatVersion + 1;
        World w;
        REQUIRE(w.registerComponent<Agent>("Agent"));
        CHECK((errorOf(loadScene(w, future)) == core::ErrorCode::kVersionMismatch));

        // Format v0 (hypothétique) : migration fournie par l'appelant.
        core::Json old = saved;
        old["version"] = 0;
        old["data"]["entities"]["gens"] = old["data"]["entities"]["generations"];
        old["data"]["entities"].erase("generations");
        SceneMigrations migrations;
        migrations.format[0] = [](core::Json &data) -> core::Result<> {
            data["entities"]["generations"] = data["entities"]["gens"];
            data["entities"].erase("gens");
            return {};
        };
        REQUIRE(loadScene(w, old, migrations));
        CHECK(fingerprint(w) == fingerprint(original));

        const Entity marker = w.create();
        const std::string before = core::dumpJson(saveScene(w));
        const auto expectError = [&](core::Json doc, core::ErrorCode code) {
            CHECK((errorOf(loadScene(w, doc)) == code));
            CHECK(w.alive(marker));
            CHECK(core::dumpJson(saveScene(w)) == before); // composants et hiérarchie compris
        };
        expectError(core::Json::array(), core::ErrorCode::kParse);
        core::Json bad = saved;
        bad["data"]["entities"]["free"].push_back(9999);
        expectError(bad, core::ErrorCode::kInvalidArgument);
        bad = saved;
        bad["data"]["components"][1]["values"][0]["stats"]["level"] = 100; // plage [0, 99]
        expectError(bad, core::ErrorCode::kOutOfRange);
        bad = saved;
        bad["data"]["components"][1]["values"][0]["speed"] = "fast";
        expectError(bad, core::ErrorCode::kParse);
        bad = saved;
        bad["data"]["components"][1]["values"][0]["big"] = -1;
        expectError(bad, core::ErrorCode::kOutOfRange);
        bad = saved;
        bad["data"]["hierarchy"].push_back(core::Json::array({0, 4})); // cycle : 0 est l'ancêtre de 4
        expectError(bad, core::ErrorCode::kParse);
        bad = saved;
        bad["data"]["components"][1]["entities"][0] = 2; // indice libre
        expectError(bad, core::ErrorCode::kParse);
        bad = saved;
        bad["data"]["components"][1]["entities"][1] = bad["data"]["components"][1]["entities"][0]; // deux valeurs, une entité
        expectError(bad, core::ErrorCode::kParse);
        bad = saved;
        bad["data"]["components"].push_back(core::Json{{"name", "Ghost"}, {"version", "invalid"}, {"entities", core::Json::array()}, {"values", core::Json::array()}});
        expectError(bad, core::ErrorCode::kParse); // type inconnu, mais version mal formée
        bad = saved;
        bad["data"]["components"].push_back(core::Json{{"name", "Ghost"}, {"version", 4}, {"entities", core::Json::array()}, {"values", core::Json::array()}});
        CHECK(loadScene(w, bad)); // type inconnu, version bien formée : ignoré (avertissement)
        CHECK((errorOf(loadSceneBinary(w, std::vector<std::uint8_t>{0xc1})) == core::ErrorCode::kParse));
    }

    TEST_CASE("Fingerprint: hashes described fields only, not padding nor the world cache; sensitive to values and order")
    {
        ComponentType<Agent> type;
        World a = makeWorld(type);
        World b = a;
        CHECK(fingerprint(a) == fingerprint(b));

        // Cache monde et remplissage hors de l'empreinte.
        const Entity e0 = Entity{0, 0};
        b.editTransform(e0)->worldPosition.x += 1.0;
        CHECK(fingerprint(a) == fingerprint(b));
        b = a;
        const Entity e3 = Entity{3, 0};
        auto *raw = reinterpret_cast<unsigned char *>(b.get(e3, type));
        raw[offsetof(Agent, active) + 1] ^= 0xffu; // octet de remplissage après le booléen
        CHECK(fingerprint(a) == fingerprint(b));

        b = a;
        b.get(e3, type)->speed = std::nextafter(b.get(e3, type)->speed, 2.f);
        CHECK(fingerprint(a) != fingerprint(b));
        b = a;
        b.get(e3, type)->heading.x = 0.f; // -0 -> +0
        CHECK(fingerprint(a) != fingerprint(b));

        // Même contenu, ordre dense différent : empreinte différente (l'ordre de parcours fait partie de l'état).
        World c = a;
        const Entity e1 = Entity{1, 0};
        c.remove(e3, type);
        const Agent saved3 = *a.get(e3, type);
        c.add(e3, type, saved3);
        CHECK(c.get(e1, type)->active == a.get(e1, type)->active);
        CHECK(fingerprint(a) != fingerprint(c));

        // Ordre d'enregistrement des types sans effet.
        World d;
        REQUIRE(d.registerComponent<HealthV1>("Health"));
        REQUIRE(d.registerComponent<Agent>("Agent"));
        REQUIRE(loadScene(d, saveScene(a)));
        CHECK(fingerprint(d) == fingerprint(a));
    }

    TEST_CASE("Fingerprint: every NaN hashes alike, so a JSON round trip keeps the fingerprint (NaN saved as \"nan\")")
    {
        ComponentType<Agent> type;
        World a = makeWorld(type);
        const Entity e3 = Entity{3, 0};
        a.get(e3, type)->speed = std::bit_cast<float>(0xFFC00001u); // NaN négatif à charge non canonique
        a.get(e3, type)->stats.weight = -std::numeric_limits<double>::quiet_NaN();
        World loaded;
        auto loadedType = loaded.registerComponent<Agent>("Agent"); // identifiant propre à ce monde (G390)
        REQUIRE(loadedType);
        REQUIRE(loadScene(loaded, core::Json::parse(core::dumpJson(saveScene(a)))));
        const Agent *reloaded = loaded.get(e3, *loadedType);
        REQUIRE(reloaded != nullptr);
        CHECK(std::isnan(reloaded->speed));
        CHECK(fingerprint(loaded) == fingerprint(a));
        // -0 et +0 restent distincts (voir le cas précédent) ; un NaN diffère d'un nombre.
        World b = a;
        b.get(e3, type)->speed = 0.f;
        CHECK(fingerprint(b) != fingerprint(a));
    }

    TEST_CASE("Scene: saveSceneFile reports a write error detected when the file is closed (full disk)")
    {
        ComponentType<Agent> type;
        const World world = makeWorld(type);
        const std::filesystem::path full = "/dev/full"; // Linux : /dev/full.tmp ne peut pas être créé ; kIo, /dev/full intact
        if (!std::filesystem::exists(full))
            return;
        CHECK((errorOf(saveSceneFile(world, full)) == core::ErrorCode::kIo));
    }

    TEST_CASE("Scene: a failed saveSceneFile keeps the previous save and removes its temporary file")
    {
        ComponentType<Agent> type;
        const World world = makeWorld(type);
        const TempDir temp;
        const auto path = temp.path / "save.json";
        REQUIRE(saveSceneFile(world, path));
        const auto readAll = [](const std::filesystem::path &p) {
            std::ifstream in(p, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        };
        const std::string previous = readAll(path);
        REQUIRE_FALSE(previous.empty());

        World changed = world;
        changed.create();
        // Le temporaire ne peut pas être créé (un répertoire porte son nom) : la dernière sauvegarde reste intacte.
        const auto tmp = temp.path / "save.json.tmp";
        REQUIRE(std::filesystem::create_directory(tmp));
        CHECK((errorOf(saveSceneFile(changed, path)) == core::ErrorCode::kIo));
        CHECK(readAll(path) == previous);
        std::filesystem::remove(tmp);

        // Le remplacement échoue (la destination est un répertoire non vide) : le temporaire écrit est supprimé.
        const auto dirTarget = temp.path / "dir.json";
        REQUIRE(std::filesystem::create_directory(dirTarget));
        { std::ofstream(dirTarget / "keep") << "x"; }
        CHECK((errorOf(saveSceneFile(changed, dirTarget)) == core::ErrorCode::kIo));
        CHECK_FALSE(std::filesystem::exists(temp.path / "dir.json.tmp"));
        CHECK(std::filesystem::exists(dirTarget / "keep"));
    }

    TEST_CASE("Scene: componentFromJson leaves the value and the warnings unchanged on error")
    {
        World world;
        auto type = world.registerComponent<Agent>("Agent");
        REQUIRE(type);
        const ComponentDesc *desc = world.componentDesc(type->id);
        REQUIRE(desc != nullptr);
        Agent value;
        value.speed = 4.f;
        value.stats.level = 5;
        std::vector<std::string> warnings;
        // `speed` (valide) est décodé avant `stats.level` (hors plage) : rien ne doit rester écrit.
        const core::Json bad{{"speed", 9.0}, {"stats", {{"level", 100}}}, {"extra", 1}};
        CHECK((errorOf(componentFromJson(*desc, &value, bad, warnings)) == core::ErrorCode::kOutOfRange));
        CHECK(value.speed == 4.f);
        CHECK(value.stats.level == 5);
        CHECK(warnings.empty());
        const core::Json good{{"speed", 9.0}, {"extra", 1}};
        REQUIRE(componentFromJson(*desc, &value, good, warnings));
        CHECK(value.speed == 9.f);
        CHECK(value.stats.level == 5); // absent : gardé
        CHECK(warnings.size() == 1);
    }

    TEST_CASE("Entity table: reset recreates identifiers and the free stack, refuses invalid tables")
    {
        World w;
        const Entity a = w.create();
        const Entity b = w.create();
        w.destroy(a);
        const World::EntityTable table = w.entityTable();
        CHECK(table.generations == std::vector<std::uint32_t>{1, 0});
        CHECK(table.freeIndices == std::vector<std::uint32_t>{0});

        World other;
        const Entity first = other.create();
        const Entity second = other.create(); // même indice et même génération que `b`
        REQUIRE(second == b);
        REQUIRE(other.setTransform(first, {}));
        REQUIRE(other.setTransform(second, {}));
        REQUIRE(other.transform(b) != nullptr);
        REQUIRE(other.resetEntities(table));
        CHECK(other.alive(b));
        CHECK(!other.alive(a));
        CHECK(other.entityCount() == 1);
        CHECK(other.transform(b) == nullptr); // composant de l'indice 1 retiré
        CHECK(other.create() == w.create());
        CHECK((errorOf(other.resetEntities({{0, 0}, {0, 0}})) == core::ErrorCode::kInvalidArgument));
        CHECK((errorOf(other.resetEntities({{0}, {1}})) == core::ErrorCode::kInvalidArgument));
    }
}
