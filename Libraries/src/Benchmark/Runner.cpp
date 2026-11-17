#include "Benchmark/Runner.h"

#include <chrono>
#include <ctime>
#include <iostream>

namespace bench
{

namespace
{

using BenchClock  = std::chrono::steady_clock;
using BenchDurSec = std::chrono::duration<double>;
using BenchDurMs  = std::chrono::duration<double, std::milli>;

double BenchElapsedSeconds(const BenchClock::time_point &start)
{
    return std::chrono::duration_cast<BenchDurSec>(BenchClock::now() - start).count();
}

double BenchElapsedMs(const BenchClock::time_point &start, const BenchClock::time_point &end)
{
    return std::chrono::duration_cast<BenchDurMs>(end - start).count();
}

std::string BenchCurrentTimestampISO()
{
    const auto        now = std::chrono::system_clock::now();
    const std::time_t t   = std::chrono::system_clock::to_time_t(now);
    std::tm           utcTm{};
#if defined(_WIN32) || defined(_WIN64)
    gmtime_s(&utcTm, &t);
#else
    gmtime_r(&t, &utcTm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &utcTm);
    return buf;
}

std::string BenchCurrentPlatform()
{
#if defined(_WIN32) || defined(_WIN64)
    return "windows";
#elif defined(__APPLE__)
    return "darwin";
#else
    return "linux";
#endif
}

ScenarioResult BenchRunScenario(Scenario &scenario, const ScenarioConfig &cfg)
{
    std::vector<double> setupDurations;
    if (cfg.measureSetup)
    {
        scenario.Setup();
        scenario.Teardown();

        setupDurations.reserve(cfg.setupSamples);
        for (std::size_t i = 0; i < cfg.setupSamples; ++i)
        {
            const auto tStart = BenchClock::now();
            scenario.Setup();
            const auto tEnd = BenchClock::now();
            setupDurations.push_back(BenchElapsedMs(tStart, tEnd));
            scenario.Teardown();
        }
    }

    scenario.Setup();

    const auto warmupStart = BenchClock::now();
    while (BenchElapsedSeconds(warmupStart) < cfg.warmupSeconds)
    {
        scenario.RunIteration();
    }

    std::vector<double> samples;
    if (cfg.minSamples > 0)
    {
        samples.reserve(cfg.minSamples);
    }

    CounterSink sink;
    const auto  measureStart = BenchClock::now();

    while (true)
    {
        const std::size_t n       = samples.size();
        const double      elapsed = BenchElapsedSeconds(measureStart);

        if (cfg.maxSamples != 0 && n >= cfg.maxSamples)
        {
            break;
        }
        if (elapsed >= cfg.measureSeconds && n >= cfg.minSamples)
        {
            break;
        }

        const auto tStart = BenchClock::now();
        scenario.RunIteration();
        const auto tEnd = BenchClock::now();

        samples.push_back(BenchElapsedMs(tStart, tEnd));
        scenario.CollectCounters(sink);
    }

    scenario.Teardown();

    ScenarioResult result;
    result.name           = cfg.name;
    result.config         = cfg;
    result.iterationStats = ComputeStatistics(std::move(samples));
    result.counters       = sink.Averages(result.iterationStats.samples);

    if (cfg.measureSetup)
    {
        result.hasSetupStats = true;
        result.setupStats    = ComputeStatistics(std::move(setupDurations));
    }

    return result;
}

} // namespace

Runner::Runner(const BenchmarkConfig &config) : m_config(config) {}

BenchmarkReport Runner::Run(ScenarioRegistry &registry)
{
    BenchmarkReport report;
    report.timestamp = BenchCurrentTimestampISO();
    report.platform  = BenchCurrentPlatform();
    report.build     = m_config.build;

    for (const ScenarioConfig &cfg : m_config.scenarios)
    {
        std::unique_ptr<Scenario> scenario = registry.Create(cfg.name);
        if (!scenario)
        {
            std::cerr << "[bench] Scenario not found: " << cfg.name << "\n";
            continue;
        }

        std::cout << "[bench] Running: " << cfg.name << " ..." << std::flush;

        ScenarioResult result = BenchRunScenario(*scenario, cfg);

        std::cout << " done (" << result.iterationStats.samples << " samples"
                  << ", mean=" << result.iterationStats.meanMs << "ms)\n";

        report.results.push_back(std::move(result));
    }

    return report;
}

} // namespace bench
