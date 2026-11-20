#include <engine/core/benchmark.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <stdexcept>

namespace engine::core::bench
{

namespace
{

using BenchClock = std::chrono::steady_clock;
using BenchDurSec = std::chrono::duration<double>;
using BenchDurMs = std::chrono::duration<double, std::milli>;

double percentileSorted(const std::vector<double> &sorted, double p)
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
    const double rank = (p / 100.0) * static_cast<double>(n - 1);
    const std::size_t lo = static_cast<std::size_t>(rank);
    const std::size_t hi = std::min(lo + 1, n - 1);
    const double frac = rank - static_cast<double>(lo);
    return sorted[lo] + frac * (sorted[hi] - sorted[lo]);
}

std::string jsonString(const std::string &s)
{
    std::ostringstream oss;
    oss << '"';
    for (const char c : s)
    {
        switch (c)
        {
            case '"':
                oss << "\\\"";
                break;
            case '\\':
                oss << "\\\\";
                break;
            case '\n':
                oss << "\\n";
                break;
            case '\r':
                oss << "\\r";
                break;
            case '\t':
                oss << "\\t";
                break;
            default:
                oss << c;
                break;
        }
    }
    oss << '"';
    return oss.str();
}

std::string jsonDouble(double v)
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3) << v;
    return oss.str();
}

void writeStatistics(std::ostream &out, const Statistics &s, const std::string &indent)
{
    out << "{\n";
    out << indent << "  \"samples\": " << s.samples << ",\n";
    out << indent << "  \"total_ms\": " << jsonDouble(s.totalMs) << ",\n";
    out << indent << "  \"mean_ms\": " << jsonDouble(s.meanMs) << ",\n";
    out << indent << "  \"stddev_ms\": " << jsonDouble(s.stddevMs) << ",\n";
    out << indent << "  \"min_ms\": " << jsonDouble(s.minMs) << ",\n";
    out << indent << "  \"max_ms\": " << jsonDouble(s.maxMs) << ",\n";
    out << indent << "  \"p50_ms\": " << jsonDouble(s.p50Ms) << ",\n";
    out << indent << "  \"p95_ms\": " << jsonDouble(s.p95Ms) << ",\n";
    out << indent << "  \"p99_ms\": " << jsonDouble(s.p99Ms) << ",\n";
    out << indent << "  \"throughput\": " << jsonDouble(s.throughput) << "\n";
    out << indent << "}";
}

void writeResult(std::ostream &out, const ScenarioResult &r)
{
    const std::string ind = "      ";
    out << "    {\n";
    out << "      \"name\": " << jsonString(r.name) << ",\n";
    out << "      \"warmup_seconds\": " << jsonDouble(r.config.warmupSeconds) << ",\n";
    out << "      \"measure_seconds\": " << jsonDouble(r.config.measureSeconds) << ",\n";
    out << "      \"measure_setup\": " << (r.config.measureSetup ? "true" : "false") << ",\n";
    out << "      \"iteration\": ";
    writeStatistics(out, r.iterationStats, ind);
    out << ",\n";

    if (r.hasSetupStats)
    {
        out << "      \"setup\": ";
        writeStatistics(out, r.setupStats, ind);
        out << ",\n";
    }

    out << "      \"counters\": {";
    bool first = true;
    for (auto it = r.counters.begin(); it != r.counters.end(); ++it)
    {
        if (!first)
        {
            out << ",";
        }
        out << "\n        " << jsonString(it->first) << ": " << jsonDouble(it->second);
        first = false;
    }
    if (!r.counters.empty())
    {
        out << "\n      ";
    }
    out << "}\n";
    out << "    }";
}

double elapsedSeconds(const BenchClock::time_point &start)
{
    return std::chrono::duration_cast<BenchDurSec>(BenchClock::now() - start).count();
}

double elapsedMs(const BenchClock::time_point &start, const BenchClock::time_point &end)
{
    return std::chrono::duration_cast<BenchDurMs>(end - start).count();
}

std::string currentTimestampIso()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm utcTm{};
#if defined(_WIN32) || defined(_WIN64)
    gmtime_s(&utcTm, &t);
#else
    gmtime_r(&t, &utcTm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &utcTm);
    return buf;
}

std::string currentPlatform()
{
#if defined(_WIN32) || defined(_WIN64)
    return "windows";
#elif defined(__APPLE__)
    return "darwin";
#else
    return "linux";
#endif
}

