// UI-13 : interface économe. Un élément caché ne produit aucune commande ; un élément entièrement hors de sa zone de découpage
// (contenu défilé d'un ScrollView, conteneur overflow(.hidden), hors de la fenêtre) non plus, et le rendu visible est inchangé.
#include "ui/ui_harness.hpp"

namespace ui = orosmo::ui;
namespace rd = orosmo::renderer;
using tests::UiHarness;

namespace
{
std::size_t commandsOf(UiHarness &h)
{
    rd::DrawList list;
    h.ui.draw(list);
    return list.size();
}

template <typename T> T &rootChild(UiHarness &h, std::unique_ptr<T> child)
{
    auto root = std::make_unique<ui::Overlay>();
    child->setAlign(ui::Align::kStart, ui::Align::kStart);
    T &ref = root->add(std::move(child));
    h.ui.setRoot(std::move(root));
    h.frame();
    return ref;
}
} // namespace

TEST_SUITE("ui/lean")
{
    TEST_CASE("hidden element, inactive tab and folded section produce no command")
    {
        UiHarness h;
        auto box = std::make_unique<ui::VBox>("panel");
        auto &shown = box->emplace<ui::Button>("shown");
        auto &hidden = box->emplace<ui::Button>("hidden");
        auto &tabs = box->emplace<ui::Tabs>();
        auto &page1 = tabs.addPage("One", std::make_unique<ui::Button>("page one"));
        auto &page2 = tabs.addPage("Two", std::make_unique<ui::Button>("page two"));
        auto &section = box->emplace<ui::Section>("Folded");
        section.body().emplace<ui::Button>("inside");
        rootChild(h, std::move(box));

        auto count = [&](ui::Widget &w)
        {
            rd::DrawList l;
            w.draw(h.ui, l);
            return l.size();
        };
        hidden.setVisible(false);
        h.frame();
        CHECK(count(shown) > 0);
        CHECK(count(hidden) == 0);
        CHECK(count(page1) > 0);
        CHECK(count(page2) == 0); // onglet inactif
        const std::size_t open = count(section);
        section.setExpanded(false);
        h.frame();
        CHECK(count(section.body()) == 0); // section repliée
        CHECK(count(section) < open);
    }

    TEST_CASE("a long list in a ScrollView only emits the visible rows, and scrolling keeps the visible rows")
    {
        UiHarness h;
        auto scroll = std::make_unique<ui::ScrollView>();
        scroll->setWidth(ui::SizeSpec::fixed(100)).setHeight(ui::SizeSpec::fixed(50));
        auto &list = scroll->emplace<ui::VBox>();
        std::vector<ui::Button *> rows;
        for (int i = 0; i < 1000; ++i)
        {
            rows.push_back(&list.emplace<ui::Button>("row" + std::to_string(i)));
        }
        auto &view = rootChild(h, std::move(scroll));
        h.frame();

        rd::DrawList one;
        rows[0]->draw(h.ui, one);
        const std::size_t perRow = one.size();
        REQUIRE(perRow > 0);
        const std::size_t total = commandsOf(h);
        CHECK(total < 10 * perRow + 10); // 50 px de vue = 3 ou 4 lignes, pas 1000

        view.setScroll(5000.0f);
        h.frame();
        const std::size_t scrolled = commandsOf(h);
        CHECK(scrolled < 10 * perRow + 10);
        rd::DrawList far;
        rows[0]->draw(h.ui, far);
        CHECK(far.size() == 0); // sorti par le haut
        rd::DrawList visible;
        int visibleRows = 0;
        for (auto *r : rows)
        {
            rd::DrawList l;
            r->draw(h.ui, l);
            if (l.size() > 0)
                ++visibleRows;
        }
        CHECK(visibleRows >= 3);
        CHECK(visibleRows <= 5);
    }

    TEST_CASE("overflow hidden container drops children entirely outside it; an inside child is kept")
    {
        UiHarness h;
        auto box = std::make_unique<ui::VBox>("panel");
        box->setWidth(ui::SizeSpec::fixed(60)).setHeight(ui::SizeSpec::fixed(20));
        box->overflow = ui::Overflow::kHidden;
        auto &a = box->emplace<ui::Button>("A");
        auto &b = box->emplace<ui::Button>("B");
        auto &c = box->emplace<ui::Button>("C");
        auto &outer = rootChild(h, std::move(box));
        (void)outer;
        rd::DrawList la, lc;
        a.draw(h.ui, la);
        c.draw(h.ui, lc);
        CHECK(la.size() > 0);
        CHECK(b.rect().position.y > 0.0f);
        CHECK(lc.size() == 0);
    }

    TEST_CASE("an element entirely outside the window produces no command")
    {
        UiHarness h;
        auto root = std::make_unique<ui::Overlay>();
        auto b = std::make_unique<ui::Button>("off");
        b->setAlign(ui::Align::kStart, ui::Align::kStart);
        b->margin = {1000.0f, 1000.0f, 0.0f, 0.0f};
        auto &off = root->add(std::move(b));
        h.ui.setRoot(std::move(root));
        h.frame();
        rd::DrawList l;
        off.draw(h.ui, l);
        CHECK(l.size() == 0);
    }
}
