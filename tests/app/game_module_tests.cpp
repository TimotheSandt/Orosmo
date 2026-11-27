// GAME-01, GAME-02, GAME-07 (A79 lot L5) : module de jeu chargé, rechargé à chaud, version finale statique.
// Bibliothèques de test : tests/app/game_module/game.cpp construit en plusieurs versions (`bin/tests/test_game_<v><ext>`).
#include <doctest/doctest.h>

#include "app/game_module/test_game.hpp"

#include <orosmo/app/game_module.hpp>
#include <orosmo/app/params_host.hpp>
#include <orosmo/core/system.hpp>
#include <orosmo/platform/dynamic_library.hpp>
#include <orosmo/sim/simulation.hpp>
#include <orosmo/ui/ui.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace orosmo;
using core::ErrorCode;
using test_game::Counter;
namespace fs = std::filesystem;

namespace
{
fs::path builtGame(const std::string &variant)
{
    return core::system::executablePath().parent_path() /
           ("test_game_" + variant + std::string(platform::DynamicLibrary::extension()));
}

// Dossier propre par test : la bibliothèque « game<ext> » y est remplacée comme le ferait l'éditeur de liens.
struct GameFolder
{
    fs::path dir;
    fs::path library;

    explicit GameFolder(const std::string &name)
    {
        dir = core::system::executablePath().parent_path() / "game_module_tests" / name;
        fs::remove_all(dir);
        fs::create_directories(dir);
        library = dir / ("game" + std::string(platform::DynamicLibrary::extension()));
    }
    ~GameFolder()
    {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    // Remplace la bibliothèque par une version construite ; date avancée pour que l'observateur voie le changement même si le
    // système de fichiers arrondit les dates.
    void install(const std::string &variant)
    {
        const fs::path source = builtGame(variant);
        REQUIRE_MESSAGE(fs::exists(source), "test game not built: " << source.string());
        const bool existed = fs::exists(library);
        const auto before = existed ? fs::last_write_time(library) : fs::file_time_type{};
        fs::copy_file(source, library, fs::copy_options::overwrite_existing);
        if (existed)
            fs::last_write_time(library, before + std::chrono::seconds(2));
    }
};

struct Host
{
    sim::Simulation simulation;
    ui::ActionRegistry actions;

    app::GameModuleSettings settings(const fs::path &library)
    {
        return {.path = library,
                .targets = {.simulation = &simulation, .actions = &actions},
                .settleDelay = std::chrono::milliseconds(0),
                .pollInterval = std::chrono::milliseconds(0)};
    }

    Counter counter()
    {
        Counter out{};
        auto type = simulation.world().componentType<Counter>("Counter");
        REQUIRE(type.has_value());
        int count = 0;
        simulation.world().view(*type).each([&](scene::Entity, Counter &c) {
            out = c;
            ++count;
        });
        CHECK(count == 1);
        return out;
    }

