// M3 (revue 3) : durée de vie et fenêtres de l'exécution de l'interface. Le focus clavier ne vise jamais un widget détruit par un
// `clearChildren` public ; une fenêtre modale ferme les fenêtres surgissantes ouvertes hors d'elle et garde les siennes ; les
// écritures différées des éditeurs (courbe, roue, dégradé) ne touchent pas un widget détruit ; la teinte d'un gris est la même pour
// la roue et les curseurs. Sans GPU ; à lancer aussi sous ASan / UBSan (make demo-test SANITIZE=address,undefined).
#include "ui/ui_harness.hpp"

#include <orosmo/ui/color.hpp>
#include <orosmo/ui/controls.hpp>
#include <orosmo/ui/menus.hpp>

#include <functional>

namespace ui = orosmo::ui;
using tests::UiHarness;

namespace
{
std::vector<ui::Slider *> slidersOf(ui::Widget &root)
{
    std::vector<ui::Slider *> out;
    std::function<void(ui::Widget &)> walk = [&](ui::Widget &w) {
        if (auto *s = dynamic_cast<ui::Slider *>(&w))
            out.push_back(s);
        for (const auto &c : w.children())
            walk(*c);
    };
    walk(root);
    return out;
}

template <typename T> T *firstOf(ui::Widget &root)
{
    if (auto *t = dynamic_cast<T *>(&root))
        return t;
    for (const auto &c : root.children())
        if (T *t = firstOf<T>(*c))
            return t;
    return nullptr;
}
} // namespace

