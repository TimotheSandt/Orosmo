// UI-05 / UI-11 : une vue écrite dans le langage d'interface (tests/ui/cui/test_panel.cui), compilée par cui vers engine::ui puis par
// le compilateur C++ : l'arbre généré, les liaisons dans les deux sens, les événements (appel direct, action nommée, fonction
// libre qualifiée), les surcharges de style, la mise en page du langage. Les cas d'erreur sont dans tests/ui/cui_errors/
// (make cui-test) et dans les tests du compilateur (cargo test).
#include "ui/ui_harness.hpp"

#include "tests/ui/cui/test_panel.gen.h"

namespace ui = engine::ui;
using tests::UiHarness;

TEST_SUITE("ui/cui")
{
    TEST_CASE("the generated view builds the declared tree with its ids, bindings and layout")
    {
        UiHarness h;
        h.viewport = {600, 800};
        CuiTestState state;
        CuiTestStats stats;
        h.ui.setRoot(cui::buildTestPanel(h.ui, state, stats));
        h.frame();
        ui::Widget *root = h.ui.root();
        REQUIRE(root != nullptr);
        CHECK(root->rect().size.x == doctest::Approx(240)); // .width(panelWidth)
        CHECK(root->styleName() == "panel");
        auto *title = root->findAs<ui::Label>("title");
        REQUIRE(title != nullptr);
        CHECK(title->styleName() == "title"); // .variant("title")
        auto *fps = root->findAs<ui::Label>("fps");
        REQUIRE(fps != nullptr);
        CHECK(fps->text() == "FPS 59.87 - 25 %"); // interpolation, func de la vue, deux décimales au plus
        stats.fps = 120.0;
        CHECK(fps->text() == "FPS 120 - 25 %");    // relu à chaque image
        CHECK(root->findAs<ui::NumberField>("seed")->text() == "42");
        CHECK(root->findAs<ui::Slider>("scale")->formatted() == "120");
        CHECK(root->findAs<ui::Dropdown>("preset")->options().size() == 3);
        CHECK(root->findAs<ui::TextField>("name")->text() == "terrain");
        CHECK(root->findAs<ui::ColorPicker>("tint")->color() == ui::Color(1.0f, 0.5f, 0.0f, 1.0f));
        CHECK(root->find("random")->tooltip() == "new seed");
        CHECK(root->find("params") != nullptr);
        CHECK(root->find("params.gain") == nullptr); // ParamPanel : identifiants relatifs au panneau
        CHECK(root->find("gain") != nullptr);
        CHECK(root->find("plot") != nullptr);
        CHECK_FALSE(root->find("plot")->parent()->visible()); // page du deuxième onglet
        auto *exportButton = root->find("export");
        REQUIRE(exportButton != nullptr);
        CHECK_FALSE(exportButton->enabled()); // .enabled(state.canExport)
        state.canExport = true;
        CHECK(exportButton->enabled());
        auto *apply = root->find("apply");
        REQUIRE(apply != nullptr);
        CHECK(apply->margin == ui::Insets{3, 2, 3, 0});
        CHECK(apply->alignX == ui::Align::kEnd);
        CHECK(apply->style(h.ui.theme()).background == ui::Color(1, 0, 0, 1)); // .style(background: "accent")
        CHECK(apply->style(h.ui.theme()).radius == 4.0f);
    }

    TEST_CASE("events call the named C++ functions; two-way bindings write the state")
    {
        UiHarness h;
        h.viewport = {600, 800};
        CuiTestState state;
        CuiTestStats stats;
        int presetActions = 0;
        h.ui.actions().add("applyPreset", [&](const ui::Event &) { ++presetActions; });
        h.ui.setRoot(cui::buildTestPanel(h.ui, state, stats));
        CHECK(h.ui.actions().missing().empty());
        h.frame();
        ui::Widget *root = h.ui.root();

        h.click(UiHarness::center(*root->find("random"))); // state.randomize() : appel direct, sans l'événement
        CHECK(state.randomized == 1);
        CHECK(state.seed == 42u * 1664525u + 1013904223u);
        CHECK(root->findAs<ui::NumberField>("seed")->text() == std::to_string(state.seed));

        const ui::Rect scale = root->find("scale")->rect();
        h.click({scale.position.x + scale.size.x * 0.5f, scale.position.y + 2}); // $state.scale ; onChange(state.changed)
        CHECK(state.scale == doctest::Approx(250.0f));
        CHECK(state.changes == 1);

        h.click(UiHarness::center(*root->find("snow"))); // $state.snow
        CHECK(state.snow);

        h.click(UiHarness::center(*root->find("preset"))); // action nommée « applyPreset », liée tardivement
        REQUIRE(h.ui.popup() != nullptr);
        const auto &items = h.ui.popup()->children()[0]->children();
        h.click(UiHarness::center(*items[1]));
        CHECK(state.preset == 1);
        CHECK(presetActions == 1);

        h.click(UiHarness::center(*root->find("apply"))); // state.apply(event) : reçoit l'événement
        CHECK(state.applied == 1);

        // ParamPanel($state.params) : un changement d'un champ écrit la structure et appelle state.changed.
        const ui::Rect gain = root->find("gain")->rect();
        h.click({gain.position.x + gain.size.x * 0.9f, gain.position.y + 2});
        CHECK(state.params.gain == doctest::Approx(0.9f).epsilon(0.05));
        CHECK(state.changes == 2);

        // Fonction libre qualifiée (Tools::exportNow) dans une section repliée : on la déplie, on active le bouton.
        auto *more = dynamic_cast<ui::Section *>(root->find("export")->parent()->parent());
        REQUIRE(more != nullptr);
        more->setExpanded(true);
        state.canExport = true;
        h.frame();
        const int before = Tools::exported;
        h.click(UiHarness::center(*root->find("export")));
        CHECK(Tools::exported == before + 1);
    }
}
