// SIM-01 (A79 lot L3) : pas fixe, systèmes par nom, entrées datées, capture et restauration, rejeu, empreinte.
#include <doctest/doctest.h>

#include <orosmo/sim/simulation.hpp>

#include <chrono>
#include <limits>
#include <stdexcept>
#include <cstdint>
#include <string>
#include <vector>

using namespace orosmo;
using namespace orosmo::sim;
using namespace std::chrono_literals;

namespace
{
// Code d'erreur d'un résultat qui DOIT être une erreur : REQUIRE avant `.error()` (un succès inattendu est signalé, sans
// appeler `.error()` sur une valeur).
template <typename T> core::ErrorCode errorOf(const core::Result<T> &result)
{
    REQUIRE(!result);
    return result.error().code;
}

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
    TEST_CASE("Simulation: while no call hits maxStepsPerFrame, the number of steps depends on the elapsed time only")
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
        // Le plafond casse l'indépendance du découpage : 100 ms d'un coup donnent 4 pas, dix fois 10 ms en donnent 10.
        Simulation capped({.step = 10ms, .maxStepsPerFrame = 4});
        CHECK(capped.advance(100ms) == 4);
        Simulation split({.step = 10ms, .maxStepsPerFrame = 4});
        int splitSteps = 0;
        for (int i = 0; i < 10; ++i)
            splitSteps += split.advance(10ms);
        CHECK(splitSteps == 10);
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
        CHECK((errorOf(add("first", Phase::kInput, 0)) == core::ErrorCode::kAlreadyExists));
        CHECK((errorOf(sim.addSystem({"", Phase::kUpdate, 0, [](StepContext &) {}})) == core::ErrorCode::kInvalidArgument));
        CHECK((errorOf(sim.addSystem({"empty", Phase::kUpdate, 0, {}})) == core::ErrorCode::kInvalidArgument));
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
        CHECK((errorOf(sim.queueInput({1, "late", {}})) == core::ErrorCode::kInvalidArgument));
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
        CHECK((errorOf(c.loadState(core::Json::object())) == core::ErrorCode::kParse));
        core::Json wrong = state;
        wrong["version"] = 2;
        CHECK((errorOf(c.loadState(wrong)) == core::ErrorCode::kVersionMismatch));
        wrong = state;
        wrong["data"]["random"]["state"] = "x";
        CHECK((errorOf(c.loadState(wrong)) == core::ErrorCode::kParse));
        CHECK(c.stepIndex() == 0);
    }

    TEST_CASE("Simulation: a recording replays to the same state, also from JSON")
    {
        Simulation live({.seed = 99});
        setup(live, 10);
        for (const Input &i : someInputs())
            REQUIRE(live.queueInput(i));
        live.run(160);
        auto recorded = live.recording();
        REQUIRE(recorded);
        const Replay replay = *recorded;
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
        CHECK((errorOf(otherSeed.play(replay)) == core::ErrorCode::kInvalidArgument));
        Simulation advanced({.seed = 99});
        advanced.step();
        CHECK((errorOf(advanced.play(replay)) == core::ErrorCode::kInvalidArgument));
        CHECK((errorOf(replayFromJson(core::Json{{"version", 1}, {"data", core::Json::object()}})) == core::ErrorCode::kParse));
    }

    TEST_CASE("Simulation: an input queued during a step for that step is refused; later steps are not blocked")
    {
        Simulation sim;
        std::vector<std::pair<std::uint64_t, std::string>> seen;
        bool queued = false;
        REQUIRE(sim.addSystem({"log", Phase::kInput, 0, [&](StepContext &ctx) {
                                   for (const Input &i : ctx.inputs)
                                       seen.emplace_back(ctx.step, i.name);
                                   if (ctx.step == 0 && !queued)
                                   {
                                       queued = true;
                                       CHECK((errorOf(sim.queueInput({0, "now", {}})) == core::ErrorCode::kInvalidArgument));
                                       CHECK(sim.queueInput({1, "next", {}}));
                                   }
                               }}));
        REQUIRE(sim.queueInput({0, "a", {}})); // avant le pas : le pas courant reste ouvert
        REQUIRE(sim.queueInput({1, "b", {}}));
        sim.run(2);
        CHECK(seen == std::vector<std::pair<std::uint64_t, std::string>>{{0, "a"}, {1, "b"}, {1, "next"}});
        CHECK(sim.pendingInputs().empty());
    }

    TEST_CASE("Simulation: stepping again from a system is refused and leaves the state unchanged")
    {
        Simulation sim({.step = 10ms, .seed = 3});
        setup(sim, 4);
        const Snapshot snapshot = sim.capture();
        const Replay empty{3, 0, 2, {}};
        int calls = 0;
        REQUIRE(sim.addSystem({"nested", Phase::kPostUpdate, 0, [&](StepContext &ctx) {
                                   ++calls;
                                   sim.step();
                                   sim.run(3);
                                   CHECK(sim.advance(1s) == 0);
                                   sim.restore(snapshot);
                                   CHECK((errorOf(sim.play(empty)) == core::ErrorCode::kInvalidArgument));
                                   CHECK((errorOf(sim.loadState(sim.saveState())) == core::ErrorCode::kInvalidArgument));
                                   CHECK(sim.stepIndex() == ctx.step);
                               }}));
        Simulation reference({.step = 10ms, .seed = 3});
        setup(reference, 4);
        REQUIRE(reference.addSystem({"nested", Phase::kPostUpdate, 0, [](StepContext &) {}}));
        sim.run(2);
        reference.run(2);
        CHECK(calls == 2);
        CHECK(sim.stepIndex() == 2);
        CHECK(sim.fingerprint() == reference.fingerprint());
        CHECK(sim.removeSystem("nested")); // plus pendant un pas
        CHECK(sim.advance(25ms) == 2);      // le temps n'avait pas été consommé par l'appel refusé
    }

    TEST_CASE("Simulation: a system that throws does not leave the simulation stuck in a step")
    {
        Simulation sim;
        bool thrown = false;
        REQUIRE(sim.addSystem({"throws", Phase::kUpdate, 0, [&](StepContext &) {
                                   if (!thrown)
                                   {
                                       thrown = true;
                                       throw std::runtime_error("system failure");
                                   }
                               }}));
        CHECK_THROWS_AS(sim.step(), std::runtime_error);
        CHECK(sim.addSystem({"after", Phase::kUpdate, 1, [](StepContext &) {}}));
        CHECK(sim.removeSystem("after"));
        sim.step();
        CHECK(sim.stepIndex() == 1);
    }

    TEST_CASE("Simulation: input payloads must be representable in JSON text (no infinity nor NaN)")
    {
        Simulation sim;
        const double inf = std::numeric_limits<double>::infinity();
        const double nan = std::numeric_limits<double>::quiet_NaN();
        CHECK((errorOf(sim.queueInput({0, "x", inf})) == core::ErrorCode::kInvalidArgument));
        CHECK((errorOf(sim.queueInput({0, "x", core::Json::array({1.0, -inf})})) == core::ErrorCode::kInvalidArgument));
        CHECK((errorOf(sim.queueInput({0, "x", core::Json{{"a", core::Json{{"b", nan}}}}})) == core::ErrorCode::kInvalidArgument));
        CHECK(sim.pendingInputs().empty());
        REQUIRE(sim.queueInput({0, "ok", core::Json{{"v", core::Json::array({1.5, -0.0, 3, "s", nullptr})}}}));
        CHECK((errorOf(sim.play(Replay{0, 0, 1, {{0, "x", nan}}})) == core::ErrorCode::kInvalidArgument));
        CHECK(sim.pendingInputs().size() == 1);
        core::Json state = sim.saveState();
        state["data"]["pending"][0]["data"] = inf;
        Simulation other;
        CHECK((errorOf(other.loadState(state)) == core::ErrorCode::kInvalidArgument));
    }

    TEST_CASE("Simulation: state and replay accept non-negative integers built in C++ (signed) as well as parsed ones")
    {
        Simulation a({.seed = 7});
        setup(a, 3);
        a.run(10);
        core::Json state = a.saveState();
        state["data"]["step"] = 10; // int : entier signé dans core::Json
        state["data"]["pending"] = core::Json::array({core::Json{{"step", 12}, {"name", "bump"}, {"data", 1}}});
        Simulation b({.seed = 7});
        setup(b, 3);
        REQUIRE(b.loadState(state));
        CHECK(b.stepIndex() == 10);
        CHECK(b.pendingInputs().size() == 1);
        for (const core::Json &bad : {core::Json(-1), core::Json(10.5)})
        {
            core::Json wrong = state;
            wrong["data"]["step"] = bad;
            CHECK((errorOf(b.loadState(wrong)) == core::ErrorCode::kParse));
        }
        auto replay = replayFromJson(core::Json{{"version", 1},
                                                {"data", {{"seed", 7}, {"firstStep", 0}, {"lastStep", 3}, {"inputs", core::Json::array()}}}});
        REQUIRE(replay);
        CHECK(replay->lastStep == 3);
    }

    TEST_CASE("Simulation: a state with a pending input before its step is refused; the log restarts at the loaded step")
    {
        Simulation a({.seed = 5});
        setup(a, 6);
        for (const Input &i : someInputs())
            REQUIRE(a.queueInput(i));
        a.run(100);
        core::Json state = a.saveState();

        Simulation b({.seed = 5});
        setup(b, 6);
        core::Json wrong = state;
        wrong["data"]["pending"].push_back(core::Json{{"step", 99}, {"name", "bump"}, {"data", 1}});
        CHECK((errorOf(b.loadState(wrong)) == core::ErrorCode::kParse));
        CHECK(b.stepIndex() == 0);
        wrong = state;
        wrong["data"]["pending"].push_back(core::Json{{"step", 100}, {"name", "bump"}, {"data", 1}});
        REQUIRE(b.loadState(wrong)); // une entrée au pas sauvegardé reste valide
        CHECK(b.pendingInputs().front().step == 100);

        Simulation c({.seed = 5});
        setup(c, 6);
        c.run(20); // journal d'une autre partie
        REQUIRE(c.loadState(state));
        CHECK(c.inputLog().empty());
        CHECK(c.firstLoggedStep() == 100);
        CHECK((errorOf(c.recording(0)) == core::ErrorCode::kInvalidArgument)); // l'entrée du pas 3 est inconnue
        c.run(60);
        auto fromLoad = c.recording(100);
        REQUIRE(fromLoad);
        CHECK(fromLoad->inputs.size() == 1); // « kill » au pas 150
        CHECK((errorOf(c.recording(161)) == core::ErrorCode::kInvalidArgument));

        // Instantané antérieur au début du journal : le journal recommence à son pas.
        Simulation d({.seed = 5});
        setup(d, 6);
        const Snapshot early = d.capture();
        d.run(5);
        REQUIRE(d.loadState(state));
        d.restore(early);
        CHECK(d.firstLoggedStep() == 0);
    }

    TEST_CASE("Simulation: play validates the whole replay first and refuses inputs outside [firstStep, lastStep)")
    {
        Simulation sim({.seed = 2});
        setup(sim, 3);
        sim.run(10);
        REQUIRE(sim.queueInput({20, "bump", 1}));
        const std::vector<Input> before = sim.pendingInputs();
        CHECK((errorOf(sim.play(Replay{2, 10, 12, {{11, "bump", 1}, {12, "bump", 1}}})) == core::ErrorCode::kInvalidArgument));
        CHECK((errorOf(sim.play(Replay{2, 10, 12, {{10, "bump", 1}, {9, "bump", 1}}})) == core::ErrorCode::kInvalidArgument));
        CHECK(sim.pendingInputs() == before);
        CHECK(sim.stepIndex() == 10);
        REQUIRE(sim.play(Replay{2, 10, 12, {{10, "bump", 1}, {11, "bump", 1}}}));
        CHECK(sim.stepIndex() == 12);
        CHECK(sim.pendingInputs() == before); // l'entrée du pas 20, hors de l'intervalle, reste
    }

    TEST_CASE("Simulation: replaying from a restored snapshot does not deliver its pending inputs twice")
    {
        Simulation live({.seed = 11});
        setup(live, 5);
        live.run(4);
        REQUIRE(live.queueInput({5, "bump", 10}));
        REQUIRE(live.queueInput({30, "bump", 3})); // après la fin du rejeu
        const Snapshot snapshot = live.capture();
        live.run(6);
        auto recorded = live.recording(4);
        REQUIRE(recorded);
        CHECK(recorded->inputs == std::vector<Input>{{5, "bump", 10}});
        const std::uint64_t expected = live.fingerprint();

        live.restore(snapshot);
        REQUIRE(live.play(*recorded));
        CHECK(live.stepIndex() == 10);
        CHECK(live.fingerprint() == expected);
        CHECK(live.pendingInputs() == std::vector<Input>{{30, "bump", 3}});
    }
}
