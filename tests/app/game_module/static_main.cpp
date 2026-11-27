// Version finale statique (GAME-07, A79 lot L5) : la bibliothèque de jeu de test (game.cpp, v1) est compilée avec
// OROSMO_GAME_STATIC et liée DANS cet exécutable (`bin/tests/test_game_static`, apps/demo/Makefile). Aucun chargement
// dynamique : `GameModule::fromApi(orosmoGameApi(), ...)`. Après l'édition de liens, `nm` vérifie qu'aucun symbole de
// rechargement (`platform::DynamicLibrary`, `GameModule::load`) n'est présent ; `make test` lance ensuite ce programme, qui
// rend 0 si le jeu tourne comme attendu. UI-08 (A79 lot L8) : une vue .cui liée dans l'exécutable (v1 de
// tests/app/view_module/HotView.cui) est installée par la forme liée de `app::ViewHost`, sans son code de rechargement
// (`ViewHost::mountFromLibrary`, `runCommand` : absents, même contrôle `nm`).
#include "app/game_module/test_game.hpp"
#include "tests/app/view_module/v1/HotView.gen.h"

#include <orosmo/app/game_module.hpp>
#include <orosmo/app/view_host.hpp>
#include <orosmo/ui/font.hpp>
#include <orosmo/sim/simulation.hpp>
#include <orosmo/ui/ui.hpp>

#include <cstdio>

int main()
{
    using namespace orosmo;
    sim::Simulation simulation;
    ui::ActionRegistry actions;
    auto game = app::GameModule::fromApi(orosmoGameApi(), {.simulation = &simulation, .actions = &actions});
    if (!game)
    {
        std::fprintf(stderr, "test_game_static: %s\n", game.error().describe().c_str());
        return 1;
    }
    simulation.run(3);
    game->update(1.0 / 60.0);
    const bool reloadRefused = !game->reload() && !game->hotReloadable();
    const bool actionsRegistered = actions.contains("game.v1") && actions.contains("game.version");

    auto type = simulation.world().componentType<test_game::Counter>("Counter");
    std::int64_t value = -1;
    std::int64_t frames = -1;
    if (type)
        simulation.world().view(*type).each([&](scene::Entity, test_game::Counter &c) {
            value = c.value;
            frames = c.frames;
        });

    ui::FixedFont font;
    ui::Ui interface(font);
    HotState hot;
    bool viewLinked = false;
    {
        app::ViewHost views(interface, {.game = &*game});
        viewLinked = views.setRoot(cui::HotView{.state = &hot}).has_value() && !views.reloadable() && interface.find("press") != nullptr;
        views.update();
    }
    viewLinked = viewLinked && interface.root() == nullptr;

    game->stop();
    const bool unregistered = simulation.systemOrder().empty() && !actions.contains("game.v1");

    if (value != 3 || frames != 1 || !reloadRefused || !actionsRegistered || !unregistered || !viewLinked)
    {
        std::fprintf(stderr, "test_game_static: unexpected state (value %lld, frames %lld, reload refused %d, actions %d, "
                             "unregistered %d, linked view %d)\n",
                     static_cast<long long>(value), static_cast<long long>(frames), reloadRefused, actionsRegistered,
                     unregistered, viewLinked);
        return 1;
    }
    std::printf("test_game_static: OK (static game and linked view, no hot reload code linked)\n");
    return 0;
}
