// Démo de l'interface (galerie) : l'état C++ que la vue Gallery.cui lie et dont elle nomme les fonctions. Toute la logique est
// ici (règle 12) ; la vue ne fait que décrire.
#pragma once

#include <engine/core/reflection.hpp>
#include <engine/ui/widget.hpp>

#include <glm/glm.hpp>

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

enum class Quality : std::uint8_t
{
    kLow,
    kMedium,
    kHigh,
};

struct Lighting
{
    float intensity = 1.0f;
    glm::vec3 sunColor{1.0f, 0.95f, 0.8f};
};

// Structure réfléchie : affichée par ParamPanel sans code d'interface (UI-12).
struct GalleryParams
{
    bool shadows = true;
    Quality quality = Quality::kMedium;
    int samples = 4;
    float exposure = 0.5f;
    double gamma = 2.2;     // sans plage : champ numérique
    std::string label = "scene";
    Lighting lighting;
};

template <> struct engine::core::EnumNames<Quality>
{
    static constexpr std::array<const char *, 3> names{"Low", "Medium", "High"};
};
template <> struct engine::core::Reflect<Lighting>
{
    static constexpr int kVersion = 1;
    static constexpr auto fields()
    {
        return std::make_tuple(field("intensity", &Lighting::intensity, range(0.0, 4.0), "sun intensity"),
                               field("sunColor", &Lighting::sunColor, {}, "colour of the sun (name contains 'color': colour picker)"));
    }
};
template <> struct engine::core::Reflect<GalleryParams>
{
    static constexpr int kVersion = 1;
    static constexpr auto fields()
    {
        using P = GalleryParams;
        return std::make_tuple(field("shadows", &P::shadows, {}, "bool: checkbox"), field("quality", &P::quality, {}, "named enum: dropdown"),
                               field("samples", &P::samples, range(1, 16), "bounded int: slider with a step of 1"),
                               field("exposure", &P::exposure, range(0.0, 1.0), "bounded float: slider (right click: default)"),
                               field("gamma", &P::gamma, {}, "unbounded number: number field"), field("label", &P::label, {}, "string: text field"),
                               field("lighting", &P::lighting, {}, "nested structure: foldable section"));
    }
};

struct GalleryState
{
    // ── Lié par la vue ──
    std::string name = "Ada";
    bool enabled = true;
    bool notifications = false;
    float volume = 0.7f;
    int count = 3;
    std::uint32_t seed = 42;
    int fruit = 1;
    int theme = 0;
    int tab = 0;
    glm::vec4 tint{0.30f, 0.55f, 1.0f, 1.0f};
    GalleryParams params;
    int clicks = 0;

    // ── Appelé par la vue (événements) ──
    void clicked(const engine::ui::Event &e);
    void changed(const engine::ui::Event &e);
    void hovered(const engine::ui::Event &e);
    void focused(const engine::ui::Event &e);
    void applyTheme() { themeRequested = true; }
    void clearLog() { log.clear(); }

    // ── Lu par la vue ──
    std::vector<std::string> fruits() const { return {"Apple", "Banana", "Cherry", "Durian"}; }
    std::vector<std::string> themes() const { return {"dark", "light", "framed"}; }
    std::vector<float> wave() const { return samples; }
    std::string logLine(int i) const { return i < static_cast<int>(log.size()) ? log[static_cast<std::size_t>(i)] : std::string(); }
    std::string summary() const;
    bool canSubmit() const { return enabled && !name.empty(); }

    // ── Côté C++ ──
    void record(std::string line);
    void tick(float deltaTime); // anime la courbe du graphique
    std::deque<std::string> log;
    std::vector<float> samples;
    float time = 0.0f;
    bool themeRequested = false;
};
