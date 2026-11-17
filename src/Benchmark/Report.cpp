#include "Benchmark/Report.h"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace bench
{

namespace
{

std::string BenchJsonStr(const std::string &s)
{
    std::ostringstream oss;
    oss << '"';
    for (const char c : s)
    {
        switch (c)
        {
            case '"':  oss << "\\\""; break;
            case '\\': oss << "\\\\"; break;
            case '\n': oss << "\\n";  break;
            case '\r': oss << "\\r";  break;
            case '\t': oss << "\\t";  break;
            default:   oss << c;      break;
        }
    }
    oss << '"';
    return oss.str();
}

std::string BenchJsonDouble(double v)
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3) << v;
    return oss.str();
}

void BenchWriteStatistics(std::ostream &out, const Statistics &s, const std::string &indent)
{
    out << "{\n";
    out << indent << "  \"samples\": "    << s.samples                    << ",\n";
    out << indent << "  \"total_ms\": "   << BenchJsonDouble(s.totalMs)   << ",\n";
    out << indent << "  \"mean_ms\": "    << BenchJsonDouble(s.meanMs)    << ",\n";
    out << indent << "  \"stddev_ms\": "  << BenchJsonDouble(s.stddevMs)  << ",\n";
    out << indent << "  \"min_ms\": "     << BenchJsonDouble(s.minMs)     << ",\n";
    out << indent << "  \"max_ms\": "     << BenchJsonDouble(s.maxMs)     << ",\n";
    out << indent << "  \"p50_ms\": "     << BenchJsonDouble(s.p50Ms)     << ",\n";
    out << indent << "  \"p95_ms\": "     << BenchJsonDouble(s.p95Ms)     << ",\n";
    out << indent << "  \"p99_ms\": "     << BenchJsonDouble(s.p99Ms)     << ",\n";
    out << indent << "  \"throughput\": " << BenchJsonDouble(s.throughput) << "\n";
    out << indent << "}";
}

void BenchWriteResult(std::ostream &out, const ScenarioResult &r)
{
    const std::string ind = "      ";
    out << "    {\n";
    out << "      \"name\": "            << BenchJsonStr(r.name)                            << ",\n";
    out << "      \"warmup_seconds\": "  << BenchJsonDouble(r.config.warmupSeconds)         << ",\n";
    out << "      \"measure_seconds\": " << BenchJsonDouble(r.config.measureSeconds)        << ",\n";
    out << "      \"measure_setup\": "   << (r.config.measureSetup ? "true" : "false")      << ",\n";
    out << "      \"iteration\": ";
    BenchWriteStatistics(out, r.iterationStats, ind);
    out << ",\n";

    if (r.hasSetupStats)
    {
        out << "      \"setup\": ";
        BenchWriteStatistics(out, r.setupStats, ind);
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
        out << "\n        " << BenchJsonStr(it->first) << ": " << BenchJsonDouble(it->second);
        first = false;
    }
    if (!r.counters.empty())
    {
        out << "\n      ";
    }
    out << "}\n";
    out << "    }";
}

} // namespace

void WriteReport(const BenchmarkReport &report, const std::string &outputPath)
{
    std::ofstream file(outputPath);
    if (!file.is_open())
    {
        throw std::runtime_error("Cannot open report file: " + outputPath);
    }

    file << "{\n";
    file << "  \"schema_version\": 1,\n";
    file << "  \"timestamp\": "  << BenchJsonStr(report.timestamp) << ",\n";
    file << "  \"environment\": {\n";
    file << "    \"platform\": " << BenchJsonStr(report.platform)  << ",\n";
    file << "    \"build\": "    << BenchJsonStr(report.build)     << "\n";
    file << "  },\n";
    file << "  \"results\": [\n";

    for (std::size_t i = 0; i < report.results.size(); ++i)
    {
        BenchWriteResult(file, report.results[i]);
        if (i + 1 < report.results.size())
        {
            file << ",";
        }
        file << "\n";
    }

    file << "  ]\n";
    file << "}\n";
}

} // namespace bench
