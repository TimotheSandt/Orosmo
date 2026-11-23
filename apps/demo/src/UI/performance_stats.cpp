#include "UI/performance_stats.hpp"

#include <orosmo/ui/widget.hpp>

#include <algorithm>
#include <cstdio>
#include <fstream>

#if defined(__linux__)
#include <unistd.h>
#endif

namespace
{
constexpr const char *kMissing = "n/d";

template <typename... Args> std::string format(const char *pattern, Args... args)
{
    char buffer[256];
    const int n = std::snprintf(buffer, sizeof buffer, pattern, args...);
    return n <= 0 ? std::string() : std::string(buffer, static_cast<std::size_t>(std::min(n, static_cast<int>(sizeof buffer) - 1)));
}

std::string count(const std::optional<std::uint32_t> &value) { return value ? std::to_string(*value) : std::string(kMissing); }

std::string milliseconds(const std::optional<double> &value) { return value ? format("%.2f ms", *value) : std::string(kMissing); }
} // namespace

PerformanceText formatPerformance(const PerformanceSample &s)
{
    PerformanceText t;
    t.fps = format("FPS: %.0f", s.fpsAverage);
    t.fpsDetail = format("1%% low %.0f   min %.0f   max %.0f", s.fpsLow1, s.fpsMin, s.fpsMax);
    t.frame = format("Frame CPU: %.2f ms   waiting %.2f ms (vsync, GPU)", s.frameCpuMs, s.frameWaitMs);
    t.gpu = "GPU (Render): " + milliseconds(s.gpuMs);
    // Passes du graphe : trois par ligne au plus, trois lignes (les suivantes sont résumées par « ... »).
    constexpr std::size_t kPerLine = 3;
    for (std::size_t i = 0; i < s.gpuPasses.size() && i / kPerLine < t.passes.size(); ++i)
    {
        std::string &line = t.passes[i / kPerLine];
        line += (i % kPerLine == 0 ? "  " : "   ") + format("%s %.2f", s.gpuPasses[i].first.c_str(), s.gpuPasses[i].second);
        if (i + 1 == t.passes.size() * kPerLine && s.gpuPasses.size() > i + 1)
        {
            line += " ...";
        }
    }
    t.cpu = format("CPU: update %.2f  scene %.2f  graph %.2f  submit %.2f  present %.2f ms", s.updateMs, s.recordMs, s.graphMs, s.submitMs,
                   s.presentMs);
    t.draws = "Draws: scene " + count(s.sceneMeshes) + "  instanced " + count(s.instancedDraws) + " (" + count(s.instances) + " inst.)  shadows " +
              count(s.shadowDraws) + "  UI " + count(s.uiDraws);
    t.objects = "Objects visible: " + count(s.visibleObjects) + " / " + count(s.objects) + "   lights " + std::to_string(s.lights);
    t.device = s.backend + ": " + s.device;
    t.resolution = format("Output %ux%u   render %ux%u (x%.2f)   vsync %s", s.outputWidth, s.outputHeight, s.renderWidth, s.renderHeight,
                          static_cast<double>(s.renderScale), s.pacing.c_str());
    t.modes = s.modes;
    double scenePass = 0.0;
    double uiPass = 0.0;
    for (const auto &[name, ms] : s.gpuPasses)
    {
        scenePass = name == "scene" ? ms : scenePass;
        uiPass = name == "ui" ? ms : uiPass;
    }
    t.summary = orosmo::ui::text("FPS: ", s.windowFps, " | Render: ", s.gpuMs.value_or(0.0), " ms | Scene: ", scenePass, " ms | UI: ", uiPass, " ms");
    t.memory = "Memory: RSS " + (s.residentMiB ? format("%.0f MiB", *s.residentMiB) : std::string(kMissing)) + "   GPU " +
               (s.gpuMemoryMiB ? format("%.0f MiB", *s.gpuMemoryMiB) : std::string(kMissing));
    return t;
}

std::optional<double> processResidentMiB()
{
#if defined(__linux__)
    std::ifstream statm("/proc/self/statm");
    unsigned long long size = 0;
    unsigned long long resident = 0;
    if (!(statm >> size >> resident))
    {
        return std::nullopt;
    }
    const long page = sysconf(_SC_PAGESIZE);
    if (page <= 0)
    {
        return std::nullopt;
    }
    return static_cast<double>(resident) * static_cast<double>(page) / (1024.0 * 1024.0);
#else
    return std::nullopt;
#endif
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
    text = formatPerformance(shown);
    if (count_ > 0)
    {
        const std::vector<float> cpu = cpuHistory();
        const std::vector<float> gpu = gpuHistory();
        text.cpuGraph = format("CPU work (ms): last %.2f  max %.2f", static_cast<double>(cpu.back()),
                               static_cast<double>(*std::max_element(cpu.begin(), cpu.end())));
        const float gpuMax = *std::max_element(gpu.begin(), gpu.end());
        text.gpuGraph = gpuMax > 0.0f ? format("GPU frame (ms): last %.2f  max %.2f", static_cast<double>(gpu.back()), static_cast<double>(gpuMax))
                                      : std::string("GPU frame (ms): n/d");
    }
    return true;
}

void PerformanceStats::pushFrame(float cpuMs, float gpuMs, float intervalMs, float waitMs)
{
    cpu_[next_] = cpuMs;
    wait_[next_] = waitMs;
    gpu_[next_] = gpuMs;
    interval_[next_] = intervalMs;
    next_ = (next_ + 1) % kHistorySize;
    count_ = std::min(count_ + 1, kHistorySize);
}

std::vector<float> PerformanceStats::ordered(const std::array<float, kHistorySize> &ring) const
{
    std::vector<float> out;
    out.reserve(count_);
    const std::size_t first = (next_ + kHistorySize - count_) % kHistorySize;
    for (std::size_t i = 0; i < count_; ++i)
    {
        out.push_back(ring[(first + i) % kHistorySize]);
    }
    return out;
}

void PerformanceStats::fpsFromHistory(PerformanceSample &sample) const
{
    std::vector<float> intervals;
    intervals.reserve(count_);
    for (const float v : ordered(interval_))
    {
        if (v > 0.0f)
        {
            intervals.push_back(v);
        }
    }
    if (intervals.empty())
    {
        sample.fpsAverage = sample.fpsLow1 = sample.fpsMin = sample.fpsMax = 0.0;
        return;
    }
    double sum = 0.0;
    for (const float v : intervals)
    {
        sum += static_cast<double>(v);
    }
    std::sort(intervals.begin(), intervals.end());
    const std::size_t p99 = std::min(intervals.size() - 1, (intervals.size() * 99) / 100);
    sample.fpsAverage = 1000.0 * static_cast<double>(intervals.size()) / sum;
    sample.fpsLow1 = 1000.0 / static_cast<double>(intervals[p99]);
    sample.fpsMin = 1000.0 / static_cast<double>(intervals.back());
    sample.fpsMax = 1000.0 / static_cast<double>(intervals.front());
}

namespace
{
double average(const std::vector<float> &values)
{
    if (values.empty())
    {
        return 0.0;
    }
    double sum = 0.0;
    for (const float v : values)
    {
        sum += static_cast<double>(v);
    }
    return sum / static_cast<double>(values.size());
}
} // namespace

double PerformanceStats::averageCpuMs() const { return average(ordered(cpu_)); }

double PerformanceStats::averageWaitMs() const { return average(ordered(wait_)); }
