// UI-08 (A79 lot L8) : un arbre recréé (rechargement à chaud d'une vue .cui) garde, par identifiant de widget, le focus, le
// défilement, les sections ouvertes et l'onglet choisi (orosmo/ui/view_state.hpp).
#include "ui/ui_harness.hpp"

#include <orosmo/ui/controls.hpp>
#include <orosmo/ui/view_state.hpp>

#include <memory>
#include <string>

using namespace orosmo::ui;

namespace
{

// La même vue avant et après une modification : `extra` ajoute un widget (le .cui a changé).
std::unique_ptr<Widget> makeView(bool extra, std::string *notes)
{
    auto root = std::make_unique<VBox>();
    root->emplace<Label>("title").setId("title");
    if (extra)
        root->emplace<Label>("added").setId("added");
    root->emplace<Button>("Ok").setId("ok");
    root->emplace<Button>("Unnamed"); // sans identifiant : rien n'est gardé pour lui

    auto &scroll = root->emplace<ScrollView>();
    scroll.setId("list");
    scroll.height = SizeSpec::fixed(40.0f);
    auto &lines = scroll.emplace<VBox>();
    for (int i = 0; i < 20; ++i)
        lines.emplace<Label>("line " + std::to_string(i));

    auto &section = root->emplace<Section>("More", true);
    section.setId("more");
    section.body().emplace<Label>("inside");

    auto &tabs = root->emplace<Tabs>();
    tabs.setId("tabs");
    tabs.addPage("A", std::make_unique<Label>("page a"));
    tabs.addPage("B", std::make_unique<Label>("page b"));

    auto &area = root->emplace<TextArea>(Binding<std::string>::ref(*notes));
    area.setId("notes");
    area.height = SizeSpec::fixed(24.0f);
    return root;
}

} // namespace

TEST_SUITE("ui/view_state")
{
    TEST_CASE("a recreated tree keeps focus, scrolling, open sections and the tab, by widget id")
    {
        tests::UiHarness h;
        std::string notes;
        for (int i = 0; i < 30; ++i)
            notes += "line " + std::to_string(i) + "\n";
        h.ui.setRoot(makeView(false, &notes));
        h.frame();

        auto *scroll = dynamic_cast<ScrollView *>(h.ui.find("list"));
        REQUIRE(scroll != nullptr);
        REQUIRE(scroll->maxScroll() > 30.0f);
        scroll->setScroll(30.0f);
        dynamic_cast<Section *>(h.ui.find("more"))->setExpanded(false);
        dynamic_cast<Tabs *>(h.ui.find("tabs"))->select(h.ui, 1);
        h.ui.focus(h.ui.find("ok"));
        h.ui.setFocusVisible(true);
        h.frame();
        const float areaScroll = 17.0f;
        dynamic_cast<TextArea *>(h.ui.find("notes"))->restoreRetained(h.ui, RetainedState{.scroll = areaScroll});
        h.frame();
        const float keptAreaScroll = dynamic_cast<TextArea *>(h.ui.find("notes"))->scroll();
        REQUIRE(keptAreaScroll > 0.0f);

        const std::size_t before = countWidgets(*h.ui.root());
        std::unique_ptr<Widget> next = makeView(true, &notes);
        const Widget *nextRoot = next.get();
        REQUIRE(replaceRoot(h.ui, next).has_value());
        CHECK(next == nullptr);
        CHECK(h.ui.root() == nextRoot);
        CHECK(countWidgets(*h.ui.root()) == before + 1);

        // Focus et anneau gardés tout de suite ; défilement borné à la mise en page suivante.
        REQUIRE(h.ui.focused() != nullptr);
        CHECK(h.ui.focused()->id() == "ok");
        CHECK(h.ui.focusVisible());
        h.frame();
        CHECK(dynamic_cast<ScrollView *>(h.ui.find("list"))->scroll() == doctest::Approx(30.0f));
        CHECK_FALSE(dynamic_cast<Section *>(h.ui.find("more"))->expanded());
        CHECK(dynamic_cast<Tabs *>(h.ui.find("tabs"))->selected() == 1);
        CHECK(dynamic_cast<TextArea *>(h.ui.find("notes"))->scroll() == doctest::Approx(keptAreaScroll));
    }

    TEST_CASE("a state larger than the new content is bounded by the next layout; a missing id starts fresh")
    {
        tests::UiHarness h;
        std::string notes;
        h.ui.setRoot(makeView(false, &notes));
        h.frame();
        ViewState state = captureViewState(h.ui);
        state.widgets["list"].scroll = 1.0e6f;
        state.focused = "gone";
        h.ui.setRoot(makeView(false, &notes));
        restoreViewState(h.ui, state);
        h.frame();
        const auto *scroll = dynamic_cast<ScrollView *>(h.ui.find("list"));
        CHECK(scroll->scroll() == doctest::Approx(scroll->maxScroll()));
        CHECK(h.ui.focused() == nullptr);
    }

    TEST_CASE("captureViewState keeps only widgets with an id and something to keep")
    {
        tests::UiHarness h;
        std::string notes;
        h.ui.setRoot(makeView(false, &notes));
        h.frame();
        const ViewState state = captureViewState(h.ui);
        CHECK(state.focused.empty());
        CHECK(state.widgets.contains("list"));
        CHECK(state.widgets.contains("more"));
        CHECK(state.widgets.contains("tabs"));
        CHECK(state.widgets.contains("notes"));
        CHECK_FALSE(state.widgets.contains("title")); // un libellé n'a pas d'état d'interaction
        CHECK_FALSE(state.widgets.contains("ok"));
    }

    TEST_CASE("a bound tab is not restored: the application state keeps it")
    {
        tests::UiHarness h;
        int selected = 1;
        auto build = [&] {
            auto tabs = std::make_unique<Tabs>(Binding<int>::ref(selected));
            tabs->setId("tabs");
            tabs->addPage("A", std::make_unique<Label>("a"));
            tabs->addPage("B", std::make_unique<Label>("b"));
            return tabs;
        };
        h.ui.setRoot(build());
        h.frame();
        CHECK_FALSE(captureViewState(h.ui).widgets.contains("tabs"));
        selected = 0; // l'application a changé l'onglet entre deux versions de la vue
        std::unique_ptr<Widget> next = build();
        REQUIRE(replaceRoot(h.ui, next).has_value());
        CHECK(dynamic_cast<Tabs *>(h.ui.root())->selected() == 0);
    }

    TEST_CASE("replaceRoot during Ui::update is refused and the new tree is given back")
    {
        tests::UiHarness h;
        std::unique_ptr<Widget> next = std::make_unique<Label>("next");
        bool refused = false;
        auto root = std::make_unique<Button>("Swap");
        root->setId("swap");
        root->onClick([&](const Event &) {
            auto result = replaceRoot(h.ui, next);
            refused = !result && result.error().code == orosmo::core::ErrorCode::kBusy && next != nullptr;
        });
        Button *button = root.get();
        h.ui.setRoot(std::move(root));
        h.frame();
        h.click(button->rect().position + button->rect().size * 0.5f);
        CHECK(refused);
        CHECK(h.ui.root() == button);
    }
}