    void click(const std::string &action) { actions.invoke(action, ui::Event{}); }
};

// Une table de jeu liée dans l'exécutable des tests (comme une version finale) : pas de bibliothèque.
bool staticInit(app::GameContext *context)
{
    auto type = context->registerComponent<Counter>("Counter");
    if (!type)
        return false;
    context->world().add(context->world().create(), *type, Counter{});
    const scene::ComponentType<Counter> counter = *type;
    return context
        ->addSystem({"static.count", sim::Phase::kUpdate, 0,
                     [counter](sim::StepContext &step) {
                         step.world.view(counter).each([](scene::Entity, Counter &c) { c.value += 2; });
                     }})
        .has_value();
}
constexpr OrosmoGameApi kStaticApi{app::kGameAbiVersion, "static game", &staticInit, nullptr, nullptr, nullptr};
constexpr OrosmoGameApi kWrongAbi{app::kGameAbiVersion + 1, "wrong", &staticInit, nullptr, nullptr, nullptr};
} // namespace

TEST_SUITE("app/game_module")
{
    TEST_CASE("GameModule: loads the game library, which registers components, systems and actions by name")
    {
        GameFolder folder("load");
        folder.install("v1");
        Host host;
        auto game = app::GameModule::load(host.settings(folder.library));
        REQUIRE_MESSAGE(game.has_value(), (game ? std::string() : game.error().describe()));

        CHECK(game->running());
        CHECK(game->hotReloadable());
        CHECK(game->version() == 1);
        CHECK(game->name() == "test game");
        CHECK(game->components() == std::vector<std::string>{"Counter", "V1Only"});
        CHECK(game->systems() == std::vector<std::string>{"count", "v1.only"});
        CHECK(game->actions() == std::vector<std::string>{"game.v1", "game.version"});
        CHECK(host.simulation.systemOrder() == std::vector<std::string>{"count", "v1.only"});
        CHECK(host.actions.contains("game.v1"));
        // Copie sous un nom unique, pas le fichier surveillé.
        CHECK(game->loadedPath() != fs::absolute(folder.library));
        CHECK(fs::exists(game->loadedPath()));

        host.simulation.run(5);
        game->update(1.0 / 60.0);
        host.click("game.version");
        const Counter c = host.counter();
        CHECK(c.value == 5);
        CHECK(c.frames == 1);
        CHECK(c.clicked == 1);
    }

    TEST_CASE("GameModule: on file change, the new version replaces the old one; component state is intact; no entry of "
              "the old version remains")
    {
        GameFolder folder("reload");
        folder.install("v1");
        Host host;
        auto game = app::GameModule::load(host.settings(folder.library));
        REQUIRE(game.has_value());
        host.simulation.run(5);
        const fs::path firstCopy = game->loadedPath();

        folder.install("v2");
        game->update(1.0 / 60.0); // voit le changement, recharge, puis appelle `update` de la v2
        REQUIRE_MESSAGE(game->version() == 2, (game->lastError() ? game->lastError()->describe() : std::string()));
        CHECK(game->failedReloads() == 0);

        // Registres : seulement la v2.
        CHECK(game->systems() == std::vector<std::string>{"count", "v2.only"});
        CHECK(host.simulation.systemOrder() == std::vector<std::string>{"count", "v2.only"});
        CHECK_FALSE(host.simulation.hasSystem("v1.only"));
        CHECK(game->actions() == std::vector<std::string>{"game.v2", "game.version"});
        CHECK_FALSE(host.actions.contains("game.v1"));
        CHECK(host.actions.contains("game.v2"));
        CHECK(game->components() == std::vector<std::string>{"Counter"});
        CHECK(game->orphanComponents() == std::vector<std::string>{"V1Only"});
        CHECK(host.simulation.world().componentId("V1Only").has_value()); // gardé avec ses données (L6 décidera)

        // L'ancienne copie est déchargée et effacée ; la nouvelle tourne.
        CHECK_FALSE(fs::exists(firstCopy));
        CHECK(fs::exists(game->loadedPath()));

        // État intact (5 pas de la v1), nouveau comportement (+10 par pas), `onReload` appelé une fois sur la v2.
        Counter c = host.counter();
        CHECK(c.value == 5);
        CHECK(c.reloads == 1);
        CHECK(c.loadedVersion == 2);
        CHECK(c.frames == 1);
        host.simulation.run(2);
        host.click("game.version");
        c = host.counter();
        CHECK(c.value == 25);
        CHECK(c.clicked == 2);

        // Retour à la v1 : même mécanisme dans l'autre sens, le type « V1Only » n'est plus orphelin.
        folder.install("v1");
        REQUIRE(game->reload().has_value());
        CHECK(game->version() == 3);
        CHECK(host.simulation.systemOrder() == std::vector<std::string>{"count", "v1.only"});
        CHECK(game->orphanComponents().empty());
        host.simulation.run(1);
        CHECK(host.counter().value == 26);
    }

    TEST_CASE("GameModule: a library with another abiVersion is refused, at load and at reload (old version kept)")
    {
        GameFolder folder("abi");
        folder.install("badabi");
        Host host;
        auto refused = app::GameModule::load(host.settings(folder.library));
        REQUIRE_FALSE(refused.has_value());
        CHECK((refused.error().code == ErrorCode::kVersionMismatch));
        CHECK(host.simulation.systemOrder().empty());

        folder.install("v1");
        auto game = app::GameModule::load(host.settings(folder.library));
        REQUIRE(game.has_value());
        host.simulation.run(1);
        folder.install("badabi");
        game->update(0.0);
        CHECK(game->version() == 1);
        CHECK(game->failedReloads() == 1);
        REQUIRE(game->lastError().has_value());
        CHECK((game->lastError()->code == ErrorCode::kVersionMismatch));
        CHECK(host.simulation.systemOrder() == std::vector<std::string>{"count", "v1.only"});
        host.simulation.run(1);
        CHECK(host.counter().value == 2); // toujours +1 : la v1 tourne
    }

    TEST_CASE("GameModule: a new version whose init fails is unloaded; the old one keeps running with its registrations")
    {
        GameFolder folder("failinit");
        folder.install("v1");
        Host host;
        auto game = app::GameModule::load(host.settings(folder.library));
        REQUIRE(game.has_value());

        folder.install("failinit");
        auto result = game->reload();
        REQUIRE_FALSE(result.has_value());
        CHECK((result.error().code == ErrorCode::kCancelled));
        CHECK(game->version() == 1);
        CHECK_FALSE(host.simulation.hasSystem("failinit.system"));
        CHECK(host.simulation.systemOrder() == std::vector<std::string>{"count", "v1.only"});
        CHECK(game->actions() == std::vector<std::string>{"game.v1", "game.version"});
        CHECK(host.actions.contains("game.v1"));
        host.simulation.run(3);
        CHECK(host.counter().value == 3);

        // Une version valide passe ensuite normalement.
        folder.install("v2");
        game->update(0.0);
        CHECK(game->version() == 2);
    }

    TEST_CASE("GameModule: a missing file or a broken library is refused without crashing")
    {
        Host host;
        auto missing = app::GameModule::load(host.settings("no/such/game" + std::string(platform::DynamicLibrary::extension())));
        REQUIRE_FALSE(missing.has_value());
        CHECK((missing.error().code == ErrorCode::kNotFound));

        GameFolder folder("broken");
        folder.install("v1");
        auto game = app::GameModule::load(host.settings(folder.library));
        REQUIRE(game.has_value());
        {
            // Fichier tronqué (écriture en cours de l'éditeur de liens) : refusé, la v1 reste.
            std::ofstream out(folder.library, std::ios::binary | std::ios::trunc);
            out << "not a library";
        }
        auto result = game->reload();
        CHECK_FALSE(result.has_value());
        CHECK(game->version() == 1);
        CHECK(host.simulation.hasSystem("count"));
    }

    TEST_CASE("GameModule: stop calls shutdown, removes systems and actions, keeps component data")
    {
        GameFolder folder("stop");
        folder.install("v1");
        Host host;
        auto game = app::GameModule::load(host.settings(folder.library));
        REQUIRE(game.has_value());
        host.simulation.run(4);
        const fs::path copy = game->loadedPath();
        game->stop();
        CHECK_FALSE(game->running());
        CHECK(host.simulation.systemOrder().empty());
        CHECK_FALSE(host.actions.contains("game.v1"));
        CHECK_FALSE(host.actions.contains("game.version"));
        CHECK_FALSE(fs::exists(copy));
        const Counter c = host.counter();
        CHECK(c.value == 4);
        CHECK(c.shutdowns == 1);
        game->stop(); // sans effet
        CHECK((game->reload().error().code == ErrorCode::kInvalidArgument));
    }

    TEST_CASE("GameModule: a name already taken by the host is refused (kAlreadyExists) and the game does not start")
    {
        GameFolder folder("taken");
        folder.install("v1");
        Host host;
        host.actions.add("game.v1", [](const ui::Event &) {});
        auto game = app::GameModule::load(host.settings(folder.library));
        REQUIRE_FALSE(game.has_value());
        CHECK((game.error().code == ErrorCode::kCancelled));
        CHECK(host.actions.contains("game.v1")); // celle de l'hôte, gardée
        CHECK(host.simulation.systemOrder().empty());
    }

    TEST_CASE("GameModule::fromApi (static build): starts a linked table, no hot reload")
    {
        Host host;
        auto game = app::GameModule::fromApi(&kStaticApi, {.simulation = &host.simulation});
        REQUIRE(game.has_value());
        CHECK_FALSE(game->hotReloadable());
        CHECK(game->loadedPath().empty());
        CHECK((game->reload().error().code == ErrorCode::kUnsupported));
        host.simulation.run(3);
        CHECK(host.counter().value == 6);
        game->update(0.0); // `update` nul : sans effet

        // Sans registre d'actions : addAction refusé (kUnsupported) ; sans monde : refusé.
        CHECK((app::GameModule::fromApi(&kWrongAbi, {.simulation = &host.simulation}).error().code ==
               ErrorCode::kVersionMismatch));
        CHECK((app::GameModule::fromApi(nullptr, {.simulation = &host.simulation}).error().code ==
               ErrorCode::kInvalidArgument));
        CHECK((app::GameModule::fromApi(&kStaticApi, {}).error().code == ErrorCode::kInvalidArgument));
    }
}

// ── GAME-03, GAME-04, GAME-05 (A79 lot L6) ───────────────────────────────────

namespace
{
using test_game::CounterV3;

// Écrit un fichier ; date avancée pour que l'observateur voie le changement même si la taille ne change pas.
void writeFile(const fs::path &path, const std::string &text)
{
    const bool existed = fs::exists(path);
    const auto before = existed ? fs::last_write_time(path) : fs::file_time_type{};
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << text;
    }
    if (existed)
        fs::last_write_time(path, before + std::chrono::seconds(2));
}

template <typename T> T only(scene::World &world, const char *name)
{
    auto type = world.componentType<T>(name);
    REQUIRE_MESSAGE(type.has_value(), (type ? std::string() : type.error().describe()));
    T out{};
    int count = 0;
    world.view(*type).each([&](scene::Entity, T &value) {
        out = value;
        ++count;
    });
    CHECK(count == 1);
    return out;
}

// Tables liées dans l'exécutable des tests (`GameModule::replace`) : « Mover » change de disposition.
struct MoverV1
{
    std::int64_t hp = 7;
    float speed = 2.5f;
};
struct MoverChanged
{
    double hp = -1.0;       // entier -> réel : converti
    core::Vec3 speed{3.f};  // réel -> vecteur : valeur par défaut
};
} // namespace

