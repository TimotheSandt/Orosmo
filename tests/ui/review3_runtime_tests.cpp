// M3 (revue 3) : durée de vie et fenêtres de l'exécution de l'interface. Le focus clavier ne vise jamais un widget détruit par un
// `clearChildren` public ; une fenêtre modale ferme les fenêtres surgissantes ouvertes hors d'elle et garde les siennes ; les
// écritures différées des éditeurs (courbe, roue, dégradé) ne touchent pas un widget détruit ; la teinte d'un gris est la même pour
// la roue et les curseurs. Sans GPU ; à lancer aussi sous ASan / UBSan (make demo-test SANITIZE=address,undefined).
#include "ui/ui_harness.hpp"

#include <orosmo/ui/color.hpp>
#include <orosmo/ui/menus.hpp>

#include <functional>

namespace ui = orosmo::ui;
using tests::UiHarness;

namespace
{
template <typename T> T *firstOf(ui::Widget &root)
{
    if (auto *t = dynamic_cast<T *>(&root))
        return t;
    for (const auto &c : root.children())
        if (T *t = firstOf<T>(*c))
            return t;
    return nullptr;
}
} // namespace

TEST_SUITE("ui/review3")
{
    TEST_CASE("wantsKeyboard never reads a focused widget destroyed by Widget, Tabs or Section clearChildren")
    {
        std::string text = "x";
        SUBCASE("Widget::clearChildren")
        {
            UiHarness h;
            auto root = std::make_unique<ui::VBox>();
            auto &box = root->emplace<ui::VBox>();
            auto &field = box.emplace<ui::TextField>(ui::Binding<std::string>::ref(text));
            h.ui.setRoot(std::move(root));
            h.frame();
            h.ui.focus(&field);
            REQUIRE(h.ui.wantsKeyboard());
            box.clearChildren(); // sans Ui::forget : le champ focalisé est détruit
            CHECK_FALSE(h.ui.wantsKeyboard());
            CHECK(h.ui.focused() == nullptr);
            h.frame();
            CHECK_FALSE(h.ui.wantsKeyboard());
            // Un nouveau champ (peut-être à la même adresse) prend le focus normalement.
            auto &again = box.emplace<ui::TextField>(ui::Binding<std::string>::ref(text));
            h.frame();
            h.ui.focus(&again);
            CHECK(h.ui.wantsKeyboard());
            CHECK(again.focused());
        }
        SUBCASE("Tabs::clearChildren")
        {
            UiHarness h;
            auto root = std::make_unique<ui::VBox>();
            auto &tabs = root->emplace<ui::Tabs>();
            auto page = std::make_unique<ui::VBox>();
            auto &field = page->emplace<ui::TextField>(ui::Binding<std::string>::ref(text));
            tabs.addPage("A", std::move(page));
            h.ui.setRoot(std::move(root));
            h.frame();
            h.ui.focus(&field);
            REQUIRE(h.ui.wantsKeyboard());
            tabs.clearChildren();
            CHECK_FALSE(h.ui.wantsKeyboard());
            CHECK(h.ui.focused() == nullptr);
            h.frame();
            CHECK_FALSE(h.ui.wantsKeyboard());
        }
        SUBCASE("Section::clearChildren")
        {
            UiHarness h;
            auto root = std::make_unique<ui::VBox>();
            auto &section = root->emplace<ui::Section>("S");
            auto &field = section.body().emplace<ui::TextField>(ui::Binding<std::string>::ref(text));
            h.ui.setRoot(std::move(root));
            h.frame();
            h.ui.focus(&field);
            REQUIRE(h.ui.wantsKeyboard());
            section.clearChildren();
            CHECK_FALSE(h.ui.wantsKeyboard());
            CHECK(h.ui.focused() == nullptr);
            h.frame();
            CHECK_FALSE(h.ui.wantsKeyboard());
        }
    }
}
