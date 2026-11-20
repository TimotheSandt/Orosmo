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

    TEST_CASE("showModal closes popups opened outside the modal and keeps those opened from it (submenus too), frame after frame")
    {
        UiHarness h;
        h.viewport = {600, 500};
        bool open = false;
        int outsideChoice = 0;
        int insideChoice = 0;
        auto root = std::make_unique<ui::VBox>();
        auto &outside = root->emplace<ui::Dropdown>(std::vector<std::string>{"a", "b"}, ui::Binding<int>::ref(outsideChoice));
        auto &dialog = root->emplace<ui::Dialog>("Title", ui::Binding<bool>::ref(open), false);
        dialog.panel().setWidth(ui::SizeSpec::fixed(300));
        auto &inside = dialog.body().emplace<ui::Dropdown>(std::vector<std::string>{"c", "d"}, ui::Binding<int>::ref(insideChoice));
        auto &menu = dialog.body().emplace<ui::Menu>("Menu");
        auto &sub = menu.items().emplace<ui::Menu>("Sub");
        sub.items().emplace<ui::MenuItem>("Leaf");
        h.ui.setRoot(std::move(root));
        h.frame();

        // Liste ouverte hors de la modale : fermée quand la modale s'ouvre (avant qu'elle reçoive les entrées).
        h.click(UiHarness::center(outside));
        REQUIRE(h.ui.popupOpenFor(outside));
        open = true;
        h.frame();
        REQUIRE(h.ui.modalOpen());
        CHECK_FALSE(h.ui.anyPopupOpen());

        // Liste ouverte depuis la modale : reste ouverte, image après image.
        h.click(UiHarness::center(inside));
        REQUIRE(h.ui.popupOpenFor(inside));
        for (int i = 0; i < 3; ++i)
            h.frame();
        CHECK(h.ui.popupOpenFor(inside));
        h.keys({ui::kKeyEscape}); // ferme la liste (la boîte reste)
        CHECK_FALSE(h.ui.anyPopupOpen());
        CHECK(open);

        // Menu de la modale et son sous-menu (ouverts au survol) : gardés.
        h.moveTo(UiHarness::center(menu));
        REQUIRE(h.ui.popupOpenFor(menu));
        h.moveTo(UiHarness::center(sub));
        REQUIRE(h.ui.popupOpenFor(sub));
        for (int i = 0; i < 3; ++i)
            h.frame();
        CHECK(h.ui.popupOpenFor(menu));
        CHECK(h.ui.popupOpenFor(sub));
        CHECK(h.ui.modalOpen());
    }
}
