// UI-09 : navigation au clavier et à la manette. Ordre de tabulation explicite (`tabIndex`, `setFocusable`), flèches entre widgets
// voisins (grille), flèches gardées par les widgets qui s'en servent (curseur, liste), manette simulée (platform::Gamepads::simulate
// et GamepadNavigator : croix, stick, répétition, A, B, LB / RB, branchement à chaud), anneau de focus dessiné par le thème
// (style "focusRing") seulement après une navigation au clavier ou à la manette. Sans GPU ni fenêtre.
#include "ui/ui_harness.hpp"

#include <orosmo/platform/gamepad.hpp>
#include <orosmo/ui/gamepad_navigation.hpp>

namespace ui = orosmo::ui;
namespace rd = orosmo::renderer;
namespace pf = orosmo::platform;
using tests::UiHarness;

namespace
{
// Thème de test avec un anneau de focus : bordure "ring" (#00ff00) de 2 px, à 3 px du widget.
constexpr const char *kRingTheme = R"({
    "name": "ring",
    "tokens": {"fg": "#ffffff", "bg": "#102030", "ring": "#00ff00", "ring.width": 2, "ring.gap": 3},
    "widgets": {
        "default": {"foreground": "fg", "fontSize": 10, "padding": 0, "spacing": 0, "width": 10},
        "box": {"spacing": 4},
        "button": {"padding": 2, "background": "bg"},
        "focusRing": {"border": "ring", "borderWidth": "ring.width", "radius": 4, "padding": "ring.gap"}
    }
})";

// Grille 3 x 3 de boutons "b0".."b8" (ligne par ligne), 40 x 20 chacun.
std::vector<ui::Button *> grid(UiHarness &h)
{
    auto g = std::make_unique<ui::Grid>(3);
    g->setAlign(ui::Align::kStart, ui::Align::kStart);
    std::vector<ui::Button *> out;
    for (int i = 0; i < 9; ++i)
    {
        auto &b = g->emplace<ui::Button>("b" + std::to_string(i));
        b.setId("b" + std::to_string(i));
        b.setWidth(ui::SizeSpec::fixed(40)).setHeight(ui::SizeSpec::fixed(20));
        out.push_back(&b);
    }
    h.ui.setRoot(std::move(g));
    h.frame();
    return out;
}

std::vector<rd::DrawRect> ringsOf(const rd::DrawList &list)
{
    std::vector<rd::DrawRect> out;
    for (const auto &c : list.commands)
        if (const auto *r = std::get_if<rd::DrawRect>(&c); r != nullptr && r->borderColor == glm::vec4(0, 1, 0, 1))
            out.push_back(*r);
    return out;
}

void useRingTheme(UiHarness &h)
{
    auto theme = ui::Theme::parse(kRingTheme, {}, "ring");
    REQUIRE(theme.has_value());
    h.ui.themes().set(std::move(*theme));
    h.frame();
}

// Une image avec la manette `pad` lue par `nav`.
void padFrame(UiHarness &h, ui::GamepadNavigator &nav, const pf::GamepadState *pad, float dt = 1.0f / 60.0f)
{
    ui::InputFrame in = h.input();
    in.deltaTime = dt;
    nav.apply(pad, in);
    h.ui.update(in, h.viewport);
}
} // namespace

