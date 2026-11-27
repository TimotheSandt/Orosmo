// SIM-01 (A79 lot L3, choix 8) : mode simulation de la démo, sans fenêtre ni GPU. L'empreinte de 500 agents, graine 42, 1 000
// pas est FIGÉE ici : ce test tourne dans chaque job de CI (`make demo-test-unit` : Linux x64 et arm64, Windows, macOS), donc
// la même valeur partout prouve le déterminisme entre plateformes (règle 9).
#include <doctest/doctest.h>

#include "Sim/agent_simulation.hpp"
#include "launch_options.hpp"

#include <orosmo/scene/fingerprint.hpp>

#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace
{
// `apps/demo/bin/debug/main --sim-agents 500 --seed 42 --sim-steps 1000` affiche la même valeur.
constexpr std::uint64_t kFingerprint500Agents42Seed1000Steps = 0xa3f0f4166622ae05u;

orosmo::core::Result<demo::LaunchOptions> parse(std::vector<const char *> args)
{
    args.insert(args.begin(), "main");
    return demo::parseLaunchOptions(static_cast<int>(args.size()), args.data());
}
} // namespace

TEST_SUITE("demo/simulation")
{
    TEST_CASE("Agent simulation: 500 agents, seed 42, 1000 steps give the frozen fingerprint, twice")
    {
        const std::uint64_t first = demo::runAgentSimulation(500, 42, 1000);
        CHECK(first == kFingerprint500Agents42Seed1000Steps);
        CHECK(demo::runAgentSimulation(500, 42, 1000) == first);
        CHECK(demo::runAgentSimulation(500, 43, 1000) != first);
    }

    TEST_CASE("Agent simulation: capture at step 500 then restore, and save/load at step 500, give the same fingerprint")
    {
        orosmo::sim::Simulation simulation({.seed = 42});
        REQUIRE(demo::setupAgentSimulation(simulation, 500));
        simulation.run(500);
        const orosmo::sim::Snapshot snapshot = simulation.capture();
        const orosmo::core::Json state = simulation.saveState();
        simulation.run(500);
        const std::uint64_t reference = simulation.fingerprint();
        CHECK(reference == demo::runAgentSimulation(500, 42, 1000));

        // Une simulation jouée ailleurs depuis le pas 500, puis restaurée : mêmes 500 pas suivants.
        simulation.restore(snapshot);
        simulation.run(500);
        CHECK(simulation.fingerprint() == reference);

        orosmo::sim::Simulation reloaded({.seed = 42});
        REQUIRE(demo::setupAgentSimulation(reloaded, 1));
        const std::vector<std::uint8_t> binary = orosmo::core::toBinary(state);
        auto parsed = orosmo::core::fromBinary(binary);
        REQUIRE(parsed);
        REQUIRE(reloaded.loadState(*parsed));
        reloaded.run(500);
        CHECK(reloaded.fingerprint() == reference);
    }

    TEST_CASE("Agent simulation: agents are replaced when exhausted (free indices reused), timed inputs change the state")
    {
        orosmo::sim::Simulation simulation({.seed = 42});
        REQUIRE(demo::setupAgentSimulation(simulation, 100));
        simulation.run(1000);
        CHECK(simulation.world().entityCount() == 100);
        bool reused = false;
        for (const std::uint32_t generation : simulation.world().entityTable().generations)
            reused = reused || generation > 0;
        CHECK(reused);

        orosmo::sim::Simulation pushed({.seed = 42});
        REQUIRE(demo::setupAgentSimulation(pushed, 100));
        REQUIRE(pushed.queueInput({10, "agents.push", orosmo::core::Json::array({1.0, 0.0, -0.5})}));
        pushed.run(1000);
        CHECK(pushed.fingerprint() != simulation.fingerprint());
        // Rejoué depuis l'enregistrement : même état.
        orosmo::sim::Simulation replayed({.seed = 42});
        REQUIRE(demo::setupAgentSimulation(replayed, 100));
        auto recorded = pushed.recording();
        REQUIRE(recorded);
        REQUIRE(replayed.play(*recorded));
        CHECK(replayed.fingerprint() == pushed.fingerprint());

        // Impulsion mal formée (élément non numérique, mauvaise taille) : ignorée sans exception, le pas continue.
        orosmo::sim::Simulation malformed({.seed = 42});
        REQUIRE(demo::setupAgentSimulation(malformed, 100));
        REQUIRE(malformed.queueInput({10, "agents.push", orosmo::core::Json::array({"x", 0.0, 0.0})}));
        REQUIRE(malformed.queueInput({11, "agents.push", orosmo::core::Json::array({1.0, 0.0})}));
        CHECK_NOTHROW(malformed.run(1000));
        CHECK(malformed.stepIndex() == 1000);
        CHECK(malformed.fingerprint() == simulation.fingerprint());
    }

    TEST_CASE("Simulation mode options: --sim-agents, --seed, --sim-steps, refused combinations, output")
    {
        auto options = parse({"--sim-agents", "500", "--seed", "18446744073709551615", "--sim-steps=20"});
        REQUIRE(options);
        CHECK(options->simAgents == 500);
        CHECK(options->simSeed == 18446744073709551615ull);
        CHECK(options->simSteps == 20);
        auto defaults = parse({"--sim-agents", "3"});
        REQUIRE(defaults);
        CHECK(defaults->simSeed == 42);
        CHECK(defaults->simSteps == 1000);
        CHECK(parse({}).value().simAgents == 0);
        CHECK(!parse({"--sim-agents", "0"}));
        CHECK(!parse({"--sim-agents", "1000001"}));
        CHECK(!parse({"--seed", "1"}));
        CHECK(!parse({"--sim-steps", "1"}));
        CHECK(!parse({"--sim-agents", "5", "--seed", "-1"}));
        CHECK(!parse({"--sim-agents", "5", "--sim-steps", "10000001"}));
        CHECK(!parse({"--sim-agents", "5", "--headless"}));
        CHECK(!parse({"--sim-agents", "5", "--capture", "out"}));

        std::ostringstream out;
        auto small = parse({"--sim-agents", "500", "--seed", "42", "--sim-steps", "1000"});
        REQUIRE(small);
        CHECK(demo::runSimulationMode(*small, out) == 0);
        CHECK(out.str().find("steps=1000 alive=500 fingerprint=0x") != std::string::npos);
        char expected[19];
        std::snprintf(expected, sizeof expected, "0x%016llx", static_cast<unsigned long long>(kFingerprint500Agents42Seed1000Steps));
        CHECK(out.str().find(expected) != std::string::npos);
    }
}
