#include "UI/performance_stats.hpp"

#include <engine/core/profiler.hpp>

PerformanceSample samplePerformance(double averageFps)
{
    using engine::core::Profiler;
    PerformanceSample s;
    s.fps = averageFps;
    s.renderMs = Profiler::GetAverageTimeMs("Render");
    s.renderWorldMs = Profiler::GetAverageTimeMs("RenderWorld");
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
