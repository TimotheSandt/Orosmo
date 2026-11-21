// UI-07 : fonds en neuf parties (images de thème) : coins à taille fixe, bords et centre étirés, répétés ou supprimés ; lecture
// depuis le JSON du thème ; dessin par le widget. Sans GPU (le dessin GPU est dans tests/renderer/canvas_tests.cpp).
#include "ui/ui_harness.hpp"
#include "support/temp_dir.hpp"

#include <orosmo/renderer/image.hpp>
#include <orosmo/ui/nine_slice.hpp>

namespace ui = orosmo::ui;
namespace rd = orosmo::renderer;

namespace
{
ui::ImageStyle image12(ui::SliceFill edges, ui::SliceFill center, float scale = 1.0f)
{
    auto atlas = std::make_shared<rd::AtlasImage>();
    atlas->id = rd::newAtlasId();
    atlas->width = 12;
    atlas->height = 12;
    atlas->rgba.assign(12 * 12 * 4, 255);
    ui::ImageStyle s;
    s.image = atlas;
    s.left = s.top = s.right = s.bottom = 4; // coins 4x4, bandes de 4 texels
    s.edges = edges;
    s.center = center;
    s.scale = scale;
    return s;
}
std::size_t quads(const std::vector<float> &v) { return v.size() / 24; }
// Rectangle destination du quadrilatère `i`.
ui::Rect quadRect(const std::vector<float> &v, std::size_t i)
{
    const float *q = v.data() + i * 24;
    return {{q[0], q[1]}, {q[8] - q[0], q[9] - q[1]}};
}
} // namespace

