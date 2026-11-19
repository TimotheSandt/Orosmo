// Outils des tests de l'interface (engine::ui) : police à chasse fixe, thème de test aux valeurs simples, images d'entrée.
#pragma once

#include <doctest/doctest.h>

#include <engine/ui/layout.hpp>
#include <engine/ui/ui.hpp>
#include <engine/ui/widgets.hpp>

#include <string>
#include <vector>

namespace tests
{

// Taille de police 10, caractère de 5 px (FixedFont 0.5) ; aucune marge intérieure sauf "panel" (5) et "button" (2) ; espacement 4
// dans "box", 2 dans "panel" ; taille de référence ("width") 10.
inline constexpr const char *kTestTheme = R"({
    "name": "test",
    "tokens": {"fg": "#ffffff", "bg": "#102030", "accent": "#ff0000", "muted": "#808080", "tip": "#000000"},
    "widgets": {
        "default": {"foreground": "fg", "accent": "accent", "muted": "muted", "fontSize": 10, "padding": 0, "spacing": 0, "width": 10,
                    "states": {"focus": {"border": "accent", "borderWidth": 1}}},
        "box": {"spacing": 4},
        "panel": {"padding": 5, "spacing": 2, "background": "bg"},
        "button": {"padding": 2, "background": "bg", "states": {"hover": {"background": "muted"}}},
        "slider": {"background": "bg"},
        "textField": {"padding": 2, "background": "bg"},
        "dropdown": {"padding": 2, "background": "bg"},
        "popup": {"background": "bg"},
        "tooltip": {"background": "tip", "padding": 3},
        "scroll": {"width": 4}
    }
})";

struct UiHarness
{
    engine::ui::FixedFont font{0.5f};
    engine::ui::Ui ui{font};
    glm::vec2 viewport{400.0f, 300.0f};
    bool down = false;
    glm::vec2 mouse{-100.0f, -100.0f};

    UiHarness()
    {
        auto theme = engine::ui::Theme::parse(kTestTheme, {}, "test");
        REQUIRE(theme.has_value());
        ui.themes().set(std::move(*theme));
    }

    engine::ui::InputFrame input() const
    {
        engine::ui::InputFrame in;
        in.mouse = mouse;
        in.mouseDown = down;
        return in;
    }
    void frame() { ui.update(input(), viewport); }
    void frame(const engine::ui::InputFrame &in)
    {
        mouse = in.mouse;
        down = in.mouseDown;
        ui.update(in, viewport);
    }
    void moveTo(glm::vec2 p)
    {
        mouse = p;
        frame();
    }
    void press(glm::vec2 p)
    {
        mouse = p;
        down = true;
        frame();
    }
    void release(glm::vec2 p)
    {
        mouse = p;
        down = false;
        frame();
    }
    void secondaryClick(glm::vec2 p)
    {
        mouse = p;
        engine::ui::InputFrame in = input();
        in.secondaryDown = true;
        ui.update(in, viewport);
        frame();
    }
    // Déplacement avec le bouton principal enfoncé et Maj (glissement fin).
    void dragShift(glm::vec2 p)
    {
        mouse = p;
        engine::ui::InputFrame in = input();
        in.shift = true;
        ui.update(in, viewport);
    }
    void click(glm::vec2 p)
    {
        press(p);
        release(p);
    }
    void keys(std::vector<int> k, bool shift = false)
    {
        engine::ui::InputFrame in = input();
        in.keys = std::move(k);
        in.shift = shift;
        ui.update(in, viewport);
    }
    // Combinaisons appuyées pendant une image ("Ctrl+Z", "F5").
    void shortcut(std::string chord)
    {
        engine::ui::InputFrame in = input();
        in.shortcuts = {std::move(chord)};
        ui.update(in, viewport);
    }
    void type(std::string text)
    {
        engine::ui::InputFrame in = input();
        in.text = std::move(text);
        ui.update(in, viewport);
    }
    void wheel(glm::vec2 at, float y)
    {
        mouse = at;
        engine::ui::InputFrame in = input();
        in.wheel = {0.0f, y};
        ui.update(in, viewport);
    }
    void wait(float seconds)
    {
        engine::ui::InputFrame in = input();
        in.deltaTime = seconds;
        ui.update(in, viewport);
    }
    static glm::vec2 center(const engine::ui::Widget &w) { return w.rect().position + w.rect().size * 0.5f; }
};

inline bool sameRect(const engine::ui::Rect &r, glm::vec2 position, glm::vec2 size, float eps = 1e-3f)
{
    return glm::all(glm::lessThan(glm::abs(r.position - position), glm::vec2(eps))) && glm::all(glm::lessThan(glm::abs(r.size - size), glm::vec2(eps)));
}

} // namespace tests
