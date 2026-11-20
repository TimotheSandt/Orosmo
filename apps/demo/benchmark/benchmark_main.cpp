#include <engine/core/benchmark.hpp>

#include <cstdlib>
#include <exception>
#include <iostream>

// ── Enregistrement des scénarios ─────────────────────────────────────────────
//
// Exemple :
//
//   #include "MyScenario.hpp"
//
//   static void registerScenarios(engine::core::bench::ScenarioRegistry &registry)
//   {
//       registry.registerScenario("my-scenario", [] { return std::make_unique<MyScenario>(); });
//   }
//
// Un scénario implémente engine::core::bench::Scenario :
//
//   class MyScenario final : public engine::core::bench::Scenario
//   {
//     public:
//       std::string name() const override { return "my-scenario"; }
//       void        setup()        override { /* allocations */ }
//       void        runIteration() override { /* travail mesuré */ }
//       void        teardown()     override { /* libération */ }
//   };
//
// Ajouter ensuite sa configuration ci-dessous et lancer : make benchmark
//
// ─────────────────────────────────────────────────────────────────────────────

static void registerScenarios(engine::core::bench::ScenarioRegistry & /*registry*/)
{
    // Ajouter les scénarios ici.
}

static engine::core::bench::BenchmarkConfig buildConfig()
{
    engine::core::bench::BenchmarkConfig config;
    config.outputPath = "benchmark_report.json";
    config.build = "release";

    // Ajouter une configuration par scénario enregistré :
    //
    //   engine::core::bench::ScenarioConfig cfg;
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
        engine::core::bench::ScenarioRegistry registry;
        registerScenarios(registry);

        const engine::core::bench::BenchmarkConfig config = buildConfig();

        if (config.scenarios.empty())
        {
            std::cout << "[bench] No scenarios configured. Add them in benchmark_main.cpp.\n";
            return EXIT_SUCCESS;
        }

        engine::core::bench::Runner runner(config);
        const engine::core::bench::BenchmarkReport report = runner.run(registry);

        engine::core::bench::writeReport(report, config.outputPath);
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
