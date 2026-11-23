// Panneau de performances de la démo (vue UI/Views/PerformanceView.cui, thème res/themes/hud.json). Quatre niveaux (--hud,
// Ctrl+F) : rien, FPS, toutes les mesures, toutes les mesures + historique des temps d'image (en bas à droite).
// Les mesures (`PerformanceSample`) viennent de Game::samplePerformance ; une mesure qui n'existe pas est absente (std::nullopt)
// et s'affiche « n/d ». Les textes ne sont refaits qu'au rafraîchissement (environ 4 fois par seconde) : la vue relit des chaînes
// déjà prêtes. L'historique (une entrée par image) est un anneau de taille fixe.
#pragma once

#include "launch_options.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

struct PerformanceSample
{
    // Images par seconde sur l'historique (intervalles entre débuts d'image) : moyenne, 1 % bas (99e centile de l'intervalle),
    // plus lente et plus rapide. Zéro sans historique.
    double fpsAverage = 0.0;
    double fpsLow1 = 0.0;
    double fpsMin = 0.0;
    double fpsMax = 0.0;
    double windowFps = 0.0;                                 // moyenne de la fenêtre (Window::getAverageFPS ; 0 sans fenêtre)
    double frameCpuMs = 0.0;                                // temps CPU moyen d'une image (mise à jour -> présentation)
    std::optional<double> gpuMs;                            // « Render » (requêtes de temps GPU)
    std::vector<std::pair<std::string, double>> gpuPasses;  // passes actives du graphe (RenderGraph/<passe>), ms GPU
    // Découpage CPU (moyennes du Profiler, ms).
    double updateMs = 0.0;
    double recordMs = 0.0;  // enregistrement de la scène
    double graphMs = 0.0;   // exécution du graphe (passes encodées, interface comprise)
    double submitMs = 0.0;  // soumission
    double presentMs = 0.0; // présentation (SwapBuffers)
    // Dessins de la dernière image.
    std::optional<std::uint32_t> sceneMeshes;    // maillages dessinés par la scène (un dessin chacun)
    std::optional<std::uint32_t> instancedDraws; // --instances : dessins instanciés (culling CPU)
    std::optional<std::uint32_t> instances;      // instances dessinées
    std::optional<std::uint32_t> uiDraws;        // dessins 2D de l'interface (lots du CanvasRenderer)
    std::optional<std::uint32_t> shadowDraws;    // --shadows on
    std::optional<std::uint32_t> visibleObjects; // --instances, culling CPU
    std::optional<std::uint32_t> objects;
    std::uint32_t lights = 0;
    std::string backend;
    std::string device;
    std::uint32_t outputWidth = 0;
    std::uint32_t outputHeight = 0;
    std::uint32_t renderWidth = 0;
    std::uint32_t renderHeight = 0;
    float renderScale = 1.0f;
    bool vsync = false;
    std::string modes; // anticrénelage, post-traitements, ombres, éclairage
    std::optional<double> residentMiB;  // mémoire résidente du processus
    std::optional<double> gpuMemoryMiB; // le RHI ne l'expose pas encore : toujours absente
};

// Textes du panneau, refaits au rafraîchissement seulement.
struct PerformanceText
{
    std::string fps;
    std::string fpsDetail;
    std::string frame;
    std::string gpu;
    std::array<std::string, 3> passes; // passes du graphe, quelques-unes par ligne ; vides si absentes
    std::string cpu;
    std::string draws;
    std::string objects;
    std::string device;
    std::string resolution;
    std::string modes;
    std::string memory;
    std::string cpuGraph;
    std::string gpuGraph;
    std::string summary; // une ligne pour l'onglet Scene de l'interface complète (DemoView.cui)
};

// Textes d'une mesure (pur : testable sans fenêtre).
PerformanceText formatPerformance(const PerformanceSample &sample);

// Mémoire résidente du processus (Linux : /proc/self/statm) ; absente ailleurs (docs/GAPS.md).
std::optional<double> processResidentMiB();

struct PerformanceStats
{
    static constexpr float kRefreshSeconds = 0.25f;
    static constexpr std::size_t kHistorySize = 240;

    demo::HudLevel level = demo::HudLevel::kFull;
    PerformanceSample shown;              // dernière mesure
    PerformanceText text;                 // ce que la vue affiche
    float sinceRefresh = kRefreshSeconds; // la première image rafraîchit tout de suite

    // Avance de `deltaSeconds` ; toutes les kRefreshSeconds, remplace `shown` par `sample()` et refait les textes. Vrai si
    // `shown` a changé.
    bool tick(float deltaSeconds, const std::function<PerformanceSample()> &sample);
    void cycleLevel() { level = demo::nextHudLevel(level); }

    // Une image : temps CPU, temps GPU (0 : inconnu) et intervalle depuis le début de la précédente (0 pour la première), en ms.
    void pushFrame(float cpuMs, float gpuMs, float intervalMs);
    std::size_t historySize() const noexcept { return count_; }
    // Historique de la plus ancienne à la plus récente (lu par les graphes à chaque image : copie de kHistorySize flottants).
    std::vector<float> cpuHistory() const { return ordered(cpu_); }
    std::vector<float> gpuHistory() const { return ordered(gpu_); }
    // FPS de l'historique (intervalles) : moyenne, 1 % bas, min, max ; zéros sans intervalle.
    void fpsFromHistory(PerformanceSample &sample) const;
    // Temps CPU moyen de l'historique (ms).
    double averageCpuMs() const;

    // Lus par la vue (règle 12 : la vue ne décide pas, elle demande).
    bool showFps() const noexcept { return level != demo::HudLevel::kOff; }
    bool showDetails() const noexcept { return level == demo::HudLevel::kFull || level == demo::HudLevel::kGraph; }
    bool showGraph() const noexcept { return level == demo::HudLevel::kGraph; }
    bool showPassLine(int i) const { return showDetails() && i >= 0 && i < 3 && !text.passes[static_cast<std::size_t>(i)].empty(); }
    const std::string &passLine(int i) const { return text.passes[static_cast<std::size_t>(std::clamp(i, 0, 2))]; }

  private:
    std::vector<float> ordered(const std::array<float, kHistorySize> &ring) const;
    std::array<float, kHistorySize> cpu_{};
    std::array<float, kHistorySize> gpu_{};
    std::array<float, kHistorySize> interval_{};
    std::size_t next_ = 0;
    std::size_t count_ = 0;
};
