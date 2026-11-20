// Revue M3 (3e passe) : arrondi des liaisons entières, bornes des champs numériques réfléchis selon le type, valeurs par défaut d'un
// panneau réfléchi sans T{} superflu, curseurs entiers bornés aux entiers de la plage, alpha du texte jusqu'à la liste de dessin,
// découpage UTF-8 de l'édition identique au rendu, identifiants d'atlas. Sans GPU.
#include "ui/ui_harness.hpp"
#include "renderer/repo_paths.hpp"

#include <orosmo/renderer/draw_list.hpp>
#include <orosmo/ui/font.hpp>
#include <orosmo/ui/reflect.hpp>
#include <orosmo/ui/text_renderer.hpp>

#include <cstdint>
#include <limits>
#include <string>

namespace ui = orosmo::ui;
namespace rd = orosmo::renderer;
using tests::UiHarness;

namespace
{
struct Numbers
{
    double big = 1.0;
    float small = 1.0f;
    orosmo::core::Vec3 offset{0.0f};
    int between = 2;   // plage [0.5, 3.5] : entiers 1..3
    int closed = 0;    // plage [0, 3]
    int noInteger = 0; // plage [0.2, 0.8] : aucun entier
};

// Compte ses constructions par défaut (pour vérifier qu'aucun T{} n'est construit quand des valeurs par défaut sont données).
int gCountedDefaults = 0;
struct Counted
{
    Counted() { ++gCountedDefaults; }
    float level = 0.5f;
};

// Sans constructeur par défaut : un panneau ne peut pas en construire.
struct NoDefault
{
    explicit NoDefault(float v) : level(v) {}
    float level;
};

std::filesystem::path roboto() { return tests::repoRoot(__FILE__) / "res" / "fonts" / "Roboto-Regular.ttf"; }

// Remplace le texte du champ par `text` et valide (Entrée).
void enter(UiHarness &h, ui::Widget &field, const std::string &text)
{
    h.click(UiHarness::center(field));
    h.keys({ui::kKeyEnd});
    h.keys(std::vector<int>(64, ui::kKeyBackspace));
    h.type(text);
    h.keys({ui::kKeyEnter});
}

float glyphAlpha(const rd::DrawList &list)
{
    for (const auto &c : list.commands)
        if (const auto *g = std::get_if<rd::DrawGlyphs>(&c))
            return g->alpha;
    return -1.0f;
}
} // namespace

template <> struct orosmo::core::Reflect<Numbers>
{
    static constexpr int kVersion = 1;
    static constexpr auto fields()
    {
        using N = Numbers;
        return std::make_tuple(field("big", &N::big), field("small", &N::small), field("offset", &N::offset),
                               field("between", &N::between, range(0.5, 3.5)), field("closed", &N::closed, range(0.0, 3.0)),
                               field("noInteger", &N::noInteger, range(0.2, 0.8)));
    }
};
template <> struct orosmo::core::Reflect<Counted>
{
    static constexpr int kVersion = 1;
    static constexpr auto fields() { return std::make_tuple(field("level", &Counted::level, range(0.0, 1.0))); }
};
template <> struct orosmo::core::Reflect<NoDefault>
{
    static constexpr int kVersion = 1;
    static constexpr auto fields() { return std::make_tuple(field("level", &NoDefault::level, range(0.0, 1.0))); }
};

