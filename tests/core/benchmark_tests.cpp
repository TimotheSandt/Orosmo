#include <doctest/doctest.h>

#include <engine/core/benchmark.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace engine::core::bench;

namespace
{

struct CallCounters
{
    int setupCount = 0;
    int iterationCount = 0;
    int teardownCount = 0;
    int counterCalls = 0;
    bool activeDuringIterations = true;
};

class CountingScenario final : public Scenario
{
  public:
    explicit CountingScenario(CallCounters &counts) : mCounts(counts) {}

    std::string name() const override { return "counting"; }

    void setup() override
    {
        ++mCounts.setupCount;
        mActive = true;
    }

    void runIteration() override
    {
        if (!mActive)
        {
            mCounts.activeDuringIterations = false;
        }
        ++mCounts.iterationCount;
    }

    void teardown() override
    {
        mActive = false;
        ++mCounts.teardownCount;
    }

    void collectCounters(CounterSink &sink) const override
    {
        ++mCounts.counterCalls;
        sink.add("items", 4.0);
    }

  private:
    CallCounters &mCounts;
    bool mActive = false;
};

} // namespace

TEST_SUITE("core/benchmark")
{
    TEST_CASE("computeStatistics sur liste vide renvoie des zéros")
    {
        const Statistics s = computeStatistics({});
        CHECK(s.samples == 0);
        CHECK(s.totalMs == doctest::Approx(0.0));
        CHECK(s.meanMs == doctest::Approx(0.0));
        CHECK(s.stddevMs == doctest::Approx(0.0));
        CHECK(s.minMs == doctest::Approx(0.0));
        CHECK(s.maxMs == doctest::Approx(0.0));
        CHECK(s.p50Ms == doctest::Approx(0.0));
        CHECK(s.p95Ms == doctest::Approx(0.0));
        CHECK(s.p99Ms == doctest::Approx(0.0));
        CHECK(s.throughput == doctest::Approx(0.0));
    }

    TEST_CASE("computeStatistics sur un seul échantillon")
    {
        const Statistics s = computeStatistics({4.0});
        CHECK(s.samples == 1);
        CHECK(s.totalMs == doctest::Approx(4.0));
        CHECK(s.meanMs == doctest::Approx(4.0));
        CHECK(s.stddevMs == doctest::Approx(0.0));
        CHECK(s.minMs == doctest::Approx(4.0));
        CHECK(s.maxMs == doctest::Approx(4.0));
        CHECK(s.p50Ms == doctest::Approx(4.0));
        CHECK(s.p95Ms == doctest::Approx(4.0));
        CHECK(s.p99Ms == doctest::Approx(4.0));
        CHECK(s.throughput == doctest::Approx(250.0));
    }

    TEST_CASE("computeStatistics calcule moyenne, médiane, écart type, percentiles et débit sur des valeurs connues")
    {
        // Trié : 10, 20, 30, 40, 50 -> moyenne = 30, variance = 200, p50 = 30, p95 = 48, p99 = 49.6.
        const Statistics odd = computeStatistics({40.0, 10.0, 50.0, 20.0, 30.0});
        CHECK(odd.samples == 5);
        CHECK(odd.totalMs == doctest::Approx(150.0));
        CHECK(odd.meanMs == doctest::Approx(30.0));
        CHECK(odd.stddevMs == doctest::Approx(std::sqrt(200.0)));
        CHECK(odd.minMs == doctest::Approx(10.0));
        CHECK(odd.maxMs == doctest::Approx(50.0));
        CHECK(odd.p50Ms == doctest::Approx(30.0));
        CHECK(odd.p95Ms == doctest::Approx(48.0));
        CHECK(odd.p99Ms == doctest::Approx(49.6));
        CHECK(odd.throughput == doctest::Approx(5.0 / 0.15));

        // Nombre pair : 2, 4, 6, 8 -> médiane interpolée = 5, variance = 5.
        const Statistics even = computeStatistics({8.0, 2.0, 6.0, 4.0});
        CHECK(even.samples == 4);
        CHECK(even.meanMs == doctest::Approx(5.0));
        CHECK(even.stddevMs == doctest::Approx(std::sqrt(5.0)));
        CHECK(even.p50Ms == doctest::Approx(5.0));
    }

    TEST_CASE("CounterSink accumule et calcule les moyennes")
    {
        CounterSink sink;
        sink.add("ops", 10.0);
        sink.add("ops", 20.0);
        sink.add("bytes", 60.0);

        CHECK(sink.averages(0).empty());

        const auto avg = sink.averages(2);
        REQUIRE(avg.size() == 2);
        CHECK(avg.at("ops") == doctest::Approx(15.0));
        CHECK(avg.at("bytes") == doctest::Approx(30.0));
    }

    TEST_CASE("ScenarioRegistry enregistre, vérifie et instancie les scénarios")
    {
        CallCounters counts;
        ScenarioRegistry registry;
        CHECK_FALSE(registry.contains("counting"));
        CHECK(registry.create("counting") == nullptr);

        registry.registerScenario("counting", [&] { return std::make_unique<CountingScenario>(counts); });
        CHECK(registry.contains("counting"));

        auto scenario = registry.create("counting");
        REQUIRE(scenario != nullptr);
        CHECK(scenario->name() == "counting");
    }

    TEST_CASE("Runner exécute setup, runIteration et teardown le bon nombre de fois")
    {
        CallCounters counts;
        ScenarioRegistry registry;
        registry.registerScenario("counting", [&] { return std::make_unique<CountingScenario>(counts); });

        ScenarioConfig present;
        present.name = "counting";
        present.warmupSeconds = 0.0;
        present.measureSeconds = 0.0;
        present.minSamples = 7;
        present.maxSamples = 7;
        present.measureSetup = false;

        ScenarioConfig missing;
        missing.name = "absent";

        BenchmarkConfig cfg;
        cfg.build = "test";
        cfg.scenarios = {missing, present};

        Runner runner(cfg);
        const BenchmarkReport report = runner.run(registry);

        CHECK(report.build == "test");
        CHECK_FALSE(report.timestamp.empty());
        CHECK_FALSE(report.platform.empty());
        REQUIRE(report.results.size() == 1);

        const ScenarioResult &res = report.results[0];
        CHECK(res.name == "counting");
        CHECK(res.iterationStats.samples == 7);
        CHECK_FALSE(res.hasSetupStats);
        CHECK(counts.setupCount == 1);
        CHECK(counts.iterationCount == 7);
        CHECK(counts.teardownCount == 1);
        CHECK(counts.counterCalls == 7);
        CHECK(counts.activeDuringIterations);
        REQUIRE(res.counters.count("items") == 1);
        CHECK(res.counters.at("items") == doctest::Approx(4.0));
    }

    TEST_CASE("Runner avec measureSetup mesure le nombre demandé de cycles setup/teardown")
    {
        CallCounters counts;
        ScenarioRegistry registry;
        registry.registerScenario("counting", [&] { return std::make_unique<CountingScenario>(counts); });

        ScenarioConfig sc;
        sc.name = "counting";
        sc.warmupSeconds = 0.0;
        sc.measureSeconds = 0.0;
        sc.minSamples = 4;
        sc.maxSamples = 4;
        sc.measureSetup = true;
        sc.setupSamples = 3;

        BenchmarkConfig cfg;
        cfg.scenarios = {sc};

        Runner runner(cfg);
        const BenchmarkReport report = runner.run(registry);

        REQUIRE(report.results.size() == 1);
        const ScenarioResult &res = report.results[0];
        CHECK(res.hasSetupStats);
        CHECK(res.setupStats.samples == 3);
        CHECK(res.iterationStats.samples == 4);
        // 1 cycle d'échauffement + 3 cycles mesurés + 1 cycle pour les itérations.
        CHECK(counts.setupCount == 5);
        CHECK(counts.teardownCount == 5);
        CHECK(counts.iterationCount == 4);
        CHECK(counts.activeDuringIterations);
    }

    TEST_CASE("writeReport sérialise un JSON valide et lève une erreur sur chemin invalide")
    {
        BenchmarkReport report;
        report.timestamp = "2026-01-01T00:00:00Z";
        report.platform = "linux";
        report.build = "test";

        ScenarioResult res;
        res.name = "demo \"escaped\"";
        res.config.warmupSeconds = 0.0;
        res.config.measureSeconds = 0.0;
        res.config.measureSetup = true;
        res.iterationStats = computeStatistics({10.0, 20.0});
        res.hasSetupStats = true;
        res.setupStats = computeStatistics({5.0});
        res.counters["items"] = 4.0;
        report.results.push_back(res);

        const auto path = std::filesystem::temp_directory_path() / "engine_benchmark_test_report.json";
        writeReport(report, path.string());

        std::stringstream ss;
        {
            std::ifstream in(path);
            ss << in.rdbuf();
        }
        std::filesystem::remove(path);

        const auto j = nlohmann::json::parse(ss.str(), nullptr, false);
        REQUIRE_FALSE(j.is_discarded());
        CHECK(j["schema_version"] == 1);
        CHECK(j["environment"]["platform"] == "linux");
        CHECK(j["environment"]["build"] == "test");
        REQUIRE(j["results"].size() == 1);
        CHECK(j["results"][0]["name"] == "demo \"escaped\"");
        CHECK(j["results"][0]["iteration"]["samples"] == 2);
        CHECK(j["results"][0]["iteration"]["mean_ms"].get<double>() == doctest::Approx(15.0));
        CHECK(j["results"][0]["setup"]["samples"] == 1);
        CHECK(j["results"][0]["counters"]["items"].get<double>() == doctest::Approx(4.0));

        CHECK_THROWS_AS(writeReport(report, "/nonexistent-dir-xyz/benchmark_report.json"), std::runtime_error);
    }
}
