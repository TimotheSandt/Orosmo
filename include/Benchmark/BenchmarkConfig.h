#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace bench
{

struct ScenarioConfig
{
    std::string name;
    double      warmupSeconds  = 0.5;
    double      measureSeconds = 5.0;
    std::size_t minSamples     = 100;
    std::size_t maxSamples     = 0;
    bool        measureSetup   = false;
    std::size_t setupSamples   = 30;
};

struct BenchmarkConfig
{
    std::vector<ScenarioConfig> scenarios;
    std::string                 outputPath = "benchmark_report.json";
    std::string                 build      = "release";
};

} // namespace bench
