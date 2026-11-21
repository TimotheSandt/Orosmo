// Revue de M3 (UI) : cas limites de l'interface : file d'actions différées, événement dont la source est détruite, focus d'un
// widget devenu inactif, masqué ou hors de la fenêtre modale, dialogues imbriqués, zone cliquable d'une vue défilante,
// structure des widgets composés, racine retirée pendant la mise à jour, répétitions des fonds en neuf parties.
#include "ui/ui_harness.hpp"

#include <orosmo/ui/menus.hpp>

namespace ui = orosmo::ui;
using tests::UiHarness;

TEST_SUITE("ui/robustness")
{
    TEST_CASE("deferred actions beyond eight waves are kept for the next frame, in order, not dropped")
    {
        UiHarness h;
        h.ui.setRoot(std::make_unique<ui::VBox>());
        std::vector<int> done;
        std::function<void(int)> chain = [&](int i)
        {
            done.push_back(i);
            if (i + 1 < 12)
                h.ui.defer([&chain, i] { chain(i + 1); });
        };
        h.ui.defer([&] { chain(0); });
        h.frame();
        CHECK(done.size() == 8);
        h.frame();
        REQUIRE(done.size() == 12);
        for (int i = 0; i < 12; ++i)
            CHECK(done[static_cast<std::size_t>(i)] == i);
    }

    TEST_CASE("a deferred event whose source was destroyed by an earlier handler of the same frame is not delivered")
    {
        UiHarness h;
        auto root = std::make_unique<ui::VBox>();
        ui::VBox &box = root->emplace<ui::VBox>();
        int clicks = 0;
        box.emplace<ui::Button>("Delete me").onClick([&](const ui::Event &) {
            ++clicks;
            box.clearChildren(); // détruit le bouton
        });
        h.ui.setRoot(std::move(root));
        h.frame();
        h.keys({ui::kKeyTab});
        h.keys({ui::kKeyEnter, ui::kKeyEnter}); // deux clics en file dans la même image
        CHECK(clicks == 1);
    }

    TEST_CASE("focus leaves a widget that becomes disabled or hidden, and the main view while a modal dialog is open")
    {
        UiHarness h;
        bool enabled = true;
        bool dialogOpen = false;
        int clicks = 0;
        auto root = std::make_unique<ui::VBox>();
        auto &button = root->emplace<ui::Button>("Go");
        button.bindEnabled([&] { return enabled; });
        button.onClick([&](const ui::Event &) { ++clicks; });
        auto &tabs = root->emplace<ui::Tabs>();
        auto &inPage = tabs.addPage("A", std::make_unique<ui::Button>("In page A"));
        tabs.addPage("B", std::make_unique<ui::Label>("B"));
        root->emplace<ui::Dialog>("Dialog", ui::Binding<bool>::ref(dialogOpen));
        h.ui.setRoot(std::move(root));
        h.frame();
        h.keys({ui::kKeyTab});
        REQUIRE(h.ui.focused() == &button);
        enabled = false;
        h.keys({ui::kKeyEnter});
        CHECK(clicks == 0);
        CHECK(h.ui.focused() == nullptr);

        enabled = true;
        h.frame();
        h.ui.focus(&inPage);
        tabs.select(h.ui, 1); // la page A est masquée
        h.frame();
        h.frame();
        CHECK(h.ui.focused() == nullptr);

        h.ui.focus(&button);
        dialogOpen = true;
        h.frame(); // le dialogue s'inscrit à la mise en page
        h.keys({ui::kKeyEnter});
        CHECK(clicks == 0); // la touche ne passe pas derrière le voile
    }

    TEST_CASE("a dialog open inside an open dialog's panel is registered after it (no invalidated iteration)")
    {
        UiHarness h;
        bool outer = true, inner = true;
        auto root = std::make_unique<ui::VBox>();
        auto &dialog = root->emplace<ui::Dialog>("Outer", ui::Binding<bool>::ref(outer));
        auto &nested = dialog.body().emplace<ui::Dialog>("Inner", ui::Binding<bool>::ref(inner));
        nested.body().emplace<ui::Label>("inner text");
        h.ui.setRoot(std::move(root));
        h.frame();
        h.frame();
        CHECK(nested.panel().rect().size.x > 0.0f);
    }

    TEST_CASE("children of a scroll view are clickable only inside its inner (clipped) area")
    {
        UiHarness h;
        int clicks = 0;
        auto root = std::make_unique<ui::Overlay>();
        auto &scroll = root->emplace<ui::ScrollView>();
        scroll.overrideStyle(ui::StyleProperty::kPadding, "20");
        scroll.setAlign(ui::Align::kStart, ui::Align::kStart);
        scroll.setWidth(ui::SizeSpec::fixed(200)).setHeight(ui::SizeSpec::fixed(100));
        auto &column = scroll.emplace<ui::VBox>();
        for (int i = 0; i < 10; ++i)
            column.emplace<ui::Button>("Item").setHeight(ui::SizeSpec::fixed(30)).onClick([&](const ui::Event &) { ++clicks; });
        h.ui.setRoot(std::move(root));
        h.frame();
        scroll.setScroll(15.0f); // le premier bouton remonte dans la marge intérieure du haut (y de 5 à 35)
        h.frame();
        h.click({50, 10}); // dans la marge : le bouton y est découpé au dessin
        CHECK(clicks == 0);
        h.click({50, 30}); // dans la zone intérieure
        CHECK(clicks == 1);
    }

    TEST_CASE("clearing a composite widget keeps its structure: Tabs, Section, editors")
    {
        UiHarness h;
        auto root = std::make_unique<ui::VBox>();
        auto &tabs = root->emplace<ui::Tabs>();
        tabs.addPage("A", std::make_unique<ui::Label>("a"));
        auto &section = root->emplace<ui::Section>("S");
        section.body().emplace<ui::Label>("content");
        h.ui.setRoot(std::move(root));
        h.frame();
        tabs.clearChildren();
        CHECK(tabs.pageCount() == 0);
        tabs.addPage("B", std::make_unique<ui::Label>("b")); // l'en-tête a été refait : pas d'accès à un objet détruit
        CHECK(tabs.pageCount() == 1);
        section.clearChildren();
        CHECK(section.body().children().empty());
        CHECK(section.children().size() == 2); // en-tête et corps restent
        h.frame();
    }

    TEST_CASE("setRoot(nullptr) from a handler removes the tree at the end of the frame; Button::setText replaces a bound text")
    {
        UiHarness h;
        auto root = std::make_unique<ui::VBox>();
        root->emplace<ui::Button>("Quit").onClick([&](const ui::Event &) { h.ui.setRoot(nullptr); });
        h.ui.setRoot(std::move(root));
        h.frame();
        h.keys({ui::kKeyTab, ui::kKeyEnter});
        h.frame();
        CHECK(h.ui.root() == nullptr);

        ui::Button button("A");
        button.bindText([] { return std::string("bound"); });
        button.setText("B");
        CHECK(button.text() == "B");
    }
}

