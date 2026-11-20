// UI-03 : les composants ajoutés au langage d'interface (tests/ui/cui/test_widgets.cui), compilés par cui puis par le compilateur
// C++ : types des widgets, liaisons, enfants des menus et de la boîte de dialogue, taille de la fenêtre, ajustement de l'image.
#include "ui/ui_harness.hpp"

#include "tests/ui/cui/test_widgets.gen.h"

namespace ui = orosmo::ui;
using tests::UiHarness;

TEST_SUITE("ui/cui")
{
    TEST_CASE("UI-03 components in the interface language: controls, color, menus, dialog, notifications, image")
    {
        UiHarness h;
        h.viewport = {800, 1400};
        CuiWidgetsState state;
        h.ui.setRoot(cui::buildWidgetsPanel(h.ui, state));
        h.frame();
        ui::Widget *root = h.ui.root();
        REQUIRE(root != nullptr);
        auto *low = root->findAs<ui::RadioButton>("low");
        auto *high = root->findAs<ui::RadioButton>("high");
        REQUIRE(low != nullptr);
        REQUIRE(high != nullptr);
        h.click(UiHarness::center(*low));
        CHECK(state.quality == 0);
        CHECK(root->findAs<ui::RangeSlider>("range") != nullptr);
        CHECK(root->findAs<ui::SpinBox>("count") != nullptr);
        CHECK(root->findAs<ui::TextArea>("notes") != nullptr);
        CHECK(root->findAs<ui::ColorEditor>("color")->color() == ui::Color(0.2f, 0.4f, 0.6f, 1.0f));
        CHECK(root->findAs<ui::ColorWheel>("wheel")->rect().size == glm::vec2(80, 80));
        CHECK(root->findAs<ui::GradientEditor>("gradient")->gradient().stops.size() == 2);
        CHECK(root->findAs<ui::CurveEditor>("curve")->curve().points.size() == 2);
        CHECK(root->findAs<ui::ImageView>("minimap") != nullptr);

        // Menu : les éléments sont dans le menu (détachés), l'événement appelle l'état, la case cochée est liée.
        ui::Menu *menu = nullptr;
        for (const auto &c : root->children()[0]->children()[0]->children())
            menu = dynamic_cast<ui::Menu *>(c.get());
        REQUIRE(menu != nullptr);
        h.click(UiHarness::center(*menu));
        REQUIRE(h.ui.popupOpenFor(*menu));
        h.click(UiHarness::center(*root->find("save")));
        CHECK(state.saved == 1);
        h.click(UiHarness::center(*menu));
        h.click(UiHarness::center(*root->find("grid")));
        CHECK(state.grid);

        // Boîte de dialogue : .width s'applique à la fenêtre ; dismiss: false, un clic dehors ne la ferme pas.
        state.dialog = true;
        h.frame();
        auto *dialog = root->findAs<ui::Dialog>("dialog");
        REQUIRE(dialog != nullptr);
        CHECK(dialog->panel().rect().size.x == doctest::Approx(220));
        CHECK(root->find("inside") != nullptr);
        h.click({5, 5});
        CHECK(state.dialog);
        h.keys({ui::kKeyEscape});
        CHECK_FALSE(state.dialog);
        h.frame();

        // Menu contextuel.
        h.secondaryClick(UiHarness::center(*root->find("contextHost")));
        REQUIRE(h.ui.anyPopupOpen());
        h.click(UiHarness::center(*root->find("copy")));
        CHECK(state.copied == 1);

        // Notifications : au plus 3.
        for (int i = 0; i < 5; ++i)
            h.ui.notify("n");
        h.frame();
        CHECK(h.ui.notifications().size() == 3);
    }
}
