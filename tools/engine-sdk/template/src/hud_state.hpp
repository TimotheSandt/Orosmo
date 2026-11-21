// Minimal starter state bound to HudView.cui.
#pragma once

#include <cstdint>
#include <string>

struct HudState
{
    std::string title = "{{PROJECT_NAME}}";
    bool rotate = true;
    std::uint64_t frame = 0;

    void resetFrame() { frame = 0; }
};
