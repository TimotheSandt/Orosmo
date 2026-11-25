// Images vivantes de la démo (apps/demo/src/UI/demo_ui_state.cpp, DemoUiState::refreshLiveImages) : la prévisualisation et la
// boussole ne sont dessinées que si l'onglet « Windows » de l'interface complète est affiché et que leurs entrées ont changé.
// Sans GPU.
#include "support/test_support.hpp"

#include "UI/demo_ui_state.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace
{
bool anyOpaquePixel(DemoUiState &state)
{
    const auto pixels = state.previewImage.pixels();
    for (std::size_t i = 3; i < pixels.size(); i += 4)
    {
        if (pixels[i] != 0)
        {
            return true;
        }
    }
    return false;
}

void showWindowsTab(DemoUiState &state) { REQUIRE(state.selectTabByName("windows")); }
} // namespace

TEST_SUITE("apps/demo")
{
    TEST_CASE("the live images are never drawn while the full UI is hidden (--ui off or hud)")
    {
        DemoUiState state;
        showWindowsTab(state);
        for (int frame = 0; frame < 120; ++frame)
        {
            state.tick(1.0f / 60.0f, 16.0f);
            state.cameraYawRadians += 0.01f;
            state.refreshLiveImages(false);
        }
        CHECK(state.previewDraws == 0);
        CHECK(state.compassDraws == 0);
        CHECK(state.previewImage.image()->version == 0);
        CHECK(state.compass.image()->version == 0);
        CHECK_FALSE(anyOpaquePixel(state));
    }

    TEST_CASE("the live images are not drawn on the other tabs of the full UI")
    {
        DemoUiState state;
        REQUIRE(state.selectTabByName("scene"));
        state.refreshLiveImages(true);
        REQUIRE(state.selectTabByName("color"));
        state.refreshLiveImages(true);
        CHECK(state.previewDraws == 0);
        CHECK(state.compassDraws == 0);
    }

    TEST_CASE("the live images are drawn once when the full UI opens, then only when their inputs change")
    {
        DemoUiState state;
        showWindowsTab(state);
        state.refreshLiveImages(false);
        state.refreshLiveImages(true); // ouverture de l'interface complète
        CHECK(state.previewDraws == 1);
        CHECK(state.compassDraws == 1);
        CHECK(state.previewImage.image()->version == 1);
        CHECK(anyOpaquePixel(state));

        for (int frame = 0; frame < 30; ++frame)
        {
            state.tick(1.0f / 60.0f, 16.0f);
            state.refreshLiveImages(true);
        }
        CHECK(state.previewDraws == 1); // rien n'a changé
        CHECK(state.compassDraws == 1);

        state.rangeLow = 30.0;
        state.refreshLiveImages(true);
        CHECK(state.previewDraws == 2);
        CHECK(state.compassDraws == 1);

        state.steps = 6;
        state.refreshLiveImages(true);
        state.paletteGradient.stops.back().color = {1.0f, 0.0f, 0.0f, 1.0f};
        state.refreshLiveImages(true);
        state.responseCurve.points[1].y = 0.5f;
        state.refreshLiveImages(true);
        CHECK(state.previewDraws == 5);

        state.cameraYawRadians += 0.5f; // le marqueur et l'aiguille suivent la caméra
        state.refreshLiveImages(true);
        CHECK(state.previewDraws == 6);
        CHECK(state.compassDraws == 2);

        state.showMarker = false;
        state.refreshLiveImages(true);
        CHECK(state.previewDraws == 7);
        state.cameraYawRadians += 0.5f; // sans marqueur, la prévisualisation ne dépend plus du lacet
        state.refreshLiveImages(true);
        CHECK(state.previewDraws == 7);
        CHECK(state.compassDraws == 3);
    }

    TEST_CASE("a change made while the full UI is hidden is drawn when it reopens")
    {
        DemoUiState state;
        showWindowsTab(state);
        state.refreshLiveImages(true);
        REQUIRE(state.previewDraws == 1);
        const std::vector<std::uint8_t> before(state.previewImage.pixels().begin(), state.previewImage.pixels().end());

        state.rangeHigh = 40.0;
        state.cameraYawRadians = 1.0f;
        state.refreshLiveImages(false); // cachée : rien
        CHECK(state.previewDraws == 1);
        CHECK(state.compassDraws == 1);

        state.refreshLiveImages(true); // rouverte : redessinée
        CHECK(state.previewDraws == 2);
        CHECK(state.compassDraws == 2);
        CHECK_FALSE(std::equal(before.begin(), before.end(), state.previewImage.pixels().begin()));
    }
}
