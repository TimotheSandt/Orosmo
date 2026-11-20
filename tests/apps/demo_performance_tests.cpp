// Panneau de performances de la démo (apps/demo/src/UI/Views/PerformanceView.cui, compilé par cui vers engine::ui) : contenu,
// rafraîchissement environ 4 fois par seconde, thème res/themes/hud.json. Sans GPU : police à chasse fixe.
#include "renderer/repo_paths.hpp"
#include "ui/ui_harness.hpp"

#include "UI/Views/PerformanceView.gen.h"
#include "UI/performance_stats.hpp"

#include <engine/ui/theme.hpp>

namespace ui = engine::ui;
using tests::UiHarness;

namespace
{
PerformanceSample sample(double base)
{
    return {.fps = base, .renderMs = base + 1.0, .renderWorldMs = base + 2.0, .upscaleMs = base + 3.0, .uiUpscaleMs = base + 4.0,
            .swapBuffersMs = base + 5.0};
}

std::string labelText(ui::Widget &root, std::string_view id)
{
    auto *label = root.findAs<ui::Label>(id);
    REQUIRE(label != nullptr);
    return label->text();
}
} // namespace

TEST_SUITE("apps/demo")
{
    TEST_CASE("the performance panel shows the FPS and the Profiler averages in ms, in the top left corner")
    {
        UiHarness h;
        h.viewport = {1280, 720};
        PerformanceStats stats;
        CHECK(stats.tick(1.0f / 60.0f, [] { return sample(60.0); })); // la première image rafraîchit
        h.ui.setRoot(cui::buildPerformanceView(h.ui, stats));
        h.frame();
        ui::Widget *root = h.ui.root();
        REQUIRE(root != nullptr);
        CHECK(root->styleName() == "hud");
        CHECK(root->rect().position == glm::vec2(0.0f, 0.0f));
        CHECK(root->rect().size.x == doctest::Approx(260));
        CHECK(root->rect().size.y < 360.0f); // ajusté au contenu, pas toute la hauteur
        CHECK(labelText(*root, "fps") == "FPS: 60");
        CHECK(labelText(*root, "render") == "Render: 61 ms");
        CHECK(labelText(*root, "renderWorld") == "Render World: 62 ms");
        CHECK(labelText(*root, "upscale") == "Upscale: 63 ms");
        CHECK(labelText(*root, "uiUpscale") == "UI Upscale: 64 ms");
        CHECK(labelText(*root, "swapBuffers") == "Swap Buffers: 65 ms");
        CHECK(root->findAs<ui::Label>("fps")->styleName() == "hudTitle");
        CHECK(root->findAs<ui::Label>("render")->styleName() == "hudMetric");
    }

    TEST_CASE("the performance values change about four times per second")
    {
        PerformanceStats stats;
        int calls = 0;
        auto next = [&] { return sample(static_cast<double>(++calls)); };
        CHECK(stats.tick(0.01f, next));
        CHECK(stats.shown.fps == 1.0);
        for (int frame = 0; frame < 20; ++frame) // 0,2 s à 100 images par seconde : pas encore
        {
            CHECK_FALSE(stats.tick(0.01f, next));
        }
        CHECK(calls == 1);
        CHECK(stats.shown.fps == 1.0);
        for (int frame = 0; frame < 5; ++frame) // 0,25 s atteint
        {
            (void)stats.tick(0.01f, next);
        }
        CHECK(calls == 2);
        CHECK(stats.shown.fps == 2.0);
    }

    TEST_CASE("the demo theme hud.json loads and gives the panel its translucent background and text sizes")
    {
        const auto root = tests::repoRoot(__FILE__);
        REQUIRE_FALSE(root.empty());
        auto theme = ui::Theme::load(root / "res" / "themes" / "hud.json");
        REQUIRE_MESSAGE(theme.has_value(), (theme ? std::string() : theme.error().message));
        const ui::Style &panel = theme->style("hud", ui::WidgetState::kNormal);
        CHECK(panel.background.a == doctest::Approx(0xa6 / 255.0f).epsilon(0.01));
        CHECK(panel.padding == doctest::Approx(12));
        CHECK(theme->style("hudTitle", ui::WidgetState::kNormal).fontSize == doctest::Approx(24));
        CHECK(theme->style("hudMetric", ui::WidgetState::kNormal).fontSize == doctest::Approx(14.4));
    }
}
