#include "UI/performance_stats.hpp"

#include <orosmo/core/profiler.hpp>

PerformanceSample samplePerformance(double averageFps)
{
    using orosmo::core::Profiler;
    PerformanceSample s;
    s.fps = averageFps;
    s.renderMs = Profiler::GetAverageTimeMs("Render");
    s.renderSceneMs = Profiler::GetAverageTimeMs("RenderScene");
    s.upscaleMs = Profiler::GetAverageTimeMs("Upscale");
    s.uiUpscaleMs = Profiler::GetAverageTimeMs("UIUpscale");
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
