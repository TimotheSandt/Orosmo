#include "scene_benchmarks.hpp"

#include <orosmo/core/benchmark.hpp>

#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>

// ── Enregistrement des scénarios ─────────────────────────────────────────────
//
// Exemple :
//
//   #include "MyScenario.hpp"
//
//   static void registerScenarios(orosmo::core::bench::ScenarioRegistry &registry)
//   {
//       registry.registerScenario("my-scenario", [] { return std::make_unique<MyScenario>(); });
//   }
//
// Un scénario implémente orosmo::core::bench::Scenario :
//
//   class MyScenario final : public orosmo::core::bench::Scenario
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

static void registerScenarios(orosmo::core::bench::ScenarioRegistry &registry)
{
    // Ajouter les scénarios ici.
    registry.registerScenario("scene-view", [] { return std::make_unique<scene_bench::ViewScenario>(); });
    registry.registerScenario("scene-transforms", [] { return std::make_unique<scene_bench::TransformScenario>(); });
}

static orosmo::core::bench::BenchmarkConfig buildConfig()
{
    orosmo::core::bench::BenchmarkConfig config;
    config.outputPath = "benchmark_report.json";
    config.build = "release";

    // Ajouter une configuration par scénario enregistré :
    //
    //   orosmo::core::bench::ScenarioConfig cfg;
    //   cfg.name           = "my-scenario";
    //   cfg.warmupSeconds  = 0.5;
    //   cfg.measureSeconds = 5.0;
    //   cfg.minSamples     = 100;
    //   cfg.measureSetup   = false;
    //   config.scenarios.push_back(cfg);

    for (const char *name : {"scene-view", "scene-transforms"})
    {
        orosmo::core::bench::ScenarioConfig cfg;
        cfg.name = name;
        cfg.warmupSeconds = 0.5;
        cfg.measureSeconds = 3.0;
        cfg.minSamples = 50;
        config.scenarios.push_back(cfg);
    }

    return config;
}

int main()
{
    try
    {
        orosmo::core::bench::ScenarioRegistry registry;
        registerScenarios(registry);

        const orosmo::core::bench::BenchmarkConfig config = buildConfig();

        if (config.scenarios.empty())
        {
            std::cout << "[bench] No scenarios configured. Add them in benchmark_main.cpp.\n";
            return EXIT_SUCCESS;
        }

        orosmo::core::bench::Runner runner(config);
        const orosmo::core::bench::BenchmarkReport report = runner.run(registry);

        orosmo::core::bench::writeReport(report, config.outputPath);
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
