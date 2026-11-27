// UI-08 (A79 lot L8) : forme liée de `app::ViewHost` (version finale, projet sans bibliothèque de jeu) : la vue générée liée dans
// l'exécutable est construite directement ; rien n'est observé ni reconstruit. La forme rechargeable est dans view_host_tests.cpp.
#include <doctest/doctest.h>

#include "tests/ui/cui/test_panel.gen.h"
#include "ui/ui_harness.hpp"

#include <orosmo/app/view_host.hpp>
#include <orosmo/ui/view_state.hpp>

#include <memory>

using namespace orosmo;

namespace
{
// Widget qui note, à sa destruction, si la structure de la vue qui l'a construit vit encore.
class ViewProbe : public ui::Widget
{
  public:
    ViewProbe(std::weak_ptr<int> marker, int *viewAliveAtDestruction)
        : ui::Widget("panel"), marker_(std::move(marker)), out_(viewAliveAtDestruction)
    {
    }
    ~ViewProbe() override { *out_ = marker_.expired() ? 0 : 1; }

  private:
    std::weak_ptr<int> marker_;
    int *out_;
};

struct ProbeView
{
    std::shared_ptr<int> marker = std::make_shared<int>(0);
    int *viewAliveAtDestruction = nullptr;
    std::unique_ptr<ui::Widget> build(ui::Ui &) const { return std::make_unique<ViewProbe>(marker, viewAliveAtDestruction); }
};
} // namespace

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
            REQUIRE(h.ui.focused() != nullptr);

            // Une seconde installation (vue recréée par l'hôte) garde le focus par identifiant.
            REQUIRE(views.setRoot(cui::TestPanel{.state = &state, .stats = &stats}).has_value());
            REQUIRE(h.ui.focused() != nullptr);
            CHECK(h.ui.focused() == h.ui.find("seed"));
            views.update();
            CHECK(views.builds() == 0);
            CHECK(views.rebuilds() == 0);
        }
        CHECK(h.ui.root() == nullptr);
    }

    TEST_CASE("a host destroyed during Ui::update keeps its view alive until the tree is really removed (revue PR #37)")
    {
        tests::UiHarness h;
        int viewAlive = -1;
        auto views = std::make_unique<app::ViewHost>(h.ui, app::ViewHostSettings{.buildCommand = "never run"});
        REQUIRE(views->setLinkedRoot(ProbeView{.viewAliveAtDestruction = &viewAlive}).has_value());
        h.frame();
        // Rappel exécuté pendant `update` (fin de l'image) : le retrait de l'arbre est différé.
        h.ui.defer([&] { views.reset(); });
        h.frame();
        CHECK(views == nullptr);
        CHECK(h.ui.root() == nullptr);
        CHECK(viewAlive == 1); // la structure vivait encore quand l'arbre a été détruit
    }
}
