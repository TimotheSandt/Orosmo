// Mode simulation de la démo (SIM-01, A79 lot L3). Voir agent_simulation.hpp.
// Chemin rejoué : aucune fonction transcendante (sin, cos, exp, pow, log, atan…), aucune horloge, aucun rand().
#include "agent_simulation.hpp"

#include "launch_options.hpp"

#include <orosmo/core/log.hpp>
#include <orosmo/scene/world.hpp>

#include <cstdio>
#include <cstdlib>
#include <ostream>
#include <vector>

namespace demo
{

namespace sim = orosmo::sim;
namespace scene = orosmo::scene;
using orosmo::core::DVec3;

namespace
{
constexpr double kBound = 50.0;      // demi-côté du cube des agents (m)
constexpr double kMaxSpeed = 3.0;    // par composante (m/s)
constexpr double kWander = 4.0;      // accélération aléatoire maximale (m/s², par composante)
constexpr double kCohesion = 0.05;   // attraction vers le centre du groupe (1/s²)
constexpr double kDrain = 0.0005;    // énergie perdue par pas
constexpr double kEffort = 0.002;    // énergie perdue par (m/s)² et par seconde

double signedUnit(orosmo::core::Random &random) { return random.nextDouble() * 2.0 - 1.0; }

double clampAbs(double v, double limit) { return v > limit ? limit : (v < -limit ? -limit : v); }

void spawn(scene::World &world, scene::ComponentType<Agent> type, orosmo::core::Random &random)
{
    const scene::Entity e = world.create();
    const DVec3 position{signedUnit(random) * kBound, signedUnit(random) * kBound, signedUnit(random) * kBound};
    if (auto placed = world.setTransform(e, {.position = position}); !placed) // entité tout juste créée : ne doit pas échouer
        LOG_WARNING("agents: setTransform failed: ", placed.error().message);
    Agent agent;
    agent.velocity = DVec3{signedUnit(random), signedUnit(random), signedUnit(random)};
    agent.energy = 0.5 + random.nextDouble() * 0.5;
    world.add(e, type, agent);
}

// Rebond sur une face du cube : position repliée à l'intérieur, composante de vitesse inversée.
void bounce(double &position, double &velocity)
{
    if (position > kBound)
    {
        position = 2.0 * kBound - position;
        velocity = -velocity;
    }
    else if (position < -kBound)
    {
        position = -2.0 * kBound - position;
        velocity = -velocity;
    }
}
} // namespace

orosmo::core::Result<> setupAgentSimulation(sim::Simulation &simulation, std::uint32_t agents)
{
    scene::World &world = simulation.world();
    auto registered = world.registerComponent<Agent>("Agent");
    if (!registered)
        return std::unexpected(registered.error());
    const scene::ComponentType<Agent> agentType = *registered;
    const scene::ComponentType<scene::Transform> transformType = world.transformType();

    const sim::SystemDesc systems[] = {
        {"agents.push", sim::Phase::kInput, 0,
         [agentType](sim::StepContext &ctx) {
             for (const sim::Input &input : ctx.inputs)
             {
                 if (input.name != "agents.push")
                     continue;
                 const orosmo::core::Json &d = input.data;
                 if (!d.is_array() || d.size() != 3 || !d[0].is_number() || !d[1].is_number() || !d[2].is_number())
                 {
                     // Pas d'exception de conversion dans le pas : l'entrée invalide est ignorée (la même au rejeu).
                     LOG_WARNING("agents.push: expected [x, y, z] numbers at step ", ctx.step, ", got ", d.dump());
                     continue;
                 }
                 const DVec3 impulse{input.data[0].get<double>(), input.data[1].get<double>(), input.data[2].get<double>()};
                 ctx.world.view(agentType).each([&](scene::Entity, Agent &a) { a.velocity += impulse; });
             }
         }},
        {"agents.wander", sim::Phase::kUpdate, 0,
         [agentType](sim::StepContext &ctx) {
             ctx.world.view(agentType).each([&](scene::Entity, Agent &a) {
                 for (int i = 0; i < 3; ++i)
                     a.velocity[i] = clampAbs(a.velocity[i] + signedUnit(ctx.random) * kWander * ctx.dt, kMaxSpeed);
             });
         }},
        {"agents.cohesion", sim::Phase::kUpdate, 10,
         [agentType, transformType](sim::StepContext &ctx) {
             DVec3 sum{0.0};
             double count = 0.0;
             ctx.world.view(transformType, agentType).each([&](scene::Entity, scene::Transform &t, Agent &) {
                 sum += t.position;
                 count += 1.0;
             });
             if (count == 0.0)
                 return;
             const DVec3 centre = sum / count;
             ctx.world.view(transformType, agentType).each([&](scene::Entity, scene::Transform &t, Agent &a) {
                 a.velocity += (centre - t.position) * (kCohesion * ctx.dt);
             });
         }},
        {"agents.move", sim::Phase::kUpdate, 20,
         [agentType, transformType](sim::StepContext &ctx) {
             ctx.world.view(transformType, agentType).each([&](scene::Entity e, scene::Transform &t, Agent &a) {
                 t.position += a.velocity * ctx.dt;
                 for (int i = 0; i < 3; ++i)
                     bounce(t.position[i], a.velocity[i]);
                 ctx.world.markTransformDirty(e);
             });
         }},
        {"agents.energy", sim::Phase::kPostUpdate, 0,
         [agentType](sim::StepContext &ctx) {
             std::vector<scene::Entity> exhausted;
             ctx.world.view(agentType).each([&](scene::Entity e, Agent &a) {
                 const DVec3 &v = a.velocity;
                 a.energy -= kDrain + (v.x * v.x + v.y * v.y + v.z * v.z) * kEffort * ctx.dt;
                 ++a.age;
                 if (a.energy <= 0.0)
                     exhausted.push_back(e);
             });
             // Hors du parcours : un agent épuisé est remplacé (indice libéré puis réutilisé, rejoué à l'identique).
             for (const scene::Entity e : exhausted)
             {
                 ctx.world.destroy(e);
                 spawn(ctx.world, agentType, ctx.random);
             }
         }},
    };
    for (const sim::SystemDesc &system : systems)
        if (auto added = simulation.addSystem(system); !added)
            return added;

    for (std::uint32_t i = 0; i < agents; ++i)
        spawn(world, agentType, simulation.random());
    world.updateTransforms();
    return {};
}

std::uint64_t runAgentSimulation(std::uint32_t agents, std::uint64_t seed, std::uint64_t steps)
{
    sim::Simulation simulation({.seed = seed});
    if (auto ready = setupAgentSimulation(simulation, agents); !ready)
        return 0;
    simulation.run(steps);
    return simulation.fingerprint();
}

int runSimulationMode(const LaunchOptions &options, std::ostream &out)
{
    sim::Simulation simulation({.seed = options.simSeed});
    if (auto ready = setupAgentSimulation(simulation, options.simAgents); !ready)
    {
        out << "Simulation: setup failed: " << ready.error().message << '\n';
        return EXIT_FAILURE;
    }
    simulation.run(options.simSteps);
    char fingerprint[19];
    std::snprintf(fingerprint, sizeof fingerprint, "0x%016llx", static_cast<unsigned long long>(simulation.fingerprint()));
    out << "Simulation: agents=" << options.simAgents << " seed=" << options.simSeed << " steps=" << simulation.stepIndex()
        << " alive=" << simulation.world().entityCount() << " fingerprint=" << fingerprint << '\n';
    return EXIT_SUCCESS;
}

} // namespace demo
