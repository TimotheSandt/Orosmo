#include "Benchmark/Runner.h"
#include "Benchmark/Report.h"
#include "Benchmark/Scenario.h"

#include <cstdlib>
#include <exception>
#include <iostream>

// ── Register your scenarios here ─────────────────────────────────────────────
//
// Example:
//
//   #include "MyScenario.h"
//
//   static void RegisterScenarios(bench::ScenarioRegistry &registry)
//   {
//       registry.Register("my-scenario", [] { return std::make_unique<MyScenario>(); });
//   }
//
// A scenario only needs to implement bench::Scenario:
//
//   class MyScenario final : public bench::Scenario
//   {
//     public:
//       std::string Name() const override { return "my-scenario"; }
//       void        Setup()        override { /* allocate */ }
//       void        RunIteration() override { /* work to measure */ }
//       void        Teardown()     override { /* release */ }
//   };
//
// Then add it to the config below and run: make benchmark
//
// ─────────────────────────────────────────────────────────────────────────────

static void RegisterScenarios(bench::ScenarioRegistry & /*registry*/)
{
    // Add your scenarios here.
}

static bench::BenchmarkConfig BuildConfig()
{
    bench::BenchmarkConfig config;
    config.outputPath = "benchmark_report.json";
    config.build      = "release";

    // Add scenario configs here, one per registered scenario:
    //
    //   bench::ScenarioConfig cfg;
    //   cfg.name           = "my-scenario";
    //   cfg.warmupSeconds  = 0.5;
    //   cfg.measureSeconds = 5.0;
    //   cfg.minSamples     = 100;
    //   cfg.measureSetup   = false;
    //   config.scenarios.push_back(cfg);

    return config;
}

int main()
{
    try
    {
        bench::ScenarioRegistry registry;
        RegisterScenarios(registry);

        const bench::BenchmarkConfig config = BuildConfig();

        if (config.scenarios.empty())
        {
            std::cout << "[bench] No scenarios configured. Add them in benchmark_main.cpp.\n";
            return EXIT_SUCCESS;
        }

        bench::Runner runner(config);
        const bench::BenchmarkReport report = runner.Run(registry);

        WriteReport(report, config.outputPath);
        std::cout << "[bench] Report written: " << config.outputPath << "\n";

        return EXIT_SUCCESS;
    }
    catch (const std::exception &e)
    {
        std::cerr << "[bench] Fatal error: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
    catch (...)
    {
        std::cerr << "[bench] Fatal error: unknown exception\n";
        return EXIT_FAILURE;
    }
}
