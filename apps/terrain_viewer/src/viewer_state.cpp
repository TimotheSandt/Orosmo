#include "viewer_state.hpp"

#include <orosmo/core/serialization.hpp>

#include <orosmo/core/log.hpp>

#include <algorithm>
#include <fstream>
#include <random>
#include <sstream>

void ViewerState::randomizeSeed()
{
    // Graine choisie au hasard, explicitement et journalisée (A12) : la génération reste déterministe pour cette graine.
    std::random_device device;
    seed = device();
    LOG_INFO("Viewer: random seed ", seed);
    status = "Seed " + std::to_string(seed);
    regenerateRequested = true;
}

void ViewerState::paramsChanged()
{
    if (live)
    {
        paramsChangedAt = now;
    }
}

void ViewerState::applyPreset()
{
    if (preset >= 0 && preset < static_cast<int>(presets.size()))
    {
        params = presets[static_cast<std::size_t>(preset)];
        status = "Preset " + names[static_cast<std::size_t>(preset)];
        regenerateRequested = true;
    }
}

void ViewerState::savePreset()
{
    // Préréglage de l'utilisateur : `custom.json` dans le dossier des préréglages (remplacé à chaque enregistrement).
    std::error_code ec;
    std::filesystem::create_directories(presetDirectory, ec);
    const std::filesystem::path path = presetDirectory / "custom.json";
    // Écrit à côté puis remplace : un échec (disque plein, quota) laisse l'ancien custom.json intact. La fermeture est vérifiée :
    // c'est elle qui vide le tampon.
    const std::filesystem::path temporary = presetDirectory / "custom.json.tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out << orosmo::core::saveDocument(params).dump(2) << '\n';
        out.close();
        if (!out)
        {
            std::filesystem::remove(temporary, ec);
            status = "Cannot write " + path.generic_string();
            LOG_ERROR(1, status);
            return;
        }
    }
    std::filesystem::rename(temporary, path, ec);
    if (ec)
    {
        std::filesystem::remove(temporary, ec);
        status = "Cannot replace " + path.generic_string();
        LOG_ERROR(1, status);
        return;
    }
    loadPresets(presetDirectory);
    selectPreset("custom");
    status = "Saved " + path.filename().generic_string();
}

void ViewerState::loadPresets(const std::filesystem::path &directory)
{
    presetDirectory = directory;
    std::vector<std::pair<std::string, orosmo::world::TerrainParams>> found;
    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator(directory, ec))
    {
        if (entry.path().extension() != ".json")
        {
            continue;
        }
        std::ifstream in(entry.path(), std::ios::binary);
        std::ostringstream text;
        text << in.rdbuf();
        auto json = orosmo::core::parseJson(text.str());
        orosmo::world::TerrainParams p;
        if (!json || !orosmo::core::loadDocument(p, *json))
        {
            LOG_WARNING("Viewer: preset ignored (invalid): ", entry.path().generic_string());
            continue;
        }
        found.emplace_back(entry.path().stem().string(), p);
    }
    std::sort(found.begin(), found.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    names.clear();
    presets.clear();
    for (auto &[name, p] : found)
    {
        names.push_back(name);
        presets.push_back(p);
    }
}

bool ViewerState::selectPreset(const std::string &name)
{
    const auto it = std::find(names.begin(), names.end(), name);
    if (it == names.end())
    {
        return false;
    }
    preset = static_cast<int>(it - names.begin());
    return true;
}
