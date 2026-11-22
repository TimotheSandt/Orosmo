// Unified demo UI state bound by UI/Views/DemoView.cui: owns live 3D scene parameters, engine action flags, core
// system demonstrations (TaskSystem benchmark, reflected JSON persistence), and UI widget showcase state.
#pragma once

#include "Scene/demo_scene.hpp"
#include "UI/performance_stats.hpp"

#include <orosmo/ui/color.hpp>
#include <orosmo/ui/menus.hpp>
#include <orosmo/ui/widget.hpp>

#include <glm/glm.hpp>

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct DemoUiState
{
    // ── Live 3D scene parameters (edited via ParamPanel and persisted via JSON) ──
    demo::DemoSceneParams scene;
    std::string deviceInfo = "OpenGL";
    std::string taskBenchmarkResult = "Ready";
    std::string configStatus = "Ready";

    // ── Application requests consumed by Game ──
    bool quitRequested = false;
    bool closeUiRequested = false;
    bool resetCameraRequested = false;
    bool captureRequested = false;
    bool themeRequested = false;
    int theme = 0; // 0=dark, 1=light, 2=framed
    int tab = 0;   // 0=scene, 1=widgets, 2=controls, 3=color, 4=windows, 5=layout, 6=themes
    float cameraYawRadians = 0.0f;
    orosmo::ui::Ui *ui = nullptr;

    // ── UI showcase widget state ──
    std::string name = "Ada";
    bool enabled = true;
    bool notifications = false;
    float volume = 0.7f;
    int count = 3;
    std::uint32_t sampleId = 42;
    int fruit = 1;
    glm::vec4 tint{0.30f, 0.55f, 1.0f, 1.0f};
    int clicks = 0;

    int detail = 1;
    double rangeLow = 20.0;
    double rangeHigh = 70.0;
    int steps = 4;
    std::string notes =
        "Multi-line text editor.\nSelect with Shift or the mouse; Ctrl+A, C, X, V; Ctrl+Z to undo, Ctrl+Y to redo. "
        "A line longer than the editor wraps at the last space that fits, and its number is shown once.\nLine numbers on the left.";

    glm::vec4 accent{0.95f, 0.55f, 0.20f, 1.0f};
    orosmo::ui::Gradient paletteGradient{{{0.0f, {0.10f, 0.25f, 0.60f, 1.0f}},
                                          {0.38f, {0.25f, 0.55f, 0.85f, 1.0f}},
                                          {0.42f, {0.85f, 0.80f, 0.55f, 1.0f}},
                                          {0.50f, {0.30f, 0.60f, 0.25f, 1.0f}},
                                          {0.75f, {0.45f, 0.40f, 0.35f, 1.0f}},
                                          {0.92f, {0.95f, 0.95f, 0.97f, 1.0f}}}};
    orosmo::ui::Curve responseCurve{{{0.0f, 0.0f}, {0.45f, 0.35f}, {1.0f, 1.0f}}};

    bool dialogOpen = false;
    bool showMarker = true;
    orosmo::ui::DynamicImage previewImage{96, 96};
    orosmo::ui::DynamicImage compass{64, 64};

    // ── Theme and tab helpers called by Game & DemoView.cui ──
    std::vector<std::string> themes() const;
    static std::vector<std::string> tabNames();
    bool selectThemeByName(std::string_view name);
    bool selectTabByName(std::string_view name);

    // ── Engine / application handlers ──
    void requestQuit();
    void requestCloseUi();
    void requestResetCamera();
    void requestCapture();
    void runTaskBenchmark();
    void saveConfigJson();
    void loadConfigJson();

    // ── UI showcase event handlers ──
    void clicked(const orosmo::ui::Event &e);
    void changed(const orosmo::ui::Event &e);
    void hovered(const orosmo::ui::Event &e);
    void focused(const orosmo::ui::Event &e);
    void applyTheme() { themeRequested = true; }
    void clearLog() { log.clear(); }
    void openDialog() { dialogOpen = true; }
    void closeDialog() { dialogOpen = false; }
    void notifyInfo();
    void notifySuccess();
    void notifyWarning();
    void notifyError();

    // ── Read by DemoView.cui ──
    std::vector<std::string> fruits() const { return {"Apple", "Banana", "Cherry", "Durian"}; }
    std::vector<float> frameHistory() const { return frameSamples; }
    std::string logLine(int i) const;
    std::string summary() const;
    std::string controlsSummary() const;
    bool canSubmit() const { return enabled && !name.empty(); }

    // ── Frame update & event log ──
    void record(std::string line);
    void tick(float deltaTime, float frameTimeMs);
    // Images vivantes (prévisualisation et boussole, onglet « Windows ») : redessinées seulement si elles sont visibles
    // (`fullUiShown` et onglet « Windows ») et que leurs entrées ont changé depuis le dernier dessin. À appeler après la mise à
    // jour de l'interface (l'onglet choisi cette image) et avant son dessin.
    void refreshLiveImages(bool fullUiShown);
    bool liveImagesVisible(bool fullUiShown) const;

    std::deque<std::string> log;
    std::vector<float> frameSamples;
    float time = 0.0f;
    int notified = 0;
    // Nombre de dessins de chaque image vivante depuis la création (tests et diagnostics).
    std::uint64_t previewDraws = 0;
    std::uint64_t compassDraws = 0;

  private:
    // Entrées dont dépendent les pixels de la prévisualisation.
    struct PreviewInputs
    {
        double rangeLow = 0.0;
        double rangeHigh = 0.0;
        int steps = 0;
        bool showMarker = false;
        float cameraYawRadians = 0.0f;
        glm::vec4 accent{0.0f};
        orosmo::ui::Gradient gradient;
        orosmo::ui::Curve curve;
    };
    bool previewInputsChanged() const;
    void drawPreview();
    void drawCompass();
    std::optional<PreviewInputs> drawnPreview_;
    std::optional<float> drawnCompassYaw_;
};
