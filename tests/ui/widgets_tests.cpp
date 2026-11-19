// UI-03 / UI-05 (côté exécution) / UI-07 : widgets de base pilotés par des entrées simulées : clics, glissements, clavier, focus et
// tabulation, liste déroulante, champs de texte, onglets, sections, infobulles ; événements nommés (onClick, onChange, onHover,
// onFocus) et registre d'actions ; écritures différées (visibles après `update`) ; état visuel lu dans le thème. Sans GPU.
#include "ui/ui_harness.hpp"

#include <engine/ui/reflect.hpp>

namespace ui = engine::ui;
namespace rd = engine::renderer;
using tests::UiHarness;

namespace
{
template <typename T> T &place(UiHarness &h, std::unique_ptr<T> w, glm::vec2 size = {100, 20})
{
    auto root = std::make_unique<ui::Overlay>();
    w->setAlign(ui::Align::kStart, ui::Align::kStart);
    w->setWidth(ui::SizeSpec::fixed(size.x)).setHeight(ui::SizeSpec::fixed(size.y));
    T &ref = root->add(std::move(w));
    h.ui.setRoot(std::move(root));
    h.frame();
    return ref;
}

std::vector<rd::DrawRect> rectsOf(const rd::DrawList &list)
{
    std::vector<rd::DrawRect> out;
    for (const auto &c : list.commands)
        if (const auto *r = std::get_if<rd::DrawRect>(&c))
            out.push_back(*r);
    return out;
}

std::size_t glyphCount(const rd::DrawList &list)
{
    std::size_t n = 0;
    for (const auto &c : list.commands)
        if (const auto *g = std::get_if<rd::DrawGlyphs>(&c))
            n += g->vertices.size() / 24;
    return n;
}
} // namespace

