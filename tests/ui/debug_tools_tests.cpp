// UI-04 et UI-10 (A79 lot L10) : outils de debug sur orosmo::ui. Console (commandes par nom, erreurs, journal), inspecteur
// d'interface (arbre et tailles calculées, surlignage), inspecteur d'entités (champs décrits à l'exécution, modifiables), rien
// quand les outils sont fermés (UI-13), historique des temps d'image.
// GCC 16 (MinGW) dévirtualise à tort `box->clearChildren()` vers Section::clearChildren (-Warray-bounds, faux positif, G34).
// Avant les inclusions : l'avertissement est rattaché à widgets.hpp, hors de la portée du pragma de test_support.hpp.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif

#include "ui_harness.hpp"

#include <orosmo/core/log.hpp>
#include <orosmo/core/reflection.hpp>
#include <orosmo/scene/world.hpp>
#include <orosmo/ui/debug_tools.hpp>
#include <orosmo/ui/frame_history.hpp>
#include <orosmo/ui/reflect.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

namespace
{
namespace ui = orosmo::ui;
namespace dbg = orosmo::ui::debug;
namespace core = orosmo::core;
namespace scene = orosmo::scene;

struct Gauge
{
    float speed = 1.0f;
    std::int32_t count = 3;
    bool active = false;
    double precise = 0.5;
    std::uint8_t level = 2;
};

struct Nested
{
    core::Vec3 offset{0.0f};
    Gauge gauge;
};
} // namespace

template <> struct orosmo::core::Reflect<Gauge>
{
    static constexpr auto fields()
    {
        return std::make_tuple(field("speed", &Gauge::speed, range(0.0, 10.0)), field("count", &Gauge::count), field("active", &Gauge::active),
                               field("precise", &Gauge::precise), field("level", &Gauge::level));
    }
};
template <> struct orosmo::core::Reflect<Nested>
{
    static constexpr auto fields() { return std::make_tuple(field("offset", &Nested::offset), field("gauge", &Nested::gauge)); }
};

namespace
{
// Tape `text` puis Entrée dans le champ (focus donné d'abord : le texte remplace la valeur sélectionnée).
void typeInto(ui::Ui &target, ui::Widget &field, const std::string &text, glm::vec2 viewport)
{
    target.focus(nullptr); // reprendre le focus resélectionne tout
    target.focus(&field);
    ui::InputFrame in;
    in.events.push_back({ui::kKeyNone, false, text});
    in.events.push_back({ui::kKeyEnter, false, {}});
    target.update(in, viewport);
    target.update(ui::InputFrame{}, viewport);
}

struct Tools
{
    ui::FixedFont font{0.5f};
    dbg::DebugTools tools{font};
    glm::vec2 viewport{1000.0f, 700.0f};

    Tools()
    {
        auto theme = ui::Theme::parse(tests::kTestTheme, {}, "test");
        REQUIRE(theme.has_value());
        tools.ui().themes().set(std::move(*theme));
    }
    void frame(const ui::InputFrame &in = {}) { tools.update(in, viewport); }
};
} // namespace