TEST_SUITE("ui/review3")
{
    TEST_CASE("wantsKeyboard never reads a focused widget destroyed by Widget, Tabs or Section clearChildren")
    {
        std::string text = "x";
        SUBCASE("Widget::clearChildren")
        {
            UiHarness h;
            auto root = std::make_unique<ui::VBox>();
            auto &box = root->emplace<ui::VBox>();
            auto &field = box.emplace<ui::TextField>(ui::Binding<std::string>::ref(text));
            h.ui.setRoot(std::move(root));
            h.frame();
            h.ui.focus(&field);
            REQUIRE(h.ui.wantsKeyboard());
            box.clearChildren(); // sans Ui::forget : le champ focalisé est détruit
            CHECK_FALSE(h.ui.wantsKeyboard());
            CHECK(h.ui.focused() == nullptr);
            h.frame();
            CHECK_FALSE(h.ui.wantsKeyboard());
            // Un nouveau champ (peut-être à la même adresse) prend le focus normalement.
            auto &again = box.emplace<ui::TextField>(ui::Binding<std::string>::ref(text));
            h.frame();
            h.ui.focus(&again);
            CHECK(h.ui.wantsKeyboard());
            CHECK(again.focused());
        }
        SUBCASE("Tabs::clearChildren")
        {
            UiHarness h;
            auto root = std::make_unique<ui::VBox>();
            auto &tabs = root->emplace<ui::Tabs>();
            auto page = std::make_unique<ui::VBox>();
            auto &field = page->emplace<ui::TextField>(ui::Binding<std::string>::ref(text));
            tabs.addPage("A", std::move(page));
            h.ui.setRoot(std::move(root));
            h.frame();
            h.ui.focus(&field);
            REQUIRE(h.ui.wantsKeyboard());
            tabs.clearChildren();
            CHECK_FALSE(h.ui.wantsKeyboard());
            CHECK(h.ui.focused() == nullptr);
            h.frame();
            CHECK_FALSE(h.ui.wantsKeyboard());
        }
        SUBCASE("Section::clearChildren")
        {
            UiHarness h;
            auto root = std::make_unique<ui::VBox>();
            auto &section = root->emplace<ui::Section>("S");
            auto &field = section.body().emplace<ui::TextField>(ui::Binding<std::string>::ref(text));
            h.ui.setRoot(std::move(root));
            h.frame();
            h.ui.focus(&field);
            REQUIRE(h.ui.wantsKeyboard());
            section.clearChildren();
            CHECK_FALSE(h.ui.wantsKeyboard());
            CHECK(h.ui.focused() == nullptr);
            h.frame();
            CHECK_FALSE(h.ui.wantsKeyboard());
        }
    }

    TEST_CASE("showModal closes popups opened outside the modal and keeps those opened from it (submenus too), frame after frame")
    {
        UiHarness h;
        h.viewport = {600, 500};
        bool open = false;
        int outsideChoice = 0;
        int insideChoice = 0;
        auto root = std::make_unique<ui::VBox>();
        auto &outside = root->emplace<ui::Dropdown>(std::vector<std::string>{"a", "b"}, ui::Binding<int>::ref(outsideChoice));
        auto &dialog = root->emplace<ui::Dialog>("Title", ui::Binding<bool>::ref(open), false);
        dialog.panel().setWidth(ui::SizeSpec::fixed(300));
        auto &inside = dialog.body().emplace<ui::Dropdown>(std::vector<std::string>{"c", "d"}, ui::Binding<int>::ref(insideChoice));
        auto &menu = dialog.body().emplace<ui::Menu>("Menu");
        auto &sub = menu.items().emplace<ui::Menu>("Sub");
        sub.items().emplace<ui::MenuItem>("Leaf");
        h.ui.setRoot(std::move(root));
        h.frame();

        // Liste ouverte hors de la modale : fermée quand la modale s'ouvre (avant qu'elle reçoive les entrées).
        h.click(UiHarness::center(outside));
        REQUIRE(h.ui.popupOpenFor(outside));
        open = true;
        h.frame();
        REQUIRE(h.ui.modalOpen());
        CHECK_FALSE(h.ui.anyPopupOpen());

        // Liste ouverte depuis la modale : reste ouverte, image après image.
        h.click(UiHarness::center(inside));
        REQUIRE(h.ui.popupOpenFor(inside));
        for (int i = 0; i < 3; ++i)
            h.frame();
        CHECK(h.ui.popupOpenFor(inside));
        h.keys({ui::kKeyEscape}); // ferme la liste (la boîte reste)
        CHECK_FALSE(h.ui.anyPopupOpen());
        CHECK(open);

        // Menu de la modale et son sous-menu (ouverts au survol) : gardés.
        h.moveTo(UiHarness::center(menu));
        REQUIRE(h.ui.popupOpenFor(menu));
        h.moveTo(UiHarness::center(sub));
        REQUIRE(h.ui.popupOpenFor(sub));
        for (int i = 0; i < 3; ++i)
            h.frame();
        CHECK(h.ui.popupOpenFor(menu));
        CHECK(h.ui.popupOpenFor(sub));
        CHECK(h.ui.modalOpen());
    }

    TEST_CASE("CurveEditor: a deferred write never touches the editor destroyed earlier in the frame or by its own setter")
    {
        SUBCASE("destroyed by an onHover handler queued before the write")
        {
            UiHarness h;
            ui::Curve curve;
            int changes = 0;
            auto root = std::make_unique<ui::VBox>();
            auto &box = root->emplace<ui::VBox>();
            auto &editor = box.emplace<ui::CurveEditor>(ui::Binding<ui::Curve>::ref(curve));
            editor.setWidth(ui::SizeSpec::fixed(200)).setHeight(ui::SizeSpec::fixed(200));
            editor.onHover([&](const ui::Event &e) {
                if (e.active)
                    box.clearChildren();
            });
            editor.onChange([&](const ui::Event &) { ++changes; });
            h.ui.setRoot(std::move(root));
            h.frame();
            h.press(UiHarness::center(editor)); // survol (différé) puis ajout d'un point (écriture différée) dans la même image
            CHECK(box.children().empty());
            CHECK(curve.points.size() == 2); // l'éditeur détruit n'a rien écrit
            CHECK(changes == 0);
            h.release({1, 1});
        }
        SUBCASE("destroyed by the binding's setter: written, then nothing else touched")
        {
            UiHarness h;
            ui::Curve curve;
            int changes = 0;
            auto root = std::make_unique<ui::VBox>();
            auto &box = root->emplace<ui::VBox>();
            ui::Binding<ui::Curve> binding{[&] { return curve; },
                                           [&](const ui::Curve &c) {
                                               curve = c;
                                               box.clearChildren();
                                           }};
            auto &editor = box.emplace<ui::CurveEditor>(std::move(binding));
            editor.setWidth(ui::SizeSpec::fixed(200)).setHeight(ui::SizeSpec::fixed(200));
            editor.onChange([&](const ui::Event &) { ++changes; });
            h.ui.setRoot(std::move(root));
            h.frame();
            h.press(UiHarness::center(editor));
            CHECK(curve.points.size() == 3); // écrit avant la destruction
            CHECK(box.children().empty());
            CHECK(changes == 0); // onChange (après l'écriture) : la source n'existe plus
            h.release({1, 1});
        }
        SUBCASE("alive: the write comes before onChange")
        {
            UiHarness h;
            ui::Curve curve;
            std::size_t seen = 0;
            auto root = std::make_unique<ui::VBox>();
            auto &editor = root->emplace<ui::CurveEditor>(ui::Binding<ui::Curve>::ref(curve));
            editor.setWidth(ui::SizeSpec::fixed(200)).setHeight(ui::SizeSpec::fixed(200));
            editor.onChange([&](const ui::Event &) { seen = curve.points.size(); });
            h.ui.setRoot(std::move(root));
            h.frame();
            h.click(UiHarness::center(editor));
            CHECK(seen == 3);
            CHECK(editor.curve().points.size() == 3);
        }
    }

    TEST_CASE("Slider and RangeSlider: a deferred write never touches the slider destroyed by its own setter")
    {
        SUBCASE("Slider")
        {
            UiHarness h;
            double value = 0.0;
            int writes = 0;
            auto root = std::make_unique<ui::VBox>();
            auto &box = root->emplace<ui::VBox>();
            ui::Binding<double> binding{[&] { return value; },
                                        [&](const double &v) {
                                            value = v;
                                            ++writes;
                                            box.clearChildren();
                                        }};
            auto &slider = box.emplace<ui::Slider>(std::move(binding), 0.0, 10.0);
            slider.setWidth(ui::SizeSpec::fixed(200));
            h.ui.setRoot(std::move(root));
            h.frame();
            h.click(UiHarness::center(slider)); // écriture différée : le setter détruit le curseur
            CHECK(writes == 1);
            CHECK(value > 0.0);
            CHECK(box.children().empty());
        }
        SUBCASE("RangeSlider")
        {
            UiHarness h;
            double low = 0.0;
            double high = 10.0;
            int writes = 0;
            auto root = std::make_unique<ui::VBox>();
            auto &box = root->emplace<ui::VBox>();
            auto clearing = [&](double &target) {
                return ui::Binding<double>{[&target] { return target; },
                                           [&target, &writes, &box](const double &v) {
                                               target = v;
                                               ++writes;
                                               box.clearChildren();
                                           }};
            };
            auto &range = box.emplace<ui::RangeSlider>(clearing(low), clearing(high), 0.0, 10.0);
            range.setWidth(ui::SizeSpec::fixed(200));
            h.ui.setRoot(std::move(root));
            h.frame();
            h.click(UiHarness::center(range)); // la poignée la plus proche bouge, son setter détruit le curseur
            CHECK(writes == 1);
            CHECK(box.children().empty());
        }
    }

    TEST_CASE("ColorWheel and GradientBar: a deferred write never touches the widget destroyed earlier in the frame")
    {
        SUBCASE("ColorWheel")
        {
            UiHarness h;
            ui::Color color{1, 0, 0, 1};
            auto root = std::make_unique<ui::VBox>();
            auto &box = root->emplace<ui::VBox>();
            auto &wheel = box.emplace<ui::ColorWheel>(ui::Binding<ui::Color>::ref(color));
            wheel.setWidth(ui::SizeSpec::fixed(200)).setHeight(ui::SizeSpec::fixed(200));
            wheel.onHover([&](const ui::Event &e) {
                if (e.active)
                    box.clearChildren();
            });
            h.ui.setRoot(std::move(root));
            h.frame();
            h.press(UiHarness::center(wheel) + glm::vec2(0, 90)); // anneau : teinte 1/4, écriture différée après le survol
            CHECK(box.children().empty());
            CHECK(color == ui::Color{1, 0, 0, 1});
            h.release({1, 1});
        }
        SUBCASE("ColorWheel destroyed by its setter")
        {
            UiHarness h;
            ui::Color color{1, 0, 0, 1};
            auto root = std::make_unique<ui::VBox>();
            auto &box = root->emplace<ui::VBox>();
            ui::Binding<ui::Color> binding{[&] { return color; },
                                           [&](const ui::Color &c) {
                                               color = c;
                                               box.clearChildren();
                                           }};
            auto &wheel = box.emplace<ui::ColorWheel>(std::move(binding));
            wheel.setWidth(ui::SizeSpec::fixed(200)).setHeight(ui::SizeSpec::fixed(200));
            h.ui.setRoot(std::move(root));
            h.frame();
            h.press(UiHarness::center(wheel) + glm::vec2(0, 90));
            CHECK(box.children().empty());
            CHECK(ui::rgbToHsv(glm::vec3(color)).x == doctest::Approx(0.25f).epsilon(0.02));
            h.release({1, 1});
        }
        SUBCASE("GradientBar")
        {
            UiHarness h;
            ui::Gradient gradient;
            int changes = 0;
            auto root = std::make_unique<ui::VBox>();
            auto &box = root->emplace<ui::VBox>();
            auto &editor = box.emplace<ui::GradientEditor>(ui::Binding<ui::Gradient>::ref(gradient));
            editor.setWidth(ui::SizeSpec::fixed(400));
            editor.onChange([&](const ui::Event &) { ++changes; });
            ui::Widget &bar = *editor.children()[0];
            bar.onHover([&](const ui::Event &e) {
                if (e.active)
                    box.clearChildren(); // GradientEditor lui-même (son clearChildren ne retire rien) : via son parent
            });
            h.viewport = {600, 600};
            h.ui.setRoot(std::move(root));
            h.frame();
            const glm::vec2 at = bar.rect().position + glm::vec2(bar.rect().size.x * 0.5f, 2.0f); // sur la barre, loin des arrêts
            h.press(at);
            CHECK(box.children().empty());
            CHECK(gradient.stops.size() == 2);
            CHECK(changes == 0);
            h.release({1, 1});
        }
    }

    TEST_CASE("ColorEditor: a grey's hue is shared by the wheel and the sliders")
    {
        UiHarness h;
        h.viewport = {600, 600};
        ui::Color color{0.5f, 0.5f, 0.5f, 1.0f};
        auto root = std::make_unique<ui::Overlay>();
        auto &editor = root->emplace<ui::ColorEditor>(ui::Binding<ui::Color>::ref(color));
        editor.setAlign(ui::Align::kStart, ui::Align::kStart);
        editor.setWidth(ui::SizeSpec::fixed(420)).setHeight(ui::SizeSpec::fixed(380));
        h.ui.setRoot(std::move(root));
        h.frame();
        auto *format = editor.findAs<ui::Dropdown>("format");
        REQUIRE(format != nullptr);
        h.click(UiHarness::center(*format));
        REQUIRE(h.ui.popup() != nullptr);
        h.click(UiHarness::center(*h.ui.popup()->children()[0]->children()[1])); // HSV
        h.frame();
        REQUIRE(editor.format() == ui::ColorFormat::kHsv);
        auto *wheel = firstOf<ui::ColorWheel>(editor);
        REQUIRE(wheel != nullptr);
        auto sliders = slidersOf(editor);
        REQUIRE(sliders.size() == 3); // H, S, V

        // Teinte choisie à la roue pour un gris (la couleur ne change pas) : le curseur H la montre, S part d'elle.
        const float outer = std::min(wheel->rect().size.x, wheel->rect().size.y) * 0.5f;
        h.click(UiHarness::center(*wheel) + glm::vec2(0, outer * 0.9f)); // bas de l'anneau : teinte 1/4
        REQUIRE(wheel->hue() == doctest::Approx(0.25f).epsilon(0.02));
        CHECK(color == ui::Color{0.5f, 0.5f, 0.5f, 1.0f});
        h.frame();
        CHECK(sliders[0]->value() == doctest::Approx(wheel->hue() * 360.0f).epsilon(0.01));
        h.ui.focus(sliders[1]);
        h.keys({ui::kKeyEnd}); // S = 100 %
        h.frame();
        const glm::vec3 hsv = ui::rgbToHsv(glm::vec3(color));
        CHECK(hsv.y == doctest::Approx(1.0f).epsilon(0.01));
        CHECK(hsv.x == doctest::Approx(0.25f).epsilon(0.02));

        // Retour au gris, puis teinte réglée au curseur H : la roue la prend.
        h.ui.focus(sliders[1]);
        h.keys({ui::kKeyHome}); // S = 0
        h.frame();
        h.ui.focus(sliders[0]);
        h.keys({ui::kKeyHome}); // H = 0
        h.frame();
        CHECK(wheel->hue() == doctest::Approx(0.0f).epsilon(0.01));
    }
}
