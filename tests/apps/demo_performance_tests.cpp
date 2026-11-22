// Panneau de performances de la démo (apps/demo/src/UI/Views/PerformanceView.cui, compilé par cui vers orosmo::ui) : niveaux
// (--hud, Ctrl+F), contenu par niveau, textes « n/d », rafraîchissement environ 4 fois par seconde, historique des temps d'image,
// thème res/themes/hud.json. Sans GPU : police à chasse fixe.
#include "renderer/repo_paths.hpp"
#include "ui/ui_harness.hpp"

#include "UI/Views/PerformanceView.gen.h"
#include "UI/performance_stats.hpp"

#include <orosmo/ui/theme.hpp>

#include <string>

namespace ui = orosmo::ui;
using demo::HudLevel;
using tests::UiHarness;

namespace
{
PerformanceSample sample(double base)
{
    PerformanceSample s;
    s.fpsAverage = base;
    s.fpsLow1 = base - 10.0;
    s.fpsMin = base - 20.0;
    s.fpsMax = base + 20.0;
    s.frameCpuMs = 1.5;
    s.gpuMs = 0.75;
    s.gpuPasses = {{"scene", 0.25}, {"upscale", 0.125}, {"ui", 0.0625}, {"present", 0.5}};
    s.updateMs = 0.1;
    s.recordMs = 0.2;
    s.graphMs = 0.3;
    s.submitMs = 0.4;
    s.presentMs = 0.5;
    s.sceneMeshes = 6;
    s.uiDraws = 12;
    s.lights = 1;
    s.backend = "OpenGL";
    s.device = "Test GPU (1.0)";
    s.outputWidth = 1280;
    s.outputHeight = 720;
    s.renderWidth = 640;
    s.renderHeight = 360;
    s.renderScale = 0.5f;
    s.modes = "AA none   post none   shadows off   lighting forward";
    s.residentMiB = 123.0;
    return s;
}

bool shown(UiHarness &h, std::string_view id)
{
    ui::Widget *w = h.ui.root()->find(id);
    REQUIRE(w != nullptr);
    for (; w != nullptr; w = w->parent())
    {
        if (!w->visible())
        {
            return false;
        }
    }
    return true;
}

std::string labelText(UiHarness &h, std::string_view id)
{
    auto *label = h.ui.root()->findAs<ui::Label>(id);
    REQUIRE(label != nullptr);
    return label->text();
}
} // namespace

