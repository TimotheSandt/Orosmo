// UI-08 (A79 lot L8) : vue .cui compilée dans la bibliothèque de jeu, recréée après son rechargement en gardant focus, défilement
// et sections ouvertes ; structure changée ou construction en échec : ancienne vue gardée, message affiché.
// Bibliothèques de test : tests/app/view_module/game.cpp + le C++ généré de tests/app/view_module/<v>/HotView.cui
// (`bin/tests/test_view_<v><ext>`, apps/demo/Makefile). Ce fichier est compilé comme un hôte de développement : la vue n'est pas
// liée dans l'exécutable des tests, elle est retrouvée dans la bibliothèque chargée.
#define OROSMO_CUI_RELOADABLE 1

#include <doctest/doctest.h>

#include "tests/app/view_module/v1/HotView.gen.h" // structure de la vue telle que l'hôte la connaît (v1)
#include "renderer/repo_paths.hpp"
#include "rhi/backends.hpp"
#include "ui/ui_harness.hpp"

#include <orosmo/app/application.hpp>
#include <orosmo/app/default_game.hpp>
#include <orosmo/app/game_module.hpp>
#include <orosmo/app/view_host.hpp>
#include <orosmo/core/system.hpp>
#include <orosmo/platform/dynamic_library.hpp>
#include <orosmo/sim/simulation.hpp>
#include <orosmo/ui/layout.hpp>
#include <orosmo/ui/view_state.hpp>
#include <orosmo/ui/widgets.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <string>
#include <utility>

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

// Construction pilotée par le test : ni processus, ni thread, ni horloge (le processus réel est vu par tools/e2e_view_reload.py).
struct FakeBuild
{
    std::promise<core::Result<platform::CommandResult>> promise;
    std::uint32_t started = 0;
    std::string command;

    app::ViewHostSettings settings(app::GameModule &game, const fs::path &sources)
    {
        return {.game = &game,
                .sources = sources,
                .buildCommand = "make game",
                .settleDelay = std::chrono::milliseconds(0),
                .pollInterval = std::chrono::milliseconds(0),
                .startBuild = [this](const std::string &line, const fs::path &) {
                    ++started;
                    command = line;
                    promise = {};
                    return promise.get_future();
                }};
    }
    void finish(int exitCode, std::string output)
    {
        promise.set_value(platform::CommandResult{.exitCode = exitCode, .output = std::move(output)});
    }
};

// Un .cui observé ; `touch` change sa taille (vu au prochain `update`, sans dépendre de la date du fichier).
struct Sources
{
    fs::path dir;
    fs::path cui;

    explicit Sources(const ViewFolder &folder) : dir(folder.dir / "src"), cui(dir / "HotView.cui")
    {
        fs::create_directories(dir);
        std::ofstream(cui) << "view HotView {}\n";
    }
    void touch() const { std::ofstream(cui, std::ios::app) << "// edited, longer\n"; }
};

// Même structure que cui::HotView (nom, empreinte, disposition), mais construite par le test : la forme liée de
// `Application::setView` (bibliothèque de jeu absente) appelle ce `build`, la forme rechargeable celui de la bibliothèque. Le C++
// généré de HotView n'est pas lié dans l'exécutable des tests (il définirait les mêmes symboles que les bibliothèques v2, v3).
struct LinkedHotView
{
    static constexpr const char *cui_name = cui::HotView::cui_name;
    static constexpr std::uint64_t cui_bindings = cui::HotView::cui_bindings;
    HotState *state = nullptr;

    std::unique_ptr<ui::Widget> build(ui::Ui &) const
    {
        auto label = std::make_unique<ui::Label>("linked");
        label->setId("linked");
        return label;
    }
};
static_assert(sizeof(LinkedHotView) == sizeof(cui::HotView));

app::ApplicationSettings uiSettings(rhi::Backend backend)
{
    const fs::path repo = tests::repoRoot(__FILE__);
    app::ApplicationSettings s;
    s.width = 64;
    s.height = 64;
    s.backend = backend;
    s.backendFallback = false;
    s.headless = true;
    s.resourceDirectory = repo / "res";
    s.shaderDirectory = repo / "shaders" / "rhi";
    s.frames = 0;
    return s;
}