TEST_SUITE("ui/debug")
{
    TEST_CASE("console runs a registered command and reports an unknown one")
    {
        dbg::Console console;
        std::vector<std::string> received;
        REQUIRE(console
                    .add("echo", "echo <text...>",
                         [&](std::span<const std::string> args) -> core::Result<std::string>
                         {
                             received.assign(args.begin(), args.end());
                             std::string out;
                             for (const auto &a : args)
                                 out += (out.empty() ? "" : " ") + a;
                             return out;
                         })
                    .has_value());

        auto ran = console.execute(R"(echo hello "two words")");
        REQUIRE(ran.has_value());
        CHECK(*ran == "hello two words");
        CHECK(received == std::vector<std::string>{"hello", "two words"});
        REQUIRE(console.lines().size() >= 2);
        CHECK(console.lines()[console.lines().size() - 2].kind == dbg::ConsoleLineKind::kInput);
        CHECK(console.lines().back().text == "hello two words");

        auto unknown = console.execute("nope 1");
        REQUIRE_FALSE(unknown.has_value());
        CHECK((unknown.error().code == core::ErrorCode::kNotFound));
        CHECK(console.lines().back().kind == dbg::ConsoleLineKind::kError);
        CHECK(console.lines().back().text.find("unknown command 'nope'") != std::string::npos);

        auto failing = console.add("fail", "", [](std::span<const std::string>) -> core::Result<std::string>
                                   { return core::makeError(core::ErrorCode::kInvalidArgument, "bad argument"); });
        REQUIRE(failing.has_value());
        CHECK_FALSE(console.execute("fail").has_value());
        CHECK(console.lines().back().text == "bad argument");

        CHECK((console.add("echo", "", [](std::span<const std::string>) { return core::Result<std::string>{}; }).error().code ==
              core::ErrorCode::kAlreadyExists));
        CHECK((console.add("two words", "", [](std::span<const std::string>) { return core::Result<std::string>{}; }).error().code ==
              core::ErrorCode::kInvalidArgument));
        CHECK((console.execute("echo \"open").error().code == core::ErrorCode::kInvalidArgument));
        CHECK(console.execute("   ").value().empty()); // ligne vide : rien d'ajouté

        CHECK(console.execute("help").value().find("echo <text...>") != std::string::npos);
        CHECK(console.names() == std::vector<std::string>{"clear", "echo", "fail", "help"});
        (void)console.execute("clear");
        CHECK(console.lines().empty());
    }

    TEST_CASE("a console command keeps its state between two runs and the console cannot be copied")
    {
        static_assert(!std::is_copy_constructible_v<dbg::Console>);
        static_assert(!std::is_copy_assignable_v<dbg::Console>);
        static_assert(!std::is_move_constructible_v<dbg::Console>);
        static_assert(!std::is_move_assignable_v<dbg::Console>);

        dbg::Console console;
        REQUIRE(console
                    .add("count", "",
                         [n = 0](std::span<const std::string>) mutable -> core::Result<std::string> { return std::to_string(++n); })
                    .has_value());
        CHECK(console.execute("count").value() == "1");
        CHECK(console.execute("count").value() == "2");

        // La commande peut se retirer pendant son appel.
        REQUIRE(console
                    .add("once", "",
                         [&console, text = std::string("done")](std::span<const std::string>) -> core::Result<std::string>
                         {
                             CHECK(console.remove("once"));
                             return text;
                         })
                    .has_value());
        CHECK(console.execute("once").value() == "done");
        CHECK_FALSE(console.contains("once"));
    }

    TEST_CASE("console mirrors the engine log")
    {
        dbg::Console console;
        LOG_WARNING("debug console warning test");
        LOG_ERROR(3, "debug console error test");
        CHECK(console.pollLog());
        REQUIRE(console.lines().size() == 2);
        CHECK(console.lines()[0].text == "debug console warning test");
        CHECK(console.lines()[0].kind == dbg::ConsoleLineKind::kWarning);
        CHECK(console.lines()[1].kind == dbg::ConsoleLineKind::kError);
        CHECK_FALSE(console.pollLog());
    }

    TEST_CASE("the widget inspector lists the tree with the layout sizes")
    {
        tests::UiHarness h;
        auto root = std::make_unique<ui::Panel>();
        root->setId("root");
        auto &play = root->emplace<ui::Button>("Play");
        play.setId("play").setWidth(ui::SizeSpec::fixed(120.0f)).setHeight(ui::SizeSpec::fixed(30.0f));
        auto &hidden = root->emplace<ui::Label>("hidden");
        hidden.setVisible(false);
        h.ui.setRoot(std::move(root));
        h.frame();

        const auto tree = dbg::describeWidgetTree(*h.ui.root());
        REQUIRE(tree.size() == 3);
        CHECK(tree[0].type == "Panel");
        CHECK(tree[0].depth == 0);
        CHECK(tree[1].type == "Button");
        CHECK(tree[1].id == "play");
        CHECK(tree[1].depth == 1);
        CHECK(tree[1].rect == play.rect());
        CHECK(tree[1].rect.size == glm::vec2(120.0f, 30.0f));
        CHECK(tree[1].rect.position == glm::vec2(5.0f, 5.0f)); // marge intérieure du panneau (5)
        CHECK_FALSE(tree[2].visible);
        CHECK(dbg::describeWidget(tree[1]) == "  Button #play [button] 5,5 120x30");
        CHECK(dbg::describeWidget(tree[2]).ends_with("(hidden)"));
        CHECK(dbg::describeWidgetTree(*h.ui.root(), 2).size() == 2);

        // Dans les outils : onglet Widgets, une ligne par widget ; survoler la ligne surligne le widget inspecté.
        Tools t;
        t.tools.inspect(&h.ui);
        t.tools.open();
        t.tools.selectTab(dbg::DebugTab::kWidgets);
        t.frame();
        t.frame();
        REQUIRE(t.tools.widgetTree().size() == 3);
        ui::Widget *list = t.tools.ui().find("debugWidgetTree");
        REQUIRE(list != nullptr);
        REQUIRE(list->children().size() == 3);
        CHECK(t.tools.highlight() == nullptr);
        ui::InputFrame hover;
        hover.mouse = tests::UiHarness::center(*list->children()[1]);
        t.frame(hover);
        t.frame(hover);
        REQUIRE(t.tools.highlight() != nullptr);
        CHECK(*t.tools.highlight() == play.rect());
        {
            orosmo::renderer::DrawList drawn;
            t.tools.draw(drawn);
            CHECK_FALSE(drawn.empty());
        }
        // Sans ligne survolée : le widget survolé de l'interface inspectée.
        t.frame();
        h.moveTo(tests::UiHarness::center(play));
        t.frame();
        REQUIRE(t.tools.highlight() != nullptr);
        CHECK(*t.tools.highlight() == play.rect());
    }

    TEST_CASE("the pinned highlight follows its widget and drops when the widget disappears")
    {
        tests::UiHarness h;
        auto root = std::make_unique<ui::VBox>();
        ui::VBox *box = root.get();
        auto addButton = [box](const char *id) -> ui::Button &
        {
            auto &b = box->emplace<ui::Button>(id);
            b.setId(id).setHeight(ui::SizeSpec::fixed(30.0f));
            return b;
        };
        addButton("a");
        ui::Button &b = addButton("b");
        h.ui.setRoot(std::move(root));
        h.frame();

        Tools t;
        t.tools.inspect(&h.ui);
        t.tools.open();
        t.tools.selectTab(dbg::DebugTab::kWidgets);
        t.frame();
        t.frame();
        ui::Widget *list = t.tools.ui().find("debugWidgetTree");
        REQUIRE(list != nullptr);
        REQUIRE(list->children().size() == 3);
        ui::InputFrame press;
        press.mouse = tests::UiHarness::center(*list->children()[2]);
        press.mouseDown = true;
        t.frame(press);
        ui::InputFrame release = press;
        release.mouseDown = false;
        t.frame(release);
        t.frame(); // souris hors des outils : seul le surlignage gardé reste
        t.frame();
        REQUIRE(t.tools.highlight() != nullptr);
        CHECK(*t.tools.highlight() == b.rect());

        // Arbre inchangé : surlignage gardé.
        h.frame();
        t.frame();
        REQUIRE(t.tools.highlight() != nullptr);

        // Reconstruit avec le même nombre de lignes : le widget choisi a disparu.
        box->clearChildren();
        addButton("c");
        addButton("d");
        h.frame();
        t.frame();
        t.frame();
        CHECK(t.tools.highlight() == nullptr);
        for (const auto &row : list->children())
            CHECK(row->styleName() == "debugRow");
    }

    TEST_CASE("the runtime reflected panel edits fields through their description")
    {
        tests::UiHarness h;
        Nested object;
        const std::vector<core::FieldDesc> fields = core::describeFields<Nested>();
        bool alive = true;
        auto panel = ui::reflectedPanel([&]() -> void * { return alive ? &object : nullptr; }, fields, "n.");
        bool changed = false;
        panel->onChange([&](const ui::Event &) { changed = true; });
        h.ui.setRoot(std::move(panel));
        h.frame();

        REQUIRE(h.ui.find("n.gauge.speed") != nullptr);
        CHECK(dynamic_cast<ui::Slider *>(h.ui.find("n.gauge.speed")) != nullptr); // plage : curseur
        REQUIRE(h.ui.find("n.offset") != nullptr);
        auto *count = dynamic_cast<ui::NumberField *>(h.ui.find("n.gauge.count"));
        REQUIRE(count != nullptr);
        CHECK(count->text() == "3");
        typeInto(h.ui, *count, "42", h.viewport);
        CHECK(object.gauge.count == 42);
        CHECK(changed);

        auto *precise = dynamic_cast<ui::NumberField *>(h.ui.find("n.gauge.precise"));
        REQUIRE(precise != nullptr);
        typeInto(h.ui, *precise, "0.1", h.viewport);
        CHECK(object.gauge.precise == 0.1); // double gardé en double

        auto *level = dynamic_cast<ui::NumberField *>(h.ui.find("n.gauge.level"));
        REQUIRE(level != nullptr);
        typeInto(h.ui, *level, "200", h.viewport);
        CHECK(object.gauge.level == 200);
        typeInto(h.ui, *level, "300", h.viewport);
        CHECK(object.gauge.level == 200); // hors du type (uint8) : refusé

        auto *active = dynamic_cast<ui::Checkbox *>(h.ui.find("n.gauge.active"));
        REQUIRE(active != nullptr);
        h.click(tests::UiHarness::center(*active));
        CHECK(object.gauge.active);

        // Objet disparu : rien n'est lu ni écrit.
        alive = false;
        typeInto(h.ui, *count, "7", h.viewport);
        CHECK(object.gauge.count == 42);
    }

    TEST_CASE("the entity inspector modifies a component")
    {
        scene::World world;
        auto gauge = world.registerComponent<Gauge>("Gauge");
        REQUIRE(gauge.has_value());
        const scene::Entity first = world.create();
        const scene::Entity e = world.create();
        world.add(e, *gauge, Gauge{});
        REQUIRE(world.setTransform(e, {}).has_value());
        CHECK(dbg::describeEntity(world, e).find("Gauge") != std::string::npos);

        Tools t;
        t.tools.inspectWorld(&world);
        t.tools.open();
        t.tools.selectTab(dbg::DebugTab::kEntities);
        t.frame();
        ui::Widget *list = t.tools.ui().find("debugEntityList");
        REQUIRE(list != nullptr);
        REQUIRE(list->children().size() == 2);
        // Choix de l'entité par un clic sur sa ligne.
        ui::Widget *row = list->children()[1].get();
        ui::InputFrame press;
        press.mouse = tests::UiHarness::center(*row);
        press.mouseDown = true;
        t.frame(press);
        ui::InputFrame release = press;
        release.mouseDown = false;
        t.frame(release);
        CHECK(t.tools.selectedEntity() == e);
        t.frame();

        auto *count = dynamic_cast<ui::NumberField *>(t.tools.ui().find("Gauge.count"));
        REQUIRE(count != nullptr);
        REQUIRE(t.tools.ui().find("Transform.position") != nullptr);
        typeInto(t.tools.ui(), *count, "9", t.viewport);
        CHECK(world.get(e, *gauge)->count == 9);

        // Un composant ajouté ailleurs déplace les données : le panneau relit la nouvelle adresse.
        world.add(first, *gauge, Gauge{});
        typeInto(t.tools.ui(), *count, "11", t.viewport);
        CHECK(world.get(e, *gauge)->count == 11);
        CHECK(world.get(first, *gauge)->count == 3);

        // Entité détruite : le panneau est refait.
        world.destroy(e);
        t.frame();
        CHECK(t.tools.ui().find("Gauge.count") == nullptr);
    }

    TEST_CASE("closed debug tools neither lay out nor draw")
    {
        Tools t;
        orosmo::renderer::DrawList list;
        t.frame();
        t.tools.draw(list);
        CHECK(list.empty());
        CHECK(t.tools.ui().root()->rect().size == glm::vec2(0.0f)); // aucune mise en page
        CHECK_FALSE(t.tools.wantsMouse());

        ui::InputFrame f12;
        f12.shortcuts = {"F12"};
        CHECK(t.tools.handleToggle(f12));
        CHECK(t.tools.isOpen());
        t.frame(f12); // F12 n'est pas transmis aux widgets des outils
        t.tools.draw(list);
        CHECK_FALSE(list.empty());

        CHECK(t.tools.handleToggle(f12));
        CHECK_FALSE(t.tools.isOpen());
        list.clear();
        t.frame();
        t.tools.draw(list);
        CHECK(list.empty());
        CHECK_FALSE(t.tools.handleToggle(ui::InputFrame{}));
    }

    TEST_CASE("the console input runs the typed line")
    {
        Tools t;
        bool ran = false;
        REQUIRE(t.tools.console()
                    .add("ping", "ping",
                         [&](std::span<const std::string>) -> core::Result<std::string>
                         {
                             ran = true;
                             return std::string("pong");
                         })
                    .has_value());
        t.tools.open();
        t.tools.selectTab(dbg::DebugTab::kConsole);
        t.frame();
        auto *input = dynamic_cast<ui::TextField *>(t.tools.ui().find("debugConsoleInput"));
        REQUIRE(input != nullptr);
        typeInto(t.tools.ui(), *input, "ping", t.viewport);
        CHECK(ran);
        CHECK(input->text().empty()); // le champ est vidé après Entrée
        CHECK(t.tools.console().lines().back().text == "pong");
        t.frame();
        // La dernière ligne affichée est celle de la console.
        ui::Widget *lines = t.tools.ui().find("debugConsoleLines");
        REQUIRE(lines != nullptr);
        const ui::Widget *lastShown = nullptr;
        for (const auto &child : lines->children())
            if (child->visible())
                lastShown = child.get();
        REQUIRE(lastShown != nullptr);
        CHECK(dynamic_cast<const ui::Label *>(lastShown)->text() == "pong");
    }

    TEST_CASE("debug tab names")
    {
        dbg::DebugTab tab = dbg::DebugTab::kConsole;
        CHECK(dbg::debugTabFromName("profiler", tab));
        CHECK(tab == dbg::DebugTab::kProfiler);
        CHECK_FALSE(dbg::debugTabFromName("nope", tab));
    }

    TEST_CASE("frame history computes the frame rate")
    {
        ui::FrameHistory history;
        CHECK(history.fps().average == 0.0);
        history.push(5.0f, 0.0f, 0.0f); // première image : sans intervalle
        for (int i = 0; i < 9; ++i)
            history.push(4.0f, 2.0f, 10.0f, 1.0f);
        history.push(4.0f, 2.0f, 20.0f, 1.0f);
        CHECK(history.size() == 11);
        const ui::FpsStats fps = history.fps();
        CHECK(fps.average == doctest::Approx(1000.0 * 10.0 / 110.0));
        CHECK(fps.min == doctest::Approx(50.0));
        CHECK(fps.max == doctest::Approx(100.0));
        CHECK(history.cpu().front() == 5.0f);
        for (std::size_t i = 0; i < ui::FrameHistory::kSize; ++i)
            history.push(1.0f, 0.0f, 1.0f);
        CHECK(history.size() == ui::FrameHistory::kSize);
        CHECK(history.averageCpuMs() == doctest::Approx(1.0));
    }
}
