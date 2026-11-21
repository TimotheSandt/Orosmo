#include "UI/performance_stats.hpp"

#include <orosmo/core/profiler.hpp>

PerformanceSample samplePerformance(double averageFps)
{
    using orosmo::core::Profiler;
    PerformanceSample s;
    s.fps = averageFps;
    s.renderMs = Profiler::GetAverageTimeMs("Render");
    // Passes du graphe de l'image (REN-01), une mesure GPU par passe.
    s.renderSceneMs = Profiler::GetAverageTimeMs("RenderGraph/scene");
    s.upscaleMs = Profiler::GetAverageTimeMs("RenderGraph/upscale");
    s.uiUpscaleMs = Profiler::GetAverageTimeMs("RenderGraph/ui");
    s.swapBuffersMs = Profiler::GetAverageTimeMs("SwapBuffers");
    return s;
}

bool PerformanceStats::tick(float deltaSeconds, const std::function<PerformanceSample()> &sample)
{
    sinceRefresh += deltaSeconds;
    if (sinceRefresh < kRefreshSeconds)
    {
        return false;
    }
    sinceRefresh = 0.0f;
    shown = sample();
    return true;
}