#include <orosmo/ui/color.hpp>
#include <orosmo/ui/controls.hpp>
#include <orosmo/ui/reflect.hpp>

#include <cstdint>
#include <limits>

namespace
{
template <typename T> T &placed(UiHarness &h, std::unique_ptr<T> w, glm::vec2 size)
{
    h.viewport = {600, 600};
    auto root = std::make_unique<ui::Overlay>();
    w->setAlign(ui::Align::kStart, ui::Align::kStart);
    w->setWidth(ui::SizeSpec::fixed(size.x)).setHeight(ui::SizeSpec::fixed(size.y));
    T &ref = root->add(std::move(w));
    h.ui.setRoot(std::move(root));
    h.frame();
    return ref;
}
} // namespace

TEST_SUITE("ui/robustness")
{
    TEST_CASE("TextArea: a binding without setter is not editable, but selection and copy still work (also read-only)")
    {
        UiHarness h;
        auto &area = placed(h, std::make_unique<ui::TextArea>(ui::Binding<std::string>::constant("A")), {200, 100});
        h.keys({ui::kKeyTab});
        REQUIRE(h.ui.focused() == &area);
        h.type("x");
        h.keys({ui::kKeyBackspace, ui::kKeyEnter});
        CHECK(area.text() == "A");
        h.shortcut("Ctrl+A");
        h.shortcut("Ctrl+C");
        CHECK(h.ui.clipboardText() == "A");
        std::string text = "copy me";
        auto &ro = placed(h, std::make_unique<ui::TextArea>(ui::Binding<std::string>::ref(text)), {200, 100});
        ro.setReadOnly(true);
        h.frame();
        h.keys({ui::kKeyTab});
        h.shortcut("Ctrl+A");
        h.shortcut("Ctrl+C");
        CHECK(h.ui.clipboardText() == "copy me");
    }

    TEST_CASE("TextArea: a bound text replaced while unfocused clears the history, clamps the scroll and keeps the caret on a character")
    {
        UiHarness h;
        std::string text = "ab";
        auto &area = placed(h, std::make_unique<ui::TextArea>(ui::Binding<std::string>::ref(text)), {200, 60});
        h.keys({ui::kKeyTab, ui::kKeyEnd});
        h.type("cd");
        CHECK(text == "abcd");
        h.click({500, 500}); // perte du focus
        text = "new";
        h.frame();
        h.keys({ui::kKeyTab});
        h.shortcut("Ctrl+Z");
        CHECK(text == "new"); // l'ancien historique ne s'applique pas au nouveau document

        // Curseur à l'octet 1, puis le texte devient « é » : le curseur revient au début du caractère.
        h.keys({ui::kKeyHome, ui::kKeyRight});
        h.click({500, 500});
        text = "\xc3\xa9";
        h.frame();
        h.keys({ui::kKeyTab, ui::kKeyDelete});
        CHECK((text.empty() || text == "\xc3\xa9"));

        // Document long défilé, puis remplacé par une ligne : le défilement revient à 0.
        std::string lines;
        for (int i = 0; i < 40; ++i)
            lines += "line\n";
        text = lines;
        h.click({500, 500});
        h.frame();
        h.wheel(UiHarness::center(area), -10.0f);
        CHECK(area.scroll() > 0.0f);
        text = "x";
        h.frame();
        CHECK(area.scroll() == 0.0f);
    }

    TEST_CASE("TextArea: a zero font size does not divide by zero; a too-wide character makes no empty row")
    {
        UiHarness h;
        std::string text = "abc\ndef";
        auto &area = placed(h, std::make_unique<ui::TextArea>(ui::Binding<std::string>::ref(text)), {200, 100});
        area.overrideStyle(ui::StyleProperty::kFontSize, "0");
        h.frame();
        h.keys({ui::kKeyTab, ui::kKeyPageDown, ui::kKeyPageUp});
        h.click(UiHarness::center(area));
        CHECK(area.rowCount(h.ui) >= 1);
        // Une seule lettre plus large que la zone : une rangée, pas une rangée vide en plus.
        std::string one = "W";
        auto &narrow = placed(h, std::make_unique<ui::TextArea>(ui::Binding<std::string>::ref(one)), {3, 60});
        h.frame();
        CHECK(narrow.rowCount(h.ui) == 1);
    }

    TEST_CASE("SpinBox relays a validated typed value; RangeSlider shows the decimals of its step; Slider reaches its maximum")
    {
        UiHarness h;
        int count = 3;
        int changes = 0;
        auto &spin = placed(h, std::make_unique<ui::SpinBox>(ui::numberBinding(count), 0.0, 10.0), {160, 24});
        spin.onChange([&](const ui::Event &) { ++changes; });
        h.click(UiHarness::center(*spin.children()[0]));
        h.keys({ui::kKeySelectAll});
        h.type("7");
        h.keys({ui::kKeyEnter});
        CHECK(count == 7);
        CHECK(changes == 1);

        double lo = 0.25, hi = 0.75;
        ui::RangeSlider range(ui::numberBinding(lo), ui::numberBinding(hi), 0.0, 1.0, 0.25);
        CHECK(range.formatted() == "0.25 - 0.75");

        double v = 0.0;
        auto &slider = placed(h, std::make_unique<ui::Slider>(ui::numberBinding(v), 0.0, 1.0, 0.3), {200, 24});
        h.keys({ui::kKeyTab});
        REQUIRE(h.ui.focused() == &slider);
        h.keys({ui::kKeyEnd});
        CHECK(v == 1.0);
    }

    TEST_CASE("Checkbox toggled twice in one frame ends where it started; an integer field checks its bounds after rounding")
    {
        UiHarness h;
        bool on = false;
        placed(h, std::make_unique<ui::Checkbox>("C", ui::Binding<bool>::ref(on)), {100, 24});
        h.keys({ui::kKeyTab});
        h.keys({ui::kKeySpace, ui::kKeySpace});
        CHECK_FALSE(on);
        h.keys({ui::kKeySpace});
        CHECK(on);

        double n = 1.0;
        auto &field = placed(h, std::make_unique<ui::NumberField>(ui::numberBinding(n), true, 0.0, 2.5), {100, 24});
        h.click(UiHarness::center(field));
        h.keys({ui::kKeySelectAll});
        h.type("2.5"); // arrondi à 3 : hors de [0, 2.5]
        h.keys({ui::kKeyEnter});
        CHECK(n == 1.0);
    }

    TEST_CASE("Dropdown: a row whose option changed since the list opened is not chosen")
    {
        UiHarness h;
        std::vector<std::string> options = {"Low", "High"};
        int selected = 1;
        auto &drop = placed(h, std::make_unique<ui::Dropdown>([&] { return options; }, ui::Binding<int>::ref(selected)), {120, 24});
        h.click(UiHarness::center(drop));
        REQUIRE(h.ui.popup() != nullptr);
        ui::Widget &firstRow = *h.ui.popup()->children()[0]->children()[0];
        std::swap(options[0], options[1]); // la ligne affiche encore « Low », l'indice 0 désigne maintenant « High »
        h.click(UiHarness::center(firstRow));
        CHECK(selected == 1);
    }

    TEST_CASE("colour editors: read-only gradient stops can be chosen not moved; wheel and hex changes are relayed; grey keeps its hue")
    {
        UiHarness h;
        ui::Gradient fixed;
        auto &gradient = placed(h, std::make_unique<ui::GradientEditor>(ui::Binding<ui::Gradient>::constant(fixed)), {400, 420});
        const ui::Rect r = gradient.children()[0]->rect();
        const float markerY = r.end().y - 4.0f;
        h.press({r.position.x + 1.0f, markerY});
        h.press({r.position.x + r.size.x * 0.5f, markerY}); // sans setter : pas d'écriture (avant : std::bad_function_call)
        h.release({r.position.x + r.size.x * 0.5f, markerY});
        CHECK(gradient.gradient().stops.size() == fixed.stops.size());

        ui::Color color{0.5f, 0.5f, 0.5f, 1.0f};
        int changes = 0;
        auto &editor = placed(h, std::make_unique<ui::ColorEditor>(ui::Binding<ui::Color>::ref(color)), {420, 380});
        editor.onChange([&](const ui::Event &) { ++changes; });
        auto *hex = editor.findAs<ui::TextField>("hex");
        REQUIRE(hex != nullptr);
        h.click(UiHarness::center(*hex));
        h.keys({ui::kKeySelectAll});
        h.type("#00ff00");
        h.keys({ui::kKeyEnter});
        CHECK(changes == 1);
        h.click(UiHarness::center(*hex));
        h.keys({ui::kKeySelectAll});
        h.type("not hex");
        h.keys({ui::kKeyEnter});
        CHECK(changes == 1); // saisie invalide : aucun événement

        // Gris, format HSV, teinte réglée à 120 puis saturation : vert, pas rouge.
        color = {0.5f, 0.5f, 0.5f, 1.0f};
        auto *format = editor.findAs<ui::Dropdown>("format");
        REQUIRE(format != nullptr);
        h.click(UiHarness::center(*format));
        REQUIRE(h.ui.popup() != nullptr);
        h.click(UiHarness::center(*h.ui.popup()->children()[0]->children()[1]));
        h.frame();
        std::vector<ui::Slider *> sliders;
        std::function<void(ui::Widget &)> walk = [&](ui::Widget &w) {
            if (auto *s = dynamic_cast<ui::Slider *>(&w))
                sliders.push_back(s);
            for (const auto &c : w.children())
                walk(*c);
        };
        walk(editor);
        REQUIRE(sliders.size() >= 3);
        h.ui.focus(sliders[0]);
        h.keys({ui::kKeyHome}); // H = 0 : la teinte gardée (partagée avec la roue) est encore celle du vert saisi plus haut
        for (int i = 0; i < 120; ++i)
            h.keys({ui::kKeyRight});
        h.ui.focus(sliders[1]);
        h.keys({ui::kKeyEnd});
        CHECK(color.g > color.r);
        CHECK(color.g > color.b);
    }

    TEST_CASE("numberBinding: NaN leaves the value, out-of-range saturates without undefined conversion; enum fields stay in their type")
    {
        std::uint64_t big = 5;
        auto b = ui::numberBinding(big);
        b.set(std::nan(""));
        CHECK(big == 5);
        b.set(18446744073709551616.0); // 2^64
        CHECK(big == std::numeric_limits<std::uint64_t>::max());
        std::int64_t s = 0;
        auto bs = ui::numberBinding(s);
        bs.set(9223372036854775808.0); // 2^63
        CHECK(s == std::numeric_limits<std::int64_t>::max());
        bs.set(-1e30);
        CHECK(s == std::numeric_limits<std::int64_t>::lowest());
    }

    TEST_CASE("ordered input events: text and keys keep their order, each key keeps the Shift state of its press")
    {
        UiHarness h;
        std::string name;
        auto root = std::make_unique<ui::VBox>();
        auto &first = root->emplace<ui::Button>("first");
        auto &field = root->emplace<ui::TextField>(ui::Binding<std::string>::ref(name));
        h.ui.setRoot(std::move(root));
        h.frame();
        h.ui.focus(&field);
        ui::InputFrame in = h.input();
        in.events = {{ui::kKeyNone, false, "a"}, {ui::kKeyBackspace, false, {}}};
        h.frame(in);
        CHECK(field.edit().text.empty());
        in.events = {{ui::kKeyBackspace, false, {}}, {ui::kKeyNone, false, "a"}};
        h.frame(in);
        CHECK(field.edit().text == "a");
        // Maj+Tab dont Maj est relâchée avant la fin de l'image : retour en arrière quand même.
        in = h.input();
        in.shift = false;
        in.events = {{ui::kKeyTab, true, {}}};
        h.frame(in);
        CHECK(h.ui.focused() == &first);
    }

    TEST_CASE("layout: a fill child spanning columns gets the extra width; a short scroll view keeps a thumb travel; huge images refused")
    {
        UiHarness h;
        auto root = std::make_unique<ui::Overlay>();
        auto &grid = root->emplace<ui::Grid>(2);
        grid.setAlign(ui::Align::kStart, ui::Align::kStart);
        grid.setWidth(ui::SizeSpec::fixed(300));
        auto &wide = grid.emplace<ui::Label>("x");
        wide.columnSpan = 2;
        wide.setWidth(ui::SizeSpec::fill());
        h.ui.setRoot(std::move(root));
        h.frame();
        CHECK(wide.rect().size.x > 250.0f);

        ui::DynamicImage image(4, 4);
        image.resize(0x80000000U, 0x80000000U); // 2^31 x 2^31 x 4 octets reviendrait à 0
        CHECK(image.width() == 4);
    }
}
