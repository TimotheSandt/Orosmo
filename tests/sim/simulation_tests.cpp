// SIM-01 (A79 lot L3) : pas fixe, systèmes par nom, entrées datées, capture et restauration, rejeu, empreinte.
#include <doctest/doctest.h>

#include <orosmo/sim/simulation.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

using namespace orosmo;
using namespace orosmo::sim;
using namespace std::chrono_literals;

namespace
{
struct Counter
{
    std::int64_t value = 0;
    double sum = 0.0;
};
} // namespace

template <> struct orosmo::core::Reflect<Counter>
{
    static constexpr auto fields() { return std::make_tuple(field("value", &Counter::value), field("sum", &Counter::sum)); }
};

namespace
{
// Petite simulation : N entités avec un compteur ; « add » ajoute une valeur aléatoire, une entrée « bump » ajoute sa charge,
// une entrée « spawn » crée une entité et une entrée « kill » en détruit une (changements de structure rejoués).
void setup(Simulation &sim, int entities)
{
    auto type = *sim.world().registerComponent<Counter>("Counter");
    for (int i = 0; i < entities; ++i)
        sim.world().add(sim.world().create(), type, Counter{i, 0.0});
    REQUIRE(sim.addSystem({"counter.add", Phase::kUpdate, 0, [type](StepContext &ctx) {
                               ctx.world.view(type).each([&](scene::Entity, Counter &c) {
                                   c.value += ctx.random.range(-3, 5);
                                   c.sum = c.sum * 0.5 + ctx.random.nextDouble() * ctx.dt;
                               });
                           }}));
    REQUIRE(sim.addSystem({"counter.input", Phase::kInput, 0, [type](StepContext &ctx) {
                               for (const Input &input : ctx.inputs)
                               {
                                   if (input.name == "bump")
                                       ctx.world.view(type).each([&](scene::Entity, Counter &c) { c.value += input.data.get<std::int64_t>(); });
                                   else if (input.name == "spawn")
                                       ctx.world.add(ctx.world.create(), type, Counter{input.data.get<std::int64_t>(), 0.0});
                                   else if (input.name == "kill" && ctx.world.entityCount() > 0)
                                       ctx.world.destroy(ctx.world.entities()[input.data.get<std::size_t>() % ctx.world.entityCount()]);
                               }
                           }}));
}

std::vector<Input> someInputs()
{
    return {{3, "bump", 10}, {3, "spawn", 77}, {40, "kill", 2}, {41, "spawn", 5}, {90, "bump", -4}, {150, "kill", 0}};
}
} // namespace