TEST_SUITE("UI-09 navigation")
{
    TEST_CASE("UI-09 tab order follows tabIndex: positive ascending, then 0 in tree order, negative and non-focusable skipped")
    {
        UiHarness h;
        auto root = std::make_unique<ui::VBox>();
        auto &a = root->emplace<ui::Button>("a");
        auto &b = root->emplace<ui::Button>("b");
        auto &c = root->emplace<ui::Button>("c");
        auto &d = root->emplace<ui::Button>("d");
        auto &e = root->emplace<ui::Button>("e");
        auto &f = root->emplace<ui::Button>("f");
        c.setTabIndex(2);
        e.setTabIndex(1);
        b.setTabIndex(-1); // hors de l'ordre de tabulation
        f.setFocusable(false);
        h.ui.setRoot(std::move(root));
        h.frame();

        std::vector<ui::Widget *> seen;
        for (int i = 0; i < 4; ++i)
        {
            h.keys({ui::kKeyTab});
            seen.push_back(h.ui.focused());
        }
        CHECK(seen == std::vector<ui::Widget *>{&e, &c, &a, &d});
        h.keys({ui::kKeyTab});
        CHECK(h.ui.focused() == &e); // boucle
        h.keys({ui::kKeyTab}, true);
        CHECK(h.ui.focused() == &d); // Maj+Tab : en arrière

        // Le clic atteint un widget hors de l'ordre de tabulation, pas un widget non focalisable.
        h.click(UiHarness::center(b));
        CHECK(h.ui.focused() == &b);
        h.click(UiHarness::center(f));
        CHECK(h.ui.focused() == nullptr);
        CHECK_FALSE(f.canTakeFocus());
    }

    TEST_CASE("UI-09 arrows move the focus to the neighbour in a grid, without wrapping")
    {
        UiHarness h;
        auto b = grid(h);
        h.ui.focus(b[4]); // centre
        h.keys({ui::kKeyRight});
        CHECK(h.ui.focused() == b[5]);
        h.keys({ui::kKeyDown});
        CHECK(h.ui.focused() == b[8]);
        h.keys({ui::kKeyLeft, ui::kKeyLeft});
        CHECK(h.ui.focused() == b[6]);
        h.keys({ui::kKeyLeft}); // bord : le focus reste
        CHECK(h.ui.focused() == b[6]);
        h.keys({ui::kKeyUp, ui::kKeyUp});
        CHECK(h.ui.focused() == b[0]);
        h.keys({ui::kKeyUp});
        CHECK(h.ui.focused() == b[0]);
        h.keys({ui::kKeyRight, ui::kKeyDown});
        CHECK(h.ui.focused() == b[4]);
        CHECK(h.ui.focusVisible());

        // Sans focus, les flèches du clavier ne font rien (l'application peut s'en servir) ; Tab commence la navigation.
        h.ui.focus(nullptr);
        h.keys({ui::kKeyDown});
        CHECK(h.ui.focused() == nullptr);
    }

    TEST_CASE("UI-09 widgets that use the arrows keep them; the gamepad leaves a horizontal slider vertically")
    {
        UiHarness h;
        double value = 50.0;
        auto root = std::make_unique<ui::VBox>();
        root->setAlign(ui::Align::kStart, ui::Align::kStart);
        auto &top = root->emplace<ui::Button>("top");
        auto &slider = root->emplace<ui::Slider>(ui::numberBinding(value), 0.0, 100.0, 1.0);
        slider.setWidth(ui::SizeSpec::fixed(100));
        auto &bottom = root->emplace<ui::Button>("bottom");
        h.ui.setRoot(std::move(root));
        h.frame();

        h.ui.focus(&slider);
        h.keys({ui::kKeyRight, ui::kKeyDown}); // clavier : le curseur garde les quatre flèches (comportement d'avant UI-09)
        h.frame();
        CHECK(value == doctest::Approx(50.0));
        CHECK(h.ui.focused() == &slider);
        h.keys({ui::kKeyRight});
        h.frame();
        CHECK(value == doctest::Approx(51.0));

        ui::GamepadNavigator nav;
        pf::GamepadState pad;
        pad.connected = true;
        pad.setButton(pf::GamepadButton::kDpadRight, true);
        padFrame(h, nav, &pad); // manette : Droite change la valeur
        h.frame();
        CHECK(value == doctest::Approx(52.0));
        pad.setButton(pf::GamepadButton::kDpadRight, false);
        pad.setButton(pf::GamepadButton::kDpadDown, true);
        padFrame(h, nav, &pad); // Bas sort du curseur
        CHECK(h.ui.focused() == &bottom);
        pad.setButton(pf::GamepadButton::kDpadDown, false);
        padFrame(h, nav, &pad);
        pad.setButton(pf::GamepadButton::kDpadUp, true);
        padFrame(h, nav, &pad);
        pad.setButton(pf::GamepadButton::kDpadUp, false);
        padFrame(h, nav, &pad);
        pad.setButton(pf::GamepadButton::kDpadUp, true);
        padFrame(h, nav, &pad);
        CHECK(h.ui.focused() == &top);
    }

    TEST_CASE("UI-09 simulated gamepad: hot-plug, d-pad and stick with repeat, A activates, B cancels, bumpers tab")
    {
        UiHarness h;
        auto b = grid(h);
        int clicks = 0;
        b[3]->onClick(ui::handler([&] { ++clicks; }));

        pf::Gamepads pads;
        CHECK(pads.first() == nullptr);
        pf::GamepadState pad;
        pad.connected = true;
        pad.name = "Simulated";
        pads.simulate(2, pad);
        REQUIRE(pads.events().size() == 1);
        CHECK(pads.events()[0].slot == 2);
        CHECK(pads.events()[0].connected);
        REQUIRE(pads.first() != nullptr);
        CHECK(pads.first()->name == "Simulated");

        ui::GamepadNavigator nav;
        // Sans focus, Bas donne le focus au premier widget de l'ordre de tabulation.
        pad.setButton(pf::GamepadButton::kDpadDown, true);
        pads.simulate(2, pad);
        padFrame(h, nav, pads.first());
        CHECK(h.ui.focused() == b[0]);
        CHECK(h.ui.focusVisible());
        // Tenu : pas de répétition avant le délai, puis une par intervalle.
        padFrame(h, nav, pads.first(), 0.2f);
        CHECK(h.ui.focused() == b[0]);
        padFrame(h, nav, pads.first(), 0.25f); // 0.45 s >= 0.4 s
        CHECK(h.ui.focused() == b[3]);
        pad.setButton(pf::GamepadButton::kDpadDown, false);
        pads.simulate(2, pad);
        padFrame(h, nav, pads.first());

        // A active le bouton focalisé, une seule fois même tenu.
        pad.setButton(pf::GamepadButton::kA, true);
        pads.simulate(2, pad);
        padFrame(h, nav, pads.first());
        padFrame(h, nav, pads.first(), 1.0f);
        CHECK(clicks == 1);
        pad.setButton(pf::GamepadButton::kA, false);

        // Stick gauche au-delà de la zone morte : une flèche.
        pad.setAxis(pf::GamepadAxis::kLeftX, 0.9f);
        pads.simulate(2, pad);
        padFrame(h, nav, pads.first());
        CHECK(h.ui.focused() == b[4]);
        pad.setAxis(pf::GamepadAxis::kLeftX, 0.3f); // dans la zone morte : relâché
        pads.simulate(2, pad);
        padFrame(h, nav, pads.first());
        CHECK(h.ui.focused() == b[4]);

        // RB : suivant dans l'ordre de tabulation ; LB : précédent.
        pad.setButton(pf::GamepadButton::kRightBumper, true);
        pads.simulate(2, pad);
        padFrame(h, nav, pads.first());
        CHECK(h.ui.focused() == b[5]);
        pad.setButton(pf::GamepadButton::kRightBumper, false);
        pad.setButton(pf::GamepadButton::kLeftBumper, true);
        pads.simulate(2, pad);
        padFrame(h, nav, pads.first());
        CHECK(h.ui.focused() == b[4]);

        // Débranchée : plus aucune commande, boutons oubliés ; l'événement est signalé.
        pf::GamepadState unplugged;
        pads.simulate(2, unplugged);
        REQUIRE_FALSE(pads.events().empty());
        CHECK_FALSE(pads.events().back().connected);
        CHECK(pads.first() == nullptr);
        CHECK_FALSE(pads.state(2).pressed(pf::GamepadButton::kLeftBumper));
        ui::InputFrame in;
        nav.apply(pads.first(), in);
        CHECK(in.navigation.empty());
        CHECK(pads.state(99).connected == false); // hors bornes : déconnecté
    }

    TEST_CASE("UI-09 gamepad B closes an open dropdown, like Escape")
    {
        UiHarness h;
        int choice = 0;
        auto root = std::make_unique<ui::VBox>();
        root->setAlign(ui::Align::kStart, ui::Align::kStart);
        auto &dd = root->emplace<ui::Dropdown>(std::vector<std::string>{"a", "b", "c"}, ui::Binding<int>::ref(choice));
        dd.setWidth(ui::SizeSpec::fixed(80));
        h.ui.setRoot(std::move(root));
        h.frame();
        ui::GamepadNavigator nav;
        pf::GamepadState pad;
        pad.connected = true;
        pad.setButton(pf::GamepadButton::kRightBumper, true);
        padFrame(h, nav, &pad);
        CHECK(h.ui.focused() == &dd);
        pad.setButton(pf::GamepadButton::kRightBumper, false);
        pad.setButton(pf::GamepadButton::kA, true);
        padFrame(h, nav, &pad);
        CHECK(h.ui.popupOpenFor(dd));
        pad.setButton(pf::GamepadButton::kA, false);
        pad.setButton(pf::GamepadButton::kB, true);
        padFrame(h, nav, &pad);
        CHECK_FALSE(h.ui.popupOpenFor(dd));
    }

    TEST_CASE("UI-09 the focus ring comes from the theme and only shows after keyboard or gamepad navigation")
    {
        UiHarness h;
        auto b = grid(h);
        {
            rd::DrawList list;
            h.keys({ui::kKeyTab});
            h.ui.draw(list);
            CHECK(ringsOf(list).empty()); // thème sans "focusRing" : aucun anneau, aucune valeur en dur
        }
        useRingTheme(h);
        {
            rd::DrawList list;
            h.ui.draw(list);
            const auto rings = ringsOf(list);
            REQUIRE(rings.size() == 1);
            const ui::Rect &r = b[0]->rect();
            CHECK(rings[0].position == r.position - glm::vec2(3.0f));
            CHECK(rings[0].size == r.size + glm::vec2(6.0f));
            CHECK(rings[0].borderWidth == doctest::Approx(2.0f));
            CHECK(rings[0].radius == doctest::Approx(4.0f));
            CHECK(rings[0].color.a == doctest::Approx(0.0f)); // contour seul
        }
        // Un clic donne le focus sans anneau.
        h.click(UiHarness::center(*b[4]));
        CHECK(h.ui.focused() == b[4]);
        CHECK_FALSE(h.ui.focusVisible());
        {
            rd::DrawList list;
            h.ui.draw(list);
            CHECK(ringsOf(list).empty());
        }
        // Une flèche le montre à nouveau, autour du nouveau widget.
        h.keys({ui::kKeyRight});
        {
            rd::DrawList list;
            h.ui.draw(list);
            const auto rings = ringsOf(list);
            REQUIRE(rings.size() == 1);
            CHECK(rings[0].position == b[5]->rect().position - glm::vec2(3.0f));
        }
    }
}