TEST_SUITE("ui/nineslice")
{
    TEST_CASE("stretch: 9 pieces; the corners keep their size whatever the widget size")
    {
        const auto s = image12(ui::SliceFill::kStretch, ui::SliceFill::kStretch, 2.0f);
        const auto v = ui::nineSliceVertices(s, {{10, 20}, {100, 40}});
        REQUIRE(quads(v) == 9);
        CHECK(tests::sameRect(quadRect(v, 0), {10, 20}, {8, 8}));  // coin haut gauche : 4 texels × 2
        CHECK(tests::sameRect(quadRect(v, 3), {102, 52}, {8, 8})); // coin bas droit
        CHECK(tests::sameRect(quadRect(v, 4), {18, 20}, {84, 8})); // bord haut étiré
        CHECK(tests::sameRect(quadRect(v, 8), {18, 28}, {84, 24})); // centre
        // UV du coin haut gauche : texels [0, 4] rentrés d'un demi-texel.
        CHECK(v[2] == doctest::Approx(0.5f / 12.0f));
        CHECK(v[10] == doctest::Approx(3.5f / 12.0f));
    }

    TEST_CASE("repeat: the bands are tiled at their size, the last tile is cut (not squeezed)")
    {
        const auto s = image12(ui::SliceFill::kRepeat, ui::SliceFill::kRepeat);
        // Largeur intérieure 10 = 4 + 4 + 2 ; hauteur intérieure 4 = 1 tuile.
        const auto v = ui::nineSliceVertices(s, {{0, 0}, {18, 12}});
        // 4 coins + haut/bas (3 tuiles × 2) + gauche/droite (1 × 2) + centre 3 × 1.
        REQUIRE(quads(v) == 4 + 6 + 2 + 3);
        const ui::Rect lastTop = quadRect(v, 4 + 4); // troisième tuile du haut (paires haut/bas)
        CHECK(tests::sameRect(lastTop, {12, 0}, {2, 4}));
        // Sa source : texels [4, 6] (coupée), pas [4, 8] comprimés.
        const float *q = v.data() + (4 + 4) * 24;
        CHECK(q[2] == doctest::Approx(4.5f / 12.0f));
        CHECK(q[10] == doctest::Approx(5.5f / 12.0f));
    }

    TEST_CASE("none: bands removed (a frame without a centre, or the corners alone); corners shrink in a small widget")
    {
        const auto frame = image12(ui::SliceFill::kStretch, ui::SliceFill::kNone);
        CHECK(quads(ui::nineSliceVertices(frame, {{0, 0}, {50, 50}})) == 8);
        const auto corners = image12(ui::SliceFill::kNone, ui::SliceFill::kNone);
        CHECK(quads(ui::nineSliceVertices(corners, {{0, 0}, {50, 50}})) == 4);
        const auto v = ui::nineSliceVertices(image12(ui::SliceFill::kStretch, ui::SliceFill::kStretch), {{0, 0}, {4, 50}});
        CHECK(tests::sameRect(quadRect(v, 0), {0, 0}, {2, 4})); // deux coins de 4 dans 4 px : réduits à 2
    }

    TEST_CASE("repeat: the number of repetitions is bounded; a tiny scale cannot loop forever; the theme refuses such a scale")
    {
        // Période de 4e-10 px : l'ancienne boucle `d += période` ne progressait plus en float.
        const auto v = ui::nineSliceVertices(image12(ui::SliceFill::kRepeat, ui::SliceFill::kRepeat, 1e-10f), {{100, 100}, {300, 200}});
        CHECK(quads(v) <= 9u * 4096u);
        tests::TempDir dir("ui_nineslice");
        rd::Image png;
        png.width = 12;
        png.height = 12;
        png.rgba.assign(12 * 12 * 4, 200);
        REQUIRE(rd::savePng(png, dir.path / "wood.png").has_value());
        dir.write("t.json", R"({"images": {"wood": {"file": "wood.png", "slice": 4, "edges": "repeat", "scale": 1e-10}}})");
        auto theme = ui::Theme::load(dir.path / "t.json");
        REQUIRE_FALSE(theme.has_value());
        CHECK(theme.error().message.find("scale") != std::string::npos);
    }

    TEST_CASE("theme: images are read from JSON (file relative to the theme), set per widget and per state, errors are named")
    {
        tests::TempDir dir("ui_nineslice");
        rd::Image png;
        png.width = 12;
        png.height = 12;
        png.rgba.assign(12 * 12 * 4, 200);
        std::filesystem::create_directories(dir.path / "img");
        REQUIRE(rd::savePng(png, dir.path / "img" / "wood.png").has_value());
        dir.write("t.json", R"({
            "images": {"wood": {"file": "img/wood.png", "slice": [4, 4, 4, 4], "edges": "repeat", "center": "stretch", "scale": 2}},
            "tokens": {"white": "#ffffff", "red": "#ff0000"},
            "widgets": {"button": {"image": "wood", "imageTint": "white", "states": {"hover": {"imageTint": "red"}, "disabled": {"image": "none"}}}}
        })");
        auto theme = ui::Theme::load(dir.path / "t.json");
        INFO((theme ? std::string() : theme.error().message));
        REQUIRE(theme.has_value());
        const ui::Style &normal = theme->style("button", ui::WidgetState::kNormal);
        REQUIRE(normal.image != nullptr);
        CHECK(normal.image->edges == ui::SliceFill::kRepeat);
        CHECK(normal.image->scale == 2.0f);
        CHECK(normal.image->image->width == 12);
        CHECK(theme->style("button", ui::WidgetState::kHover).imageTint == ui::Color(1, 0, 0, 1));
        CHECK(theme->style("button", ui::WidgetState::kDisabled).image == nullptr);
        CHECK(theme->sources().back().filename() == "wood.png"); // rechargé à chaud quand l'image change

        // Dessin : le fond du bouton est l'image (une commande DrawImage), pas un rectangle de couleur.
        tests::UiHarness h;
        h.ui.themes().set(std::move(*theme));
        auto root = std::make_unique<ui::Overlay>();
        auto &b = root->emplace<ui::Button>("x");
        b.setAlign(ui::Align::kStart, ui::Align::kStart);
        b.setWidth(ui::SizeSpec::fixed(60)).setHeight(ui::SizeSpec::fixed(30));
        h.ui.setRoot(std::move(root));
        h.frame();
        rd::DrawList list;
        h.ui.draw(list);
        REQUIRE(list.count<rd::DrawImage>() == 1);
        CHECK(list.count<rd::DrawRect>() == 0);

        auto expectError = [&](const char *json, const std::string &fragment)
        {
            dir.write("bad.json", json);
            auto t = ui::Theme::load(dir.path / "bad.json");
            REQUIRE_FALSE(t.has_value());
            INFO(t.error().message);
            CHECK(t.error().message.find(fragment) != std::string::npos);
        };
        expectError(R"({"images": {"w": {"file": "missing.png"}}})", "images.w.file: cannot read");
        expectError(R"({"images": {"w": {"file": "img/wood.png", "slice": [8, 0, 8, 0]}}})", "images.w.slice: the corners do not fit");
        expectError(R"({"images": {"w": {"file": "img/wood.png", "edges": "tile"}}})", "images.w.edges: expected \"stretch\", \"repeat\" or \"none\"");
        expectError(R"({"widgets": {"button": {"image": "nope"}}})", "widgets.button.image: unknown image 'nope'");
        expectError(R"({"images": {"w": {"file": "img/wood.png"}}, "widgets": {"button": {"radius": "w"}}})", "widgets.button.radius: expects a number");
    }

    TEST_CASE("a local override can name an image of the theme")
    {
        tests::TempDir dir("ui_nineslice2");
        rd::Image png;
        png.width = 4;
        png.height = 4;
        png.rgba.assign(64, 255);
        REQUIRE(rd::savePng(png, dir.path / "p.png").has_value());
        dir.write("t.json", R"({"images": {"plain": {"file": "p.png"}}})");
        auto theme = ui::Theme::load(dir.path / "t.json");
        REQUIRE(theme.has_value());
        ui::Button b("x");
        b.overrideStyle(ui::StyleProperty::kImage, "plain");
        CHECK(b.style(*theme).image == theme->image("plain"));
        b.overrideStyle(ui::StyleProperty::kImage, "none");
        CHECK(b.style(*theme).image == nullptr);
    }
}
