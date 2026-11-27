// UI-08 (A79 lot L8) : forme liée de `app::ViewHost` (version finale, projet sans bibliothèque de jeu) : la vue générée liée dans
// l'exécutable est construite directement ; rien n'est observé ni reconstruit. La forme rechargeable est dans view_host_tests.cpp.
#include <doctest/doctest.h>

#include "tests/ui/cui/test_panel.gen.h"
#include "ui/ui_harness.hpp"

#include <orosmo/app/view_host.hpp>
#include <orosmo/ui/view_state.hpp>

using namespace orosmo;

TEST_SUITE("app/view_host")
{
    TEST_CASE("a linked view is installed as the root, with the previous tree's state, and removed with the host")
    {
        tests::UiHarness h;
        CuiTestState state;
        CuiTestStats stats;
        {
            app::ViewHost views(h.ui, {.buildCommand = "never run"});
            auto mounted = views.setRoot(cui::TestPanel{.state = &state, .stats = &stats});
            REQUIRE_MESSAGE(mounted.has_value(), mounted.error().describe());
            CHECK_FALSE(views.reloadable());
            REQUIRE(h.ui.root() != nullptr);
            CHECK(h.ui.find("title") != nullptr);
            h.frame();
            h.ui.focus(h.ui.find("seed"));
            const bool focusable = h.ui.focused() != nullptr;

            // Une seconde installation (vue recréée par l'hôte) garde le focus par identifiant.
            REQUIRE(views.setRoot(cui::TestPanel{.state = &state, .stats = &stats}).has_value());
            if (focusable)
                CHECK(h.ui.focused() == h.ui.find("seed"));
            views.update();
            CHECK(views.builds() == 0);
            CHECK(views.rebuilds() == 0);
        }
        CHECK(h.ui.root() == nullptr);
    }
}
