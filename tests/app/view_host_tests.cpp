// UI-08 (A79 lot L8) : vue .cui compilée dans la bibliothèque de jeu, recréée après son rechargement en gardant focus, défilement
// et sections ouvertes ; structure changée ou construction en échec : ancienne vue gardée, message affiché.
// Bibliothèques de test : tests/app/view_module/game.cpp + le C++ généré de tests/app/view_module/<v>/HotView.cui
// (`bin/tests/test_view_<v><ext>`, apps/demo/Makefile). Ce fichier est compilé comme un hôte de développement : la vue n'est pas
// liée dans l'exécutable des tests, elle est retrouvée dans la bibliothèque chargée.
#define OROSMO_CUI_RELOADABLE 1

#include <doctest/doctest.h>

#include "tests/app/view_module/v1/HotView.gen.h" // structure de la vue telle que l'hôte la connaît (v1)
#include "ui/ui_harness.hpp"

#include <orosmo/app/game_module.hpp>
#include <orosmo/app/view_host.hpp>
#include <orosmo/core/system.hpp>
#include <orosmo/platform/dynamic_library.hpp>
#include <orosmo/sim/simulation.hpp>
#include <orosmo/ui/layout.hpp>
#include <orosmo/ui/view_state.hpp>
#include <orosmo/ui/widgets.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

using namespace orosmo;
using core::ErrorCode;
namespace fs = std::filesystem;

namespace
{

fs::path builtView(const std::string &variant)
{
    return core::system::executablePath().parent_path() /
           ("test_view_" + variant + std::string(platform::DynamicLibrary::extension()));
}

// Dossier propre par test : la bibliothèque « game<ext> » y est remplacée comme le ferait l'éditeur de liens.
struct ViewFolder
{
    fs::path dir;
    fs::path library;

    explicit ViewFolder(const std::string &name)
    {
        dir = core::system::executablePath().parent_path() / "view_host_tests" / name;
        fs::remove_all(dir);
        fs::create_directories(dir);
        library = dir / ("game" + std::string(platform::DynamicLibrary::extension()));
    }
    ~ViewFolder()
    {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    void install(const std::string &variant)
    {
        const fs::path source = builtView(variant);
        REQUIRE_MESSAGE(fs::exists(source), "test view library not built: " << source.string());
        fs::copy_file(source, library, fs::copy_options::overwrite_existing);
    }
};

struct Host
{
    sim::Simulation simulation;
    tests::UiHarness harness;
    HotState state;

