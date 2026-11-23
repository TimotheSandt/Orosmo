// Cadence d'images : changement de mode à chaud, sans fenêtre ni GPU (l'état et les appels aux deux fonctions reliées).
#include <doctest/doctest.h>

#include <orosmo/renderer/frame_pacing.hpp>

#include <vector>

using namespace orosmo::renderer;

namespace
{
struct Recorder
{
    std::vector<bool> vsync;
    std::vector<int> limits;
    FramePacer make()
    {
        return FramePacer([this](bool v) { vsync.push_back(v); }, [this](int l) { limits.push_back(l); });
    }
};
} // namespace

TEST_SUITE("renderer/frame_pacing")
{
    TEST_CASE("the default mode is vsync, no limiter")
    {
        CHECK(FramePacing{}.mode == FramePacingMode::kVSync);
        Recorder r;
        FramePacer pacer = r.make();
        pacer.setMode(FramePacing{});
        REQUIRE(r.vsync.size() == 1);
        CHECK(r.vsync[0]);
        REQUIRE(r.limits.size() == 1);
        CHECK(r.limits[0] == 0);
        CHECK(pacer.vsyncActive());
        CHECK(pacer.effectiveLimit() == 0);
    }

    TEST_CASE("the three modes drive the device vsync and the window limiter")
    {
        Recorder r;
        FramePacer pacer = r.make();
        pacer.setMode(FramePacing{FramePacingMode::kVSync, 60});

        pacer.setFpsLimit(120);
        CHECK(pacer.current().mode == FramePacingMode::kLimited);
        CHECK(pacer.effectiveLimit() == 120);
        CHECK_FALSE(r.vsync.back());
        CHECK(r.limits.back() == 120);

        pacer.setMode(FramePacingMode::kUnlimited);
        CHECK_FALSE(pacer.vsyncActive());
        CHECK(pacer.effectiveLimit() == 0);
        CHECK_FALSE(r.vsync.back());
        CHECK(r.limits.back() == 0);

        pacer.setMode(FramePacingMode::kVSync);
        CHECK(r.vsync.back());
        CHECK(r.limits.back() == 0);
    }

    TEST_CASE("a setter is called only when its value changes")
    {
        Recorder r;
        FramePacer pacer = r.make();
        pacer.setFpsLimit(90);
        const std::size_t vsyncCalls = r.vsync.size();
        const std::size_t limitCalls = r.limits.size();
        pacer.setFpsLimit(90);
        pacer.setMode(FramePacing{FramePacingMode::kLimited, 90});
        CHECK(r.vsync.size() == vsyncCalls);
        CHECK(r.limits.size() == limitCalls);
        pacer.setFpsLimit(144); // seule la limite change : le vsync n'est pas retouché
        CHECK(r.vsync.size() == vsyncCalls);
        CHECK(r.limits.size() == limitCalls + 1);
    }

    TEST_CASE("the limit is clamped and the mode is remembered before binding")
    {
        FramePacer pacer;
        pacer.setFpsLimit(100000);
        CHECK(pacer.current().fpsLimit == kMaxFpsLimit);
        pacer.setFpsLimit(1);
        CHECK(pacer.current().fpsLimit == kMinFpsLimit);

        Recorder r;
        pacer.setFpsLimit(75);
        pacer.bind([&](bool v) { r.vsync.push_back(v); }, [&](int l) { r.limits.push_back(l); });
        REQUIRE(r.limits.size() == 1); // la liaison applique le mode déjà choisi
        CHECK(r.limits[0] == 75);
        CHECK_FALSE(r.vsync.at(0));
    }

    TEST_CASE("the panel text names the mode")
    {
        CHECK(describeFramePacing(FramePacing{FramePacingMode::kVSync, 60}) == "vsync");
        CHECK(describeFramePacing(FramePacing{FramePacingMode::kLimited, 120}) == "limit 120 FPS");
        CHECK(describeFramePacing(FramePacing{FramePacingMode::kUnlimited, 60}) == "unlimited");
        CHECK(framePacingModeName(FramePacingMode::kLimited) == "limited");
    }
}
