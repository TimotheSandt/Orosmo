// Mode simulation de la démo (SIM-01, A79 lot L3, choix 8) : agents sur la boucle à pas fixe, sans fenêtre NI périphérique
// graphique (`--sim-agents N --seed S --sim-steps K`). L'empreinte affichée est figée par tests/demo/agent_simulation_tests.cpp,
// donc identique dans chaque job de CI (Linux x64 et arm64, Windows, macOS).
//
// Chemin rejoué sans fonction transcendante de la bibliothèque C (seulement + - * / et comparaisons en double), le hasard venant
// de `StepContext::random` (PCG32) ; vérifié par tools/check_deterministic_math.py (`make check`).
#pragma once

#include <orosmo/core/error.hpp>
#include <orosmo/core/math.hpp>
#include <orosmo/core/reflection.hpp>
#include <orosmo/sim/simulation.hpp>

#include <cstdint>
#include <iosfwd>
#include <tuple>

namespace demo
{

struct LaunchOptions;

// Composant d'un agent (avec le `Transform` du moteur pour sa position).
struct Agent
{
    orosmo::core::DVec3 velocity{0.0};
    double energy = 1.0;
    std::uint32_t age = 0;
};

// Enregistre le composant « Agent » et les systèmes « agents.* », puis crée `agents` agents (positions et vitesses tirées de la
// graine de la simulation). Entrée « agents.push » (charge [x, y, z]) : impulsion donnée à tous les agents.
orosmo::core::Result<> setupAgentSimulation(orosmo::sim::Simulation &simulation, std::uint32_t agents);

// Simulation de `agents` agents, graine `seed`, `steps` pas : empreinte finale (`Simulation::fingerprint`).
std::uint64_t runAgentSimulation(std::uint32_t agents, std::uint64_t seed, std::uint64_t steps);

// Mode simulation : écrit « Simulation: agents=N seed=S steps=K alive=A fingerprint=0x... » sur `out` ; code de sortie.
int runSimulationMode(const LaunchOptions &options, std::ostream &out);

} // namespace demo

template <> struct orosmo::core::Reflect<demo::Agent>
{
    static constexpr int kVersion = 1;
    static constexpr auto fields()
    {
        return std::make_tuple(field("velocity", &demo::Agent::velocity), field("energy", &demo::Agent::energy),
                               field("age", &demo::Agent::age));
    }
};
