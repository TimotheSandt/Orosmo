// UI-02 / UI-03 / UI-07 sur le GPU : un panneau de widgets (thème sombre livré, police réelle FreeType + MSDF) dessiné par le canevas
// du RHI : couleurs du thème aux bons endroits, texte visible, aucune erreur de validation.
#include <doctest/doctest.h>

#include "renderer/repo_paths.hpp"
#include "rhi/gpu_fixture.hpp"
#include "support/test_support.hpp"

#include <engine/renderer/canvas_renderer.hpp>
#include <engine/ui/ui.hpp>
#include <engine/ui/widgets.hpp>

#include "Rendering/TextRenderer.h"

namespace rd = engine::renderer;
namespace ui = engine::ui;

namespace
{
std::array<int, 4> over(const ui::Color &c)
{
    // Mélange sur une cible transparente (loadExisting = false) : couleur × alpha, alpha.
    return {static_cast<int>(std::lround(c.r * c.a * 255.0f)), static_cast<int>(std::lround(c.g * c.a * 255.0f)),
            static_cast<int>(std::lround(c.b * c.a * 255.0f)), static_cast<int>(std::lround(c.a * 255.0f))};
}
} // namespace

TEST_SUITE("ui/render")
{
    TEST_CASE("a widget panel drawn with the dark theme and the real font")
    {
        Gpu gpu;
        SKIP_TEST_IF_NULL(gpu.device.get(), "OpenGL 4.3 context");
        const auto root = tests::repoRoot(__FILE__);

        UI::TextRenderer text;
        REQUIRE(text.init(256, 128));
        REQUIRE(text.loadFont((root / "res" / "fonts" / "Roboto-Regular.ttf").string(), "default", 48));
        text.setActiveFont("default");
        ui::TextRendererFont font(text);
        CHECK(font.measure("Hg", 16.0f).y > 8.0f);
        CHECK(font.measure("abc", 16.0f).x > font.measure("a", 16.0f).x);

        ui::Ui u(font);
        REQUIRE(u.themes().load(root / "res" / "themes" / "dark.json").has_value());
        float value = 0.5f;
        bool flag = true;
        auto panel = std::make_unique<ui::Panel>();
        panel->setAlign(ui::Align::kStart, ui::Align::kStart);
        panel->setWidth(ui::SizeSpec::fixed(200));
        auto &title = panel->emplace<ui::Label>("Terrain", "title");
        auto &button = panel->emplace<ui::Button>("Generate");
        auto &slider = panel->emplace<ui::Slider>(ui::numberBinding(value), 0.0, 1.0);
        panel->emplace<ui::Checkbox>("Snow", ui::Binding<bool>::ref(flag));
        auto overlay = std::make_unique<ui::Overlay>();
        overlay->add(std::move(panel));
        u.setRoot(std::move(overlay));
        u.layout({256, 160});

        rd::DrawList list;
        u.draw(list);
        CHECK(list.count<rd::DrawGlyphs>() >= 4);

        rd::ShaderLibrary library(*gpu.device);
        rd::PipelineCache pipelines(*gpu.device);
        auto canvas = rd::CanvasRenderer::create(*gpu.device, library, pipelines, root / "shaders" / "rhi");
        REQUIRE(canvas.has_value());
        TextureHandle target = gpu.target(Format::kRGBA8Unorm, {256, 160}, "ui");
        auto encoder = gpu->createCommandEncoder();
        REQUIRE((*canvas)->render(*encoder, list, {target, Format::kRGBA8Unorm, {256, 160}}, false).has_value());
        REQUIRE(gpu->submit(std::move(encoder)).has_value());

        const ui::Theme &theme = u.theme();
        auto expect = [&](glm::vec2 p, const ui::Color &c, int tolerance)
        {
            const auto px = rgba(gpu.pixel(target, static_cast<int>(p.x), static_cast<int>(p.y)));
            const auto want = over(c);
            CAPTURE(p.x);
            CAPTURE(p.y);
            for (int i = 0; i < 3; ++i) // l'alpha d'une cible transparente dépend du mode de mélange de l'alpha : non vérifié
            {
                CAPTURE(i);
                CHECK(near(px[static_cast<std::size_t>(i)], want[static_cast<std::size_t>(i)], tolerance));
            }
        };
        // Fond du panneau (entre deux widgets, à gauche), fond du bouton (dans un coin intérieur, sans texte), remplissage du curseur
        // (moitié gauche).
        const ui::Rect b = button.rect();
        const ui::Rect s = slider.rect();
        expect({b.position.x + 3, b.position.y - 3}, theme.style("panel", ui::WidgetState::kNormal).background, 2);
        const ui::Color buttonBg = theme.style("button", ui::WidgetState::kNormal).background;
        const ui::Color panelBg = theme.style("panel", ui::WidgetState::kNormal).background;
        // Le bouton est opaque sur le panneau translucide : sa couleur l'emporte (alpha du panneau conservé par le mélange).
        const auto px = rgba(gpu.pixel(target, static_cast<int>(b.position.x + 4), static_cast<int>(b.position.y + 4)));
        CHECK(near(px[0], static_cast<int>(std::lround(buttonBg.r * 255.0f)), 3));
        CHECK(near(px[1], static_cast<int>(std::lround(buttonBg.g * 255.0f)), 3));
        const ui::Color accent = theme.style("slider", ui::WidgetState::kNormal).accent;
        const auto fill = rgba(gpu.pixel(target, static_cast<int>(s.position.x + 4), static_cast<int>(s.position.y + s.size.y * 0.5f)));
        CHECK(near(fill[2], static_cast<int>(std::lround(accent.b * 255.0f)), 3));
        (void)panelBg;
        // Du texte est visible dans le titre : au moins un pixel nettement plus clair que le fond.
        const ui::Rect t = title.rect();
        int bright = 0;
        for (int y = static_cast<int>(t.position.y); y < static_cast<int>(t.end().y); ++y)
            for (int x = static_cast<int>(t.position.x); x < static_cast<int>(t.position.x + 60); ++x)
                bright += rgba(gpu.pixel(target, x, y))[0] > 150 ? 1 : 0;
        CHECK(bright > 10);
        gpu.expectNoValidationErrors();
    }
}
