// REN-36 / UI-07 : texte accentué (UTF-8 : Latin-1, Latin étendu A, ponctuation typographique), polices nommées par un thème, et
// sans GPU (FreeType + MSDF tournent sur le processeur). Police de référence : res/fonts/Roboto-Regular.ttf.
#include "ui/ui_harness.hpp"
#include "renderer/repo_paths.hpp"
#include "support/temp_dir.hpp"

#include <orosmo/ui/font.hpp>

#include <orosmo/ui/text_renderer.hpp>

#include <algorithm>
#include <filesystem>

namespace ui = orosmo::ui;
namespace rd = orosmo::renderer;

namespace
{
std::filesystem::path roboto() { return tests::repoRoot(__FILE__) / "res" / "fonts" / "Roboto-Regular.ttf"; }

std::size_t glyphQuads(const rd::DrawList &list)
{
    std::size_t n = 0;
    for (const auto &c : list.commands)
        if (const auto *g = std::get_if<rd::DrawGlyphs>(&c))
            n += g->vertices.size() / 24;
    return n;
}
} // namespace

TEST_SUITE("ui/text")
{
    TEST_CASE("accented letters and typographic punctuation are drawn; line metrics do not change when they are loaded")
    {
        orosmo::ui::TextRenderer text;
        REQUIRE(text.init(800, 600));
        REQUIRE(text.loadFont(roboto().string(), "default", 48));
        const glm::vec2 ascii = text.measureText("Hg", 0.5f);
        CHECK(text.measureTextWidth("e", 0.5f) > 0.0f);
        // Avant : un octet non ASCII n'avait pas de glyphe (largeur nulle).
        const float e = text.measureTextWidth("é", 0.5f);
        CHECK(e > 0.0f);
        CHECK(text.measureTextWidth("éé", 0.5f) == doctest::Approx(2.0f * e).epsilon(0.05));
        CHECK(text.measureTextWidth("Ça déjà « œuvre » – 12 € …", 0.5f) > text.measureTextWidth("Ca deja", 0.5f));
        CHECK(text.measureText("Hg", 0.5f) == ascii); // hauteur et ligne de base inchangées par les glyphes étendus

        rd::DrawList list;
        text.setDrawListSink(&list);
        text.renderText("Été", 0, 0, 0.5f);
        text.setDrawListSink(nullptr);
        CHECK(glyphQuads(list) == 3); // É, t, é : trois glyphes
        // Une séquence UTF-8 invalide ne fait pas sortir de la chaîne et ne compte pas comme une lettre.
        CHECK(text.measureTextWidth(std::string("a\xc3"), 0.5f) == doctest::Approx(text.measureTextWidth("a", 0.5f)));
        // Formes trop longues : C0 8A n'est pas un saut de ligne (la mesure continue), C0 AF n'est pas « / ».
        CHECK(text.measureTextWidth(std::string("a\xc0\x8a" "bbbb"), 0.5f) == doctest::Approx(text.measureTextWidth("abbbb", 0.5f)));
        CHECK(text.measureTextWidth(std::string("\xc0\xaf"), 0.5f) < text.measureTextWidth("/", 0.5f));
    }

    TEST_CASE("letters with a flat bottom all end on the same pixel row (the baseline), at any size and position")
    {
        orosmo::ui::TextRenderer text;
        REQUIRE(text.init(800, 600));
        REQUIRE(text.loadFont(roboto().string(), "default", 48));
        int sizes = 0;
        for (float scale = 0.26f; scale <= 0.8f; scale += 0.027f)
        {
            for (float y : {0.0f, 10.3f, 21.5f, 33.8f})
            {
                rd::DrawList list;
                text.setDrawListSink(&list);
                text.renderText("xzmnhkriElLTH", 5.0f, y, scale);
                text.setDrawListSink(nullptr);
                std::vector<float> bottoms;
                for (const auto &c : list.commands)
                    if (const auto *g = std::get_if<rd::DrawGlyphs>(&c))
                        for (std::size_t q = 0; q + 24 <= g->vertices.size(); q += 24)
                            bottoms.push_back(g->vertices[q + 5]); // 2e sommet : bas gauche du glyphe
                REQUIRE(bottoms.size() == 13);
                const auto [lo, hi] = std::minmax_element(bottoms.begin(), bottoms.end());
                CAPTURE(scale);
                CAPTURE(y);
                CHECK(*hi - *lo == 0.0f);
            }
            ++sizes;
        }
        CHECK(sizes > 15);
    }

    TEST_CASE("letters are evenly spaced: a repeated pair always advances by the same distance (no per-letter rounding)")
    {
        orosmo::ui::TextRenderer text;
        REQUIRE(text.init(800, 600));
        REQUIRE(text.loadFont(roboto().string(), "default", 48));
        for (float scale = 0.26f; scale <= 0.8f; scale += 0.027f)
        {
            rd::DrawList list;
            text.setDrawListSink(&list);
            text.renderText("rlrlrlrlrlrl", 5.3f, 10.0f, scale);
            text.setDrawListSink(nullptr);
            std::vector<float> lefts;
            for (const auto &c : list.commands)
                if (const auto *g = std::get_if<rd::DrawGlyphs>(&c))
                    for (std::size_t q = 0; q + 24 <= g->vertices.size(); q += 24)
                        lefts.push_back(g->vertices[q]);
            REQUIRE(lefts.size() == 12);
            CAPTURE(scale);
            const float step = lefts[2] - lefts[0]; // « rl » : même avance à chaque répétition
            for (std::size_t i = 2; i + 2 < lefts.size(); i += 2)
                CHECK(lefts[i + 2] - lefts[i] == doctest::Approx(step).epsilon(1e-4));
            for (std::size_t i = 1; i + 2 < lefts.size(); i += 2)
                CHECK(lefts[i + 2] - lefts[i] == doctest::Approx(step).epsilon(1e-4));
        }
    }

    TEST_CASE("TextRendererFont: the font named by the style is loaded on first use; an unreadable one falls back to the default")
    {
        orosmo::ui::TextRenderer text;
        REQUIRE(text.init(800, 600));
        REQUIRE(text.loadFont(roboto().string(), "default", 48));
        ui::TextRendererFont font(text);
        const glm::vec2 base = font.measure("Hello", 20.0f);
        ui::FontFace same{"same", roboto()};
        CHECK(font.measure("Hello", 20.0f, &same) == base); // le même fichier sous un autre nom : mêmes mesures

        tests::TempDir dir("ui_text_font");
        dir.write("not-a-font.ttf", "this is not a font");
        ui::FontFace broken{"broken", dir.path / "not-a-font.ttf"};
        CHECK(font.measure("Hello", 20.0f, &broken) == base); // signalé une fois, police par défaut

        // Une autre police du système, si elle est là : des mesures différentes prouvent que la police change vraiment.
        const std::filesystem::path dejavu = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
        if (std::filesystem::exists(dejavu))
        {
            ui::FontFace other{"dejavu", dejavu};
            CHECK(font.measure("Hello world", 20.0f, &other).x != font.measure("Hello world", 20.0f).x);
            CHECK(font.measure("Hello", 20.0f).x == base.x); // la police par défaut est rétablie
        }
    }

    TEST_CASE("themes name fonts (section fonts, property font, per state); a missing file or an unknown name is an error")
    {
        tests::TempDir dir("ui_theme_font");
        std::filesystem::copy_file(roboto(), dir.path / "title.ttf");
        dir.write("t.json", R"({"fonts": {"title": "title.ttf"},
            "widgets": {"title": {"font": "title"}, "button": {"states": {"hover": {"font": "title"}}}, "label": {"font": "default"}}})");
        auto theme = ui::Theme::load(dir.path / "t.json");
        INFO((theme ? std::string() : theme.error().message));
        REQUIRE(theme.has_value());
        REQUIRE(theme->style("title", ui::WidgetState::kNormal).font != nullptr);
        CHECK(theme->style("title", ui::WidgetState::kNormal).font->name == "title");
        CHECK(theme->style("button", ui::WidgetState::kNormal).font == nullptr);
        CHECK(theme->style("button", ui::WidgetState::kHover).font == theme->font("title"));
        CHECK(theme->style("label", ui::WidgetState::kNormal).font == nullptr);
        CHECK(theme->sources().back().filename() == "title.ttf");
        ui::Label l("x");
        l.overrideStyle(ui::StyleProperty::kFont, "title"); // surcharge locale (langage : .style(font: "title"))
        CHECK(l.style(*theme).font == theme->font("title"));

        dir.write("bad1.json", R"({"fonts": {"x": "missing.ttf"}})");
        auto bad1 = ui::Theme::load(dir.path / "bad1.json");
        REQUIRE_FALSE(bad1.has_value());
        CHECK(bad1.error().message.find("fonts.x: cannot read") != std::string::npos);
        dir.write("bad2.json", R"({"widgets": {"label": {"font": "nope"}}})");
        auto bad2 = ui::Theme::load(dir.path / "bad2.json");
        REQUIRE_FALSE(bad2.has_value());
        CHECK(bad2.error().message.find("widgets.label.font: unknown font 'nope'") != std::string::npos);
    }
}