TEST_SUITE("sim/simulation")
{
    TEST_CASE("Simulation: the number of steps depends on the elapsed time only, not on the frame deltas")
    {
        const std::vector<std::vector<core::Nanoseconds>> splits = {
            std::vector<core::Nanoseconds>(60, 16'666'667ns),
            {1ms, 250ms, 3ms, 46'000'003ns, 500ms, 200ms},
            std::vector<core::Nanoseconds>(1000, 1'000'000ns),
        };
        for (const auto &deltas : splits)
        {
            Simulation sim({.step = 10ms, .maxStepsPerFrame = 1000, .seed = 1});
            int steps = 0;
            for (core::Nanoseconds d : deltas)
                steps += sim.advance(d);
            core::Nanoseconds total{0};
            for (core::Nanoseconds d : deltas)
                total += d;
            CHECK(static_cast<std::uint64_t>(steps) == static_cast<std::uint64_t>(total / 10ms));
            CHECK(sim.stepIndex() == static_cast<std::uint64_t>(steps));
            CHECK(sim.alpha() >= 0.0);
            CHECK(sim.alpha() < 1.0);
        }
        // Retard trop grand : au plus maxStepsPerFrame pas par image (le surplus est abandonné).
        Simulation slow({.step = 10ms, .maxStepsPerFrame = 4});
        CHECK(slow.advance(1s) == 4);
        CHECK(slow.stepSeconds() == doctest::Approx(0.01));
    }

    TEST_CASE("Simulation: systems by name, ordered by phase, order and name")
    {
        Simulation sim;
        std::vector<std::string> calls;
        const auto add = [&](std::string name, Phase phase, int order) {
            return sim.addSystem({name, phase, order, [&calls, name](StepContext &) { calls.push_back(name); }});
        };
        REQUIRE(add("z.late", Phase::kPostUpdate, -10));
        REQUIRE(add("b.update", Phase::kUpdate, 0));
        REQUIRE(add("a.update", Phase::kUpdate, 0));
        REQUIRE(add("first", Phase::kUpdate, -1));
        REQUIRE(add("read", Phase::kInput, 99));
        CHECK((add("first", Phase::kInput, 0).error().code == core::ErrorCode::kAlreadyExists));
        CHECK((sim.addSystem({"", Phase::kUpdate, 0, [](StepContext &) {}}).error().code == core::ErrorCode::kInvalidArgument));
        CHECK((sim.addSystem({"empty", Phase::kUpdate, 0, {}}).error().code == core::ErrorCode::kInvalidArgument));
        const std::vector<std::string> expected = {"read", "first", "a.update", "b.update", "z.late"};
        CHECK(sim.systemOrder() == expected);
        sim.step();
        CHECK(calls == expected);
        CHECK(sim.removeSystem("first"));
        CHECK(!sim.removeSystem("first"));
        CHECK(!sim.hasSystem("first"));
        // Pendant un pas : refusé.
        REQUIRE(sim.addSystem({"nested", Phase::kUpdate, 5, [&sim](StepContext &) {
                                   CHECK(!sim.addSystem({"x", Phase::kUpdate, 0, [](StepContext &) {}}));
                                   CHECK(!sim.removeSystem("a.update"));
                               }}));
        sim.step();
        CHECK((parsePhase(phaseName(Phase::kPostUpdate)) == Phase::kPostUpdate));
        CHECK(!parsePhase("later"));
    }

    TEST_CASE("Simulation: timed inputs are delivered at their step, in order, and logged")
    {
        Simulation sim;
        std::vector<std::pair<std::uint64_t, std::string>> seen;
        REQUIRE(sim.addSystem({"log", Phase::kInput, 0, [&seen](StepContext &ctx) {
                                   for (const Input &i : ctx.inputs)
                                   {
                                       CHECK(i.step == ctx.step);
                                       seen.emplace_back(ctx.step, i.name);
                                   }
                               }}));
        REQUIRE(sim.queueInput({2, "b", {}}));
        REQUIRE(sim.queueInput({0, "a", {}}));
        REQUIRE(sim.queueInput({2, "c", {}}));
        sim.run(3);
        CHECK(seen == std::vector<std::pair<std::uint64_t, std::string>>{{0, "a"}, {2, "b"}, {2, "c"}});
        CHECK(sim.inputLog().size() == 3);
        CHECK(sim.pendingInputs().empty());
        CHECK((sim.queueInput({1, "late", {}}).error().code == core::ErrorCode::kInvalidArgument));
    }

    TEST_CASE("Simulation: same seed and inputs give the same fingerprint; capture and restore resume identically")
    {
        const auto play = [](std::uint64_t seed, std::uint64_t steps) {
            Simulation sim({.seed = seed});
            setup(sim, 50);
            for (const Input &i : someInputs())
                REQUIRE(sim.queueInput(i));
            sim.run(steps);
            return sim.fingerprint();
        };
        const std::uint64_t reference = play(42, 200);
        CHECK(play(42, 200) == reference);
        CHECK(play(43, 200) != reference);
        CHECK(play(42, 199) != reference);

        Simulation sim({.seed = 42});
        setup(sim, 50);
        for (const Input &i : someInputs())
            REQUIRE(sim.queueInput(i));
        sim.run(100);
        const Snapshot snapshot = sim.capture();
        CHECK(snapshot.step == 100);
        CHECK(snapshot.pending.size() == 1); // « kill » au pas 150
        sim.run(100);
        CHECK(sim.fingerprint() == reference);
        // Retour au pas 100, puis les mêmes 100 pas.
        sim.restore(snapshot);
        CHECK(sim.stepIndex() == 100);
        CHECK(sim.inputLog().size() == someInputs().size() - 1);
        sim.run(100);
        CHECK(sim.fingerprint() == reference);
    }

    TEST_CASE("Simulation: state saved to JSON at step 100 and loaded in a new simulation resumes identically")
    {
        Simulation a({.seed = 7});
        setup(a, 20);
        for (const Input &i : someInputs())
            REQUIRE(a.queueInput(i));
        a.run(100);
        const core::Json state = a.saveState();
        const std::string text = core::dumpJson(state);
        a.run(100);

        Simulation b({.seed = 7});
        setup(b, 3); // monde initial différent : remplacé par l'état chargé
        auto parsed = core::parseJson(text);
        REQUIRE(parsed);
        REQUIRE(b.loadState(*parsed));
        CHECK(b.stepIndex() == 100);
        b.run(100);
        CHECK(b.fingerprint() == a.fingerprint());

        Simulation c({.seed = 7});
        CHECK((c.loadState(core::Json::object()).error().code == core::ErrorCode::kParse));
        core::Json wrong = state;
        wrong["version"] = 2;
        CHECK((c.loadState(wrong).error().code == core::ErrorCode::kVersionMismatch));
        wrong = state;
        wrong["data"]["random"]["state"] = "x";
        CHECK((c.loadState(wrong).error().code == core::ErrorCode::kParse));
        CHECK(c.stepIndex() == 0);
    }

    TEST_CASE("Simulation: a recording replays to the same state, also from JSON")
    {
        Simulation live({.seed = 99});
        setup(live, 10);
        for (const Input &i : someInputs())
            REQUIRE(live.queueInput(i));
        live.run(160);
        const Replay replay = live.recording();
        CHECK(replay.firstStep == 0);
        CHECK(replay.lastStep == 160);
        CHECK(replay.inputs == someInputs());

        auto restored = replayFromJson(core::Json::parse(core::dumpJson(replayToJson(replay))));
        REQUIRE(restored);
        CHECK(*restored == replay);

        Simulation again({.seed = 99});
        setup(again, 10);
        REQUIRE(again.play(*restored));
        CHECK(again.fingerprint() == live.fingerprint());

        Simulation otherSeed({.seed = 1});
        CHECK((otherSeed.play(replay).error().code == core::ErrorCode::kInvalidArgument));
        Simulation advanced({.seed = 99});
        advanced.step();
        CHECK((advanced.play(replay).error().code == core::ErrorCode::kInvalidArgument));
        CHECK((replayFromJson(core::Json{{"version", 1}, {"data", core::Json::object()}}).error().code == core::ErrorCode::kParse));
    }
}

TEST_CASE("sim : StepContext::inputs, vue des entrées du pas (type léger de sim/system.hpp, GAME-06)")
{
    Simulation sim;
    std::size_t seen = 0;
    std::string names;
    REQUIRE(sim.addSystem({"read", Phase::kInput, 0, [&](StepContext &ctx) {
                               const std::span<const Input> span = ctx.inputs; // conversion vers std::span
                               seen = span.size();
                               for (std::size_t i = 0; i < ctx.inputs.size(); ++i)
                                   names += ctx.inputs[i].name;
                           }}));
    sim.step();
    CHECK(seen == 0);
    REQUIRE(sim.queueInput({.step = 1, .name = "a", .data = {}}));
    REQUIRE(sim.queueInput({.step = 1, .name = "b", .data = {}}));
    sim.step();
    CHECK(seen == 2);
    CHECK(names == "ab");
}
