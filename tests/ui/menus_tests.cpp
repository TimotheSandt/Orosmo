// UI-03 (fenêtres et menus) : boîte de dialogue modale, barre de menus et sous-menus, menu contextuel, notifications, image du jeu.
// Entrées simulées, sans GPU (la mise à jour d'une image sur le GPU est dans tests/renderer/canvas_tests.cpp).
#include "ui/ui_harness.hpp"

#include <orosmo/ui/menus.hpp>

namespace ui = orosmo::ui;
namespace rd = orosmo::renderer;
using tests::UiHarness;

TEST_SUITE("ui/menus")
{
    TEST_CASE("Dialog: modal while open (the rest receives nothing), closed by Escape, its close button or a click on the backdrop")
    {
        UiHarness h;
        bool open = false;
        int behind = 0;
        int inside = 0;
        auto root = std::make_unique<ui::VBox>();
        auto &button = root->emplace<ui::Button>("behind");
        button.onClick([&](const ui::Event &) { ++behind; });
        auto &dialog = root->emplace<ui::Dialog>("Title", ui::Binding<bool>::ref(open));
        dialog.panel().setWidth(ui::SizeSpec::fixed(200));
        auto &ok = dialog.body().emplace<ui::Button>("OK");
        ok.onClick([&](const ui::Event &) { ++inside; });
        h.ui.setRoot(std::move(root));
        h.frame();
        CHECK(dialog.rect().size == glm::vec2(400, 0)); // ne prend pas de place dans l'arbre (étirée en largeur, hauteur nulle)
        CHECK_FALSE(h.ui.modalOpen());
        h.click(UiHarness::center(button));
        CHECK(behind == 1);

        open = true;
        h.frame();
        REQUIRE(h.ui.modalOpen());
        CHECK(dialog.panel().rect().size.x == doctest::Approx(200));
        CHECK(UiHarness::center(dialog.panel()).x == doctest::Approx(200)); // centrée
        h.click(UiHarness::center(button)); // derrière le voile : rien, et le clic sur le voile ferme
        CHECK(behind == 1);
        CHECK_FALSE(open);
        h.frame();

        open = true;
        h.frame();
        h.click(UiHarness::center(ok));
        CHECK(inside == 1);
        CHECK(open);
        h.keys({ui::kKeyTab}); // le focus reste dans la fenêtre
        REQUIRE(h.ui.focused() != nullptr);
        CHECK(h.ui.focused() != &button);
        h.keys({ui::kKeyEscape});
        CHECK_FALSE(open);
        // Bouton de fermeture.
        open = true;
        h.frame();
        ui::Widget *close = dialog.panel().children()[0]->children()[1].get();
        h.click(UiHarness::center(*close));
        CHECK_FALSE(open);
        // Rendu : voile puis fenêtre, au-dessus de l'arbre.
        open = true;
        h.frame();
        rd::DrawList list;
        h.ui.draw(list);
        CHECK(list.count<rd::DrawRect>() >= 2);
    }

    TEST_CASE("MenuBar: a click opens a menu, hovering another title switches to it, submenus open on hover, an item runs and closes")
    {
        UiHarness h;
        int saved = 0;
        int recent = 0;
        bool grid = false;
        auto root = std::make_unique<ui::VBox>();
        auto &bar = root->emplace<ui::MenuBar>();
        auto &file = bar.emplace<ui::Menu>("File");
        file.items().emplace<ui::MenuItem>("Save", "Ctrl+S").onClick([&](const ui::Event &) { ++saved; });
        auto &sub = file.items().emplace<ui::Menu>("Recent");
        sub.items().emplace<ui::MenuItem>("terrain.json").onClick([&](const ui::Event &) { ++recent; });
        auto &view = bar.emplace<ui::Menu>("View");
        auto &gridItem = view.items().emplace<ui::MenuItem>("Grid", "", ui::Binding<bool>::ref(grid));
        h.ui.setRoot(std::move(root));
        h.frame();

        h.click(UiHarness::center(file));
        REQUIRE(h.ui.popupOpenFor(file));
        CHECK(h.ui.popup()->rect().position.y == doctest::Approx(file.rect().end().y)); // sous le titre
        h.moveTo(UiHarness::center(view)); // un menu est ouvert : survoler « View » passe à lui
        CHECK(h.ui.popupOpenFor(view));
        CHECK_FALSE(h.ui.popupOpenFor(file));
        h.click(UiHarness::center(gridItem)); // élément coché : bascule, puis le menu se ferme
        CHECK(grid);
        CHECK_FALSE(h.ui.anyPopupOpen());

        h.click(UiHarness::center(file));
        h.moveTo(UiHarness::center(sub)); // sous-menu au survol, à droite
        REQUIRE(h.ui.popupOpenFor(sub));
        CHECK(h.ui.popup()->rect().position.x == doctest::Approx(sub.rect().end().x));
        CHECK(h.ui.popupOpenFor(file)); // le menu parent reste ouvert
        h.click(UiHarness::center(*h.ui.popup()->children()[0]));
        CHECK(recent == 1);
        CHECK_FALSE(h.ui.anyPopupOpen()); // tout se ferme
        h.click(UiHarness::center(file));
        h.keys({ui::kKeyEscape});
        CHECK_FALSE(h.ui.anyPopupOpen());
        CHECK(saved == 0);
    }

    TEST_CASE("Menus: a clicked item does not stay highlighted when its menu reopens; a click does not take the focus from a field")
    {
        UiHarness h;
        bool enabled = false;
        std::string name = "x";
        auto root = std::make_unique<ui::VBox>();
        auto &bar = root->emplace<ui::MenuBar>();
        auto &view = bar.emplace<ui::Menu>("View");
        auto &first = view.items().emplace<ui::MenuItem>("Player", "", ui::Binding<bool>::ref(enabled));
        auto &second = view.items().emplace<ui::MenuItem>("Enabled");
        auto &field = root->emplace<ui::TextField>(ui::Binding<std::string>::ref(name));
        h.ui.setRoot(std::move(root));
        h.frame();
        h.click(UiHarness::center(field));
        REQUIRE(h.ui.focused() == &field);
        h.click(UiHarness::center(view));
        CHECK(h.ui.focused() == &field); // le menu ne prend pas le focus au clic
        CHECK(view.state() != ui::WidgetState::kFocus);
        h.click(UiHarness::center(first));
        CHECK(enabled);
        h.click(UiHarness::center(view));
        h.click(UiHarness::center(second));
        h.moveTo({390, 290}); // le pointeur loin des menus
        h.click(UiHarness::center(view)); // rouvert : rien n'est resté survolé, appuyé ou avec le focus
        h.moveTo({390, 290});
        CHECK(first.state() == ui::WidgetState::kNormal);
        CHECK(second.state() == ui::WidgetState::kNormal);
        CHECK(view.state() != ui::WidgetState::kFocus);
    }

    TEST_CASE("ContextMenu: a right click on its parent (or inside it) opens it at the pointer; elsewhere, nothing")
    {
        UiHarness h;
        int copied = 0;
        auto root = std::make_unique<ui::VBox>();
        auto &panel = root->emplace<ui::Panel>();
        panel.setHeight(ui::SizeSpec::fixed(100));
        auto &label = panel.emplace<ui::Label>("right click me");
        auto &menu = panel.emplace<ui::ContextMenu>();
        menu.items().emplace<ui::MenuItem>("Copy").onClick([&](const ui::Event &) { ++copied; });
        root->emplace<ui::Label>("outside");
        h.ui.setRoot(std::move(root));
        h.frame();
        const glm::vec2 at = UiHarness::center(label);
        h.secondaryClick(at);
        REQUIRE(h.ui.anyPopupOpen());
        CHECK(h.ui.popup()->rect().position == at);
        h.click(UiHarness::center(*h.ui.popup()->children()[0]));
        CHECK(copied == 1);
        CHECK_FALSE(h.ui.anyPopupOpen());
        h.secondaryClick({200, 250}); // hors du panneau
        CHECK_FALSE(h.ui.anyPopupOpen());
    }

    TEST_CASE("Notifications: newest at the bottom, at most N and at most H pixels (the oldest go), they expire, a click closes one")
    {
        UiHarness h;
        auto root = std::make_unique<ui::Overlay>();
        auto &toasts = root->emplace<ui::Notifications>(3, 0.0f, 150.0f);
        toasts.setAlign(ui::Align::kEnd, ui::Align::kEnd);
        h.ui.setRoot(std::move(root));
        h.frame();
        CHECK(toasts.measured().y == 0.0f);
        for (int i = 1; i <= 5; ++i)
            h.ui.notify("message " + std::to_string(i), i == 5 ? ui::NotificationKind::kError : ui::NotificationKind::kInfo, 2.0f);
        h.frame();
        REQUIRE(h.ui.notifications().size() == 3); // les deux plus anciennes sont parties
        CHECK(h.ui.notifications().front().text == "message 3");
        CHECK(toasts.rect().end() == h.viewport); // en bas à droite
        rd::DrawList list;
        h.ui.draw(list);
        CHECK(list.count<rd::DrawGlyphs>() == 3);
        // Clic sur la plus récente (en bas) : fermée.
        h.click({toasts.rect().end().x - 20, toasts.rect().end().y - 5});
        CHECK(h.ui.notifications().size() == 2);
        CHECK(h.ui.notifications().back().text == "message 4");
        h.wait(2.5f); // durée écoulée
        CHECK(h.ui.notifications().empty());

        // Limite de hauteur : une notification fait 10 de haut ici (texte 10, marge 0) et l'espacement vaut 4.
        auto root2 = std::make_unique<ui::Overlay>();
        root2->emplace<ui::Notifications>(10, 30.0f);
        h.ui.setRoot(std::move(root2));
        for (int i = 0; i < 5; ++i)
            h.ui.notify("x");
        h.frame();
        CHECK(h.ui.notifications().size() == 2); // 10 + 4 + 10 = 24 <= 30 < 38
    }

    TEST_CASE("DynamicImage and ImageView: pixels written by the game, version bumped by commit, contain / cover / stretch")
    {
        UiHarness h;
        ui::DynamicImage map(4, 2);
        map.fill({1, 0, 0, 1});
        map.setPixel(3, 1, {0, 0, 1, 1});
        const auto before = map.image()->version;
        map.commit();
        CHECK(map.image()->version == before + 1);
        CHECK(map.pixels()[(1 * 4 + 3) * 4 + 2] == 255);

        auto root = std::make_unique<ui::VBox>();
        auto &contain = root->emplace<ui::ImageView>([&] { return map.image(); }, ui::ImageFit::kContain);
        contain.setWidth(ui::SizeSpec::fixed(100)).setHeight(ui::SizeSpec::fixed(100));
        contain.alignX = ui::Align::kStart;
        auto &cover = root->emplace<ui::ImageView>([&] { return map.image(); }, ui::ImageFit::kCover);
        cover.setWidth(ui::SizeSpec::fixed(100)).setHeight(ui::SizeSpec::fixed(100));
        cover.alignX = ui::Align::kStart;
        h.ui.setRoot(std::move(root));
        h.frame();
        rd::DrawList list;
        h.ui.draw(list);
        std::vector<const rd::DrawImage *> images;
        for (const auto &c : list.commands)
            if (const auto *i = std::get_if<rd::DrawImage>(&c))
                images.push_back(i);
        REQUIRE(images.size() == 2);
        CHECK(images[0]->image == map.image());
        // contain : 100 x 50 centré verticalement ; cover : 100 x 100 avec la moitié centrale de l'image en largeur.
        CHECK(images[0]->vertices[1] == doctest::Approx(25.0f));
        CHECK(images[0]->vertices[5] == doctest::Approx(75.0f));
        CHECK(images[1]->vertices[2] == doctest::Approx(0.25f));
        CHECK(images[1]->vertices[10] == doctest::Approx(0.75f));
    }
}
