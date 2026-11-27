// SIM-01 (A79 lot L3) : pas fixe, systèmes par nom, entrées datées, capture et restauration, rejeu, empreinte.
#include <doctest/doctest.h>

#include <orosmo/sim/simulation.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
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
        CHECK(slow.advance(1ms) == 0); // les 960 ms de retard ont été abandonnées, pas gardées
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
        REQUIRE(c.queueInput({7, "bump", 2}));
        c.run(20); // journal d'une autre partie
        REQUIRE(c.inputLog().size() == 1);
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

TEST_SUITE("sim/simulation")
{
    TEST_CASE("Simulation: a replay records the step duration and the random stream; play refuses other settings")
    {
        Simulation live({.step = 10ms, .seed = 4, .stream = 9});
        setup(live, 3);
        const Snapshot start = live.capture();
        REQUIRE(live.queueInput({2, "bump", 5}));
        live.run(6);
        auto recorded = live.recording(0);
        REQUIRE(recorded);
        CHECK(recorded->stepNanoseconds == 10'000'000);
        CHECK(recorded->stream == 9u);
        auto fromJson = replayFromJson(core::parseJson(core::dumpJson(replayToJson(*recorded))).value());
        REQUIRE(fromJson);
        CHECK(*fromJson == *recorded);

        Simulation slower({.step = 20ms, .seed = 4, .stream = 9});
        setup(slower, 3);
        REQUIRE(slower.queueInput({1, "bump", 1}));
        const std::vector<Input> before = slower.pendingInputs();
        CHECK((errorOf(slower.play(*recorded)) == core::ErrorCode::kInvalidArgument));
        CHECK(slower.pendingInputs() == before); // refusé avant de toucher à la file
        CHECK(slower.stepIndex() == 0);
        Simulation otherStream({.step = 10ms, .seed = 4, .stream = 1});
        setup(otherStream, 3);
        CHECK((errorOf(otherStream.play(*recorded)) == core::ErrorCode::kInvalidArgument));

        const std::uint64_t expected = live.fingerprint();
        live.restore(start);
        REQUIRE(live.play(*recorded)); // réglages identiques : rejoué
        CHECK(live.fingerprint() == expected);
        // Document sans ces champs (plus ancien) : durée non vérifiée, flux par défaut.
        core::Json legacy = replayToJson(Replay{0, 0, 1, {}});
        legacy["data"].erase("stepNanoseconds");
        legacy["data"].erase("stream");
        auto old = replayFromJson(legacy);
        REQUIRE(old);
        CHECK(old->stepNanoseconds == 0);
        CHECK_FALSE(old->stream.has_value()); // flux inconnu : play ne le vérifie pas (revue PR #40)
    }

    TEST_CASE("Simulation: a capture or a state saved during a step is partial and cannot be restored")
    {
        Simulation sim({.seed = 6});
        setup(sim, 2);
        std::optional<Snapshot> during;
        core::Json duringState;
        REQUIRE(sim.addSystem({"save", Phase::kInput, -1, [&](StepContext &ctx) {
                                   if (ctx.step == 0)
                                   {
                                       during = sim.capture();
                                       duringState = sim.saveState();
                                   }
                               }}));
        REQUIRE(sim.queueInput({0, "bump", 100}));
        const Snapshot between = sim.capture();
        CHECK_FALSE(between.partial);
        sim.run(3);
        REQUIRE(during);
        CHECK(during->partial);
        const std::uint64_t now = sim.fingerprint();
        sim.restore(*during); // refusé : l'entrée du pas 0 a déjà quitté la file
        CHECK(sim.stepIndex() == 3);
        CHECK(sim.fingerprint() == now);
        CHECK((errorOf(sim.loadState(duringState)) == core::ErrorCode::kInvalidArgument));
        CHECK(sim.stepIndex() == 3);
        sim.restore(between); // entre deux pas : restaurable
        CHECK(sim.stepIndex() == 0);
        CHECK(sim.pendingInputs().size() == 1);
    }

    TEST_CASE("Simulation: restoring a later snapshot that the log no longer covers restarts the log")
    {
        Simulation sim({.seed = 8});
        setup(sim, 2);
        const Snapshot s0 = sim.capture();
        REQUIRE(sim.queueInput({0, "bump", 3}));
        sim.step();
        const Snapshot s1 = sim.capture();
        sim.restore(s0); // l'entrée du pas 0 quitte le journal
        CHECK(sim.inputLog().empty());
        CHECK(sim.firstLoggedStep() == 0);
        sim.restore(s1); // pas 1 : le journal ne couvre plus [0, 1)
        CHECK(sim.firstLoggedStep() == 1);
        CHECK((errorOf(sim.recording(0)) == core::ErrorCode::kInvalidArgument));
        CHECK(sim.recording(1));
        // Retour en arrière dans le préfixe connu : le journal reste valable.
        REQUIRE(sim.queueInput({1, "bump", 1}));
        sim.run(3);
        const Snapshot s2 = sim.capture();
        sim.run(2);
        sim.restore(s2);
        CHECK(sim.firstLoggedStep() == 1);
        auto kept = sim.recording(1);
        REQUIRE(kept);
        CHECK(kept->inputs == std::vector<Input>{{1, "bump", 1}});
    }

    TEST_CASE("Simulation: inputs queued by a system are not delivered twice by a replay; later ones stay pending")
    {
        auto build = [](Simulation &sim) {
            setup(sim, 3);
            // Chaque pas, le système prévoit un « bump » pour le pas suivant (entrée régénérée au rejeu).
            REQUIRE(sim.addSystem({"plan", Phase::kPostUpdate, 0, [&sim](StepContext &ctx) {
                                       CHECK(sim.queueInput({ctx.step + 1, "bump", static_cast<std::int64_t>(ctx.step)}));
                                   }}));
        };
        Simulation live({.seed = 12});
        build(live);
        const Snapshot start = live.capture();
        REQUIRE(live.queueInput({3, "bump", 50})); // entrée externe au même pas qu'une entrée régénérée
        live.run(6);
        auto recorded = live.recording(0);
        REQUIRE(recorded);
        const std::uint64_t expected = live.fingerprint();
        const std::vector<Input> expectedPending = live.pendingInputs();
        REQUIRE(expectedPending.size() == 1); // le « bump » du pas 6, prévu au pas 5

        live.restore(start);
        REQUIRE(live.play(*recorded));
        CHECK(live.fingerprint() == expected);
        CHECK(live.pendingInputs() == expectedPending); // produite après lastStep : gardée
        auto again = live.recording(0);
        REQUIRE(again);
        CHECK(again->inputs == recorded->inputs); // même livraison, même ordre
    }

    TEST_CASE("Simulation: a system that throws gives the step inputs back; the step replays with them")
    {
        Simulation sim;
        int seen = 0;
        bool thrown = false;
        REQUIRE(sim.addSystem({"read", Phase::kInput, 0, [&](StepContext &ctx) { seen += static_cast<int>(ctx.inputs.size()); }}));
        REQUIRE(sim.addSystem({"throws", Phase::kUpdate, 0, [&](StepContext &) {
                                   if (!thrown)
                                   {
                                       thrown = true;
                                       throw std::runtime_error("system failure");
                                   }
                               }}));
        REQUIRE(sim.queueInput({0, "a", 1}));
        REQUIRE(sim.queueInput({0, "b", 2}));
        REQUIRE(sim.queueInput({1, "c", 3}));
        CHECK_THROWS_AS(sim.step(), std::runtime_error);
        CHECK(sim.stepIndex() == 0);
        CHECK(sim.inputLog().empty()); // rien de livré jusqu'au bout
        CHECK(sim.pendingInputs() == std::vector<Input>{{0, "a", 1}, {0, "b", 2}, {1, "c", 3}});
        sim.step();
        CHECK(sim.stepIndex() == 1);
        CHECK(sim.inputLog() == std::vector<Input>{{0, "a", 1}, {0, "b", 2}});
        CHECK(seen == 4); // pas interrompu (2) puis rejoué (2)
    }

    TEST_CASE("Simulation: payloads with invalid UTF-8 or binary values are refused; ordinary objects are kept")
    {
        Simulation sim;
        const std::string bad = std::string("a") + static_cast<char>(0xFF);
        CHECK((errorOf(sim.queueInput({0, "x", bad})) == core::ErrorCode::kInvalidArgument));
        CHECK((errorOf(sim.queueInput({0, "x", core::Json{{bad, 1}}})) == core::ErrorCode::kInvalidArgument));
        CHECK((errorOf(sim.queueInput({0, bad, 1})) == core::ErrorCode::kInvalidArgument));
        CHECK((errorOf(sim.queueInput({0, "x", core::Json::array({std::string("\xC0\x80")})})) == core::ErrorCode::kInvalidArgument));
        CHECK((errorOf(sim.queueInput({0, "x", core::Json::binary({1, 2})})) == core::ErrorCode::kInvalidArgument));
        CHECK((errorOf(sim.queueInput({0, "x", core::Json{{"in", core::Json::array({core::Json::binary({3})})}}})) ==
               core::ErrorCode::kInvalidArgument));
        CHECK(sim.pendingInputs().empty());
        CHECK(sim.fingerprint() != 0); // dump() ne lève pas : rien d'invalide en file
        REQUIRE(sim.queueInput({0, "\xC3\xA9", core::Json{{"bytes", core::Json::array({1})}, {"subtype", nullptr}, {"cl\xC3\xA9", "\xE2\x82\xAC \xF0\x9F\x98\x80"}}}));
        const core::Json state = sim.saveState();
        Simulation other;
        REQUIRE(other.loadState(core::parseJson(core::dumpJson(state)).value()));
        CHECK(other.pendingInputs() == sim.pendingInputs());
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

TEST_CASE("Simulation: takeEvents is refused during a step")
{
    Simulation sim;
    std::size_t taken = 99;
    REQUIRE(sim.addSystem({"emit", Phase::kUpdate, 0, [&](StepContext &ctx) {
                               ctx.emit("one");
                               taken = sim.takeEvents().size();
                           }}));
    sim.step();
    CHECK(taken == 0);
    CHECK(sim.takeEvents().size() == 1);
}

TEST_CASE("sim : StepContext::emit sans charge (forme de l'en-tête léger) et avec charge")
{
    Simulation sim;
    REQUIRE(sim.addSystem({"emit", Phase::kUpdate, 0, [](StepContext &ctx) {
                               ctx.emit("bare");
                               ctx.emit("full", core::Json{{"v", 1}});
                           }}));
    sim.step();
    const auto events = sim.takeEvents();
    REQUIRE(events.size() == 2);
    CHECK(events[0].name == "bare");
    CHECK(events[0].data.is_null());
    CHECK(events[1].data["v"] == 1);
}

TEST_SUITE("sim/simulation")
{
    TEST_CASE("Simulation: discarded JSON values are refused, even nested (revue PR #37)")
    {
        Simulation sim;
        const core::Json discarded = core::Json(core::Json::value_t::discarded);
        CHECK((errorOf(sim.queueInput({0, "x", discarded})) == core::ErrorCode::kInvalidArgument));
        CHECK((errorOf(sim.queueInput({0, "x", core::Json::array({1, discarded})})) == core::ErrorCode::kInvalidArgument));
        CHECK((errorOf(sim.queueInput({0, "x", core::Json{{"in", discarded}}})) == core::ErrorCode::kInvalidArgument));
        CHECK(sim.pendingInputs().empty());
        REQUIRE(sim.queueInput({0, "x", nullptr})); // null reste accepté
    }

    TEST_CASE("Simulation: an interrupted step drops the inputs queued and the events emitted by the attempt (revue PR #37)")
    {
        Simulation sim;
        bool thrown = false;
        REQUIRE(sim.addSystem({"fire", Phase::kInput, 0, [&](StepContext &ctx) {
                                   for (const Input &input : ctx.inputs)
                                       if (input.name == "fire")
                                       {
                                           REQUIRE(sim.queueInput({ctx.step + 1, "hit", 1}));
                                           ctx.emit("sound");
                                       }
                               }}));
        REQUIRE(sim.addSystem({"throws", Phase::kUpdate, 0, [&](StepContext &ctx) {
                                   if (ctx.step == 1 && !thrown)
                                   {
                                       thrown = true;
                                       throw std::runtime_error("system failure");
                                   }
                               }}));
        REQUIRE(sim.queueInput({0, "fire", 0}));
        REQUIRE(sim.queueInput({1, "fire", 0}));
        REQUIRE(sim.queueInput({2, "later", 0}));
        sim.step(); // pas 0 : hit au pas 1, un « sound » non repris
        REQUIRE(sim.events().size() == 1);
        CHECK_THROWS_AS(sim.step(), std::runtime_error); // pas 1 : hit au pas 2 et « sound » de la tentative abandonnés
        CHECK(sim.events().size() == 1);                 // l'événement non repris du pas 0 reste
        CHECK(sim.pendingInputs() == std::vector<Input>{{1, "fire", 0}, {1, "hit", 1}, {2, "later", 0}});
        sim.step(); // nouvel essai : un seul hit au pas 2, après « later » (ordre d'ajout)
        CHECK(sim.pendingInputs() == std::vector<Input>{{2, "later", 0}, {2, "hit", 1}});
        REQUIRE(sim.events().size() == 2);
        CHECK(sim.events()[1].step == 1);
    }

    TEST_CASE("Simulation: a replay interrupted by an exception resumes with step() without duplicate inputs (revue PR #37)")
    {
        const auto build = [](Simulation &sim, bool &armed) {
            REQUIRE(sim.addSystem({"fire", Phase::kInput, 0, [&sim](StepContext &ctx) {
                                       for (const Input &input : ctx.inputs)
                                           if (input.name == "fire")
                                               REQUIRE(sim.queueInput({ctx.step + 1, "hit", 1}));
                                   }}));
            REQUIRE(sim.addSystem({"throws", Phase::kUpdate, 0, [&armed](StepContext &ctx) {
                                       if (armed && ctx.step == 1)
                                       {
                                           armed = false;
                                           throw std::runtime_error("system failure");
                                       }
                                   }}));
        };
        bool armed = false;
        Simulation live;
        build(live, armed);
        REQUIRE(live.queueInput({1, "fire", 0}));
        live.run(3);
        auto replay = live.recording(0);
        REQUIRE(replay);
        REQUIRE(replay->inputs == std::vector<Input>{{1, "fire", 0}, {2, "hit", 1}});

        bool armedAgain = true;
        Simulation copy;
        build(copy, armedAgain);
        CHECK_THROWS_AS((void)copy.play(*replay), std::runtime_error);
        CHECK(copy.stepIndex() == 1);
        // Revue PR #37 (3) : le filtre du rejeu fait partie de l'état ; reprendre après restore ou loadState ne double rien.
        bool off = false;
        Simulation loaded;
        build(loaded, off);
        REQUIRE(loaded.loadState(copy.saveState()));
        copy.restore(copy.capture());
        copy.run(2); // reprise : le hit du pas 2 vient du rejeu, celui que le système remet en file est ignoré
        CHECK(copy.inputLog() == live.inputLog());
        CHECK(copy.fingerprint() == live.fingerprint());
        loaded.run(2);
        CHECK(loaded.fingerprint() == live.fingerprint());
        CHECK_FALSE(live.saveState()["data"].contains("replayEnd")); // absent quand rien ne filtre : document inchangé
    }

    TEST_CASE("Simulation: restorePending replaces the queue only; loadState sorts many inputs by step, stable")
    {
        Simulation sim;
        REQUIRE(sim.addSystem({"ping", Phase::kUpdate, 0, [](StepContext &ctx) { ctx.emit("ping"); }}));
        sim.step();
        REQUIRE(sim.advance(sim.settings().step / 2) == 0);
        const double alpha = sim.alpha();
        REQUIRE(sim.restorePending({{3, "b", 0}, {2, "a", 0}, {3, "c", 0}}));
        CHECK(sim.pendingInputs() == std::vector<Input>{{2, "a", 0}, {3, "b", 0}, {3, "c", 0}});
        CHECK(sim.events().size() == 1);
        CHECK(sim.alpha() == doctest::Approx(alpha));
        CHECK((errorOf(sim.restorePending({{0, "past", 0}})) == core::ErrorCode::kInvalidArgument));
        CHECK(sim.pendingInputs().size() == 3); // refus : file intacte

        // 20 000 entrées par pas décroissant, deux par pas : triées sans insertions quadratiques, ordre gardé à pas égal.
        core::Json pending = core::Json::array();
        for (std::uint64_t s = 10'000; s > 0; --s)
        {
            pending.push_back(core::Json{{"step", s}, {"name", "first"}, {"data", nullptr}});
            pending.push_back(core::Json{{"step", s}, {"name", "second"}, {"data", nullptr}});
        }
        core::Json state = Simulation{}.saveState();
        state["data"]["pending"] = std::move(pending);
        Simulation loaded;
        REQUIRE(loaded.loadState(state));
        const std::vector<Input> &queue = loaded.pendingInputs();
        REQUIRE(queue.size() == 20'000);
        bool ordered = true;
        for (std::size_t i = 0; i < queue.size(); ++i)
            ordered = ordered && queue[i].step == i / 2 + 1 && queue[i].name == (i % 2 == 0 ? "first" : "second");
        CHECK(ordered);
    }
}