ScenarioResult runScenario(Scenario &scenario, const ScenarioConfig &cfg)
{
    std::vector<double> setupDurations;
    if (cfg.measureSetup)
    {
        scenario.setup();
        scenario.teardown();

        setupDurations.reserve(cfg.setupSamples);
        for (std::size_t i = 0; i < cfg.setupSamples; ++i)
        {
            const auto tStart = BenchClock::now();
            scenario.setup();
            const auto tEnd = BenchClock::now();
            setupDurations.push_back(elapsedMs(tStart, tEnd));
            scenario.teardown();
        }
    }

    scenario.setup();

    const auto warmupStart = BenchClock::now();
    while (elapsedSeconds(warmupStart) < cfg.warmupSeconds)
    {
        scenario.runIteration();
    }

    std::vector<double> samples;
    if (cfg.minSamples > 0)
    {
        samples.reserve(cfg.minSamples);
    }

    CounterSink sink;
    const auto measureStart = BenchClock::now();

    while (true)
    {
        const std::size_t n = samples.size();
        const double elapsed = elapsedSeconds(measureStart);

        if (cfg.maxSamples != 0 && n >= cfg.maxSamples)
        {
            break;
        }
        if (elapsed >= cfg.measureSeconds && n >= cfg.minSamples)
        {
            break;
        }

        const auto tStart = BenchClock::now();
        scenario.runIteration();
        const auto tEnd = BenchClock::now();

        samples.push_back(elapsedMs(tStart, tEnd));
        scenario.collectCounters(sink);
    }

    scenario.teardown();

    ScenarioResult result;
    result.name = cfg.name;
    result.config = cfg;
    result.iterationStats = computeStatistics(std::move(samples));
    result.counters = sink.averages(result.iterationStats.samples);

    if (cfg.measureSetup)
    {
        result.hasSetupStats = true;
        result.setupStats = computeStatistics(std::move(setupDurations));
    }

    return result;
}

} // namespace

Statistics computeStatistics(std::vector<double> samples)
{
    Statistics stats;
    stats.samples = samples.size();
    if (stats.samples == 0)
    {
        return stats;
    }

    std::sort(samples.begin(), samples.end());

    stats.totalMs = std::accumulate(samples.begin(), samples.end(), 0.0);
    stats.meanMs = stats.totalMs / static_cast<double>(stats.samples);
    stats.minMs = samples.front();
    stats.maxMs = samples.back();

    double variance = 0.0;
    for (const double s : samples)
    {
        const double diff = s - stats.meanMs;
        variance += diff * diff;
    }
    stats.stddevMs = std::sqrt(variance / static_cast<double>(stats.samples));

    stats.p50Ms = percentileSorted(samples, 50.0);
    stats.p95Ms = percentileSorted(samples, 95.0);
    stats.p99Ms = percentileSorted(samples, 99.0);

    if (stats.totalMs > 0.0)
    {
        stats.throughput = static_cast<double>(stats.samples) / (stats.totalMs / 1000.0);
    }

    return stats;
}

void CounterSink::add(const std::string &name, double value)
{
    mSums[name] += value;
}

std::map<std::string, double> CounterSink::averages(std::size_t sampleCount) const
{
    std::map<std::string, double> result;
    if (sampleCount == 0)
    {
        return result;
    }
    const double n = static_cast<double>(sampleCount);
    for (const auto &kv : mSums)
    {
        result[kv.first] = kv.second / n;
    }
    return result;
}

void ScenarioRegistry::registerScenario(const std::string &name, ScenarioFactory factory)
{
    mFactories[name] = std::move(factory);
}

bool ScenarioRegistry::contains(const std::string &name) const
{
    return mFactories.count(name) != 0;
}

std::unique_ptr<Scenario> ScenarioRegistry::create(const std::string &name) const
{
    const auto it = mFactories.find(name);
    if (it == mFactories.end())
    {
        return nullptr;
    }
    return it->second();
}

void writeReport(const BenchmarkReport &report, const std::string &outputPath)
{
    std::ofstream file(outputPath);
    if (!file.is_open())
    {
        throw std::runtime_error("Cannot open report file: " + outputPath);
    }

    file << "{\n";
    file << "  \"schema_version\": 1,\n";
    file << "  \"timestamp\": " << jsonString(report.timestamp) << ",\n";
    file << "  \"environment\": {\n";
    file << "    \"platform\": " << jsonString(report.platform) << ",\n";
    file << "    \"build\": " << jsonString(report.build) << "\n";
    file << "  },\n";
    file << "  \"results\": [\n";

    for (std::size_t i = 0; i < report.results.size(); ++i)
    {
        writeResult(file, report.results[i]);
        if (i + 1 < report.results.size())
        {
            file << ",";
        }
        file << "\n";
    }

    file << "  ]\n";
    file << "}\n";
}

Runner::Runner(const BenchmarkConfig &config) : mConfig(config) {}

BenchmarkReport Runner::run(ScenarioRegistry &registry)
{
    BenchmarkReport report;
    report.timestamp = currentTimestampIso();
    report.platform = currentPlatform();
    report.build = mConfig.build;

    for (const ScenarioConfig &cfg : mConfig.scenarios)
    {
        std::unique_ptr<Scenario> scenario = registry.create(cfg.name);
        if (!scenario)
        {
            std::cerr << "[bench] Scenario not found: " << cfg.name << "\n";
            continue;
        }

        std::cout << "[bench] Running: " << cfg.name << " ..." << std::flush;

        ScenarioResult result = runScenario(*scenario, cfg);

        std::cout << " done (" << result.iterationStats.samples << " samples"
                  << ", mean=" << result.iterationStats.meanMs << "ms)\n";

        report.results.push_back(std::move(result));
    }

    return report;
}

} // namespace engine::core::bench
