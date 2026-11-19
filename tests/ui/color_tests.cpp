// UI-03 (couleur, dégradé, courbe) : conversions RGB / HSV / HSL / hexadécimal, roue de couleur, éditeur à plusieurs formats,
// éditeur de dégradé, éditeur de courbe (cubique monotone). Sans GPU.
#include "ui/ui_harness.hpp"

#include <engine/ui/color.hpp>

namespace ui = engine::ui;
using tests::UiHarness;

namespace
{
bool near3(glm::vec3 a, glm::vec3 b, float eps = 1e-3f) { return glm::all(glm::lessThan(glm::abs(a - b), glm::vec3(eps))); }

template <typename T> T &place(UiHarness &h, std::unique_ptr<T> w, glm::vec2 size)
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

TEST_SUITE("ui/color")
{
    TEST_CASE("RGB, HSV, HSL and hexadecimal conversions")
    {
        CHECK(near3(ui::rgbToHsv({1, 0, 0}), {0, 1, 1}));
        CHECK(near3(ui::rgbToHsv({0, 0.5f, 0}), {1.0f / 3.0f, 1, 0.5f}));
        CHECK(near3(ui::hsvToRgb({2.0f / 3.0f, 1, 1}), {0, 0, 1}));
        CHECK(near3(ui::rgbToHsl({1, 1, 1}), {0, 0, 1}));
        CHECK(near3(ui::hslToRgb({0, 1, 0.5f}), {1, 0, 0}));
        for (glm::vec3 c : {glm::vec3(0.2f, 0.7f, 0.4f), glm::vec3(0.9f, 0.1f, 0.6f), glm::vec3(0.5f)})
        {
            CHECK(near3(ui::hsvToRgb(ui::rgbToHsv(c)), c));
            CHECK(near3(ui::hslToRgb(ui::rgbToHsl(c)), c));
        }
        CHECK(ui::toHex({1, 0.5f, 0, 1}, false) == "#ff8000");
        CHECK(ui::toHex({1, 0.5f, 0, 0.5f}, true) == "#ff800080");
        REQUIRE(ui::fromHex("ff8000").has_value());
        CHECK(near3(glm::vec3(*ui::fromHex(" #FF8000 ")), {1, 128.0f / 255.0f, 0}));
        CHECK_FALSE(ui::fromHex("#12345").has_value());
    }

    TEST_CASE("Gradient and Curve evaluate; the smooth curve never overshoots its points")
    {
        ui::Gradient g;
        g.stops = {{1.0f, {0, 0, 1, 1}}, {0.0f, {1, 0, 0, 1}}}; // dans le désordre : évalué trié
        CHECK(near3(glm::vec3(g.evaluate(0.5f)), {0.5f, 0, 0.5f}));
        CHECK(near3(glm::vec3(g.evaluate(-1.0f)), {1, 0, 0}));
        ui::Curve c;
        c.points = {{0, 0}, {0.5f, 0.9f}, {0.6f, 0.9f}, {1, 1}};
        for (float x = 0.0f; x <= 1.0f; x += 0.01f)
        {
            const float y = c.evaluate(x);
            CHECK(y >= -1e-4f);
            CHECK(y <= 1.0f + 1e-4f);
            if (x > 0.5f && x < 0.6f)
                CHECK(y == doctest::Approx(0.9f).epsilon(1e-4)); // plateau gardé : pas de bosse entre deux points égaux
        }
        CHECK(c.evaluate(0.5f) == doctest::Approx(0.9f));
        c.smooth = false;
        CHECK(c.evaluate(0.25f) == doctest::Approx(0.45f));
    }

    TEST_CASE("ColorWheel: the ring sets the hue, the square the saturation and value; the hue survives grey")
    {
        UiHarness h;
        ui::Color color{1, 0, 0, 1};
        auto &wheel = place(h, std::make_unique<ui::ColorWheel>(ui::Binding<ui::Color>::ref(color)), {200, 200});
        const glm::vec2 c = UiHarness::center(wheel);
        h.click(c + glm::vec2(0, 90)); // bas de l'anneau : angle 90° (y vers le bas) = teinte 1/4
        CHECK(wheel.hue() == doctest::Approx(0.25f).epsilon(0.02));
        CHECK(ui::rgbToHsv(glm::vec3(color)).x == doctest::Approx(0.25f).epsilon(0.02));
        // Coin bas gauche du carré : saturation 0, valeur 0 (noir) ; la teinte reste.
        const float side = 80.0f * 1.3f;
        h.click(c + glm::vec2(-side * 0.5f + 1, side * 0.5f - 1));
        CHECK(glm::length(glm::vec3(color)) < 0.05f);
        CHECK(wheel.hue() == doctest::Approx(0.25f).epsilon(0.02));
        // Coin haut droit : saturation et valeur maximales, de cette teinte.
        h.click(c + glm::vec2(side * 0.5f - 1, -side * 0.5f + 1));
        CHECK(near3(glm::vec3(color), ui::hsvToRgb({0.25f, 1, 1}), 0.05f));
    }

    TEST_CASE("ColorEditor: format RGB/HSV/HSL with matching sliders, hexadecimal field, alpha")
    {
        UiHarness h;
        ui::Color color{0.2f, 0.4f, 0.6f, 1.0f};
        auto &editor = place(h, std::make_unique<ui::ColorEditor>(ui::Binding<ui::Color>::ref(color), true), {420, 380});
        auto *hex = editor.findAs<ui::TextField>("hex");
        REQUIRE(hex != nullptr);
        CHECK(hex->text() == "#336699ff");
        // Format HSV : la liste déroulante, puis le premier curseur est la teinte (0-360).
        auto *format = editor.findAs<ui::Dropdown>("format");
        REQUIRE(format != nullptr);
        h.click(UiHarness::center(*format));
        REQUIRE(h.ui.popup() != nullptr);
        h.click(UiHarness::center(*h.ui.popup()->children()[0]->children()[1]));
        h.frame();
        CHECK(editor.format() == ui::ColorFormat::kHsv);
        std::vector<ui::Slider *> sliders;
        std::function<void(ui::Widget &)> walk = [&](ui::Widget &w) {
            if (auto *s = dynamic_cast<ui::Slider *>(&w))
                sliders.push_back(s);
            for (const auto &c : w.children())
                walk(*c);
        };
        walk(editor);
        REQUIRE(sliders.size() == 4); // H, S, V, A
        CHECK(sliders[0]->formatted() == "H 210");
        CHECK(sliders[0]->max() == 360.0);
        CHECK(sliders[3]->formatted() == "A 255");
        // Saisie hexadécimale : la couleur change (l'alpha aussi, l'éditeur a l'alpha).
        h.click(UiHarness::center(*hex));
        h.keys({ui::kKeySelectAll});
        h.type("#ff000080");
        h.keys({ui::kKeyEnter});
        CHECK(near3(glm::vec3(color), {1, 0, 0}));
        CHECK(color.a == doctest::Approx(128.0f / 255.0f));
    }

    TEST_CASE("GradientEditor: a click on the bar adds a stop, a drag moves it, a right click removes it; the chosen stop is edited")
    {
        UiHarness h;
        ui::Gradient gradient; // noir -> blanc
        auto &editor = place(h, std::make_unique<ui::GradientEditor>(ui::Binding<ui::Gradient>::ref(gradient)), {400, 420});
        ui::Widget &bar = *editor.children()[0];
        const ui::Rect r = bar.rect();
        // Barre : de r.x à r.x + largeur ; clic au milieu (dans la barre, au-dessus des arrêts).
        h.press({r.position.x + r.size.x * 0.5f, r.position.y + 3});
        h.release({r.position.x + r.size.x * 0.5f, r.position.y + 3});
        REQUIRE(gradient.stops.size() == 3);
        CHECK(gradient.stops[2].position == doctest::Approx(0.5f).epsilon(0.02));
        CHECK(near3(glm::vec3(gradient.stops[2].color), glm::vec3(0.5f), 0.02f)); // couleur du dégradé à cet endroit
        CHECK(editor.selected() == 2);
        // Glisser cet arrêt (sous la barre) vers 75 %.
        const float markerY = r.end().y - 4.0f;
        h.press({r.position.x + r.size.x * 0.5f, markerY});
        h.press({r.position.x + r.size.x * 0.75f, markerY});
        h.release({r.position.x + r.size.x * 0.75f, markerY});
        CHECK(gradient.stops[2].position == doctest::Approx(0.75f).epsilon(0.03));
        // Clic droit : supprimé.
        h.secondaryClick({r.position.x + r.size.x * 0.75f, markerY});
        CHECK(gradient.stops.size() == 2);
    }

    TEST_CASE("CurveEditor: add a point, drag it (keeping the order), remove it; the end points stay at x = 0 and 1")
    {
        UiHarness h;
        ui::Curve curve; // (0,0) -> (1,1)
        auto &editor = place(h, std::make_unique<ui::CurveEditor>(ui::Binding<ui::Curve>::ref(curve)), {200, 200});
        const ui::Rect r = editor.rect();
        // Surface utile : marge du thème de test (padding 0) + 0,4 × width (10) = 4 px de chaque côté.
        auto at = [&](glm::vec2 p) { return glm::vec2(r.position.x + 4 + p.x * (r.size.x - 8), r.end().y - 4 - p.y * (r.size.y - 8)); };
        h.click(at({0.5f, 0.2f}));
        REQUIRE(curve.points.size() == 3);
        CHECK(curve.points[1].x == doctest::Approx(0.5f).epsilon(0.02));
        CHECK(curve.points[1].y == doctest::Approx(0.2f).epsilon(0.02));
        h.press(at({0.5f, 0.2f}));
        h.press(at({0.9f, 0.8f}));
        h.release(at({0.9f, 0.8f}));
        CHECK(curve.points[1].y == doctest::Approx(0.8f).epsilon(0.02));
        CHECK(curve.points[1].x < curve.points[2].x);
        h.press(at({0.0f, 0.0f})); // l'extrémité ne quitte pas x = 0
        h.press(at({0.3f, 0.5f}));
        h.release(at({0.3f, 0.5f}));
        CHECK(curve.points[0].x == 0.0f);
        CHECK(curve.points[0].y == doctest::Approx(0.5f).epsilon(0.02));
        h.secondaryClick(at(curve.points[1]));
        CHECK(curve.points.size() == 2);
        h.secondaryClick(at(curve.points[0])); // une extrémité ne se supprime pas
        CHECK(curve.points.size() == 2);
    }
}
