// Valeurs du panneau de performances de la démo (vue UI/Views/PerformanceView.cui, thème res/themes/hud.json) : FPS moyen de la
// fenêtre et moyennes du Profiler en millisecondes. La vue les lit (@Observed) ; elles ne changent qu'environ 4 fois par seconde
// pour rester lisibles.
#pragma once

#include <functional>

struct PerformanceSample
{
    double fps = 0.0;           // images par seconde (moyenne de la fenêtre)
    double renderMs = 0.0;      // moyennes du Profiler, en millisecondes
    double renderWorldMs = 0.0;
    double upscaleMs = 0.0;
    double uiUpscaleMs = 0.0;
    double swapBuffersMs = 0.0;
};

// Moyennes actuelles du Profiler (Render, RenderWorld, Upscale, UIUpscale, SwapBuffers) ; `averageFps` vient de la fenêtre.
PerformanceSample samplePerformance(double averageFps);

struct PerformanceStats
{
    static constexpr float kRefreshSeconds = 0.25f;

    PerformanceSample shown;                // ce que la vue affiche
    float sinceRefresh = kRefreshSeconds;   // la première image rafraîchit tout de suite

    // Avance de `deltaSeconds` ; toutes les kRefreshSeconds, remplace `shown` par `sample()`. Vrai si `shown` a changé.
    bool tick(float deltaSeconds, const std::function<PerformanceSample()> &sample);
};
