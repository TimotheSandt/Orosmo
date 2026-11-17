#pragma once

#include <cstddef>
#include <vector>

namespace bench
{

struct Statistics
{
    std::size_t samples    = 0;
    double      totalMs    = 0.0;
    double      meanMs     = 0.0;
    double      stddevMs   = 0.0;
    double      minMs      = 0.0;
    double      maxMs      = 0.0;
    double      p50Ms      = 0.0;
    double      p95Ms      = 0.0;
    double      p99Ms      = 0.0;
    double      throughput = 0.0; // iterations per second
};

// Copies samples, sorts the copy, then computes all aggregates.
// Returns a zero-filled Statistics if samples is empty.
Statistics ComputeStatistics(std::vector<double> samples);

} // namespace bench
