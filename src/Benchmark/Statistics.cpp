#include "Benchmark/Statistics.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace bench
{

namespace
{

double PercentileSorted(const std::vector<double> &sorted, double p)
{
    const std::size_t n = sorted.size();
    if (n == 0)
    {
        return 0.0;
    }
    if (n == 1)
    {
        return sorted[0];
    }
    const double      rank = (p / 100.0) * static_cast<double>(n - 1);
    const std::size_t lo   = static_cast<std::size_t>(rank);
    const std::size_t hi   = std::min(lo + 1, n - 1);
    const double      frac = rank - static_cast<double>(lo);
    return sorted[lo] + frac * (sorted[hi] - sorted[lo]);
}

} // namespace

Statistics ComputeStatistics(std::vector<double> samples)
{
    Statistics stats;
    stats.samples = samples.size();
    if (stats.samples == 0)
    {
        return stats;
    }

    std::sort(samples.begin(), samples.end());

    stats.totalMs = std::accumulate(samples.begin(), samples.end(), 0.0);
    stats.meanMs  = stats.totalMs / static_cast<double>(stats.samples);
    stats.minMs   = samples.front();
    stats.maxMs   = samples.back();

    double variance = 0.0;
    for (const double s : samples)
    {
        const double diff = s - stats.meanMs;
        variance += diff * diff;
    }
    stats.stddevMs = std::sqrt(variance / static_cast<double>(stats.samples));

    stats.p50Ms = PercentileSorted(samples, 50.0);
    stats.p95Ms = PercentileSorted(samples, 95.0);
    stats.p99Ms = PercentileSorted(samples, 99.0);

    if (stats.totalMs > 0.0)
    {
        stats.throughput = static_cast<double>(stats.samples) / (stats.totalMs / 1000.0);
    }

    return stats;
}

} // namespace bench
