// UI-07 : thèmes JSON (jetons, états, extension, erreurs lisibles, rechargement à chaud), et les thèmes livrés (res/themes).
#include <doctest/doctest.h>

#include "renderer/repo_paths.hpp"
#include "support/temp_dir.hpp"

#include <engine/core/file_watcher.hpp>
#include <engine/ui/theme.hpp>

#include <thread>

namespace ui = engine::ui;

namespace
{
bool near(const ui::Color &a, const ui::Color &b) { return glm::all(glm::lessThan(glm::abs(a - b), ui::Color(1e-6f))); }

constexpr const char *kBase = R"({
    "name": "base",
    "tokens": {"radius.small": 3, "space.medium": 8, "brand": "color.primary"},
    "widgets": {
        "default": {"foreground": "color.text", "padding": "space.medium",
                    "states": {"disabled": {"foreground": "color.muted"}, "focus": {"border": "color.primary", "borderWidth": 2}}},
        "button": {"background": "color.primary", "radius": "radius.small",
                   "states": {"hover": {"background": "#ff0000"}, "disabled": {"background": "color.muted"}}}
    }
})";
} // namespace

TEST_SUITE("ui/theme")
{
    TEST_CASE("colors are read as #rrggbb or #rrggbbaa; anything else is refused")
    {
        CHECK(near(*ui::parseColor("#ff8000"), {1.0f, 128.0f / 255.0f, 0.0f, 1.0f}));
        CHECK(near(*ui::parseColor("#00000080"), {0.0f, 0.0f, 0.0f, 128.0f / 255.0f}));
        CHECK_FALSE(ui::parseColor("ff8000").has_value());
        CHECK_FALSE(ui::parseColor("#ff80").has_value());
        CHECK_FALSE(ui::parseColor("#gg0000").has_value());
    }

    TEST_CASE("tokens, aliases and states: default, then the widget, then the state of default, then the state of the widget")
    {
        tests::TempDir dir("ui_theme");
        dir.write("base.json", kBase);
        dir.write("dark.json", R"({"name": "dark", "extends": "base.json",
            "tokens": {"color.primary": "#336699", "color.text": "#eeeeee", "color.muted": "#555555"}})");
        auto theme = ui::Theme::load(dir.path / "dark.json");
        INFO((theme ? std::string() : theme.error().message));
        REQUIRE(theme.has_value());
        CHECK(theme->name() == "dark");
        REQUIRE(theme->sources().size() == 2); // le fichier, puis son parent
        CHECK(theme->sources()[1].filename() == "base.json");

        const ui::Style &normal = theme->style("button", ui::WidgetState::kNormal);
        CHECK(near(normal.background, *ui::parseColor("#336699")));
        CHECK(near(normal.foreground, *ui::parseColor("#eeeeee"))); // hérité de default
        CHECK(normal.radius == 3.0f);
        CHECK(normal.padding == 8.0f);
        CHECK(near(theme->style("button", ui::WidgetState::kHover).background, {1, 0, 0, 1}));
        const ui::Style &disabled = theme->style("button", ui::WidgetState::kDisabled);
        CHECK(near(disabled.background, *ui::parseColor("#555555")));
        CHECK(near(disabled.foreground, *ui::parseColor("#555555"))); // état générique de default
        const ui::Style &focus = theme->style("button", ui::WidgetState::kFocus);
        CHECK(focus.borderWidth == 2.0f);
        CHECK(near(focus.background, *ui::parseColor("#336699"))); // l'état focus ne touche pas au fond
        // Type inconnu : style de default ; jeton alias résolu.
        CHECK(near(theme->style("nothing", ui::WidgetState::kNormal).foreground, *ui::parseColor("#eeeeee")));
        CHECK_FALSE(theme->hasWidget("nothing"));
        REQUIRE(theme->token("brand").has_value());
        CHECK(near(std::get<ui::Color>(*theme->token("brand")), *ui::parseColor("#336699")));
        // Valeurs d'une surcharge locale : jeton, couleur ou nombre.
        CHECK(std::get<float>(*theme->resolve("radius.small")) == 3.0f);
        CHECK(std::get<float>(*theme->resolve("12")) == 12.0f);
        CHECK(near(std::get<ui::Color>(*theme->resolve("#00ff00")), {0, 1, 0, 1}));
        CHECK((theme->resolve("nope").error().code == engine::core::ErrorCode::kNotFound));
    }

    TEST_CASE("errors name the file and the JSON path")
    {
        auto expectError = [](const char *json, const std::string &fragment)
        {
            auto theme = ui::Theme::parse(json, {}, "t.json");
            REQUIRE_FALSE(theme.has_value());
            INFO(theme.error().message);
            CHECK((theme.error().code == engine::core::ErrorCode::kParse));
            CHECK(theme.error().message.find(fragment) != std::string::npos);
            CHECK(theme.error().message.starts_with("t.json: "));
        };
        expectError(R"({"widgets": {"button": {"colour": "#ffffff"}}})", "widgets.button.colour: unknown property");
        expectError(R"({"widgets": {"button": {"background": "color.nope"}}})", "widgets.button.background: unknown token 'color.nope'");
        expectError(R"({"widgets": {"button": {"states": {"hovered": {}}}}})", "widgets.button.states.hovered: unknown state");
        expectError(R"({"tokens": {"a": 3}, "widgets": {"button": {"background": "a"}}})", "widgets.button.background: expects a color");
        expectError(R"({"tokens": {"c": "#ffffff"}, "widgets": {"button": {"radius": "c"}}})", "widgets.button.radius: expects a number");
        expectError(R"({"tokens": {"a": "b", "b": "a"}})", "tokens.a: alias cycle");
        expectError(R"({"tokens": {"a": "#12"}})", "tokens.a: invalid color");
        expectError(R"({"widget": {}})", "widget: unknown key");
        expectError(R"([1, 2])", "not a JSON object");
        auto missing = ui::Theme::parse(R"({"extends": "missing.json"})", {}, "t.json");
        REQUIRE_FALSE(missing.has_value());
        CHECK((missing.error().code == engine::core::ErrorCode::kIo));
        CHECK(missing.error().message.find("missing.json: cannot read the theme file") != std::string::npos);
    }

    TEST_CASE("a cycle of extends is refused")
    {
        tests::TempDir dir("ui_theme_cycle");
        dir.write("a.json", R"({"extends": "b.json"})");
        dir.write("b.json", R"({"extends": "a.json"})");
        auto theme = ui::Theme::load(dir.path / "a.json");
        REQUIRE_FALSE(theme.has_value());
        CHECK(theme.error().message.find("cycle") != std::string::npos);
    }

    TEST_CASE("the shipped themes (res/themes) load; dark and light share the structure and differ by their tokens")
    {
        const auto root = tests::repoRoot(__FILE__) / "res" / "themes";
        auto dark = ui::Theme::load(root / "dark.json");
        auto light = ui::Theme::load(root / "light.json");
        INFO((dark ? std::string() : dark.error().message));
        INFO((light ? std::string() : light.error().message));
        REQUIRE(dark.has_value());
        REQUIRE(light.has_value());
        for (const char *widget : {"panel", "label", "title", "button", "checkbox", "slider", "textField", "dropdown", "popup", "item", "tab",
                                   "tabs", "scroll", "tooltip", "plot", "section", "colorPicker", "separator"})
        {
            CAPTURE(widget);
            CHECK(dark->hasWidget(widget));
            CHECK(light->hasWidget(widget));
            CHECK(dark->style(widget, ui::WidgetState::kNormal).padding == light->style(widget, ui::WidgetState::kNormal).padding);
        }
        CHECK_FALSE(near(dark->style("panel", ui::WidgetState::kNormal).background, light->style("panel", ui::WidgetState::kNormal).background));
        CHECK(dark->style("button", ui::WidgetState::kFocus).borderWidth > 0.0f);
        // Variante à images (neuf parties) : elle étend le thème sombre.
        auto framed = ui::Theme::load(root / "framed.json");
        INFO((framed ? std::string() : framed.error().message));
        REQUIRE(framed.has_value());
        REQUIRE(framed->style("button", ui::WidgetState::kNormal).image != nullptr);
        CHECK(framed->style("button", ui::WidgetState::kHover).image->name == "frame.buttonHover");
        CHECK(framed->style("panel", ui::WidgetState::kNormal).image->edges == ui::SliceFill::kRepeat);
        CHECK(framed->style("slider", ui::WidgetState::kNormal).image == nullptr);
    }

    TEST_CASE("hot reload: a modified file installs the new theme; an invalid file keeps the previous one")
    {
        tests::TempDir dir("ui_theme_reload");
        dir.write("base.json", kBase);
        dir.write("t.json", R"({"extends": "base.json", "tokens": {"color.primary": "#000000", "color.text": "#111111", "color.muted": "#222222"}})");
        engine::core::FileWatcher watcher;
        ui::ThemeHost host;
        REQUIRE(host.load(dir.path / "t.json").has_value());
        host.watch(watcher);
        const std::uint64_t first = host.revision();
        CHECK_FALSE(host.reloadIfChanged());

        // Le parent change : le thème est relu (la taille du fichier change, la date aussi sur la plupart des systèmes).
        std::string changed = kBase;
        changed.replace(changed.find("\"radius.small\": 3"), 17, "\"radius.small\": 9");
        changed += "\n";
        dir.write("base.json", changed);
        CHECK(watcher.poll() >= 1);
        CHECK(host.reloadIfChanged());
        CHECK(host.revision() == first + 1);
        CHECK(host.current().style("button", ui::WidgetState::kNormal).radius == 9.0f);

        dir.write("t.json", R"({"extends": "base.json", "tokens": {"color.primary": "not a color"}})");
        CHECK(watcher.poll() >= 1);
        CHECK_FALSE(host.reloadIfChanged());
        REQUIRE(host.lastError().has_value());
        CHECK(host.lastError()->message.find("t.json") != std::string::npos);
        CHECK(host.revision() == first + 1);
        CHECK(host.current().style("button", ui::WidgetState::kNormal).radius == 9.0f); // l'ancien thème reste
        host.unwatch();
    }

    TEST_CASE("the built-in fallback theme is complete enough to draw every widget")
    {
        const ui::Theme theme = ui::Theme::fallback();
        CHECK(theme.name() == "fallback");
        CHECK(theme.style("button", ui::WidgetState::kNormal).fontSize > 0.0f);
        CHECK(theme.style("anything", ui::WidgetState::kNormal).foreground.a > 0.0f);
    }
}