    core::Result<app::GameModule> load(const fs::path &library)
    {
        return app::GameModule::load(
            {.path = library, .targets = {.simulation = &simulation, .actions = &harness.ui.actions()}, .watch = false});
    }
};

bool hasErrorNotification(const ui::Ui &ui, const std::string &part)
{
    for (const ui::Notification &n : ui.notifications())
        if (n.kind == ui::NotificationKind::kError && n.text.find(part) != std::string::npos)
            return true;
    return false;
}

// Appelle `views.update()` jusqu'à la fin de la construction lancée (au plus `seconds`).
void waitForBuild(app::ViewHost &views, std::uint32_t failuresBefore, double seconds = 20.0)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (std::chrono::steady_clock::now() < deadline)
    {
        views.update();
        if (views.builds() > 0 && !views.building())
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    FAIL("build not finished (failures " << views.failures() << ", before " << failuresBefore << ")");
}

} // namespace

TEST_SUITE("app/view_host")
{
    TEST_CASE("a view compiled into the game library is rebuilt after a reload, focus, scrolling and sections kept")
    {
        ViewFolder folder("rebuild");
        folder.install("v1");
        Host host;
        auto game = host.load(folder.library);
        REQUIRE_MESSAGE(game.has_value(), game.error().describe());
        ui::Ui &ui = host.harness.ui;
        app::ViewHost views(ui, {.game = &*game});
        auto mounted = views.setRoot(cui::HotView{.state = &host.state});
        REQUIRE_MESSAGE(mounted.has_value(), mounted.error().describe());
        CHECK(views.reloadable());
        host.harness.frame();
        const std::size_t widgetsV1 = ui::countWidgets(*ui.root());
        CHECK(ui.find("added") == nullptr);

        // État d'interaction de la v1 : focus clavier visible, défilement, section repliée ; état lié (côté hôte) modifié.
        auto *list = dynamic_cast<ui::ScrollView *>(ui.find("list"));
        REQUIRE(list != nullptr);
        REQUIRE(list->maxScroll() > 10.0f);
        list->setScroll(10.0f);
        dynamic_cast<ui::Section *>(ui.find("more"))->setExpanded(false);
        ui.focus(ui.find("press"));
        ui.setFocusVisible(true);
        host.state.presses = 3;
        host.harness.frame();

        folder.install("v2");
        REQUIRE(game->reload().has_value());
        views.update();
        CHECK(views.rebuilds() == 1);
        CHECK(views.failures() == 0);
        CHECK(ui::countWidgets(*ui.root()) == widgetsV1 + 1);
        CHECK(ui.find("added") != nullptr);
        REQUIRE(ui.focused() != nullptr);
        CHECK(ui.focused()->id() == "press");
        CHECK(ui.focusVisible());
        host.harness.frame();
        CHECK(dynamic_cast<ui::ScrollView *>(ui.find("list"))->scroll() == doctest::Approx(10.0f));
        CHECK_FALSE(dynamic_cast<ui::Section *>(ui.find("more"))->expanded());

        // Le nouvel arbre (code de la v2) écrit dans l'état de l'hôte, gardé.
        auto *press = ui.find("press");
        host.harness.click(press->rect().position + press->rect().size * 0.5f);
        CHECK(host.state.presses == 4);

        // Un rechargement sans changement de la vue la recrée aussi (le code de l'ancienne version part avec l'ancien arbre).
        REQUIRE(game->reload().has_value());
        views.update();
        CHECK(views.rebuilds() == 2);
        views.update(); // rien de neuf
        CHECK(views.rebuilds() == 2);
    }

    TEST_CASE("a library built with another view structure keeps the previous view (and its code), message shown")
    {
        ViewFolder folder("mismatch");
        folder.install("v1");
        Host host;
        auto game = host.load(folder.library);
        REQUIRE(game.has_value());
        ui::Ui &ui = host.harness.ui;
        app::ViewHost views(ui, {.game = &*game});
        REQUIRE(views.setRoot(cui::HotView{.state = &host.state}).has_value());
        host.harness.frame();
        const ui::Widget *before = ui.root();
        const std::size_t widgets = ui::countWidgets(*before);

        folder.install("v3");
        REQUIRE(game->reload().has_value()); // la logique de jeu passe à la v3
        views.update();
        CHECK(views.rebuilds() == 0);
        CHECK(views.failures() == 1);
        REQUIRE(views.lastError().has_value());
        CHECK((views.lastError()->code == ErrorCode::kVersionMismatch));
        CHECK(ui.root() == before);
        CHECK(ui::countWidgets(*ui.root()) == widgets);
        CHECK(hasErrorNotification(ui, "HotView"));

        // L'ancien arbre (code de la v1, déchargé par le module de jeu) reste utilisable : sa version est retenue.
        auto *press = ui.find("press");
        host.harness.click(press->rect().position + press->rect().size * 0.5f);
        CHECK(host.state.presses == 1);
        views.update(); // échec signalé une fois par version
        CHECK(views.failures() == 1);

        // Retour à une structure compatible : la vue est recréée.
        folder.install("v2");
        REQUIRE(game->reload().has_value());
        views.update();
        CHECK(views.rebuilds() == 1);
        CHECK(ui.find("added") != nullptr);
    }

    TEST_CASE("saving a .cui runs the build command; a failed build keeps the view and shows the error")
    {
        ViewFolder folder("build");
        folder.install("v1");
        const fs::path sources = folder.dir / "src";
        fs::create_directories(sources);
        const fs::path cui = sources / "HotView.cui";
        std::ofstream(cui) << "view HotView {}\n";
        Host host;
        auto game = host.load(folder.library);
        REQUIRE(game.has_value());
        ui::Ui &ui = host.harness.ui;
        app::ViewHost views(ui, {.game = &*game,
                                 .sources = sources,
                                 .buildCommand = "echo HotView.cui:3:5: error: unknown component Lable && exit 3",
                                 .buildDirectory = folder.dir,
                                 .settleDelay = std::chrono::milliseconds(0),
                                 .pollInterval = std::chrono::milliseconds(0)});
        REQUIRE(views.setRoot(cui::HotView{.state = &host.state}).has_value());
        const ui::Widget *before = ui.root();
        views.update();
        CHECK(views.builds() == 0); // rien n'a changé

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        std::ofstream(cui, std::ios::app) << "// edited, longer\n";
        waitForBuild(views, 0);
        CHECK(views.builds() == 1);
        CHECK(views.failures() == 1);
        REQUIRE(views.lastError().has_value());
        CHECK(views.lastError()->message.find("error: unknown component Lable") != std::string::npos);
        CHECK(views.lastBuildOutput().find("HotView.cui:3:5") != std::string::npos);
        CHECK(hasErrorNotification(ui, "unknown component"));
        CHECK(ui.root() == before);
        CHECK(views.rebuilds() == 0);
    }

    TEST_CASE("a successful build only waits for the game library reload")
    {
        ViewFolder folder("build-ok");
        folder.install("v1");
        const fs::path sources = folder.dir / "src";
        fs::create_directories(sources);
        const fs::path cui = sources / "HotView.cui";
        std::ofstream(cui) << "view HotView {}\n";
        Host host;
        auto game = host.load(folder.library);
        REQUIRE(game.has_value());
        app::ViewHost views(host.harness.ui, {.game = &*game,
                                              .sources = sources,
                                              .buildCommand = "echo built",
                                              .settleDelay = std::chrono::milliseconds(0),
                                              .pollInterval = std::chrono::milliseconds(0)});
        REQUIRE(views.setRoot(cui::HotView{.state = &host.state}).has_value());
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        std::ofstream(cui, std::ios::app) << "// edited, longer\n";
        waitForBuild(views, 0);
        CHECK(views.failures() == 0);
        CHECK(views.lastBuildOutput().find("built") != std::string::npos);
        CHECK(views.rebuilds() == 0); // la bibliothèque n'a pas changé

        folder.install("v2"); // ce que la commande aurait produit
        REQUIRE(game->reload().has_value());
        views.update();
        CHECK(views.rebuilds() == 1);
    }

    TEST_CASE("without a running game module, a view compiled into the library cannot be built")
    {
        tests::UiHarness harness;
        HotState state;
        app::ViewHost views(harness.ui);
        auto mounted = views.setRoot(cui::HotView{.state = &state});
        REQUIRE_FALSE(mounted.has_value());
        CHECK((mounted.error().code == ErrorCode::kInvalidArgument));
        CHECK(harness.ui.root() == nullptr);
    }

    TEST_CASE("the view tree is removed before the library that built it is released")
    {
        ViewFolder folder("lifetime");
        folder.install("v1");
        Host host;
        auto game = host.load(folder.library);
        REQUIRE(game.has_value());
        {
            app::ViewHost views(host.harness.ui, {.game = &*game});
            REQUIRE(views.setRoot(cui::HotView{.state = &host.state}).has_value());
            game->stop(); // le module déchargerait sa bibliothèque : l'arbre la retient
            CHECK(host.harness.ui.root() != nullptr);
            host.harness.frame(); // l'arbre construit par la bibliothèque arrêtée fonctionne encore
        }
        CHECK(host.harness.ui.root() == nullptr);
    }
}
