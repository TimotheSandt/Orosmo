// Hôte sans fenêtre ni GPU de `make e2e-init` (UI-08, A79 lot L8), lié comme le src/main.cpp d'un projet créé par `orosmo init`
// (même Makefile : en debug, la vue src/HudView.cui est compilée dans bin/debug/game.<ext>). Il installe la vue par
// `app::ViewHost`, met le focus sur le bouton « reset », annonce « widgets N » puis « ready », et suit les reconstructions lancées
// à l'enregistrement d'un .cui (tools/e2e_view_reload.py) :
//   « build-failed widgets N » : la construction a échoué, l'ancienne vue est gardée (N widgets) ;
//   « rebuilt widgets M focus <id> » : la vue a été recréée depuis la bibliothèque rechargée ; sortie (code 0) si M != N.
// Erreur : « error: … » et le code 1.
#include "HudView.gen.h"
#include "hud_state.hpp"

#include <orosmo/app/game_module.hpp>
#include <orosmo/app/view_host.hpp>
#include <orosmo/core/system.hpp>
#include <orosmo/platform/dynamic_library.hpp>
#include <orosmo/sim/simulation.hpp>
#include <orosmo/ui/font.hpp>
#include <orosmo/ui/ui.hpp>
#include <orosmo/ui/view_state.hpp>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

namespace
{
int fail(const std::string &message)
{
    std::cout << "error: " << message << std::endl;
    return 1;
}
} // namespace

int main(int argc, char **argv)
{
    using namespace std::chrono_literals;
    namespace fs = std::filesystem;
    std::string build;
    for (int i = 1; i + 1 < argc; ++i)
        if (std::string_view(argv[i]) == "--build")
            build = argv[i + 1];
    if (build.empty())
        return fail("usage: main --build <command>");

    const fs::path bin = orosmo::core::system::executablePath().parent_path(); // <projet>/bin/debug
    const fs::path project = bin.parent_path().parent_path();
    orosmo::sim::Simulation simulation;
    orosmo::ui::FixedFont font;
    orosmo::ui::Ui interface(font);
    HudState state;
    auto game = orosmo::app::GameModule::load(
        {.path = bin / ("game" + std::string(orosmo::platform::DynamicLibrary::extension())),
         .targets = {.simulation = &simulation, .actions = &interface.actions()}});
    if (!game)
        return fail(game.error().describe());
    orosmo::app::ViewHost views(interface, {.game = &*game,
                                            .sources = project / "src",
                                            .buildCommand = build,
                                            .buildDirectory = project,
                                            .settleDelay = 100ms});
    if (auto mounted = views.setRoot(cui::HudView{.state = &state}); !mounted)
        return fail(mounted.error().describe());
    if (!views.reloadable())
        return fail("the view is linked in the executable (OROSMO_CUI_RELOADABLE not defined by the project Makefile)");
    interface.focus(interface.find("reset"));
    if (interface.focused() == nullptr)
        return fail("no focusable widget 'reset' in the view");
    const glm::vec2 viewport{1280.0f, 720.0f};
    interface.update({}, viewport);
    const std::size_t initial = orosmo::ui::countWidgets(*interface.root());
    std::cout << "widgets " << initial << std::endl;
    std::cout << "ready" << std::endl;

    std::uint32_t failures = 0;
    std::uint32_t rebuilds = 0;
    const auto deadline = std::chrono::steady_clock::now() + 180s;
    while (std::chrono::steady_clock::now() < deadline)
    {
        game->update(0.01);
        views.update();
        interface.update({}, viewport);
        if (game->failedReloads() != 0)
            return fail(game->lastError() ? game->lastError()->describe() : "game library reload failed");
        const std::size_t count = orosmo::ui::countWidgets(*interface.root());
        if (views.failures() != failures)
        {
            failures = views.failures();
            std::cout << "build-failed widgets " << count << std::endl;
        }
        if (views.rebuilds() != rebuilds)
        {
            rebuilds = views.rebuilds();
            const orosmo::ui::Widget *focused = interface.focused();
            std::cout << "rebuilt widgets " << count << " focus " << (focused ? focused->id() : std::string("none")) << std::endl;
            if (count != initial)
                return 0;
        }
        std::this_thread::sleep_for(5ms);
    }
    return fail("no rebuilt view within 180 s");
}
