// TOOL-01 / TER-12 : état du visualiseur, lié par la vue TerrainPanel.cui. Toute la logique est ici (règle 12) : la vue nomme ces
// fonctions et lie ces champs ; le visualiseur (viewer.cpp) lit les demandes à chaque image.
#pragma once

#include <orosmo/world/terrain.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct ViewerStats
{
    double fps = 0.0;
    int tiles = 0;
    int pending = 0;
    double memoryMb = 0.0;
    std::vector<float> frameTimes; // ms, dernières images
};

struct ViewerState
{
    // ── Lié par la vue ──
    std::uint32_t seed = 1;
    orosmo::world::TerrainParams params;
    int preset = 0;
    int theme = 0; // index dans themeNames()
    bool live = true;
    std::string status;

    // ── Appelé par la vue (événements) ──
    void randomizeSeed();
    void regenerate() { regenerateRequested = true; }
    void paramsChanged();
    void applyPreset();
    void savePreset();
    void applyTheme() { themeRequested = true; }
    void exportHeightmap() { exportRequested = true; }
    std::vector<std::string> presetNames() const { return names; }
    std::vector<std::string> themeNames() const { return {"dark", "light", "framed"}; }

    // ── Côté C++ ──
    // Lit les préréglages (`<dossier>/*.json`, documents {"version", "data"} de TerrainParams), triés par nom.
    void loadPresets(const std::filesystem::path &directory);
    // Sélectionne le préréglage `name` s'il existe ; faux sinon.
    bool selectPreset(const std::string &name);

    std::filesystem::path presetDirectory;
    std::vector<std::string> names;
    std::vector<orosmo::world::TerrainParams> presets;
    bool regenerateRequested = false;
    bool themeRequested = false;
    bool exportRequested = false;
    double paramsChangedAt = -1.0; // instant du dernier changement (régénération différée pendant un glissement)
    double now = 0.0;
};