TEST_SUITE("apps/demo")
{
    TEST_CASE("--hud levels: parsing, names and the Ctrl+F cycle off -> fps -> full -> graph -> off")
    {
        CHECK(demo::parseHudLevel("off") == HudLevel::kOff);
        CHECK(demo::parseHudLevel("FPS") == HudLevel::kFps);
        CHECK(demo::parseHudLevel("full") == HudLevel::kFull);
        CHECK(demo::parseHudLevel("Graph") == HudLevel::kGraph);
        CHECK_FALSE(demo::parseHudLevel("all").has_value());
        CHECK(demo::nextHudLevel(HudLevel::kOff) == HudLevel::kFps);
        CHECK(demo::nextHudLevel(HudLevel::kFps) == HudLevel::kFull);
        CHECK(demo::nextHudLevel(HudLevel::kFull) == HudLevel::kGraph);
        CHECK(demo::nextHudLevel(HudLevel::kGraph) == HudLevel::kOff);
        PerformanceStats stats;
        CHECK(stats.level == HudLevel::kFull); // défaut : le panneau complet, comme avant
        for (const HudLevel expected : {HudLevel::kGraph, HudLevel::kOff, HudLevel::kFps, HudLevel::kFull})
        {
            stats.cycleLevel();
            CHECK(stats.level == expected);
            CHECK(demo::parseHudLevel(demo::hudLevelName(expected)) == expected);
        }
    }

    TEST_CASE("each HUD level shows its content: nothing, the FPS, every measurement, then the graph in the bottom right corner")
    {
        UiHarness h;
        h.viewport = {1280, 720};
        PerformanceStats stats;
        for (int i = 0; i < 10; ++i)
        {
            stats.pushFrame(2.0f + static_cast<float>(i), 1.0f, 4.0f);
        }
        CHECK(stats.tick(1.0f / 60.0f, [] { return sample(250.0); }));
        h.ui.setRoot(cui::buildPerformanceView(h.ui, stats));

        stats.level = HudLevel::kOff;
        h.frame();
        CHECK_FALSE(shown(h, "fps"));
        CHECK_FALSE(shown(h, "cpuGraph"));

        stats.level = HudLevel::kFps;
        h.frame();
        CHECK(shown(h, "fps"));
        CHECK(labelText(h, "fps") == "FPS: 250");
        for (const char *id : {"fpsDetail", "frame", "gpu", "passes0", "cpu", "draws", "objects", "device", "resolution", "modes", "memory", "hint"})
        {
            CHECK_MESSAGE(!shown(h, id), id);
        }
        CHECK_FALSE(shown(h, "cpuGraph"));
        ui::Widget *panel = h.ui.root()->find("hudPanel");
        CHECK(panel->rect().position == glm::vec2(0.0f, 0.0f)); // en haut à gauche

        stats.level = HudLevel::kFull;
        h.frame();
        for (const char *id : {"fps", "fpsDetail", "frame", "gpu", "passes0", "passes1", "cpu", "draws", "objects", "device", "resolution", "modes",
                               "memory", "hint"})
        {
            CHECK_MESSAGE(shown(h, id), id);
        }
        CHECK_FALSE(shown(h, "passes2")); // 4 passes : deux lignes
        CHECK_FALSE(shown(h, "cpuGraph"));
        CHECK(labelText(h, "fpsDetail") == "1% low 240   min 230   max 270");
        CHECK(labelText(h, "frame") == "Frame CPU: 1.50 ms");
        CHECK(labelText(h, "gpu") == "GPU (Render): 0.75 ms");
        CHECK(labelText(h, "passes0") == "  scene 0.25   upscale 0.12   ui 0.06");
        CHECK(labelText(h, "passes1") == "  present 0.50");
        CHECK(labelText(h, "cpu") == "CPU: update 0.10  scene 0.20  graph 0.30  submit 0.40  present 0.50 ms");
        CHECK(labelText(h, "draws") == "Draws: scene 6  instanced n/d (n/d inst.)  shadows n/d  UI 12");
        CHECK(labelText(h, "objects") == "Objects visible: n/d / n/d   lights 1");
        CHECK(labelText(h, "device") == "OpenGL: Test GPU (1.0)");
        CHECK(labelText(h, "resolution") == "Output 1280x720   render 640x360 (x0.50)   vsync off");
        CHECK(labelText(h, "memory") == "Memory: RSS 123 MiB   GPU n/d");

        stats.level = HudLevel::kGraph;
        h.frame();
        CHECK(shown(h, "fps"));
        CHECK(shown(h, "memory"));
        CHECK(shown(h, "cpuGraph"));
        CHECK(shown(h, "gpuGraph"));
        CHECK(labelText(h, "cpuGraphLabel") == "CPU frame (ms): last 11.00  max 11.00");
        ui::Widget *graph = h.ui.root()->find("hudGraph");
        REQUIRE(graph != nullptr);
        // En bas à droite de la vue.
        CHECK(graph->rect().end().x == doctest::Approx(1280.0f));
        CHECK(graph->rect().end().y == doctest::Approx(720.0f));
    }

    TEST_CASE("the frame history keeps the last frames in order and gives the FPS statistics")
    {
        PerformanceStats stats;
        PerformanceSample s;
        stats.fpsFromHistory(s);
        CHECK(s.fpsAverage == 0.0);
        stats.pushFrame(1.0f, 0.5f, 0.0f); // première image : pas d'intervalle
        for (std::size_t i = 1; i < PerformanceStats::kHistorySize + 10; ++i)
        {
            stats.pushFrame(static_cast<float>(i), 0.5f, i % 50 == 0 ? 20.0f : 10.0f);
        }
        CHECK(stats.historySize() == PerformanceStats::kHistorySize);
        const std::vector<float> cpu = stats.cpuHistory();
        REQUIRE(cpu.size() == PerformanceStats::kHistorySize);
        CHECK(cpu.front() == doctest::Approx(10.0f)); // les 10 plus anciennes sont sorties
        CHECK(cpu.back() == doctest::Approx(static_cast<float>(PerformanceStats::kHistorySize + 9)));
        stats.fpsFromHistory(s);
        CHECK(s.fpsMax == doctest::Approx(100.0));
        CHECK(s.fpsMin == doctest::Approx(50.0));
        CHECK(s.fpsLow1 == doctest::Approx(50.0)); // 4 intervalles lents sur 240 (1,7 %) : le 99e centile est lent
        CHECK(s.fpsAverage == doctest::Approx(1000.0 * 240.0 / (236.0 * 10.0 + 4.0 * 20.0)));
    }

    TEST_CASE("the HUD texts change about four times per second, not every frame")
    {
        PerformanceStats stats;
        int calls = 0;
        auto next = [&] { return sample(static_cast<double>(++calls)); };
        CHECK(stats.tick(0.01f, next));
        CHECK(stats.text.fps == "FPS: 1");
        for (int frame = 0; frame < 20; ++frame) // 0,2 s à 100 images par seconde : pas encore
        {
            CHECK_FALSE(stats.tick(0.01f, next));
        }
        CHECK(calls == 1);
        for (int frame = 0; frame < 5; ++frame) // 0,25 s atteint
        {
            (void)stats.tick(0.01f, next);
        }
        CHECK(calls == 2);
        CHECK(stats.text.fps == "FPS: 2");
    }

    TEST_CASE("a measurement that does not exist is shown as n/d")
    {
        PerformanceSample s;
        s.backend = "Vulkan";
        const PerformanceText t = formatPerformance(s);
        CHECK(t.gpu == "GPU (Render): n/d");
        CHECK(t.memory == "Memory: RSS n/d   GPU n/d");
        CHECK(t.passes[0].empty());
#if defined(__linux__)
        const auto rss = processResidentMiB();
        REQUIRE(rss.has_value());
        CHECK(*rss > 1.0);
#endif
    }

    TEST_CASE("the demo theme hud.json loads and gives the panel its translucent background, text sizes and graph colours")
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
        CHECK(theme->style("hudDetail", ui::WidgetState::kNormal).fontSize == doctest::Approx(12.8));
        CHECK(theme->style("hudPlotCpu", ui::WidgetState::kNormal).accent != theme->style("hudPlotGpu", ui::WidgetState::kNormal).accent);
    }
}
