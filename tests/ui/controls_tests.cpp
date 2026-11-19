// UI-03 (suite) : boutons radio, curseur à deux poignées, champ numérique à boutons, sélection et presse-papiers des champs de
// texte, éditeur de texte sur plusieurs lignes. Entrées simulées, sans GPU.
#include "ui/ui_harness.hpp"

#include <engine/ui/controls.hpp>

namespace ui = engine::ui;
using tests::UiHarness;

namespace
{
template <typename T> T &place(UiHarness &h, std::unique_ptr<T> w, glm::vec2 size)
{
    auto root = std::make_unique<ui::Overlay>();
    w->setAlign(ui::Align::kStart, ui::Align::kStart);
    w->setWidth(ui::SizeSpec::fixed(size.x)).setHeight(ui::SizeSpec::fixed(size.y));
    T &ref = root->add(std::move(w));
    h.ui.setRoot(std::move(root));
    h.frame();
    return ref;
}
} // namespace

TEST_SUITE("ui/controls")
{
    TEST_CASE("RadioButton: one choice among several, bound to the same integer")
    {
        UiHarness h;
        int mode = 0;
        int changes = 0;
        auto box = std::make_unique<ui::VBox>();
        auto &a = box->emplace<ui::RadioButton>("A", 0, ui::Binding<int>::ref(mode));
        auto &b = box->emplace<ui::RadioButton>("B", 1, ui::Binding<int>::ref(mode));
        b.onChange([&](const ui::Event &) { ++changes; });
        h.ui.setRoot(std::move(box));
        h.frame();
        CHECK(a.checked());
        CHECK_FALSE(b.checked());
        h.click(UiHarness::center(b));
        CHECK(mode == 1);
        CHECK(b.checked());
        CHECK_FALSE(a.checked());
        h.click(UiHarness::center(b)); // déjà choisi : pas de changement
        CHECK(changes == 1);
        h.keys({ui::kKeyTab}); // focus sur A, Espace le choisit
        h.keys({ui::kKeySpace});
        CHECK(mode == 0);
    }

    TEST_CASE("RangeSlider: the nearest handle moves; a drag moves it from its current value; handles do not cross; right click = defaults")
    {
        UiHarness h;
        double lo = 20, hi = 80;
        auto &r = place(h, std::make_unique<ui::RangeSlider>(ui::numberBinding(lo), ui::numberBinding(hi), 0.0, 100.0, 1.0), {100, 20});
        CHECK(r.formatted() == "20 - 80");
        h.click({10, 10}); // plus près de la poignée basse : elle vient à 10
        CHECK(lo == 10);
        CHECK(hi == 80);
        h.click({90, 10});
        CHECK(hi == 90);
        // Glissement de la poignée haute (prise à 90) de -30 px : 60, quel que soit l'endroit exact de l'appui.
        h.press({88, 10});
        h.press({58, 10});
        h.release({58, 10});
        CHECK(hi == 60);
        // La basse ne passe pas au-dessus de la haute.
        h.press({10, 10});
        h.press({95, 10});
        h.release({95, 10});
        CHECK(lo == 60);
        h.secondaryClick({50, 10});
        CHECK(lo == 20);
        CHECK(hi == 80);
    }

    TEST_CASE("SpinBox: the - and + buttons and the wheel step the value within its bounds")
    {
        UiHarness h;
        int count = 9;
        auto &s = place(h, std::make_unique<ui::SpinBox>(ui::numberBinding(count), 0.0, 10.0, 1.0), {120, 20});
        const ui::Rect plus = s.children()[2]->rect();
        const ui::Rect minus = s.children()[1]->rect();
        h.click(plus.position + plus.size * 0.5f);
        h.frame();
        CHECK(count == 10);
        h.click(plus.position + plus.size * 0.5f);
        h.frame();
        CHECK(count == 10); // borné
        h.click(minus.position + minus.size * 0.5f);
        h.frame();
        CHECK(count == 9);
        h.wheel(UiHarness::center(s), -1.0f);
        CHECK(count == 8);
    }

    TEST_CASE("TextField: focus selects all; Shift+arrows and dragging select; copy, cut, paste; typing replaces the selection")
    {
        UiHarness h;
        std::string name = "hello world";
        auto &f = place(h, std::make_unique<ui::TextField>(ui::Binding<std::string>::ref(name)), {200, 20});
        h.keys({ui::kKeyTab});
        REQUIRE(h.ui.focused() == &f);
        CHECK(f.edit().selected() == "hello world");
        h.keys({ui::kKeyHome});
        h.frame(); // (pas de Maj)
        engine::ui::InputFrame in = h.input();
        in.keys = {ui::kKeyRight, ui::kKeyRight, ui::kKeyRight, ui::kKeyRight, ui::kKeyRight};
        in.shift = true;
        h.ui.update(in, h.viewport);
        CHECK(f.edit().selected() == "hello");
        h.keys({ui::kKeyCopy});
        CHECK(h.ui.clipboardText() == "hello");
        h.keys({ui::kKeyEnd});
        h.type(" ");
        h.keys({ui::kKeyPaste});
        CHECK(f.text() == "hello world hello");
        h.keys({ui::kKeySelectAll, ui::kKeyCut});
        CHECK(f.text().empty());
        CHECK(h.ui.clipboardText() == "hello world hello");
        h.type("abc");
        // Glisser de la position 0 à la fin sélectionne tout (2 + 5 px par caractère).
        h.press({2, 10});
        h.press({2 + 15, 10});
        h.release({2 + 15, 10});
        CHECK(f.edit().selected() == "abc");
        h.type("Z");
        CHECK(f.text() == "Z");
        h.keys({ui::kKeyEnter});
        CHECK(name == "Z");
        // Presse-papiers du système branché par l'application.
        std::string system = "from the OS";
        h.ui.setClipboard([&] { return system; }, [&](const std::string &t) { system = t; });
        h.keys({ui::kKeySelectAll, ui::kKeyPaste, ui::kKeyEnter});
        CHECK(name == "from the OS");
    }

    TEST_CASE("TextArea: several lines, Enter, Tab, Up/Down keep the column, selection across lines, the binding follows each edit")
    {
        UiHarness h;
        std::string text = "first\nsecond line\nthird";
        int changes = 0;
        auto t = std::make_unique<ui::TextArea>(ui::Binding<std::string>::ref(text));
        t->setLineNumbers(true);
        t->onChange([&](const ui::Event &) { ++changes; });
        auto &area = place(h, std::move(t), {300, 100});
        CHECK(area.text() == text);
        h.keys({ui::kKeyTab}); // prend le focus
        REQUIRE(h.ui.focused() == &area);
        h.keys({ui::kKeyEnd}); // fin de la 1re ligne (le curseur est en 0)
        CHECK(area.edit().caret == 5);
        h.keys({ui::kKeyDown}); // même colonne sur la ligne suivante
        CHECK(area.edit().caret == 6 + 5);
        h.keys({ui::kKeyTab}); // Tab insère des espaces au lieu de passer le focus
        CHECK(h.ui.focused() == &area);
        CHECK(text == "first\nsecon    d line\nthird");
        h.keys({ui::kKeyEnter});
        CHECK(text == "first\nsecon    \nd line\nthird");
        CHECK(changes == 2);
        h.keys({ui::kKeySelectAll, ui::kKeyCopy});
        CHECK(h.ui.clipboardText() == text);
        h.keys({ui::kKeyBackspace});
        CHECK(text.empty());
        h.type("a");
        h.keys({ui::kKeyEnter});
        h.type("b");
        CHECK(text == "a\nb");
        h.keys({ui::kKeyUp});
        CHECK(area.edit().caret == 1);
        // Hors focus, l'éditeur suit la valeur liée.
        h.click({350, 250});
        text = "changed elsewhere";
        h.frame();
        CHECK(area.text() == "changed elsewhere");
    }
}
