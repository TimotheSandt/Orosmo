// UI-06 : mise en page (VBox, HBox, Grid, Overlay, ScrollView) : tailles ajustées, fixes, de remplissage pondérées, minimales et
// maximales ; marges ; alignements ; marge intérieure et espacement venant du thème. Sans GPU (FixedFont : 5 px par caractère, 10 de
// haut). Les tailles attendues sont calculées à la main dans chaque cas.
#include "ui/ui_harness.hpp"

namespace ui = orosmo::ui;
using tests::sameRect;
using tests::UiHarness;

namespace
{
// Racine superposée : l'enfant garde sa taille ajustée (aligné en haut à gauche) au lieu d'être étiré sur la vue.
template <typename T> T &rootChild(UiHarness &h, std::unique_ptr<T> child)
{
    auto root = std::make_unique<ui::Overlay>();
    child->setAlign(ui::Align::kStart, ui::Align::kStart);
    T &ref = root->add(std::move(child));
    h.ui.setRoot(std::move(root));
    return ref;
}
} // namespace

TEST_SUITE("ui/layout")
{
    TEST_CASE("VBox fits its children, spaced by the theme; the cross axis stretches by default, or aligns")
    {
        UiHarness h;
        auto box = std::make_unique<ui::VBox>();
        auto &a = box->emplace<ui::Label>("abcd"); // 20 x 10
        auto &b = box->emplace<ui::Label>("ab");   // 10 x 10
        auto &c = box->emplace<ui::Label>("ab");
        c.alignX = ui::Align::kEnd;
        auto &v = rootChild(h, std::move(box));
        h.ui.layout(h.viewport);
        CHECK(sameRect(v.rect(), {0, 0}, {20, 10 + 4 + 10 + 4 + 10}));
        CHECK(sameRect(a.rect(), {0, 0}, {20, 10}));
        CHECK(sameRect(b.rect(), {0, 14}, {20, 10})); // étiré
        CHECK(sameRect(c.rect(), {10, 28}, {10, 10})); // aligné à droite
    }

    TEST_CASE("HBox shares the remaining width between fill children by weight; min and max are respected")
    {
        UiHarness h;
        auto box = std::make_unique<ui::HBox>();
        box->setWidth(ui::SizeSpec::fixed(100));
        auto &label = box->emplace<ui::Label>("ab"); // 10
        auto &one = box->emplace<ui::Widget>("box");
        one.setWidth(ui::SizeSpec::fill(1));
        auto &three = box->emplace<ui::Widget>("box");
        three.setWidth(ui::SizeSpec::fill(3));
        auto &row = rootChild(h, std::move(box));
        h.ui.layout(h.viewport);
        // Reste : 100 - 10 - 2 espacements de 4 = 82 → 20,5 et 61,5.
        CHECK(row.rect().size.x == doctest::Approx(100));
        CHECK(label.rect().position.x == doctest::Approx(0));
        CHECK(one.rect().position.x == doctest::Approx(14));
        CHECK(one.rect().size.x == doctest::Approx(20.5));
        CHECK(three.rect().position.x == doctest::Approx(38.5));
        CHECK(three.rect().size.x == doctest::Approx(61.5));
        CHECK(three.rect().size.y == doctest::Approx(10)); // étiré à la hauteur de la rangée

        three.width.max = 30.0f;
        one.width.min = 25.0f;
        h.ui.layout(h.viewport);
        CHECK(three.rect().size.x == doctest::Approx(30));
        CHECK(one.rect().size.x == doctest::Approx(25));
    }

    TEST_CASE("fixed sizes, margins, theme padding; hidden children take no room")
    {
        UiHarness h;
        auto panel = std::make_unique<ui::Panel>(); // padding 5, spacing 2
        auto &a = panel->emplace<ui::Widget>("box");
        a.setWidth(ui::SizeSpec::fixed(30)).setHeight(ui::SizeSpec::fixed(8));
        a.margin = {3, 1, 0, 2};
        auto &hidden = panel->emplace<ui::Label>("hidden");
        hidden.setVisible(false);
        auto &b = panel->emplace<ui::Label>("ab");
        auto &p = rootChild(h, std::move(panel));
        h.ui.layout(h.viewport);
        // Contenu : max(3 + 30, 10) x (1 + 8 + 2 + 2 + 10) ; panneau : + 2 x 5.
        CHECK(sameRect(p.rect(), {0, 0}, {33 + 10, 23 + 10}));
        CHECK(sameRect(a.rect(), {5 + 3, 5 + 1}, {30, 8}));
        CHECK(sameRect(b.rect(), {5, 5 + 1 + 8 + 2 + 2}, {33, 10}));
        CHECK(hidden.measured() == glm::vec2(0.0f));
    }

    TEST_CASE("Grid: column width = widest child, fill columns take the rest, spans cover several columns")
    {
        UiHarness h;
        auto grid = std::make_unique<ui::Grid>(2);
        grid->setWidth(ui::SizeSpec::fixed(100));
        auto &l1 = grid->emplace<ui::Label>("a");
        auto &c1 = grid->emplace<ui::Widget>("box");
        c1.setWidth(ui::SizeSpec::fill()).setHeight(ui::SizeSpec::fixed(10));
        auto &l2 = grid->emplace<ui::Label>("abc");
        auto &c2 = grid->emplace<ui::Widget>("box");
        c2.setWidth(ui::SizeSpec::fill()).setHeight(ui::SizeSpec::fixed(12));
        auto &wide = grid->emplace<ui::Label>("abcdefgh");
        wide.columnSpan = 2;
        auto &g = rootChild(h, std::move(grid));
        h.ui.layout(h.viewport);
        // Colonne 0 : 15 (« abc ») ; colonne 1 : 100 - 15 - 4 = 81. Rangées : 10, 12, 10 ; espacement 4.
        CHECK(sameRect(l1.rect(), {0, 0}, {15, 10}));
        CHECK(sameRect(c1.rect(), {19, 0}, {81, 10}));
        CHECK(sameRect(l2.rect(), {0, 14}, {15, 12}));
        CHECK(sameRect(c2.rect(), {19, 14}, {81, 12}));
        CHECK(sameRect(wide.rect(), {0, 30}, {100, 10}));
        CHECK(g.rect().size.y == doctest::Approx(40));
    }

    TEST_CASE("Overlay aligns each child on both axes")
    {
        UiHarness h;
        auto overlay = std::make_unique<ui::Overlay>();
        overlay->setWidth(ui::SizeSpec::fixed(100)).setHeight(ui::SizeSpec::fixed(50));
        auto &centered = overlay->emplace<ui::Widget>("box");
        centered.setWidth(ui::SizeSpec::fixed(10)).setHeight(ui::SizeSpec::fixed(10)).setAlign(ui::Align::kCenter, ui::Align::kCenter);
        auto &corner = overlay->emplace<ui::Widget>("box");
        corner.setWidth(ui::SizeSpec::fixed(10)).setHeight(ui::SizeSpec::fixed(10)).setAlign(ui::Align::kEnd, ui::Align::kEnd);
        auto &stretched = overlay->emplace<ui::Widget>("box");
        rootChild(h, std::move(overlay));
        h.ui.layout(h.viewport);
        CHECK(sameRect(centered.rect(), {45, 20}, {10, 10}));
        CHECK(sameRect(corner.rect(), {90, 40}, {10, 10}));
        CHECK(sameRect(stretched.rect(), {0, 0}, {100, 50}));
    }

    TEST_CASE("the root fills the viewport; a fill child of a VBox takes the remaining height")
    {
        UiHarness h;
        auto root = std::make_unique<ui::VBox>();
        auto &top = root->emplace<ui::Label>("top");
        auto &rest = root->emplace<ui::Widget>("box");
        rest.setHeight(ui::SizeSpec::fill());
        h.ui.setRoot(std::move(root));
        h.ui.layout({200, 100});
        CHECK(sameRect(top.rect(), {0, 0}, {200, 10}));
        CHECK(sameRect(rest.rect(), {0, 14}, {200, 86}));
    }

    TEST_CASE("ScrollView: content taller than the view scrolls by the wheel, clips drawing and hit testing")
    {
        UiHarness h;
        auto scroll = std::make_unique<ui::ScrollView>();
        scroll->setWidth(ui::SizeSpec::fixed(100)).setHeight(ui::SizeSpec::fixed(50));
        auto &list = scroll->emplace<ui::VBox>();
        std::vector<ui::Button *> items;
        for (int i = 0; i < 10; ++i)
        {
            items.push_back(&list.emplace<ui::Button>("item" + std::to_string(i))); // 10 + 2 x 2 = 14 de haut
        }
        auto &view = rootChild(h, std::move(scroll));
        h.frame();
        // Contenu : 10 x 14 + 9 x 4 = 176 ; vue : 50 → défilement maximal 126 ; barre de 4.
        CHECK(view.maxScroll() == doctest::Approx(126));
        CHECK(items[0]->rect().size.x == doctest::Approx(96));
        h.wheel({50, 25}, -1.0f); // un cran vers le bas = 3 lignes de 10
        CHECK(view.scroll() == doctest::Approx(30));
        CHECK(items[2]->rect().position.y == doctest::Approx(2 * 18 - 30));
        h.wheel({50, 25}, -100.0f);
        CHECK(view.scroll() == doctest::Approx(126));
        // Un élément sorti de la vue n'est pas touché par la souris.
        h.wheel({50, 25}, 100.0f);
        CHECK(view.scroll() == doctest::Approx(0));
        CHECK(items[4]->rect().position.y > 50.0f);
        h.moveTo({50, items[4]->rect().position.y + 2});
        CHECK(h.ui.hovered() != items[4]);
        // Le dessin est découpé à la vue.
        orosmo::renderer::DrawList list2;
        h.ui.draw(list2);
        CHECK(list2.count<orosmo::renderer::PushClip>() == 1);
        CHECK(list2.count<orosmo::renderer::PopClip>() == 1);
    }

    TEST_CASE("a theme change (padding) changes the layout at the next frame")
    {
        UiHarness h;
        auto panel = std::make_unique<ui::Panel>();
        auto &label = panel->emplace<ui::Label>("ab");
        rootChild(h, std::move(panel));
        h.frame();
        CHECK(label.rect().position == glm::vec2(5, 5));
        std::string json = tests::kTestTheme;
        json.replace(json.find("\"padding\": 5"), 12, "\"padding\": 9");
        h.ui.themes().set(*ui::Theme::parse(json));
        h.frame();
        CHECK(label.rect().position == glm::vec2(9, 9));
    }

    TEST_CASE("overflow: a child larger than its parent is drawn and clickable outside it, unless the parent hides the overflow")
    {
        UiHarness h;
        auto root = std::make_unique<ui::Overlay>();
        auto &parent = root->emplace<ui::Overlay>();
        parent.setAlign(ui::Align::kStart, ui::Align::kStart);
        parent.setWidth(ui::SizeSpec::fixed(20)).setHeight(ui::SizeSpec::fixed(20));
        auto &child = parent.emplace<ui::Button>("wide");
        child.setAlign(ui::Align::kStart, ui::Align::kStart);
        child.setWidth(ui::SizeSpec::fixed(80)).setHeight(ui::SizeSpec::fixed(20));
        h.ui.setRoot(std::move(root));
        h.frame();
        CHECK(child.rect().size.x == doctest::Approx(80)); // dépasse le parent de 60 px
        h.moveTo({60, 10});
        CHECK(h.ui.hovered() == &child); // visible : cliquable là où il est dessiné
        orosmo::renderer::DrawList visible;
        h.ui.draw(visible);
        CHECK(visible.count<orosmo::renderer::PushClip>() == 0);

        parent.overflow = ui::Overflow::kHidden;
        h.moveTo({61, 10});
        CHECK(h.ui.hovered() == nullptr); // coupé : hors du parent, plus touchable
        h.moveTo({10, 10});
        CHECK(h.ui.hovered() == &child);
        orosmo::renderer::DrawList hidden;
        h.ui.draw(hidden);
        CHECK(hidden.count<orosmo::renderer::PushClip>() == 1);
    }
}
