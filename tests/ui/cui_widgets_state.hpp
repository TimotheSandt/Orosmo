// État C++ lié par la vue de test tests/ui/cui/test_widgets.cui (UI-03).
#pragma once

#include <engine/ui/color.hpp>
#include <engine/ui/menus.hpp>

#include <glm/glm.hpp>

#include <string>

struct CuiWidgetsState
{
    int quality = 1;
    double low = 20.0;
    double high = 80.0;
    int count = 3;
    std::string notes = "line 1\nline 2";
    glm::vec4 color{0.2f, 0.4f, 0.6f, 1.0f};
    engine::ui::Gradient gradient;
    engine::ui::Curve curve;
    bool grid = false;
    bool dialog = false;
    engine::ui::DynamicImage minimap{8, 4};
    int saved = 0;
    int copied = 0;

    void save() { ++saved; }
    void copy() { ++copied; }
};
