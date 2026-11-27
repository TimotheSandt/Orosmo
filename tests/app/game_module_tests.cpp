// GAME-01, GAME-02, GAME-07 (A79 lot L5) : module de jeu chargé, rechargé à chaud, version finale statique.
// Bibliothèques de test : tests/app/game_module/game.cpp construit en plusieurs versions (`bin/tests/test_game_<v><ext>`).
#include <doctest/doctest.h>

#include "app/game_module/test_game.hpp"

#include <orosmo/app/game_module.hpp>
#include <orosmo/core/system.hpp>
#include <orosmo/platform/dynamic_library.hpp>
#include <orosmo/sim/simulation.hpp>
#include <orosmo/ui/ui.hpp>

#include <chrono>
#include <cstddef>
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
        host.simulation.run(2);
        const std::uint64_t before = host.simulation.fingerprint();
        const std::size_t entities = host.simulation.world().entityCount();

        // La v « failinit » écrit 999 dans le compteur, crée une entité, enregistre un type et tire un nombre, puis échoue.
        folder.install("failinit");
        auto result = game->reload();
        REQUIRE_FALSE(result.has_value());
        CHECK((result.error().code == ErrorCode::kCancelled));
        CHECK(game->version() == 1);
        // État du moteur remis tel quel (monde, générateur) : la version active reprend inchangée.
        CHECK(host.simulation.fingerprint() == before);
        CHECK(host.simulation.world().entityCount() == entities);
        CHECK_FALSE(host.simulation.world().componentId("FailInitOnly").has_value());
        CHECK(host.counter().value == 2);
        CHECK_FALSE(host.simulation.hasSystem("failinit.system"));
        CHECK(host.simulation.systemOrder() == std::vector<std::string>{"count", "v1.only"});
        CHECK(game->actions() == std::vector<std::string>{"game.v1", "game.version"});
        CHECK(host.actions.contains("game.v1"));
        host.simulation.run(3);
        CHECK(host.counter().value == 5);

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

#if !defined(_WIN32)
    TEST_CASE("GameModule: a relative path that cannot be resolved (current directory removed) is kIo, not an exception")
    {
        // Windows refuse d'effacer le dossier courant : cas propre à POSIX.
        const fs::path previous = fs::current_path();
        const fs::path gone = core::system::executablePath().parent_path() / "game_module_tests" / "gone";
        fs::create_directories(gone);
        fs::current_path(gone);
        fs::remove(gone);
        Host host;
        auto game = app::GameModule::load(host.settings("game" + std::string(platform::DynamicLibrary::extension())));
        fs::current_path(previous);
        REQUIRE_FALSE(game.has_value());
        CHECK((game.error().code == ErrorCode::kIo));
    }
#endif

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
        // Le monde est remis à son état d'avant l'`init` refusé : ni type « Counter » ni entité créée.
        CHECK_FALSE(host.simulation.world().componentId("Counter").has_value());
        CHECK(host.simulation.world().entityCount() == 0);
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
