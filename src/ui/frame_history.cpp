#include "orosmo/ui/frame_history.hpp"

#include <algorithm>

namespace orosmo::ui
{

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

void FrameHistory::push(float cpuMs, float gpuMs, float intervalMs, float waitMs) noexcept
{
    cpu_[next_] = cpuMs;
    wait_[next_] = waitMs;
    gpu_[next_] = gpuMs;
    interval_[next_] = intervalMs;
    next_ = (next_ + 1) % kSize;
    count_ = std::min(count_ + 1, kSize);
}

std::vector<float> FrameHistory::ordered(const std::array<float, kSize> &ring) const
{
    std::vector<float> out;
    out.reserve(count_);
    const std::size_t first = (next_ + kSize - count_) % kSize;
    for (std::size_t i = 0; i < count_; ++i)
    {
        out.push_back(ring[(first + i) % kSize]);
    }
    return out;
}

FpsStats FrameHistory::fps() const
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
    FpsStats out;
    if (intervals.empty())
    {
        return out;
    }
    double sum = 0.0;
    for (const float v : intervals)
    {
        sum += static_cast<double>(v);
    }
    std::sort(intervals.begin(), intervals.end());
    const std::size_t p99 = std::min(intervals.size() - 1, (intervals.size() * 99) / 100);
    out.average = 1000.0 * static_cast<double>(intervals.size()) / sum;
    out.low1 = 1000.0 / static_cast<double>(intervals[p99]);
    out.min = 1000.0 / static_cast<double>(intervals.back());
    out.max = 1000.0 / static_cast<double>(intervals.front());
    return out;
}

double FrameHistory::averageCpuMs() const { return average(ordered(cpu_)); }

double FrameHistory::averageWaitMs() const { return average(ordered(wait_)); }

} // namespace orosmo::ui
