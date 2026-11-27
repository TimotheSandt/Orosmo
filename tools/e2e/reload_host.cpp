// Hôte sans fenêtre ni GPU de `make e2e-init` (GAME-06, A79 lot L7), lié comme le src/main.cpp d'un projet créé par
// `orosmo init` : charge bin/debug/game.<ext> (src/game/ du gabarit), annonce « ready », puis attend le rechargement après
// la reconstruction de la bibliothèque (tools/e2e_reload.py modifie src/game/game.cpp : l'angle se met à décroître).
// Sortie : « ready », puis « reloaded » (code 0) ; sinon un message « error: … » et le code 1.
#include "game/spin.hpp"

#include <orosmo/app/game_module.hpp>
#include <orosmo/core/system.hpp>
#include <orosmo/platform/dynamic_library.hpp>

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

namespace
{
float angleOf(orosmo::sim::Simulation &simulation)
{
    float angle = 0.0f;
    if (auto spin = simulation.world().componentType<game::Spin>("Spin"))
        simulation.world().view(*spin).each([&](orosmo::scene::Entity, game::Spin &s) { angle = s.angleDegrees; });
    return angle;
}

int fail(const std::string &message)
{
    std::cout << "error: " << message << std::endl;
    return 1;
}
} // namespace

int main()
{
    using namespace std::chrono_literals;
    orosmo::sim::Simulation simulation;
    const auto library = orosmo::core::system::executablePath().parent_path() /
                         ("game" + std::string(orosmo::platform::DynamicLibrary::extension()));
    auto game = orosmo::app::GameModule::load({.path = library, .targets = {.simulation = &simulation}});
    if (!game)
        return fail(game.error().describe());

    float before = angleOf(simulation);
    simulation.step();
    if (!(angleOf(simulation) > before))
        return fail("the first version does not increase the angle");
    std::cout << "ready" << std::endl;

    const auto deadline = std::chrono::steady_clock::now() + 120s;
    while (std::chrono::steady_clock::now() < deadline)
    {
        game->update(0.01);
        if (game->failedReloads() != 0)
            return fail(game->lastError() ? game->lastError()->describe() : "reload failed");
        if (game->version() >= 2)
        {
            before = angleOf(simulation); // l'état (angle) est resté dans le monde
            if (before == 0.0f)
                return fail("component data lost by the reload");
            simulation.step();
            if (!(angleOf(simulation) < before))
                return fail("the reloaded code is not running");
            std::cout << "reloaded" << std::endl;
            return 0;
        }
        std::this_thread::sleep_for(5ms);
    }
    return fail("no reload within 120 s");
}
