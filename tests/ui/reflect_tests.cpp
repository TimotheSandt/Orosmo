// UI-12 : une structure réfléchie (CORE-12) s'affiche en widgets adaptés au type et à la plage, sans code d'interface ; les
// changements écrivent la structure et remontent en onChange du panneau.
#include "ui/ui_harness.hpp"

#include <engine/ui/reflect.hpp>
#include <engine/world/terrain.hpp>

namespace ui = engine::ui;
namespace core = engine::core;
using tests::UiHarness;

namespace
{
enum class Mode : std::uint8_t
{
    kA,
    kB,
};
enum class Raw : std::uint8_t
{
    kX,
    kY,
};

struct Inner
{
    float weight = 0.5f;
};

struct Everything
{
    bool enabled = true;
    Mode mode = Mode::kA;
    Raw raw = Raw::kX;
    int count = 3;
    std::int64_t big = 7;
    float ratio = 0.25f;
    double free = 1.5;
    std::string name = "n";
    core::Vec3 tintColor{1.0f, 0.0f, 0.0f};
    core::Vec4 baseColor{0.0f, 0.0f, 1.0f, 0.5f};
    core::Vec3 offset{1.0f, 2.0f, 3.0f};
    core::Vec2 size{4.0f, 5.0f};
    std::vector<int> list{1, 2, 3};
    Inner inner;
};
} // namespace

template <> struct engine::core::EnumNames<Mode>
{
    static constexpr std::array<const char *, 2> names{"A", "B"};
};
template <> struct engine::core::Reflect<Inner>
{
    static constexpr int kVersion = 1;
    static constexpr auto fields() { return std::make_tuple(field("weight", &Inner::weight, range(0.0, 1.0), "poids")); }
};
template <> struct engine::core::Reflect<Everything>
{
    static constexpr int kVersion = 1;
    static constexpr auto fields()
    {
        using E = Everything;
        return std::make_tuple(field("enabled", &E::enabled), field("mode", &E::mode), field("raw", &E::raw), field("count", &E::count, range(1, 8)),
                               field("big", &E::big), field("ratio", &E::ratio, range(0.0, 1.0), "rapport"), field("free", &E::free),
                               field("name", &E::name), field("tintColor", &E::tintColor), field("baseColor", &E::baseColor),
                               field("offset", &E::offset), field("size", &E::size), field("list", &E::list), field("inner", &E::inner));
    }
};

TEST_SUITE("ui/reflect")
{
    TEST_CASE("field labels are readable")
    {
        CHECK(ui::fieldLabel("heightScale") == "Height scale");
        CHECK(ui::fieldLabel("tileResolution") == "Tile resolution");
        CHECK(ui::fieldLabel("seed") == "Seed");
        CHECK(ui::fieldLabel("useLOD") == "Use lOD"); // un sigle après une minuscule commence un mot
        CHECK(ui::fieldLabel("sea_level") == "Sea level");
    }

    TEST_CASE("each field type gets its widget; descriptions become tooltips; ids are field paths")
    {
        UiHarness h;
        Everything e;
        auto panel = ui::reflectedPanel(e);
        auto *p = panel.get();
        h.ui.setRoot(std::move(panel));
        h.frame();
        CHECK(p->findAs<ui::Checkbox>("enabled") != nullptr);
        CHECK(p->findAs<ui::Dropdown>("mode") != nullptr);
        CHECK(p->findAs<ui::NumberField>("raw") != nullptr);
        auto *count = p->findAs<ui::Slider>("count");
        REQUIRE(count != nullptr);
        CHECK(count->min() == 1.0);
        CHECK(count->max() == 8.0);
        CHECK(count->formatted() == "3");
        CHECK(p->findAs<ui::NumberField>("big") != nullptr);
        auto *ratio = p->findAs<ui::Slider>("ratio");
        REQUIRE(ratio != nullptr);
        CHECK(ratio->tooltip() == "rapport");
        CHECK(p->findAs<ui::NumberField>("free") != nullptr);
        CHECK(p->findAs<ui::TextField>("name") != nullptr);
        auto *tint = p->findAs<ui::ColorPicker>("tintColor");
        REQUIRE(tint != nullptr);
        CHECK(tint->children()[0]->children().size() == 3); // R, G, B
        auto *base = p->findAs<ui::ColorPicker>("baseColor");
        REQUIRE(base != nullptr);
        CHECK(base->children()[0]->children().size() == 4); // et A
        CHECK(p->find("offset")->children().size() == 3);
        CHECK(p->find("size")->children().size() == 2);
        REQUIRE(p->findAs<ui::Label>("list") != nullptr);
        CHECK(p->findAs<ui::Label>("list")->text() == "[3]");
        CHECK(p->findAs<ui::Section>("inner") != nullptr);
        CHECK(p->findAs<ui::Slider>("inner.weight") != nullptr);
    }

    TEST_CASE("TerrainParams: the noise basis is a dropdown with its names; a change writes the parameters and reaches the panel")
    {
        UiHarness h;
        h.viewport = {400, 800};
        engine::world::TerrainParams params;
        int changes = 0;
        auto panel = ui::reflectedPanel(params);
        panel->onChange([&](const ui::Event &) { ++changes; });
        auto *p = panel.get();
        auto root = std::make_unique<ui::Overlay>();
        panel->setAlign(ui::Align::kStart, ui::Align::kStart);
        panel->setWidth(ui::SizeSpec::fixed(300));
        root->add(std::move(panel));
        h.ui.setRoot(std::move(root));
        h.frame();

        auto *basis = p->findAs<ui::Dropdown>("relief.basis");
        REQUIRE(basis != nullptr);
        CHECK(basis->options() == std::vector<std::string>{"Perlin", "Simplex", "Worley"});
        auto *octaves = p->findAs<ui::Slider>("relief.octaves");
        REQUIRE(octaves != nullptr);
        CHECK(p->findAs<ui::Slider>("warp.octaves") != nullptr);
        auto *scale = p->findAs<ui::Slider>("heightScale");
        REQUIRE(scale != nullptr);

        // Clic au milieu du curseur d'octaves (1..16) : 8,5 → 9 (arrondi au pas).
        h.click(UiHarness::center(*octaves));
        CHECK(params.relief.octaves == 9);
        CHECK(changes == 1);
        // Clic droit : valeur par défaut de TerrainParams{} (6 octaves), pas celle du moment.
        h.secondaryClick(UiHarness::center(*octaves));
        CHECK(params.relief.octaves == 6);
        CHECK(changes == 2);
        changes = 1;
        // Choix de Worley dans la liste.
        h.click(UiHarness::center(*basis));
        REQUIRE(h.ui.popup() != nullptr);
        const auto &items = h.ui.popup()->children()[0]->children();
        REQUIRE(items.size() == 3);
        h.click(UiHarness::center(*items[2]));
        CHECK(params.relief.basis == engine::world::NoiseBasis::kWorley);
        CHECK(changes == 2);
        // Repli d'une section : ses contrôles disparaissent.
        auto *relief = p->findAs<ui::Section>("relief");
        REQUIRE(relief != nullptr);
        h.click({relief->rect().position.x + 3, relief->rect().position.y + 3});
        CHECK_FALSE(relief->body().visible());
    }
}
