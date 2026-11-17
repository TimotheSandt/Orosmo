#pragma once

#include "BenchmarkConfig.h"
#include "Statistics.h"

#include <map>
#include <string>
#include <vector>

namespace bench
{

struct ScenarioResult
{
    std::string                   name;
    ScenarioConfig                config;
    Statistics                    iterationStats;
    bool                          hasSetupStats = false;
    Statistics                    setupStats;
    std::map<std::string, double> counters;
};

struct BenchmarkReport
{
    std::string                 timestamp;
    std::string                 platform;
    std::string                 build;
    std::vector<ScenarioResult> results;
};

// Serialises the report to a JSON file at outputPath.
// Throws std::runtime_error if the file cannot be opened.
void WriteReport(const BenchmarkReport &report, const std::string &outputPath);

} // namespace bench
