// Démo de l'interface (galerie) : l'état C++ que la vue Gallery.cui lie et dont elle nomme les fonctions. Toute la logique est
// ici (règle 12) ; la vue ne fait que décrire.
#pragma once

#include <engine/core/reflection.hpp>
#include <engine/ui/color.hpp>
#include <engine/ui/menus.hpp>
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
    // Page « Controls » / « Color » / « Windows » (UI-03).
    int detail = 1;
    double heightLow = 20.0;
    double heightHigh = 70.0;
    int octaves = 4;
    std::string notes = "Multi-line text editor.\nSelect with Shift or the mouse; Ctrl+A, C, X, V; Ctrl+Z to undo, Ctrl+Y to redo. A line longer than the editor wraps at the last space that fits, and its number is shown once.\nLine numbers on the left.";
    glm::vec4 accent{0.95f, 0.55f, 0.20f, 1.0f};
    engine::ui::Gradient terrainColors{{{0.0f, {0.10f, 0.25f, 0.60f, 1.0f}}, {0.38f, {0.25f, 0.55f, 0.85f, 1.0f}},
                                        {0.42f, {0.85f, 0.80f, 0.55f, 1.0f}}, {0.50f, {0.30f, 0.60f, 0.25f, 1.0f}},
                                        {0.75f, {0.45f, 0.40f, 0.35f, 1.0f}}, {0.92f, {0.95f, 0.95f, 0.97f, 1.0f}}}};
    engine::ui::Curve heightCurve{{{0.0f, 0.0f}, {0.45f, 0.35f}, {1.0f, 1.0f}}};
    bool dialogOpen = false;
    bool showPlayer = true;
    bool windowsRequested = false;
    // Images produites par le jeu (ici la galerie) : mini-carte (hauteurs -> courbe -> dégradé) et boussole.
    engine::ui::DynamicImage minimap{96, 96};
    engine::ui::DynamicImage compass{64, 64};
    engine::ui::Ui *ui = nullptr; // notifications (Ui::notify)

    // ── Appelé par la vue (événements) ──
    void clicked(const engine::ui::Event &e);
    void changed(const engine::ui::Event &e);
    void hovered(const engine::ui::Event &e);
    void focused(const engine::ui::Event &e);
    void applyTheme() { themeRequested = true; }
    void clearLog() { log.clear(); }
    void openDialog() { dialogOpen = true; }
    void closeDialog() { dialogOpen = false; }
    void notifyInfo();
    void notifySuccess();
    void notifyWarning();
    void notifyError();

    // ── Lu par la vue ──
    std::vector<std::string> fruits() const { return {"Apple", "Banana", "Cherry", "Durian"}; }
    std::vector<std::string> themes() const { return {"dark", "light", "framed"}; }
    std::vector<float> wave() const { return samples; }
    std::string logLine(int i) const { return i < static_cast<int>(log.size()) ? log[static_cast<std::size_t>(i)] : std::string(); }
    std::string summary() const;
    std::string controlsSummary() const;
    bool canSubmit() const { return enabled && !name.empty(); }

    // ── Côté C++ ──
    void record(std::string line);
    void tick(float deltaTime); // anime la courbe du graphique, redessine la mini-carte et la boussole
    std::deque<std::string> log;
    std::vector<float> samples;
    float time = 0.0f;
    int notified = 0;
    bool themeRequested = false;

  private:
    void drawMinimap();
    void drawCompass();
};
