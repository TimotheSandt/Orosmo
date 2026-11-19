// État C++ lié par la vue de test tests/ui/cui/test_panel.cui (UI-11) : la logique est ici, la vue ne fait que nommer.
#pragma once

#include <engine/core/reflection.hpp>
#include <engine/ui/widget.hpp>

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

struct CuiTestParams
{
    float gain = 0.5f;
    int octaves = 4;
};

template <> struct engine::core::Reflect<CuiTestParams>
{
    static constexpr int kVersion = 1;
    static constexpr auto fields()
    {
        return std::make_tuple(field("gain", &CuiTestParams::gain, range(0.0, 1.0), "gain"),
                               field("octaves", &CuiTestParams::octaves, range(1, 8), "octaves"));
    }
};

struct CuiTestState
{
    std::uint32_t seed = 42;
    float scale = 120.0f;
    bool snow = false;
    int preset = 0;
    std::string name = "terrain";
    glm::vec3 tint{1.0f, 0.5f, 0.0f};
    CuiTestParams params;
    bool canExport = false;
    int randomized = 0;
    int changes = 0;
    int applied = 0;

    void randomize() { seed = seed * 1664525u + 1013904223u, ++randomized; }
    void changed() { ++changes; }
    void apply(const engine::ui::Event &e) { applied += e.kind == engine::ui::EventKind::kClick ? 1 : 0; } // reçoit l'événement
    std::vector<std::string> presetNames() const { return {"Hills", "Mountains", "Islands"}; }
};

struct CuiTestStats
{
    double fps = 59.873;
    double load = 0.25;
    std::vector<double> history{1.0, 3.0, 2.0};
};

namespace Tools
{
inline int exported = 0;
inline void exportNow() { ++exported; }
} // namespace Tools