TEST_SUITE("ui/review3")
{
    TEST_CASE("numberBinding<int64_t> rounds exactly (std::round) before saturating")
    {
        std::int64_t n = 7;
        auto b = ui::numberBinding(n);
        b.set(0.49999999999999994); // floor(v + 0.5) donnait 1
        CHECK(n == 0);
        b.set(4503599627370497.0); // 2^52 + 1 : floor(v + 0.5) donnait 2^52 + 2
        CHECK(n == 4503599627370497LL);
        b.set(2.5);
        CHECK(n == 3);
        b.set(-2.5);
        CHECK(n == -3);
        b.set(-0.49999999999999994);
        CHECK(n == 0);
        b.set(1e300);
        CHECK(n == std::numeric_limits<std::int64_t>::max());
        b.set(-1e300);
        CHECK(n == std::numeric_limits<std::int64_t>::lowest());
    }

    TEST_CASE("reflected number fields are bounded by their destination type")
    {
        UiHarness h;
        Numbers v;
        auto panel = ui::reflectedPanel(v);
        auto *p = panel.get();
        h.ui.setRoot(std::move(panel));
        h.frame();
        auto *big = p->findAs<ui::NumberField>("big");
        auto *small = p->findAs<ui::NumberField>("small");
        REQUIRE(big != nullptr);
        REQUIRE(small != nullptr);
        enter(h, *big, "1e301"); // représentable en double
        CHECK(v.big == 1e301);
        enter(h, *small, "1e39"); // fini en double, pas en float : refusé
        CHECK(v.small == 1.0f);
        enter(h, *small, "3e38");
        CHECK(v.small == 3e38f);
        auto *offset = p->find("offset");
        REQUIRE(offset != nullptr);
        REQUIRE(offset->children().size() == 3);
        enter(h, *offset->children()[0], "1e39"); // composante float d'un Vec3
        CHECK(v.offset.x == 0.0f);
        enter(h, *offset->children()[0], "-3e38");
        CHECK(v.offset.x == -3e38f);
    }

    TEST_CASE("an integer slider is bounded by the integers of its range; a range without an integer refuses every entry")
    {
        UiHarness h;
        Numbers v;
        auto panel = ui::reflectedPanel(v);
        auto *p = panel.get();
        h.ui.setRoot(std::move(panel));
        h.frame();
        auto *between = p->findAs<ui::Slider>("between");
        REQUIRE(between != nullptr);
        CHECK(between->min() == 1.0);
        CHECK(between->max() == 3.0);
        auto *closed = p->findAs<ui::Slider>("closed");
        REQUIRE(closed != nullptr);
        CHECK(closed->min() == 0.0);
        CHECK(closed->max() == 3.0);
        h.click(UiHarness::center(*closed));
        h.keys({ui::kKeyEnd});
        CHECK(v.closed == 3);

        CHECK(p->findAs<ui::Slider>("noInteger") == nullptr);
        auto *none = p->findAs<ui::NumberField>("noInteger");
        REQUIRE(none != nullptr);
        enter(h, *none, "1");
        CHECK(v.noInteger == 0);
        enter(h, *none, "0.5");
        CHECK(v.noInteger == 0);
    }

    TEST_CASE("reflectedPanel builds no T{} when defaults are given; types without a default constructor are accepted")
    {
        UiHarness h;
        Counted object;
        Counted defaults;
        defaults.level = 0.25f;
        gCountedDefaults = 0;
        auto panel = ui::reflectedPanel(object, {}, &defaults);
        CHECK(gCountedDefaults == 0);
        auto *slider = panel->findAs<ui::Slider>("level");
        REQUIRE(slider != nullptr);
        CHECK(slider->defaultValue() == doctest::Approx(0.25));
        auto fallback = ui::reflectedPanel(object); // sans valeurs par défaut : un T{} (0.5)
        CHECK(gCountedDefaults == 1);
        CHECK(fallback->findAs<ui::Slider>("level")->defaultValue() == doctest::Approx(0.5));

        NoDefault nd{0.75f};
        const NoDefault ndDefaults{0.1f};
        auto withDefaults = ui::reflectedPanel(nd, {}, &ndDefaults);
        CHECK(withDefaults->findAs<ui::Slider>("level")->defaultValue() == doctest::Approx(0.1));
        auto withoutDefaults = ui::reflectedPanel(nd); // défaut du curseur : valeur à sa création
        CHECK(withoutDefaults->findAs<ui::Slider>("level")->defaultValue() == doctest::Approx(0.75));
    }

    TEST_CASE("text alpha reaches the glyph runs; an RGB color stays opaque")
    {
        rd::DrawList fixed;
        ui::FixedFont{0.5f}.draw(fixed, "ab", {0, 0}, 10.0f, {1, 1, 1, 0.25f});
        CHECK(glyphAlpha(fixed) == 0.25f);

        ui::TextRenderer text;
        REQUIRE(text.init(800, 600));
        REQUIRE(text.loadFont(roboto().string(), "default", 48));
        ui::TextRendererFont font(text);
        rd::DrawList list;
        font.draw(list, "Hg", {0, 0}, 24.0f, {1, 0, 0, 0.4f});
        CHECK(glyphAlpha(list) == 0.4f);

        rd::DrawList rgb;
        text.setDrawListSink(&rgb);
        text.renderText("Hg", 0, 0, 0.5f, glm::vec3(1.0f, 0.0f, 0.0f));
        text.setDrawListSink(nullptr);
        CHECK(glyphAlpha(rgb) == 1.0f);
    }

    TEST_CASE("UTF-8 editing units follow the rendering rules: each invalid byte is one unit")
    {
        using ui::utf8Length;
        using ui::utf8Previous;
        CHECK(utf8Length("\xC3\xA9", 0) == 2);         // é
        CHECK(utf8Length("\xF0\x9F\x98\x80", 0) == 4); // U+1F600
        CHECK(utf8Length("\xE2\x82\xAC", 0) == 3);     // €
        CHECK(utf8Length("\x80", 0) == 1);             // continuation isolée
        CHECK(utf8Length("\xC0\xAF", 0) == 1);         // forme trop longue de « / »
        CHECK(utf8Length("\xE0\x80\x80", 0) == 1);     // forme trop longue
        CHECK(utf8Length("\xF0\x80\x80\x80", 0) == 1); // forme trop longue
        CHECK(utf8Length("\xED\xA0\x80", 0) == 1);     // demi-code de substitution
        CHECK(utf8Length("\xF4\x90\x80\x80", 0) == 1); // au-delà de U+10FFFF
        CHECK(utf8Length("\xF8\x80\x80\x80", 0) == 1); // F8–FF
        CHECK(utf8Length("\xFF", 0) == 1);
        CHECK(utf8Length("\xC3", 0) == 1);             // tronquée

        const std::string e = "a\xC3\xA9";
        CHECK(utf8Previous(e, 3) == 1);
        CHECK(utf8Previous(e, 1) == 0);
        CHECK(utf8Previous(e, 0) == 0);
        const std::string emoji = "\xF0\x9F\x98\x80";
        CHECK(utf8Previous(emoji, 4) == 0);
        const std::string surrogate = "a\xED\xA0\x80"; // trois octets invalides : on recule d'un seul
        CHECK(utf8Previous(surrogate, 4) == 3);
        CHECK(utf8Previous(surrogate, 3) == 2);
        CHECK(utf8Previous(surrogate, 2) == 1);
        const std::string overlong = "\xC0\xAF";
        CHECK(utf8Previous(overlong, 2) == 1);
        const std::string tooLong = "\x80\x80\x80\x80\x80"; // continuations isolées
        CHECK(utf8Previous(tooLong, 5) == 4);

        // Avancer puis reculer parcourt les mêmes frontières, quel que soit le texte.
        const std::string mixed = "x\xC3\xA9\x80\xF8\xED\xA0\x80\xE2\x82\xAC\xC0\xAFy\xF0\x9F\x98\x80";
        std::vector<std::size_t> forward{0};
        for (std::size_t i = 0; i < mixed.size(); i += utf8Length(mixed, i))
            forward.push_back(i + utf8Length(mixed, i));
        std::vector<std::size_t> backward{mixed.size()};
        for (std::size_t i = mixed.size(); i > 0;)
        {
            i = utf8Previous(mixed, i);
            backward.insert(backward.begin(), i);
        }
        CHECK(forward == backward);
    }

    TEST_CASE("atlas ids are unique, non zero and increasing")
    {
        const std::uint64_t a = rd::newAtlasId();
        const std::uint64_t b = rd::newAtlasId();
        CHECK(a != 0);
        CHECK(b > a);
    }
}