template <> struct orosmo::core::Reflect<MoverV1>
{
    static constexpr auto fields() { return std::make_tuple(field("hp", &MoverV1::hp), field("speed", &MoverV1::speed)); }
};
template <> struct orosmo::core::Reflect<MoverChanged>
{
    static constexpr auto fields()
    {
        return std::make_tuple(field("hp", &MoverChanged::hp), field("speed", &MoverChanged::speed));
    }
};

namespace
{
bool moverV1Init(app::GameContext *context)
{
    auto type = context->registerComponent<MoverV1>("Mover");
    if (!type)
        return false;
    if (context->world().componentEntities(type->id).empty())
        context->world().add(context->world().create(), *type, MoverV1{});
    const scene::ComponentType<MoverV1> mover = *type;
    return context
        ->addSystem({"move", sim::Phase::kUpdate, 0,
                     [mover](sim::StepContext &step) { step.world.view(mover).each([](scene::Entity, MoverV1 &m) { m.hp += 1; }); }})
        .has_value();
}

template <bool kFail> bool moverChangedInit(app::GameContext *context)
{
    auto type = context->registerComponent<MoverChanged>("Mover");
    if (!type)
        return false;
    const scene::ComponentType<MoverChanged> mover = *type;
    if (!context->addSystem({"move", sim::Phase::kUpdate, 0, [mover](sim::StepContext &step) {
                                 step.world.view(mover).each([](scene::Entity, MoverChanged &m) { m.hp += 0.5; });
                             }}))
        return false;
    if constexpr (kFail)
    {
        context->world().create(); // annulé avec le reste
        return false;
    }
    return true;
}

constexpr OrosmoGameApi kMoverV1{app::kGameAbiVersion, "mover v1", &moverV1Init, nullptr, nullptr, nullptr};
constexpr OrosmoGameApi kMoverChanged{app::kGameAbiVersion, "mover changed", &moverChangedInit<false>, nullptr, nullptr, nullptr};
constexpr OrosmoGameApi kMoverFails{app::kGameAbiVersion, "mover fails", &moverChangedInit<true>, nullptr, nullptr, nullptr};

struct TuningHost
{
    std::int64_t step = 1;
    float scale = 1.f;
};
} // namespace