TEST_SUITE("ui/widgets")
{
    TEST_CASE("Button: click on release inside only; hover enter/leave; pressed and hover states come from the theme")
    {
        UiHarness h;
        std::vector<std::string> events;
        auto b = std::make_unique<ui::Button>("Go");
        b->onClick([&](const ui::Event &e) { events.push_back(e.source != nullptr ? "click" : "?"); });
        b->onHover([&](const ui::Event &e) { events.push_back(e.active ? "enter" : "leave"); });
        auto &button = place(h, std::move(b));

        h.moveTo({10, 10});
        CHECK(button.state() == ui::WidgetState::kHover);
        h.press({10, 10});
        CHECK(button.state() == ui::WidgetState::kPressed);
        CHECK(events == std::vector<std::string>{"enter"}); // pas de clic à l'appui
        h.release({10, 10});
        CHECK(events == std::vector<std::string>{"enter", "click"});
        // Appui dedans, relâché dehors : pas de clic.
        h.press({10, 10});
        h.release({300, 200});
        CHECK(events == std::vector<std::string>{"enter", "click", "leave"});
        // Désactivé : aucun clic, état « désactivé ».
        button.setEnabled(false);
        h.click({10, 10});
        CHECK(events.size() == 3);
        CHECK(button.state() == ui::WidgetState::kDisabled);

        // Le fond dessiné est celui du thème dans l'état courant ("button" : bg, survol : muted).
        button.setEnabled(true);
        h.moveTo({10, 10});
        rd::DrawList list;
        h.ui.draw(list);
        const auto rects = rectsOf(list);
        REQUIRE_FALSE(rects.empty());
        CHECK(rects.front().color == *ui::parseColor("#808080"));
        CHECK(glyphCount(list) == 2); // « Go »
    }

    TEST_CASE("focus: Tab walks the focusable widgets in tree order (Shift+Tab backwards); Enter clicks the focused button; onFocus")
    {
        UiHarness h;
        auto box = std::make_unique<ui::VBox>();
        int clicks = 0;
        std::vector<bool> focusEvents;
        auto &a = box->emplace<ui::Button>("a");
        a.onFocus([&](const ui::Event &e) { focusEvents.push_back(e.active); });
        box->emplace<ui::Label>("not focusable");
        auto &b = box->emplace<ui::Button>("b");
        b.onClick([&](const ui::Event &) { ++clicks; });
        auto &c = box->emplace<ui::Button>("c");
        c.setEnabled(false);
        h.ui.setRoot(std::move(box));
        h.frame();
        h.keys({ui::kKeyTab});
        CHECK(h.ui.focused() == &a);
        CHECK(a.state() == ui::WidgetState::kFocus);
        h.keys({ui::kKeyTab});
        CHECK(h.ui.focused() == &b); // le désactivé est sauté
        h.keys({ui::kKeyTab});
        CHECK(h.ui.focused() == &a);
        h.keys({ui::kKeyTab}, true);
        CHECK(h.ui.focused() == &b);
        h.keys({ui::kKeyEnter});
        CHECK(clicks == 1);
        CHECK(focusEvents == std::vector<bool>{true, false, true, false});
        // Un clic dans le vide retire le focus.
        h.click({399, 299});
        CHECK(h.ui.focused() == nullptr);
    }

    TEST_CASE("Checkbox: toggles its binding; the write is deferred to the end of update and onChange sees the new value")
    {
        UiHarness h;
        bool value = false;
        bool seen = false;
        auto cb = std::make_unique<ui::Checkbox>("Snow", ui::Binding<bool>::ref(value));
        cb->onChange([&](const ui::Event &) { seen = value; });
        auto &box = place(h, std::move(cb));
        h.click({5, 10});
        CHECK(value);
        CHECK(seen);
        CHECK(box.checked());
        h.keys({ui::kKeyTab});
        h.keys({ui::kKeySpace});
        CHECK_FALSE(value);
        // Lecture seule : rien ne change.
        auto ro = std::make_unique<ui::Checkbox>("ro", ui::Binding<bool>::constant(true));
        auto &readOnly = place(h, std::move(ro));
        h.click({5, 10});
        CHECK(readOnly.checked());
    }

    TEST_CASE("Slider: press and drag set the value from the mouse, quantized to the step; keys; read-only bindings do not move")
    {
        UiHarness h;
        int octaves = 1;
        int changes = 0;
        auto s = std::make_unique<ui::Slider>(ui::numberBinding(octaves), 0.0, 10.0, 1.0);
        s->onChange([&](const ui::Event &) { ++changes; });
        auto &slider = place(h, std::move(s), {100, 20});
        h.press({26, 10}); // 26 % → 2,6 → 3
        CHECK(octaves == 3);
        h.press({71, 10}); // glissement
        CHECK(octaves == 7);
        h.press({500, 10}); // au-delà : borné
        CHECK(octaves == 10);
        h.release({500, 10});
        CHECK(changes == 3);
        CHECK(slider.formatted() == "10");
        h.keys({ui::kKeyLeft}); // le slider a le focus depuis l'appui
        CHECK(octaves == 9);
        h.keys({ui::kKeyHome});
        CHECK(octaves == 0);

        float gain = 0.5f;
        auto s2 = std::make_unique<ui::Slider>(ui::numberBinding(gain), 0.0, 1.0, 0.05);
        auto &fine = place(h, std::move(s2), {100, 20});
        CHECK(fine.formatted() == "0.50");
        h.click({33, 10}); // 0,33 → 0,35
        CHECK(gain == doctest::Approx(0.35f));
        CHECK(fine.formatted() == "0.35");

        auto s3 = std::make_unique<ui::Slider>(ui::Binding<double>::constant(0.25), 0.0, 1.0);
        auto &ro = place(h, std::move(s3));
        h.click({90, 10});
        CHECK(ro.value() == doctest::Approx(0.25));
        CHECK(ro.formatted() == "0.25"); // continu : chiffres significatifs
    }

    TEST_CASE("Dropdown: a click opens the list above everything; choosing writes and closes; a click outside closes without change")
    {
        UiHarness h;
        int selected = 0;
        int changes = 0;
        auto d = std::make_unique<ui::Dropdown>(std::vector<std::string>{"Perlin", "Simplex", "Worley"}, ui::Binding<int>::ref(selected));
        d->onChange([&](const ui::Event &) { ++changes; });
        auto &dropdown = place(h, std::move(d), {100, 20});
        h.click({50, 10});
        REQUIRE(h.ui.popup() != nullptr);
        CHECK(h.ui.popupOpenFor(dropdown));
        CHECK(h.ui.popup()->rect().position.y == doctest::Approx(20)); // sous la liste déroulante
        CHECK(h.ui.popup()->rect().size.x == doctest::Approx(100));    // au moins aussi large
        // Troisième élément : 10 de haut chacun (texte, sans marge intérieure), espacement 4 : de y = 20 + 2 x 14 à + 10.
        h.click({50, 20 + 28 + 5});
        CHECK(selected == 2);
        CHECK(changes == 1);
        CHECK(h.ui.popup() == nullptr);

        h.click({50, 10});
        REQUIRE(h.ui.popup() != nullptr);
        h.click({350, 250});
        CHECK(h.ui.popup() == nullptr);
        CHECK(selected == 2);
        // Clavier : flèches avec le focus.
        h.keys({ui::kKeyUp});
        CHECK(selected == 1);
        CHECK(changes == 2);
        // Le dessin de la fenêtre surgissante vient après l'arbre.
        h.click({50, 10});
        rd::DrawList list;
        h.ui.draw(list);
        CHECK(list.count<rd::DrawRect>() >= 2);
        CHECK(h.ui.wantsMouse()); // la souris est sur la liste déroulante
    }

    TEST_CASE("TextField: typing, caret moves, Backspace/Delete, Enter commits once, Escape cancels, losing the focus commits")
    {
        UiHarness h;
        std::string name = "abc";
        int changes = 0;
        auto f = std::make_unique<ui::TextField>(ui::Binding<std::string>::ref(name));
        f->onChange([&](const ui::Event &) { ++changes; });
        auto &field = place(h, std::move(f), {200, 20});
        CHECK(field.text() == "abc");
        h.click({2 + 15 + 1, 10}); // après « abc » (5 px par caractère, marge 2)
        REQUIRE(h.ui.focused() == &field);
        CHECK(h.ui.wantsKeyboard());
        CHECK(field.caret() == 3);
        h.type("de");
        CHECK(field.text() == "abcde");
        CHECK(name == "abc"); // pas encore validé
        h.keys({ui::kKeyLeft, ui::kKeyLeft, ui::kKeyBackspace});
        CHECK(field.text() == "abde");
        h.keys({ui::kKeyDelete});
        CHECK(field.text() == "abe");
        h.keys({ui::kKeyHome});
        h.type("X");
        CHECK(field.text() == "Xabe");
        h.keys({ui::kKeyEnter});
        CHECK(name == "Xabe");
        CHECK(changes == 1);
        h.type("!");
        h.keys({ui::kKeyEscape});
        CHECK(name == "Xabe");
        CHECK(h.ui.focused() == nullptr);
        CHECK(changes == 1);
        // Perte du focus par un clic ailleurs : validation.
        h.click({10, 10});
        h.keys({ui::kKeyEnd});
        h.type("?");
        h.click({390, 290});
        CHECK(name == "Xabe?");
        CHECK(changes == 2);
        // Caractères de contrôle ignorés, UTF-8 gardé entier.
        h.click({10, 10});
        h.keys({ui::kKeyEnd});
        h.type("\x01\xc3\xa9");
        h.keys({ui::kKeyBackspace});
        CHECK(field.text() == "Xabe?");
    }

    TEST_CASE("NumberField: a valid number commits (rounded for an integer), an invalid or out-of-range entry is refused")
    {
        UiHarness h;
        double scale = 120.0;
        auto f = std::make_unique<ui::NumberField>(ui::numberBinding(scale), false, 0.0, 1000.0);
        auto &field = place(h, std::move(f), {200, 20});
        CHECK(field.text() == "120");
        h.click({10, 10});
        h.keys({ui::kKeyEnd, ui::kKeyBackspace, ui::kKeyBackspace, ui::kKeyBackspace});
        h.type(" 12.5 ");
        h.keys({ui::kKeyEnter});
        CHECK(scale == doctest::Approx(12.5));
        h.keys({ui::kKeyEnd, ui::kKeyBackspace, ui::kKeyBackspace, ui::kKeyBackspace, ui::kKeyBackspace, ui::kKeyBackspace, ui::kKeyBackspace});
        h.type("abc");
        h.keys({ui::kKeyEnter});
        CHECK(scale == doctest::Approx(12.5));
        CHECK(field.text() == "12.5"); // saisie refusée : texte rétabli
        h.keys({ui::kKeyHome});
        h.type("9");
        h.keys({ui::kKeyEnter}); // 912,5 : dans la plage
        CHECK(scale == doctest::Approx(912.5));
        h.keys({ui::kKeyHome});
        h.type("9");
        h.keys({ui::kKeyEnter}); // 9912,5 : hors plage
        CHECK(scale == doctest::Approx(912.5));

        std::uint32_t seed = 42;
        auto g = std::make_unique<ui::NumberField>(ui::numberBinding(seed), true, 0.0, 4294967295.0);
        auto &seedField = place(h, std::move(g), {200, 20});
        CHECK(seedField.text() == "42");
        h.click({10, 10});
        h.keys({ui::kKeyEnd, ui::kKeyBackspace, ui::kKeyBackspace});
        h.type("7.6");
        h.keys({ui::kKeyEnter});
        CHECK(seed == 8u);
    }

    TEST_CASE("ColorPicker: one slider per channel; a channel change is a change of the color")
    {
        UiHarness h;
        ui::Color color{0.0f, 0.5f, 1.0f, 1.0f};
        int changes = 0;
        auto p = std::make_unique<ui::ColorPicker>(ui::Binding<ui::Color>::ref(color));
        p->onChange([&](const ui::Event &) { ++changes; });
        auto &picker = place(h, std::move(p), {120, 60});
        auto *sliders = picker.children()[0].get();
        REQUIRE(sliders->children().size() == 3);
        const ui::Rect red = sliders->children()[0]->rect();
        h.click({red.position.x + red.size.x * 0.75f, red.position.y + 2});
        CHECK(color.r == doctest::Approx(0.75f).epsilon(0.02));
        CHECK(color.g == doctest::Approx(0.5f));
        CHECK(changes == 1);
    }

    TEST_CASE("Plot draws one bar per value, from zero")
    {
        UiHarness h;
        std::vector<float> values{1.0f, 2.0f, 0.0f, 4.0f};
        auto &plot = place(h, std::make_unique<ui::Plot>([&] { return values; }), {40, 40});
        (void)plot;
        rd::DrawList list;
        h.ui.draw(list);
        const auto rects = rectsOf(list);
        REQUIRE(rects.size() == 3); // la valeur nulle n'a pas de barre ; pas de fond (thème)
        CHECK(rects[0].size.y == doctest::Approx(10));
        CHECK(rects[2].size.y == doctest::Approx(40));
        CHECK(rects[2].position.y == doctest::Approx(0));
    }

    TEST_CASE("Tabs: only the selected page is visible; clicking a tab switches pages and emits onChange")
    {
        UiHarness h;
        auto tabs = std::make_unique<ui::Tabs>();
        auto &first = tabs->addPage("One", std::make_unique<ui::Label>("page one"));
        auto &second = tabs->addPage("Two", std::make_unique<ui::Label>("page two"));
        int changes = 0;
        tabs->onChange([&](const ui::Event &) { ++changes; });
        auto &t = place(h, std::move(tabs), {200, 100});
        CHECK(first.visible());
        CHECK_FALSE(second.visible());
        // Onglets : « One » (15 de large), « Two » à côté.
        h.click({15 + 4 + 5, 5});
        CHECK(t.selected() == 1);
        CHECK(second.visible());
        CHECK_FALSE(first.visible());
        CHECK(changes == 1);
    }

    TEST_CASE("Section: clicking the header folds and unfolds the body")
    {
        UiHarness h;
        auto section = std::make_unique<ui::Section>("Relief");
        section->body().emplace<ui::Label>("content");
        auto &s = place(h, std::move(section), {200, 100});
        CHECK(s.body().visible());
        h.click({5, 5});
        CHECK_FALSE(s.expanded());
        CHECK_FALSE(s.body().visible());
        h.click({5, 5});
        CHECK(s.body().visible());
    }

    TEST_CASE("tooltips appear after the delay on a still mouse and disappear when it leaves")
    {
        UiHarness h;
        auto b = std::make_unique<ui::Button>("?");
        b->setTooltip("help");
        place(h, std::move(b));
        h.ui.setTooltipDelay(0.5f);
        h.moveTo({5, 5});
        CHECK(h.ui.visibleTooltip().empty());
        h.wait(0.3f);
        CHECK(h.ui.visibleTooltip().empty());
        h.wait(0.3f);
        CHECK(h.ui.visibleTooltip() == "help");
        rd::DrawList list;
        h.ui.draw(list);
        CHECK(glyphCount(list) == 1 + 4); // « ? » et « help »
        h.moveTo({300, 200});
        CHECK(h.ui.visibleTooltip().empty());
    }

    TEST_CASE("ActionRegistry: late binding by name, missing actions listed, invoked in order with the other events")
    {
        UiHarness h;
        std::vector<std::string> log;
        auto b = std::make_unique<ui::Button>("x");
        b->onClick(h.ui.actions().bind("regenerate"));
        place(h, std::move(b));
        CHECK(h.ui.actions().missing() == std::vector<std::string>{"regenerate"});
        h.click({5, 5}); // absente : signalée une fois, sans effet
        h.ui.actions().add("regenerate", [&](const ui::Event &e) { log.push_back(e.kind == ui::EventKind::kClick ? "click" : "?"); });
        CHECK(h.ui.actions().missing().empty());
        h.click({5, 5});
        CHECK(log == std::vector<std::string>{"click"});
        CHECK(h.ui.actions().invoke("regenerate", {}));
        CHECK_FALSE(h.ui.actions().invoke("nothing", {}));
    }

    TEST_CASE("ui::handler calls the function with or without the event")
    {
        int a = 0;
        int b = 0;
        ui::handler([&] { ++a; })(ui::Event{});
        ui::handler([&](const ui::Event &e) { b += e.active ? 1 : 0; })(ui::Event{});
        CHECK(a == 1);
        CHECK(b == 1);
    }

    TEST_CASE("replacing the root from an event is applied after the frame (no dangling widget)")
    {
        UiHarness h;
        int built = 0;
        std::function<void()> rebuild;
        rebuild = [&]
        {
            auto b = std::make_unique<ui::Button>("rebuild");
            b->onClick([&](const ui::Event &) { rebuild(); });
            auto root = std::make_unique<ui::Overlay>();
            b->setAlign(ui::Align::kStart, ui::Align::kStart);
            root->add(std::move(b));
            h.ui.setRoot(std::move(root));
            ++built;
        };
        rebuild();
        h.frame();
        h.click({5, 5});
        h.click({5, 5});
        CHECK(built == 3);
    }

    TEST_CASE("local style overrides name a token, a color or a number; an unknown token is ignored")
    {
        UiHarness h;
        auto b = std::make_unique<ui::Button>("x");
        b->overrideStyle(ui::StyleProperty::kBackground, "accent");
        b->overrideStyle(ui::StyleProperty::kRadius, "4");
        b->overrideStyle(ui::StyleProperty::kBorder, "no.such.token");
        auto &button = place(h, std::move(b));
        const ui::Style s = button.style(h.ui.theme());
        CHECK(s.background == ui::Color(1, 0, 0, 1));
        CHECK(s.radius == 4.0f);
        rd::DrawList list;
        h.ui.draw(list);
        CHECK(rectsOf(list).front().color == ui::Color(1, 0, 0, 1));
    }

    TEST_CASE("wantsMouse: over a widget with a background or an interactive one, not over a transparent container")
    {
        UiHarness h;
        auto root = std::make_unique<ui::Overlay>();
        auto &panel = root->emplace<ui::Panel>();
        panel.setAlign(ui::Align::kStart, ui::Align::kStart);
        panel.emplace<ui::Label>("text");
        h.ui.setRoot(std::move(root));
        h.frame(); // les entrées d'une image visent la mise en page de l'image précédente (celle qui est affichée)
        h.moveTo({3, 3});
        CHECK(h.ui.wantsMouse());
        h.moveTo({300, 200});
        CHECK_FALSE(h.ui.wantsMouse());
    }

    TEST_CASE("Label text bound to an expression is read at every frame")
    {
        UiHarness h;
        int fps = 60;
        auto l = std::make_unique<ui::Label>();
        l->bindText([&] { return "FPS " + std::to_string(fps); });
        auto &label = place(h, std::move(l));
        CHECK(label.text() == "FPS 60");
        fps = 144;
        CHECK(label.text() == "FPS 144");
        h.frame();
        CHECK(label.measured().x == doctest::Approx(100)); // taille fixe du test
    }
}
