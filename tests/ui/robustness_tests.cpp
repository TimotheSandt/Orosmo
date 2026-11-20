// Revue de M3 (UI) : cas limites de l'interface : file d'actions différées, événement dont la source est détruite, focus d'un
// widget devenu inactif, masqué ou hors de la fenêtre modale, dialogues imbriqués, zone cliquable d'une vue défilante,
// structure des widgets composés, racine retirée pendant la mise à jour, répétitions des fonds en neuf parties.
#include "ui/ui_harness.hpp"

#include <engine/ui/menus.hpp>

namespace ui = engine::ui;
using tests::UiHarness;

TEST_SUITE("ui/robustness")
{
    TEST_CASE("deferred actions beyond eight waves are kept for the next frame, in order, not dropped")
    {
        UiHarness h;
        h.ui.setRoot(std::make_unique<ui::VBox>());
        std::vector<int> done;
        std::function<void(int)> chain = [&](int i)
        {
            done.push_back(i);
            if (i + 1 < 12)
                h.ui.defer([&chain, i] { chain(i + 1); });
        };
        h.ui.defer([&] { chain(0); });
        h.frame();
        CHECK(done.size() == 8);
        h.frame();
        REQUIRE(done.size() == 12);
        for (int i = 0; i < 12; ++i)
            CHECK(done[static_cast<std::size_t>(i)] == i);
    }

    TEST_CASE("a deferred event whose source was destroyed by an earlier handler of the same frame is not delivered")
    {
        UiHarness h;
        auto root = std::make_unique<ui::VBox>();
        ui::VBox &box = root->emplace<ui::VBox>();
        int clicks = 0;
        box.emplace<ui::Button>("Delete me").onClick([&](const ui::Event &) {
            ++clicks;
            box.clearChildren(); // détruit le bouton
        });
        h.ui.setRoot(std::move(root));
        h.frame();
        h.keys({ui::kKeyTab});
        h.keys({ui::kKeyEnter, ui::kKeyEnter}); // deux clics en file dans la même image
        CHECK(clicks == 1);
    }

    TEST_CASE("focus leaves a widget that becomes disabled or hidden, and the main view while a modal dialog is open")
    {
        UiHarness h;
        bool enabled = true;
        bool dialogOpen = false;
        int clicks = 0;
        auto root = std::make_unique<ui::VBox>();
        auto &button = root->emplace<ui::Button>("Go");
        button.bindEnabled([&] { return enabled; });
        button.onClick([&](const ui::Event &) { ++clicks; });
        auto &tabs = root->emplace<ui::Tabs>();
        auto &inPage = tabs.addPage("A", std::make_unique<ui::Button>("In page A"));
        tabs.addPage("B", std::make_unique<ui::Label>("B"));
        root->emplace<ui::Dialog>("Dialog", ui::Binding<bool>::ref(dialogOpen));
        h.ui.setRoot(std::move(root));
        h.frame();
        h.keys({ui::kKeyTab});
        REQUIRE(h.ui.focused() == &button);
        enabled = false;
        h.keys({ui::kKeyEnter});
        CHECK(clicks == 0);
        CHECK(h.ui.focused() == nullptr);

        enabled = true;
        h.frame();
        h.ui.focus(&inPage);
        tabs.select(h.ui, 1); // la page A est masquée
        h.frame();
        h.frame();
        CHECK(h.ui.focused() == nullptr);

        h.ui.focus(&button);
        dialogOpen = true;
        h.frame(); // le dialogue s'inscrit à la mise en page
        h.keys({ui::kKeyEnter});
        CHECK(clicks == 0); // la touche ne passe pas derrière le voile
    }

    TEST_CASE("a dialog open inside an open dialog's panel is registered after it (no invalidated iteration)")
    {
        UiHarness h;
        bool outer = true, inner = true;
        auto root = std::make_unique<ui::VBox>();
        auto &dialog = root->emplace<ui::Dialog>("Outer", ui::Binding<bool>::ref(outer));
        auto &nested = dialog.body().emplace<ui::Dialog>("Inner", ui::Binding<bool>::ref(inner));
        nested.body().emplace<ui::Label>("inner text");
        h.ui.setRoot(std::move(root));
        h.frame();
        h.frame();
        CHECK(nested.panel().rect().size.x > 0.0f);
    }

    TEST_CASE("children of a scroll view are clickable only inside its inner (clipped) area")
    {
        UiHarness h;
        int clicks = 0;
        auto root = std::make_unique<ui::Overlay>();
        auto &scroll = root->emplace<ui::ScrollView>();
        scroll.overrideStyle(ui::StyleProperty::kPadding, "20");
        scroll.setAlign(ui::Align::kStart, ui::Align::kStart);
        scroll.setWidth(ui::SizeSpec::fixed(200)).setHeight(ui::SizeSpec::fixed(100));
        auto &column = scroll.emplace<ui::VBox>();
        for (int i = 0; i < 10; ++i)
            column.emplace<ui::Button>("Item").setHeight(ui::SizeSpec::fixed(30)).onClick([&](const ui::Event &) { ++clicks; });
        h.ui.setRoot(std::move(root));
        h.frame();
        scroll.setScroll(15.0f); // le premier bouton remonte dans la marge intérieure du haut (y de 5 à 35)
        h.frame();
        h.click({50, 10}); // dans la marge : le bouton y est découpé au dessin
        CHECK(clicks == 0);
        h.click({50, 30}); // dans la zone intérieure
        CHECK(clicks == 1);
    }

    TEST_CASE("clearing a composite widget keeps its structure: Tabs, Section, editors")
    {
        UiHarness h;
        auto root = std::make_unique<ui::VBox>();
        auto &tabs = root->emplace<ui::Tabs>();
        tabs.addPage("A", std::make_unique<ui::Label>("a"));
        auto &section = root->emplace<ui::Section>("S");
        section.body().emplace<ui::Label>("content");
        h.ui.setRoot(std::move(root));
        h.frame();
        tabs.clearChildren();
        CHECK(tabs.pageCount() == 0);
        tabs.addPage("B", std::make_unique<ui::Label>("b")); // l'en-tête a été refait : pas d'accès à un objet détruit
        CHECK(tabs.pageCount() == 1);
        section.clearChildren();
        CHECK(section.body().children().empty());
        CHECK(section.children().size() == 2); // en-tête et corps restent
        h.frame();
    }

    TEST_CASE("setRoot(nullptr) from a handler removes the tree at the end of the frame; Button::setText replaces a bound text")
    {
        UiHarness h;
        auto root = std::make_unique<ui::VBox>();
        root->emplace<ui::Button>("Quit").onClick([&](const ui::Event &) { h.ui.setRoot(nullptr); });
        h.ui.setRoot(std::move(root));
        h.frame();
        h.keys({ui::kKeyTab, ui::kKeyEnter});
        h.frame();
        CHECK(h.ui.root() == nullptr);

        ui::Button button("A");
        button.bindText([] { return std::string("bound"); });
        button.setText("B");
        CHECK(button.text() == "B");
    }
}