// Vue de même nom que HotView mais d'une autre structure : refusée par toute bibliothèque de test.
struct OtherStructureView
{
    static constexpr const char *cui_name = "HotView";
    static constexpr std::uint64_t cui_bindings = 1;
};

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
        const Sources sources(folder);
        Host host;
        auto game = host.load(folder.library);
        REQUIRE(game.has_value());
        ui::Ui &ui = host.harness.ui;
        FakeBuild build;
        app::ViewHost views(ui, build.settings(*game, sources.dir));
        REQUIRE(views.setRoot(cui::HotView{.state = &host.state}).has_value());
        const ui::Widget *before = ui.root();
        views.update();
        CHECK(views.builds() == 0); // rien n'a changé

        sources.touch();
        views.update();
        CHECK(views.builds() == 1);
        CHECK(build.started == 1);
        CHECK(build.command == "make game");
        CHECK(views.building());
        build.finish(3, "HotView.cui:3:5: error: unknown component Lable\n");
        views.update();
        CHECK_FALSE(views.building());
        CHECK(views.failures() == 1);
        REQUIRE(views.lastError().has_value());
        CHECK(views.lastError()->message.find("error: unknown component Lable") != std::string::npos);
        CHECK(views.lastBuildOutput().find("HotView.cui:3:5") != std::string::npos);
        CHECK(hasErrorNotification(ui, "unknown component"));
        CHECK(ui.root() == before);
        CHECK(views.rebuilds() == 0);
    }

    TEST_CASE("a library loaded while a build that fails is running does not replace the view")
    {
        ViewFolder folder("build-race");
        folder.install("v1");
        const Sources sources(folder);
        Host host;
        auto game = host.load(folder.library);
        REQUIRE(game.has_value());
        ui::Ui &ui = host.harness.ui;
        FakeBuild build;
        app::ViewHost views(ui, build.settings(*game, sources.dir));
        REQUIRE(views.setRoot(cui::HotView{.state = &host.state}).has_value());
        const ui::Widget *before = ui.root();
        sources.touch();
        views.update();
        REQUIRE(views.building());

        folder.install("v2"); // liée par la commande, qui échoue ensuite
        REQUIRE(game->reload().has_value());
        views.update();
        CHECK(views.rebuilds() == 0);
        CHECK(ui.root() == before);
        build.finish(2, "check: error\n");
        views.update();
        views.update();
        CHECK(views.failures() == 1);
        CHECK(views.rebuilds() == 0);
        CHECK(ui.root() == before);

        sources.touch(); // la construction suivante réussit : sa bibliothèque est prise
        views.update();
        REQUIRE(views.building());
        REQUIRE(game->reload().has_value());
        build.finish(0, "built\n");
        views.update();
        CHECK(views.rebuilds() == 1);
        CHECK(ui.find("added") != nullptr);
    }

    TEST_CASE("a successful build only waits for the game library reload")
    {
        ViewFolder folder("build-ok");
        folder.install("v1");
        const Sources sources(folder);
        Host host;
        auto game = host.load(folder.library);
        REQUIRE(game.has_value());
        FakeBuild build;
        app::ViewHost views(host.harness.ui, build.settings(*game, sources.dir));
        REQUIRE(views.setRoot(cui::HotView{.state = &host.state}).has_value());
        sources.touch();
        views.update();
        build.finish(0, "built\n");
        views.update();
        CHECK(views.failures() == 0);
        CHECK(views.lastBuildOutput().find("built") != std::string::npos);
        CHECK(views.rebuilds() == 0); // la bibliothèque n'a pas changé

        folder.install("v2"); // ce que la commande aurait produit
        REQUIRE(game->reload().has_value());
        views.update();
        CHECK(views.rebuilds() == 1);
    }

    TEST_CASE("a failed setRoot leaves the previous view following the game library")
    {
        ViewFolder folder("restore-version");
        folder.install("v1");
        Host host;
        auto game = host.load(folder.library);
        REQUIRE(game.has_value());
        ui::Ui &ui = host.harness.ui;
        app::ViewHost views(ui, {.game = &*game});
        REQUIRE(views.setRoot(cui::HotView{.state = &host.state}).has_value());
        folder.install("v2");
        REQUIRE(game->reload().has_value());
        auto other = views.setRoot(OtherStructureView{}); // avant le prochain update
        REQUIRE_FALSE(other.has_value());
        CHECK((other.error().code == ErrorCode::kVersionMismatch));
        views.update();
        CHECK(views.rebuilds() == 1);
        CHECK(ui.find("added") != nullptr);
    }

    TEST_CASE("a first view that cannot be built is mounted by a later library version")
    {
        ViewFolder folder("first-mount");
        folder.install("v3"); // autre structure que celle de l'hôte
        Host host;
        auto game = host.load(folder.library);
        REQUIRE(game.has_value());
        ui::Ui &ui = host.harness.ui;
        app::ViewHost views(ui, {.game = &*game});
        REQUIRE_FALSE(views.setRoot(cui::HotView{.state = &host.state}).has_value());
        CHECK(ui.root() == nullptr);
        views.update();
        CHECK(views.failures() == 1); // une fois par version

        folder.install("v1");
        REQUIRE(game->reload().has_value());
        views.update();
        CHECK(views.failures() == 1);
        CHECK(views.rebuilds() == 1);
        REQUIRE(ui.root() != nullptr);
        CHECK(ui.find("press") != nullptr);
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

// Suite séparée : crée un périphérique (GPU_TEST_SUITES, Makefile).
TEST_SUITE("app/view_host/gpu")
{
    TEST_CASE("Application::setView (A79 L14, G365): view built by the game library and rebuilt after its reload; linked without it")
    {
        for (const rhi::Backend backend : tests::backendsToRun())
        {
            CAPTURE(tests::backendName(backend));
            ViewFolder folder("application");
            folder.install("v1");
            HotState state; // lié par la vue : vit plus longtemps que l'application
            {
                auto settings = uiSettings(backend);
                settings.game = app::GameSource::fromLibrary(folder.library, false);
                settings.game->required = true;
                settings.views.game = nullptr; // remplacé par le module de l'application
                auto created = app::Application::create(settings);
                REQUIRE_MESSAGE(created.has_value(), created.error().describe());
                app::Application &a = **created;
                REQUIRE(a.game() != nullptr);
                auto mounted = a.setView(LinkedHotView{.state = &state});
                REQUIRE_MESSAGE(mounted.has_value(), mounted.error().describe());
                CHECK(a.views().reloadable());
                CHECK(a.ui().find("press") != nullptr); // construite par la bibliothèque, pas par LinkedHotView::build
                CHECK(a.ui().find("linked") == nullptr);

                folder.install("v2"); // un widget de plus ; rechargée, puis l'arbre est recréé par l'image suivante
                REQUIRE(a.game()->reload().has_value());
                REQUIRE(a.frame().has_value());
                CHECK(a.views().rebuilds() == 1);
                CHECK(a.ui().find("press") != nullptr);
                CHECK(a.device().validationErrorCount() == 0);
            } // l'arbre part avant la bibliothèque qui l'a construit (sinon : code déchargé appelé à la destruction)

            { // jeu lié statiquement : pas de bibliothèque d'où recharger, la vue liée aussi
                static constexpr OrosmoGameApi kStatic{app::kGameAbiVersion, "static", [](app::GameContext *) { return true; },
                                                            nullptr, nullptr, nullptr};
                auto settings = uiSettings(backend);
                settings.game = app::GameSource::fromApi(&kStatic);
                settings.game->required = true;
                auto created = app::Application::create(settings);
                REQUIRE_MESSAGE(created.has_value(), created.error().describe());
                app::Application &a = **created;
                REQUIRE(a.game() != nullptr);
                auto mounted = a.setView(LinkedHotView{.state = &state});
                REQUIRE_MESSAGE(mounted.has_value(), mounted.error().describe());
                CHECK_FALSE(a.views().reloadable());
                CHECK(a.ui().find("linked") != nullptr);
            }

            auto settings = uiSettings(backend); // sans bibliothèque de jeu : la vue liée dans l'exécutable
            auto created = app::Application::create(settings);
            REQUIRE_MESSAGE(created.has_value(), created.error().describe());
            app::Application &a = **created;
            REQUIRE(a.game() == nullptr);
            auto mounted = a.setView(LinkedHotView{.state = &state});
            REQUIRE_MESSAGE(mounted.has_value(), mounted.error().describe());
            CHECK_FALSE(a.views().reloadable());
            CHECK(a.ui().find("linked") != nullptr);
            REQUIRE(a.frame().has_value());
        }
    }

}

TEST_SUITE("app/view_host")
{
    TEST_CASE("defaultViews and defaultSettings (A79 L14): the .cui of <project>/src rebuilt by make game in <project>")
    {
        const fs::path bin = core::system::executablePath().parent_path(); // bin/tests
        const app::ViewHostSettings views = app::defaultViews();
        CHECK(views.sources == bin.parent_path().parent_path() / "src");
        CHECK(views.buildCommand == "make game BUILD_TYPE=" + bin.filename().string());
        CHECK(views.buildDirectory == bin.parent_path().parent_path());
        CHECK(views.game == nullptr);

        const app::ApplicationSettings settings = app::defaultSettings("Starter");
        CHECK(settings.title == "Starter");
        CHECK(settings.game.has_value());
        CHECK(settings.views.buildCommand == views.buildCommand);
#if defined(DEBUG)
        CHECK(settings.validation);
        CHECK(settings.logFile == fs::path("logs/log.log"));
#else
        CHECK_FALSE(settings.validation);
        CHECK(settings.logFile.empty());
#endif
    }
}
