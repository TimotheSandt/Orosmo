// UI-03 (limites levées) : annuler / rétablir dans les champs de texte, retour à la ligne automatique de l'éditeur, navigation des
// menus au clavier, raccourcis installés (éléments de menu, boutons) et leur forme normalisée. Entrées simulées, sans GPU.
#include "ui/ui_harness.hpp"

#include <orosmo/ui/controls.hpp>
#include <orosmo/ui/menus.hpp>

namespace ui = orosmo::ui;
using tests::UiHarness;

namespace
{
template <typename T> T &place(UiHarness &h, std::unique_ptr<T> w, glm::vec2 size)
{
    auto root = std::make_unique<ui::Overlay>();
    w->setAlign(ui::Align::kStart, ui::Align::kStart);
    w->setWidth(ui::SizeSpec::fixed(size.x)).setHeight(ui::SizeSpec::fixed(size.y));
    T &ref = root->add(std::move(w));
    h.ui.setRoot(std::move(root));
    h.frame();
    return ref;
}
} // namespace

TEST_SUITE("ui/editing")
{
    TEST_CASE("normalizeShortcut: modifier names and order, key names, refusals")
    {
        CHECK(ui::normalizeShortcut("control + shift + z") == "Ctrl+Shift+Z");
        CHECK(ui::normalizeShortcut("Shift+Ctrl+f5") == "Ctrl+Shift+F5");
        CHECK(ui::normalizeShortcut("cmd+s") == "Super+S");
        CHECK(ui::normalizeShortcut("Alt+Del") == "Alt+Delete");
        CHECK(ui::normalizeShortcut("Ctrl++") == "Ctrl++");
        CHECK(ui::normalizeShortcut("F12") == "F12");
        CHECK(ui::normalizeShortcut("Ctrl").empty());
        CHECK(ui::normalizeShortcut("Ctrl+A+B").empty());
        CHECK(ui::normalizeShortcut("Ctrl+Banana").empty());
    }

    TEST_CASE("TextField: undo / redo by word, deletions grouped, Ctrl+Z / Ctrl+Y from the shortcuts of the frame")
    {
        UiHarness h;
        std::string value = "x";
        auto &field = place(h, std::make_unique<ui::TextField>(ui::Binding<std::string>::ref(value)), {200, 20});
        h.keys({ui::kKeyTab}); // focus au clavier : tout sélectionné (un clic place le curseur)
        for (const char *c : {"h", "e", "l", "l", "o", " ", "w", "o", "r", "l", "d"})
            h.type(c);
        CHECK(field.text() == "hello world");
        h.keys({ui::kKeyUndo});
        CHECK(field.text() == "hello ");
        h.shortcut("Ctrl+Z");
        CHECK(field.text() == "x");
        CHECK_FALSE(field.edit().canUndo());
        h.shortcut("ctrl+y");
        CHECK(field.text() == "hello ");
        h.shortcut("Ctrl+Shift+Z");
        CHECK(field.text() == "hello world");
        h.keys({ui::kKeyBackspace, ui::kKeyBackspace, ui::kKeyBackspace});
        CHECK(field.text() == "hello wo");
        h.keys({ui::kKeyUndo}); // les trois effacements forment un pas
        CHECK(field.text() == "hello world");
        // Une nouvelle modification efface ce qui pouvait être rétabli.
        h.keys({ui::kKeyUndo});
        h.type("!");
        CHECK_FALSE(field.edit().canRedo());
        CHECK(field.text() == "hello !");
        h.keys({ui::kKeyEnter});
        CHECK(value == "hello !");
    }

    TEST_CASE("TextArea: word wrap at the width (after a space when possible), Down by row, End before the break; undo")
    {
        UiHarness h;
        std::string text = "aaaa bbbb cccc dddd"; // 19 caractères de 5 px
        auto &area = place(h, std::make_unique<ui::TextArea>(ui::Binding<std::string>::ref(text)), {60, 60}); // 12 caractères
        CHECK(area.wraps());
        CHECK(area.rowCount(h.ui) == 2); // "aaaa bbbb " | "cccc dddd"
        area.setWrap(false);
        CHECK(area.rowCount(h.ui) == 1);
        area.setWrap(true);
        h.keys({ui::kKeyTab});
        REQUIRE(h.ui.focused() == &area);
        h.keys({ui::kKeyEnd});
        CHECK(area.edit().caret == 9); // avant l'espace de coupure
        h.keys({ui::kKeyHome, ui::kKeyDown});
        CHECK(area.edit().caret == 10); // début de la 2e rangée
        h.keys({ui::kKeyEnd});
        CHECK(area.edit().caret == 19);
        // Mot plus long que la largeur : coupé au caractère.
        text = "abcdefghijklmnopqrstu";
        h.click({300, 200});
        h.frame();
        CHECK(area.rowCount(h.ui) == 2);
        // Annuler dans l'éditeur.
        h.click(UiHarness::center(area));
        h.keys({ui::kKeyEnd});
        h.type("X");
        h.keys({ui::kKeyEnter});
        h.type("Y");
        CHECK(text == "abcdefghijklmnopqrstuX\nY");
        h.shortcut("Ctrl+Z");
        h.frame();
        CHECK(text == "abcdefghijklmnopqrstuX\n");
        h.shortcut("Ctrl+Z");
        h.frame();
        CHECK(text == "abcdefghijklmnopqrstu");
    }

    TEST_CASE("Menus with the keyboard: open, Up/Down, Right/Left into submenus and across the bar, Enter, Escape")
    {
        UiHarness h;
        int saved = 0;
        int recent = 0;
        bool grid = false;
        auto root = std::make_unique<ui::VBox>();
        auto &bar = root->emplace<ui::MenuBar>();
        auto &file = bar.emplace<ui::Menu>("File");
        auto &save = file.items().emplace<ui::MenuItem>("Save", "Ctrl+S");
        save.onClick([&](const ui::Event &) { ++saved; });
        file.items().emplace<ui::Separator>();
        auto &sub = file.items().emplace<ui::Menu>("Recent");
        auto &a = sub.items().emplace<ui::MenuItem>("a.json");
        a.onClick([&](const ui::Event &) { ++recent; });
        auto &view = bar.emplace<ui::Menu>("View");
        auto &gridItem = view.items().emplace<ui::MenuItem>("Grid", "", ui::Binding<bool>::ref(grid));
        h.ui.setRoot(std::move(root));
        h.frame();

        h.keys({ui::kKeyTab});
        REQUIRE(h.ui.focused() == &file);
        h.keys({ui::kKeyDown}); // ouvre, premier élément
        CHECK(h.ui.popupOpenFor(file));
        CHECK(h.ui.focused() == &save);
        h.keys({ui::kKeyDown}); // le séparateur est sauté
        CHECK(h.ui.focused() == &sub);
        h.keys({ui::kKeyDown}); // dernier -> premier
        CHECK(h.ui.focused() == &save);
        h.keys({ui::kKeyUp});
        CHECK(h.ui.focused() == &sub);
        h.keys({ui::kKeyRight}); // sous-menu
        CHECK(h.ui.popupOpenFor(sub));
        CHECK(h.ui.focused() == &a);
        h.keys({ui::kKeyLeft}); // le referme
        CHECK_FALSE(h.ui.popupOpenFor(sub));
        CHECK(h.ui.popupOpenFor(file));
        CHECK(h.ui.focused() == &sub);
        h.keys({ui::kKeyEnter, ui::kKeyEnter}); // ouvre le sous-menu, active « a.json »
        CHECK(recent == 1);
        CHECK_FALSE(h.ui.anyPopupOpen());

        h.keys({ui::kKeyDown}); // le focus est revenu... nulle part : on repart du titre
        h.ui.focus(&file);
        h.keys({ui::kKeyEnter});
        REQUIRE(h.ui.focused() == &save);
        h.keys({ui::kKeyRight}); // menu suivant de la barre
        CHECK(h.ui.popupOpenFor(view));
        CHECK_FALSE(h.ui.popupOpenFor(file));
        CHECK(h.ui.focused() == &gridItem);
        h.keys({ui::kKeySpace});
        CHECK(grid);
        h.ui.focus(&file);
        h.keys({ui::kKeyDown, ui::kKeyEscape}); // Échap : fermé, le focus revient au titre
        CHECK_FALSE(h.ui.anyPopupOpen());
        CHECK(h.ui.focused() == &file);
        h.keys({ui::kKeyRight}); // menu fermé : le focus passe au titre suivant sans ouvrir
        CHECK(h.ui.focused() == &view);
        CHECK_FALSE(h.ui.anyPopupOpen());

        // Ouvert à la souris : Bas entre dans le menu.
        h.click(UiHarness::center(file));
        h.keys({ui::kKeyDown});
        CHECK(h.ui.focused() == &save);
        h.keys({ui::kKeyEscape});
        CHECK(saved == 0);
    }

    TEST_CASE("Shortcuts: a menu item fires with its menu closed, a button too; text fields keep their editing keys; a modal blocks the rest")
    {
        UiHarness h;
        int saved = 0;
        int refreshed = 0;
        int selectAllItem = 0;
        bool dialog = false;
        std::string name = "terrain";
        auto root = std::make_unique<ui::VBox>();
        auto &bar = root->emplace<ui::MenuBar>();
        auto &file = bar.emplace<ui::Menu>("File");
        file.items().emplace<ui::MenuItem>("Save", "Ctrl+S").onClick([&](const ui::Event &) { ++saved; });
        file.items().emplace<ui::MenuItem>("Select all", "Ctrl+A").onClick([&](const ui::Event &) { ++selectAllItem; });
        auto &refresh = root->emplace<ui::Button>("Refresh");
        refresh.setShortcut("f5").onClick([&](const ui::Event &) { ++refreshed; });
        auto &field = root->emplace<ui::TextField>(ui::Binding<std::string>::ref(name));
        auto &box = root->emplace<ui::Dialog>("D", ui::Binding<bool>::ref(dialog));
        box.body().emplace<ui::Label>("modal");
        h.ui.setRoot(std::move(root));
        h.frame();

        h.shortcut("Ctrl+S");
        CHECK(saved == 1);
        h.shortcut("F5");
        CHECK(refreshed == 1);
        h.shortcut("Ctrl+Q"); // personne
        h.click(UiHarness::center(field));
        h.keys({ui::kKeyEnd});
        h.shortcut("Ctrl+A"); // dans le champ : tout sélectionner, pas l'élément de menu
        CHECK(field.edit().hasSelection());
        CHECK(selectAllItem == 0);
        h.shortcut("Ctrl+S"); // un raccourci qui n'est pas d'édition passe
        CHECK(saved == 2);
        dialog = true;
        h.frame();
        h.shortcut("Ctrl+S");
        h.shortcut("F5");
        CHECK(saved == 2);
        CHECK(refreshed == 1);
    }
}