template <> struct orosmo::core::Reflect<TuningHost>
{
    static constexpr auto fields()
    {
        return std::make_tuple(field("step", &TuningHost::step), field("scale", &TuningHost::scale, range(0.0, 10.0)));
    }
};

TEST_SUITE("app/game_module")
{
    TEST_CASE("GameModule (GAME-03): a field added to a component -> common fields copied, new one at its default value")
    {
        GameFolder folder("migrate-added");
        folder.install("v1");
        Host host;
        auto game = app::GameModule::load(host.settings(folder.library));
        REQUIRE(game.has_value());
        host.simulation.run(5);
        host.click("game.version");
        const auto oldId = host.simulation.world().componentId("Counter");
        REQUIRE(oldId.has_value());

        folder.install("v3");
        REQUIRE_MESSAGE(game->reload().has_value(), (game->lastError() ? game->lastError()->describe() : std::string()));
        CHECK(game->version() == 2);
        REQUIRE(game->lastMigrations().size() == 1);
        const scene::ComponentMigration &migration = game->lastMigrations().front();
        CHECK(migration.name == "Counter");
        CHECK(migration.kind == scene::MigrationKind::kCopied);
        CHECK(migration.fromVersion == 1);
        CHECK(migration.toVersion == 2);
        CHECK(migration.values == 1);
        CHECK(migration.warnings.empty());
        CHECK(host.simulation.world().componentDesc(*oldId) == nullptr); // ancien identifiant refusé

        CounterV3 c = only<CounterV3>(host.simulation.world(), "Counter");
        CHECK(c.value == 5);
        CHECK(c.clicked == 1);
        CHECK(c.bonus == 42);
        CHECK(c.reloads == 1);
        CHECK(c.loadedVersion == 2);

        // Retour à la v1 : `bonus` retiré -> repli sauvegarde/restauration, perte signalée ; les autres champs gardés.
        folder.install("v1");
        REQUIRE(game->reload().has_value());
        REQUIRE(game->lastMigrations().size() == 1);
        CHECK(game->lastMigrations().front().kind == scene::MigrationKind::kRestored);
        REQUIRE(game->lastMigrations().front().warnings.size() == 1);
        CHECK(game->lastMigrations().front().warnings.front().starts_with("Counter.bonus: field removed"));
        CHECK(host.counter().value == 5);
        CHECK(host.counter().reloads == 2);
    }

    TEST_CASE("GameModule (GAME-03): changed field type -> restored by field name; a failing init keeps the old version and "
              "the state intact")
    {
        Host host;
        auto game = app::GameModule::fromApi(&kMoverV1, {.simulation = &host.simulation});
        REQUIRE(game.has_value());
        host.simulation.run(3); // hp = 10
        scene::World &world = host.simulation.world();
        const auto oldType = world.componentType<MoverV1>("Mover");
        REQUIRE(oldType.has_value());
        const std::uint64_t entitiesBefore = world.entities().size();

        // Nouvelle version qui migre « Mover » puis échoue : monde remis (données, identifiant, entités), v1 active.
        auto failed = game->replace(&kMoverFails);
        REQUIRE_FALSE(failed.has_value());
        CHECK((failed.error().code == ErrorCode::kCancelled));
        CHECK(game->version() == 1);
        CHECK(game->failedReloads() == 1);
        CHECK(world.entities().size() == entitiesBefore);
        REQUIRE(world.componentType<MoverV1>("Mover").has_value());
        CHECK(world.componentType<MoverV1>("Mover")->id == oldType->id);
        CHECK(only<MoverV1>(world, "Mover").hp == 10);
        host.simulation.run(1); // le système de la v1 (identifiant capturé) marche encore
        CHECK(only<MoverV1>(world, "Mover").hp == 11);
        CHECK(game->lastMigrations().empty());

        // Même changement, init réussi : hp entier relu en réel, speed (réel -> vecteur) à sa valeur par défaut.
        REQUIRE(game->replace(&kMoverChanged).has_value());
        CHECK(game->version() == 2);
        REQUIRE(game->lastMigrations().size() == 1);
        CHECK(game->lastMigrations().front().kind == scene::MigrationKind::kRestored);
        REQUIRE(game->lastMigrations().front().warnings.size() == 1);
        CHECK(game->lastMigrations().front().warnings.front().starts_with("Mover.speed: type changed"));
        MoverChanged m = only<MoverChanged>(world, "Mover");
        CHECK(m.hp == 11.0);
        CHECK(m.speed == core::Vec3{3.f});
        host.simulation.run(2);
        CHECK(only<MoverChanged>(world, "Mover").hp == 12.0);
    }

    TEST_CASE("GameModule (GAME-03): a component type of the host is not migrated (kAlreadyExists)")
    {
        Host host;
        REQUIRE(host.simulation.world().registerComponent<MoverChanged>("Mover").has_value());
        auto game = app::GameModule::fromApi(&kMoverV1, {.simulation = &host.simulation});
        REQUIRE_FALSE(game.has_value());
        CHECK((game.error().code == ErrorCode::kCancelled));
        CHECK(host.simulation.world().componentType<MoverChanged>("Mover").has_value());
    }

    TEST_CASE("GameModule (GAME-04): a renamed handler leaves the interface binding missing (logged), without crash")
    {
        GameFolder folder("renamed");
        folder.install("v1");
        Host host;
        // Ce que compile `.onClick("game.v1")` : liaison tardive par nom.
        const ui::Handler oldButton = host.actions.bind("game.v1");
        const ui::Handler newButton = host.actions.bind("game.v2");
        auto game = app::GameModule::load(host.settings(folder.library));
        REQUIRE(game.has_value());
        CHECK(host.actions.contains("game.v1"));

        folder.install("v2"); // « game.v1 » renommé « game.v2 »
        REQUIRE(game->reload().has_value());
        CHECK(game->missingActions() == std::vector<std::string>{"game.v1"});
        oldButton(ui::Event{}); // sans effet, journalisé une fois
        CHECK_FALSE(host.actions.contains("game.v1"));
        CHECK(host.actions.contains("game.v2"));
        newButton(ui::Event{});

        folder.install("v1");
        REQUIRE(game->reload().has_value());
        CHECK(game->missingActions() == std::vector<std::string>{"game.v2"});
    }

    TEST_CASE("GameModule (GAME-05): game parameters read from JSON, changed without compilation, invalid file ignored")
    {
        GameFolder folder("params");
        folder.install("v1");
        Host host;
        app::GameModuleSettings settings = host.settings(folder.library);
        settings.targets.paramsDirectory = folder.dir;
        settings.targets.params.pollInterval = std::chrono::milliseconds(0);
        auto game = app::GameModule::load(settings);
        REQUIRE(game.has_value());

        // v3 : « count » ajoute le pas de « tuning.json » ; fichier absent -> valeur par défaut (100).
        folder.install("v3");
        REQUIRE(game->reload().has_value());
        CHECK(game->params() == std::vector<std::string>{"tuning"});
        host.simulation.run(1);
        CHECK(only<CounterV3>(host.simulation.world(), "Counter").value == 100);

        writeFile(folder.dir / "tuning.json", R"({"step": 5})");
        game->update(0.0); // relu à l'image suivante, sans recompiler
        host.simulation.run(2);
        CHECK(only<CounterV3>(host.simulation.world(), "Counter").value == 110);

        writeFile(folder.dir / "tuning.json", R"({"step": "five"})"); // type faux : ignoré
        game->update(0.0);
        writeFile(folder.dir / "tuning.json", R"({"step": )"); // JSON illisible : ignoré
        game->update(0.0);
        host.simulation.run(1);
        CHECK(only<CounterV3>(host.simulation.world(), "Counter").value == 115);
        CHECK(game->failedReloads() == 0);
    }

    TEST_CASE("ParamsHost: defaults without a file, last valid value kept, unknown field warned, range checked")
    {
        const fs::path dir = core::system::executablePath().parent_path() / "game_module_tests" / "params-host";
        fs::remove_all(dir);
        fs::create_directories(dir);
        const fs::path file = dir / "tuning.json";

        auto params = app::ParamsHost<TuningHost>::open(file, {.watch = true, .pollInterval = std::chrono::milliseconds(0)});
        REQUIRE(params.has_value());
        CHECK(params->get().step == 1);
        CHECK(params->revision() == 0);
        REQUIRE(params->lastError().has_value());
        CHECK((params->lastError()->code == ErrorCode::kIo));
        const TuningHost *address = &params->get();

        writeFile(file, R"({"step": 4, "scale": 2.5, "extra": true})");
        CHECK(params->poll());
        CHECK(params->get().step == 4);
        CHECK(params->get().scale == 2.5f);
        CHECK(params->revision() == 1);
        CHECK_FALSE(params->lastError().has_value());
        CHECK(params->file().warnings().size() == 1);
        CHECK(&params->get() == address); // même adresse : un pointeur gardé voit la nouvelle valeur

        writeFile(file, R"({"step": 9, "scale": 50})"); // hors de la plage [0, 10] : refusé en entier
        CHECK_FALSE(params->poll());
        CHECK(params->get().step == 4);
        REQUIRE(params->lastError().has_value());
        CHECK((params->lastError()->code == ErrorCode::kOutOfRange));

        writeFile(file, R"({"scale": 3})"); // champ absent : valeur par défaut
        CHECK(params->poll());
        CHECK(params->get().step == 1);
        CHECK(params->get().scale == 3.f);
        CHECK_FALSE(params->poll()); // rien de nouveau

        fs::remove_all(dir);
    }

    TEST_CASE("GameModule: orphan component types are kept, and removed on request; replace needs a static build")
    {
        GameFolder folder("orphans");
        folder.install("v1");
        Host host;
        auto game = app::GameModule::load(host.settings(folder.library));
        REQUIRE(game.has_value());
        CHECK((game->replace(&kMoverV1).error().code == ErrorCode::kUnsupported));
        folder.install("v2");
        REQUIRE(game->reload().has_value());
        CHECK(game->orphanComponents() == std::vector<std::string>{"V1Only"});
        CHECK(game->removeOrphanComponents() == 1);
        CHECK(game->orphanComponents().empty());
        CHECK_FALSE(host.simulation.world().componentId("V1Only").has_value());
    }
}
