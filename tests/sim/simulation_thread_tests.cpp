// SIM-01, extension (A79 lot L4) : thread de simulation, cadence, instantané en double tampon, entrées et événements par
// files, arène de pas. Le test concurrent (simulation + lecteur d'instantanés qui joue le rôle du rendu, sans GPU) est fait
// pour le job TSan de la CI (`make demo-test-unit SANITIZE=thread`).
#include <doctest/doctest.h>

#include <orosmo/sim/simulation_thread.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace orosmo;
using namespace orosmo::sim;
using namespace std::chrono_literals;

namespace
{
struct Mover
{
    double vx = 0.0;
    double vy = 0.0;
    std::int64_t bounces = 0;
};
struct Tint
{
    float r = 0.f, g = 0.f, b = 0.f, a = 1.f;
};
struct NotTint // même taille et alignement que Tint, autres champs
{
    std::uint32_t id = 0;
    float x = 0.f, y = 0.f, z = 0.f;
};
struct Thrower
{
    std::int64_t armed = 0;
};

// Code d'erreur d'un résultat qui DOIT être une erreur : REQUIRE avant `.error()` (un succès inattendu est signalé, sans
// appeler `.error()` sur une valeur).
template <typename T> core::ErrorCode errorOf(const core::Result<T> &result)
{
    REQUIRE_FALSE(result);
    return result.error().code;
}
} // namespace

template <> struct orosmo::core::Reflect<Mover>
{
    static constexpr auto fields()
    {
        return std::make_tuple(field("vx", &Mover::vx), field("vy", &Mover::vy), field("bounces", &Mover::bounces));
    }
};
template <> struct orosmo::core::Reflect<NotTint>
{
    static constexpr auto fields()
    {
        return std::make_tuple(field("id", &NotTint::id), field("x", &NotTint::x), field("y", &NotTint::y), field("z", &NotTint::z));
    }
};
template <> struct orosmo::core::Reflect<Tint>
{
    static constexpr auto fields()
    {
        return std::make_tuple(field("r", &Tint::r), field("g", &Tint::g), field("b", &Tint::b), field("a", &Tint::a));
    }
};

namespace
{
// Agents qui errent (hasard du pas), rebondissent (événement « bounce »), apparaissent et disparaissent sur entrée : état,
// structure du monde et événements dépendent de la graine et des entrées.
void setupMovers(Simulation &sim, int count)
{
    scene::World &world = sim.world();
    auto mover = *world.registerComponent<Mover>("Mover");
    auto spawn = [mover](scene::World &w, core::Random &random) {
        const scene::Entity e = w.create();
        // Appelé aussi sur le thread de simulation : pas de REQUIRE (une exception y terminerait le programme).
        CHECK(w.setTransform(e, {.position = {random.range(-5.0f, 5.0f), random.range(-5.0f, 5.0f), 0.0}}));
        w.add(e, mover, Mover{random.range(-2.0f, 2.0f), random.range(-2.0f, 2.0f), 0});
    };
    for (int i = 0; i < count; ++i)
        spawn(world, sim.random());
    REQUIRE(sim.addSystem({"movers.input", Phase::kInput, 0, [mover, spawn](StepContext &ctx) {
                               for (const Input &input : ctx.inputs)
                               {
                                   if (input.name == "spawn")
                                       for (std::int64_t i = 0; i < input.data.get<std::int64_t>(); ++i)
                                           spawn(ctx.world, ctx.random);
                                   else if (input.name == "kill" && !ctx.world.componentEntities(mover.id).empty())
                                   {
                                       auto entities = ctx.world.componentEntities(mover.id);
                                       ctx.world.destroy(entities[input.data.get<std::size_t>() % entities.size()]);
                                   }
                                   else if (input.name == "impulse")
                                       ctx.world.view(mover).each([&](scene::Entity, Mover &m) { m.vx += input.data.get<double>(); });
                               }
                           }}));
    REQUIRE(sim.addSystem({"movers.move", Phase::kUpdate, 0, [mover](StepContext &ctx) {
                               ctx.world.view(ctx.world.transformType(), mover).each([&](scene::Entity e, scene::Transform &t, Mover &m) {
                                   m.vx += (ctx.random.nextDouble() - 0.5) * 0.1;
                                   t.position.x += m.vx * ctx.dt;
                                   t.position.y += m.vy * ctx.dt;
                                   if (t.position.x > 10.0 || t.position.x < -10.0)
                                   {
                                       m.vx = -m.vx;
                                       ++m.bounces;
                                       ctx.emit("bounce", core::Json{{"entity", e.index}, {"bounces", m.bounces}});
                                   }
                                   ctx.world.markTransformDirty(e);
                               });
                           }}));
}

std::vector<Input> moverInputs()
{
    return {{5, "spawn", 3}, {40, "kill", 1}, {41, "impulse", 6.0}, {120, "spawn", 2}, {121, "kill", 4}, {199, "impulse", -9.0}};
}

// Référence mono-thread.
struct Reference
{
    std::uint64_t fingerprint = 0;
    std::vector<Event> events;
};
Reference runSingleThread(std::uint64_t seed, core::Nanoseconds step, std::uint64_t steps)
{
    Simulation sim({.step = step, .seed = seed});
    setupMovers(sim, 12);
    for (const Input &input : moverInputs())
        REQUIRE(sim.queueInput(input));
    sim.run(steps);
    return {sim.fingerprint(), sim.takeEvents()};
}

void drainEvents(SimulationThread &thread, std::vector<Event> &out)
{
    while (auto event = thread.popEvent())
        out.push_back(std::move(*event));
}
} // namespace

TEST_SUITE("sim/thread")
{
    TEST_CASE("SnapshotBuffer: the reader sees the last published value, unchanged until its next acquire")
    {
        SnapshotBuffer<int> buffer;
        CHECK(buffer.acquire() == 0);
        CHECK_FALSE(buffer.fresh());
        buffer.back() = 1;
        buffer.publish();
        CHECK(buffer.fresh());
        CHECK(buffer.acquire() == 1);
        CHECK_FALSE(buffer.fresh());
        buffer.back() = 2;
        buffer.publish();
        buffer.back() = 3;
        buffer.publish(); // 2 n'a jamais été lu : remplacé
        CHECK(buffer.front() == 1);
        CHECK(buffer.acquire() == 3);
        CHECK(buffer.acquire() == 3);
        for (int i = 4; i < 20; ++i)
        {
            buffer.back() = i;
            buffer.publish();
            CHECK(buffer.acquire() == i);
        }
    }

    TEST_CASE("Config: thread mode, cadence and rate are read from core::Config, single-thread by default")
    {
        core::Config config;
        declareSimulationConfig(config);
        auto defaults = readSimulationConfig(config);
        REQUIRE(defaults);
        CHECK_FALSE(defaults->threaded);
        CHECK(defaults->thread.cadence == Cadence::kFixedRate);
        CHECK(defaults->thread.stepsPerFrame == 1);
        CHECK(defaults->step == stepForRate(60.0));

        config.set("sim.thread", true);
        config.set("sim.cadence", "frame");
        config.set("sim.stepsPerFrame", 4);
        config.set("sim.rateHz", 120.0);
        auto set = readSimulationConfig(config);
        REQUIRE(set);
        CHECK(set->threaded);
        CHECK(set->thread.cadence == Cadence::kStepsPerFrame);
        CHECK(set->thread.stepsPerFrame == 4);
        CHECK(set->step == 8'333'333ns);

        auto args = std::vector<std::string>{"--sim.cadence=rate", "--sim.rateHz=20"};
        REQUIRE(config.parseArgs(args));
        auto cli = readSimulationConfig(config);
        REQUIRE(cli);
        CHECK(cli->thread.cadence == Cadence::kFixedRate);
        CHECK(cli->step == 50ms);

        core::Config bad;
        declareSimulationConfig(bad);
        bad.set("sim.cadence", "sometimes");
        CHECK((errorOf(readSimulationConfig(bad)) == core::ErrorCode::kInvalidArgument));
        bad.set("sim.cadence", "frame");
        bad.set("sim.stepsPerFrame", 0);
        CHECK((errorOf(readSimulationConfig(bad)) == core::ErrorCode::kInvalidArgument));
        bad.set("sim.stepsPerFrame", 1);
        bad.set("sim.rateHz", 0.0);
        CHECK((errorOf(readSimulationConfig(bad)) == core::ErrorCode::kInvalidArgument));

        CHECK(stepForRate(20.0) == 50ms);
        CHECK(stepForRate(50.0) == 20ms);
        CHECK(stepForRate(120.0) == 8'333'333ns);
        // Durée qui ne tient pas dans Nanoseconds (1e21 ns) : 0, et refusée par la configuration (pas de conversion indéfinie).
        CHECK(stepForRate(1e-12) == 0ns);
        CHECK(stepForRate(0.0) == 0ns);
        core::Config tiny;
        declareSimulationConfig(tiny);
        tiny.set("sim.rateHz", 1e-12);
        CHECK((errorOf(readSimulationConfig(tiny)) == core::ErrorCode::kInvalidArgument));
        CHECK(parseCadence(cadenceName(Cadence::kStepsPerFrame)) == Cadence::kStepsPerFrame);
        CHECK(parseCadence(cadenceName(Cadence::kFixedRate)) == Cadence::kFixedRate);
    }

    TEST_CASE("interpolate: exact at both ends, linear position and scale, rotation on the short path")
    {
        RenderTransform a{.position = {0.0, 0.0, 0.0}, .linear = core::DMat3(1.0)};
        RenderTransform b{.position = {10.0, -4.0, 2.0}, .linear = core::DMat3(2.0)};
        CHECK(interpolate(a, b, 0.0) == a);
        CHECK(interpolate(a, b, 1.0) == b);
        CHECK(interpolate(a, b, -3.0) == a);
        CHECK(interpolate(a, b, 7.0) == b);
        const RenderTransform mid = interpolate(a, b, 0.25);
        CHECK(mid.position.x == doctest::Approx(2.5));
        CHECK(mid.position.y == doctest::Approx(-1.0));
        CHECK(mid.linear[0][0] == doctest::Approx(1.25));
        CHECK(mid.linear[1][1] == doctest::Approx(1.25));
        CHECK(mid.linear[0][1] == doctest::Approx(0.0));

        // 90° autour de z, à moitié : 45°, sans perte d'échelle (une interpolation terme à terme donnerait 0,707).
        RenderTransform r{.position = {}, .linear = core::DMat3(0.0, 1.0, 0.0, -1.0, 0.0, 0.0, 0.0, 0.0, 1.0)};
        const RenderTransform half = interpolate(a, r, 0.5);
        const double s = std::sqrt(0.5);
        CHECK(half.linear[0][0] == doctest::Approx(s));
        CHECK(half.linear[0][1] == doctest::Approx(s));
        CHECK(half.linear[1][0] == doctest::Approx(-s));
        CHECK(half.linear[2][2] == doctest::Approx(1.0));

        // Cisaillement (parent à échelle non uniforme) : gardé tel quel, pas transformé en rotation et échelle.
        const RenderTransform shear{.position = {}, .linear = core::DMat3(1.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.0, 0.0, 1.0)};
        CHECK(interpolate(shear, shear, 0.5) == shear);
        CHECK(interpolate(shear, shear, 0.25) == shear);

        RenderSnapshot snapshot;
        CHECK(snapshot.alpha(123ms) == 1.0); // non daté : état courant
        snapshot.timed = true;
        snapshot.stepDuration = 10ms;
        snapshot.time = 100ms;
        CHECK(snapshot.alpha(100ms) == 0.0);
        CHECK(snapshot.alpha(104ms) == doctest::Approx(0.4));
        CHECK(snapshot.alpha(50ms) == 0.0);
        CHECK(snapshot.alpha(200ms) == 1.0);
    }

    TEST_CASE("Simulation: events are emitted by systems, the scratch arena is reset at each step")
    {
        Simulation sim({.step = 10ms, .seed = 3, .scratchBytes = 4096});
        std::vector<std::size_t> usedAtStart;
        REQUIRE(sim.addSystem({"scratch", Phase::kUpdate, 0, [&usedAtStart](StepContext &ctx) {
                                   usedAtStart.push_back(ctx.scratch.used());
                                   REQUIRE(ctx.scratch.allocate(1000) != nullptr);
                                   ctx.emit("tick", static_cast<std::int64_t>(ctx.step));
                               }}));
        sim.run(3);
        CHECK(usedAtStart == std::vector<std::size_t>{0, 0, 0});
        CHECK(sim.scratch().capacity() == 4096);
        REQUIRE(sim.events().size() == 3);
        auto events = sim.takeEvents();
        CHECK(sim.events().empty());
        CHECK(events[2] == Event{2, "tick", 2});
        const Snapshot snapshot = sim.capture();
        sim.step();
        CHECK(sim.events().size() == 1);
        sim.restore(snapshot); // événements non repris : abandonnés
        CHECK(sim.events().empty());
    }

    TEST_CASE("SimulationThread: invalid settings are refused")
    {
        Simulation sim({.step = 10ms});
        core::ManualClock clock;
        CHECK((errorOf(SimulationThread::start(sim, clock, {.cadence = Cadence::kStepsPerFrame, .stepsPerFrame = 0})) ==
               core::ErrorCode::kInvalidArgument));
        CHECK((errorOf(SimulationThread::start(sim, clock, {.cadence = Cadence::kStepsPerFrame, .stepsPerFrame = 65})) ==
               core::ErrorCode::kInvalidArgument));
        CHECK_FALSE(SimulationThread::start(sim, clock, {.inputCapacity = 0}));
        CHECK_FALSE(SimulationThread::start(sim, clock, {.eventCapacity = 0}));
        Simulation zero({.step = 0ns});
        CHECK_FALSE(SimulationThread::start(zero, clock));
    }

    TEST_CASE("SimulationThread: same fingerprint and same events as the single-thread loop (N steps per frame)")
    {
        const Reference reference = runSingleThread(42, 10ms, 300);
        REQUIRE_FALSE(reference.events.empty()); // le scénario rebondit, sinon le test ne prouve rien sur les événements
        for (int perFrame : {1, 3, 4})
        {
            CAPTURE(perFrame);
            Simulation sim({.step = 10ms, .seed = 42});
            setupMovers(sim, 12);
            core::ManualClock clock;
            auto thread = SimulationThread::start(sim, clock, {.cadence = Cadence::kStepsPerFrame, .stepsPerFrame = perFrame});
            REQUIRE(thread);
            for (const Input &input : moverInputs())
                REQUIRE((*thread)->pushInput(input));
            std::vector<Event> events;
            const int frames = 300 / perFrame;
            for (int frame = 0; frame < frames; ++frame)
            {
                (*thread)->frame();
                clock.advance(16ms);
                drainEvents(**thread, events);
            }
            (*thread)->sync();
            CHECK((*thread)->stepIndex() == static_cast<std::uint64_t>(frames * perFrame));
            (*thread)->stop();
            CHECK((*thread)->rejectedInputs() == 0);
            drainEvents(**thread, events);
            sim.run(300 - sim.stepIndex()); // 300 n'est pas un multiple de 4 : la fin en mono-thread, après stop
            for (Event &event : sim.takeEvents())
                events.push_back(std::move(event));
            CHECK(sim.stepIndex() == 300);
            CHECK(sim.fingerprint() == reference.fingerprint);
            CHECK(events == reference.events);
        }
    }

    TEST_CASE("SimulationThread: same fingerprint as the single-thread loop at a fixed rate (ManualClock)")
    {
        const Reference reference = runSingleThread(7, 10ms, 300);
        Simulation sim({.step = 10ms, .seed = 7});
        setupMovers(sim, 12);
        core::ManualClock clock;
        auto thread = SimulationThread::start(sim, clock, {.cadence = Cadence::kFixedRate});
        REQUIRE(thread);
        for (const Input &input : moverInputs())
            REQUIRE((*thread)->pushInput(input));
        std::vector<Event> events;
        // 3 s par images de 16,666667 ms : 300 pas de 10 ms (reste 60 ns).
        for (int frame = 0; frame < 180; ++frame)
        {
            clock.advance(16'666'667ns);
            (*thread)->sync();
            drainEvents(**thread, events);
        }
        (*thread)->stop();
        drainEvents(**thread, events);
        CHECK(sim.stepIndex() == 300);
        CHECK(sim.fingerprint() == reference.fingerprint);
        CHECK(events == reference.events);
    }

    TEST_CASE("SimulationThread: 20, 50 and 120 Hz play the expected number of steps (ManualClock)")
    {
        for (double hz : {20.0, 50.0, 120.0})
        {
            CAPTURE(hz);
            const core::Nanoseconds step = stepForRate(hz);
            Simulation sim({.step = step, .maxStepsPerFrame = 8, .seed = 1});
            setupMovers(sim, 4);
            core::ManualClock clock;
            auto thread = SimulationThread::start(sim, clock, {.cadence = Cadence::kFixedRate});
            REQUIRE(thread);
            core::Nanoseconds elapsed{0};
            for (int frame = 0; frame < 120; ++frame) // 2 s à 60 images par seconde
            {
                clock.advance(16'666'667ns);
                elapsed += 16'666'667ns;
                (*thread)->frame(); // sans effet sur le nombre de pas en fréquence fixe
                (*thread)->sync();
                const auto expected = static_cast<std::uint64_t>(elapsed / step);
                REQUIRE((*thread)->stepIndex() == expected);
                const RenderSnapshot &snapshot = (*thread)->acquire();
                CHECK(snapshot.step == expected);
                CHECK(snapshot.timed);
                CHECK(snapshot.stepDuration == step);
                // Le rendu est entre l'état d'avant et l'état courant, à la fraction du FixedStepper.
                const double fraction = static_cast<double>((elapsed - step * static_cast<std::int64_t>(expected)).count()) /
                                        static_cast<double>(step.count());
                if (expected > 0)
                    CHECK(std::abs(snapshot.alpha(clock.now()) - fraction) < 1e-9);
            }
            CHECK((*thread)->stepIndex() == static_cast<std::uint64_t>(elapsed / step));
            CHECK((*thread)->stepIndex() == static_cast<std::uint64_t>(2.0 * hz));
        }
    }

    TEST_CASE("SimulationThread: 1, 2 and 4 steps per frame play N steps per frame, one frame of latency at most")
    {
        for (int perFrame : {1, 2, 4})
        {
            CAPTURE(perFrame);
            Simulation sim({.step = 20ms, .seed = 1});
            setupMovers(sim, 4);
            core::ManualClock clock;
            auto thread = SimulationThread::start(sim, clock, {.cadence = Cadence::kStepsPerFrame, .stepsPerFrame = perFrame});
            REQUIRE(thread);
            for (int frame = 1; frame <= 50; ++frame)
            {
                (*thread)->frame();
                clock.advance(16'666'667ns); // l'horloge ne compte pas dans ce mode
                // frame() a attendu le lot de l'image précédente : au plus une image de retard.
                CHECK((*thread)->stepIndex() >= static_cast<std::uint64_t>((frame - 1) * perFrame));
                CHECK((*thread)->stepIndex() <= static_cast<std::uint64_t>(frame * perFrame));
                const RenderSnapshot &snapshot = (*thread)->acquire();
                CHECK_FALSE(snapshot.timed);
                CHECK(snapshot.alpha(clock.now()) == 1.0);
            }
            (*thread)->sync();
            CHECK((*thread)->stepIndex() == static_cast<std::uint64_t>(50 * perFrame));
            CHECK((*thread)->acquire().step == static_cast<std::uint64_t>(50 * perFrame));
        }
    }

    TEST_CASE("SimulationThread: the snapshot holds transforms, the previous step and the listed render components")
    {
        Simulation sim({.step = 10ms, .seed = 5});
        auto tint = *sim.world().registerComponent<Tint>("Tint");
        const scene::Entity a = sim.world().create();
        const scene::Entity b = sim.world().create();
        REQUIRE(sim.world().setTransform(a, {.position = {1.0, 0.0, 0.0}}));
        REQUIRE(sim.world().setTransform(b, {.position = {0.0, 2.0, 0.0}, .scale = {3.0, 3.0, 3.0}}));
        sim.world().add(a, tint, Tint{1.f, 0.5f, 0.25f, 1.f});
        // Pas d'updateTransforms ici : start() met le cache monde à jour avant le premier instantané.
        REQUIRE(sim.addSystem({"slide", Phase::kUpdate, 0, [](StepContext &ctx) {
                                   ctx.world.view(ctx.world.transformType()).each([&](scene::Entity e, scene::Transform &t) {
                                       t.position.x += 1.0;
                                       ctx.world.markTransformDirty(e);
                                   });
                                   for (const Input &input : ctx.inputs)
                                       if (input.name == "spawn")
                                           CHECK(ctx.world.setTransform(ctx.world.create(), {.position = {-7.0, 0.0, 0.0}}));
                               }}));
        core::ManualClock clock;
        auto thread = SimulationThread::start(sim, clock,
                                              {.cadence = Cadence::kStepsPerFrame, .stepsPerFrame = 2, .renderComponents = {"Tint", "Missing"}});
        REQUIRE(thread);
        const RenderSnapshot &initial = (*thread)->acquire();
        CHECK(initial.step == 0);
        REQUIRE(initial.entities.size() == 2);
        CHECK(initial.previous == initial.transforms);
        CHECK(initial.transforms[0].position.x == 1.0);
        CHECK(initial.transforms[1].position.y == 2.0);

        REQUIRE((*thread)->pushInput({.step = 1, .name = "spawn", .data = {}}));
        (*thread)->frame();
        (*thread)->sync();
        const RenderSnapshot &snapshot = (*thread)->acquire();
        CHECK(snapshot.step == 2);
        REQUIRE(snapshot.entities.size() == 3);
        CHECK(snapshot.entities[0] == a);
        CHECK(snapshot.transforms[0].position.x == 3.0);
        CHECK(snapshot.previous[0].position.x == 2.0);
        CHECK(snapshot.transforms[1].linear[0][0] == 3.0);
        CHECK(snapshot.transformAt(0, 0.5).position.x == doctest::Approx(2.5));
        // Créée au pas 1, après le déplacement de ce pas : nouvelle, sans état d'avant (previous = état courant).
        CHECK(snapshot.transforms[2].position.x == -7.0);
        CHECK(snapshot.previous[2].position.x == -7.0);

        auto tintColumn = snapshot.column("Tint");
        REQUIRE(tintColumn);
        Tint value;
        REQUIRE(snapshot.read(*tintColumn, 0, value));
        CHECK(value.g == 0.5f);
        CHECK_FALSE(snapshot.read(*tintColumn, 1, value)); // b n'a pas de Tint
        NotTint other; // même taille et alignement, autres champs : refusé (pas de lecture d'un type incompatible)
        static_assert(sizeof(NotTint) == sizeof(Tint) && alignof(NotTint) == alignof(Tint));
        CHECK_FALSE(snapshot.read(*tintColumn, 0, other));
        Tint bytes;
        CHECK(snapshot.readBytes(*tintColumn, 0, &bytes, sizeof bytes));
        CHECK(bytes.b == 0.25f);
        CHECK_FALSE(snapshot.readBytes(*tintColumn, 0, &bytes, sizeof bytes - 1));
        auto missing = snapshot.column("Missing");
        REQUIRE(missing);
        CHECK(snapshot.components[*missing].size == 0);
        CHECK_FALSE(snapshot.read(*missing, 0, value));
        CHECK_FALSE(snapshot.column("Other"));

        // Lot suivant (pas 2 et 3) : l'état d'avant est celui du pas 2, pas celui du lot précédent.
        (*thread)->frame();
        (*thread)->sync();
        const RenderSnapshot &next = (*thread)->acquire();
        CHECK(next.step == 4);
        CHECK(next.transforms[2].position.x == -5.0);
        CHECK(next.previous[2].position.x == -6.0);
        CHECK(next.previous[0].position.x == 4.0);
        (*thread)->stop();
        (*thread)->stop(); // idempotent
        CHECK_FALSE((*thread)->running());
    }

    TEST_CASE("SimulationThread: inputs dated in the past are refused and counted, inputs left in the queue go back to the simulation")
    {
        Simulation sim({.step = 10ms, .seed = 5});
        setupMovers(sim, 2);
        core::ManualClock clock;
        auto thread = SimulationThread::start(sim, clock, {.cadence = Cadence::kStepsPerFrame, .stepsPerFrame = 4});
        REQUIRE(thread);
        (*thread)->frame();
        (*thread)->sync();
        REQUIRE((*thread)->stepIndex() == 4);
        REQUIRE((*thread)->pushInput({.step = 1, .name = "impulse", .data = 1.0})); // déjà joué
        (*thread)->sync();
        CHECK((*thread)->rejectedInputs() == 1);
        REQUIRE((*thread)->pushInput({.step = 50, .name = "impulse", .data = 1.0}));
        (*thread)->stop();
        // Livrée au thread avant son arrêt ou rendue par stop : en attente dans la simulation dans les deux cas.
        REQUIRE(sim.pendingInputs().size() == 1);
        CHECK(sim.pendingInputs()[0].step == 50);
    }

    TEST_CASE("SimulationThread: no event is lost when the event queue is full")
    {
        Simulation sim({.step = 1ms, .seed = 9});
        REQUIRE(sim.addSystem({"burst", Phase::kUpdate, 0, [](StepContext &ctx) {
                                   for (int i = 0; i < 5; ++i)
                                       ctx.emit("e", static_cast<std::int64_t>(ctx.step * 5 + static_cast<std::uint64_t>(i)));
                               }}));
        core::ManualClock clock;
        auto thread = SimulationThread::start(sim, clock, {.cadence = Cadence::kStepsPerFrame, .stepsPerFrame = 8, .eventCapacity = 4});
        REQUIRE(thread);
        std::vector<Event> events;
        for (int frame = 0; frame < 10; ++frame)
        {
            (*thread)->frame();
            (*thread)->sync();
            drainEvents(**thread, events); // au plus 4 par tour : le reste attend, en ordre
        }
        // Plus aucun pas : chaque sync() doit encore passer l'arriéré dans la file (thread en marche, rien après stop).
        for (int i = 0; i < 100 && events.size() < 400; ++i)
        {
            (*thread)->sync();
            drainEvents(**thread, events);
        }
        REQUIRE(events.size() == 400);
        for (std::size_t i = 0; i < events.size(); ++i)
            CHECK(events[i].data.get<std::uint64_t>() == i);
        (*thread)->stop();
        CHECK_FALSE((*thread)->popEvent());
    }

    TEST_CASE("SimulationThread: after stop the events still in the backlog are read, inputs are refused")
    {
        Simulation sim({.step = 1ms, .seed = 9});
        REQUIRE(sim.addSystem({"burst", Phase::kUpdate, 0, [](StepContext &ctx) {
                                   for (int i = 0; i < 5; ++i)
                                       ctx.emit("e", static_cast<std::int64_t>(ctx.step * 5 + static_cast<std::uint64_t>(i)));
                               }}));
        core::ManualClock clock;
        auto thread = SimulationThread::start(sim, clock, {.cadence = Cadence::kStepsPerFrame, .stepsPerFrame = 8, .eventCapacity = 4});
        REQUIRE(thread);
        (*thread)->frame();
        (*thread)->stop(); // le lot demandé est joué avant l'arrêt : 8 pas, 40 événements dont 36 en arriéré
        CHECK(sim.stepIndex() == 8);
        std::vector<Event> events;
        drainEvents(**thread, events);
        REQUIRE(events.size() == 40);
        for (std::size_t i = 0; i < events.size(); ++i)
            CHECK(events[i].data.get<std::uint64_t>() == i);
        CHECK_FALSE((*thread)->pushInput({.step = 100, .name = "late", .data = {}}));
        (*thread)->stop();
        CHECK(sim.pendingInputs().empty());
    }

    TEST_CASE("SimulationThread: stop() plays the batch accepted by frame() first (N steps per frame)")
    {
        for (int perFrame : {1, 4, 64})
        {
            CAPTURE(perFrame);
            Simulation sim({.step = 1ms, .seed = 2});
            setupMovers(sim, 3);
            core::ManualClock clock;
            auto thread = SimulationThread::start(sim, clock, {.cadence = Cadence::kStepsPerFrame, .stepsPerFrame = perFrame});
            REQUIRE(thread);
            (*thread)->frame();
            (*thread)->frame(); // attend le premier lot, accepte le second
            (*thread)->stop();
            CHECK(sim.stepIndex() == static_cast<std::uint64_t>(2 * perFrame));
        }
        // Sans demande en attente : l'arrêt ne joue aucun pas.
        Simulation idle({.step = 1ms, .seed = 2});
        core::ManualClock clock;
        auto thread = SimulationThread::start(idle, clock, {.cadence = Cadence::kStepsPerFrame, .stepsPerFrame = 4});
        REQUIRE(thread);
        (*thread)->stop();
        CHECK(idle.stepIndex() == 0);
    }

    TEST_CASE("SimulationThread: a system that throws stops the thread with an error instead of terminating")
    {
        Simulation sim({.step = 1ms, .seed = 2});
        REQUIRE(sim.addSystem({"explode", Phase::kUpdate, 0, [](StepContext &ctx) {
                                   if (ctx.step == 2)
                                       throw std::runtime_error("boom");
                               }}));
        core::ManualClock clock;
        auto thread = SimulationThread::start(sim, clock, {.cadence = Cadence::kStepsPerFrame, .stepsPerFrame = 4});
        REQUIRE(thread);
        CHECK((*thread)->status());
        (*thread)->frame();
        (*thread)->sync();  // ne bloque pas : le thread s'est arrêté
        (*thread)->frame(); // non plus
        auto status = (*thread)->status();
        REQUIRE_FALSE(status);
        CHECK(status.error().message.find("boom") != std::string::npos);
        // Sorti sur l'erreur, pas encore rejoint : ne joue plus, et refuse les entrées qu'il ne livrerait pas.
        CHECK_FALSE((*thread)->running());
        CHECK_FALSE((*thread)->pushInput({10, "late", 1}));
        (*thread)->stop();
        CHECK_FALSE((*thread)->running());
        CHECK(sim.stepIndex() == 2); // le pas 2 a levé : non compté, non rejoué
        CHECK_THROWS_AS(sim.step(), std::runtime_error); // le propriétaire a repris la simulation : même pas, même erreur
    }

    TEST_CASE("SimulationThread: once status() reports the error, the worker is stopped and inputs are refused (revue PR #37)")
    {
        Simulation sim({.step = 1ms, .seed = 3});
        REQUIRE(sim.addSystem({"explode", Phase::kUpdate, 0, [](StepContext &ctx) {
                                   if (ctx.step == 1)
                                       throw std::runtime_error("boom");
                               }}));
        core::ManualClock clock;
        auto thread = SimulationThread::start(sim, clock, {.cadence = Cadence::kStepsPerFrame, .stepsPerFrame = 4});
        REQUIRE(thread);
        (*thread)->frame(); // pas de sync : l'erreur est observée par status() seul, comme le ferait le rendu
        bool failed = false;
        for (int i = 0; i < 2'000'000 && !failed; ++i)
        {
            failed = !(*thread)->status();
            if (!failed)
                std::this_thread::yield();
        }
        REQUIRE(failed);
        // Erreur et arrêt publiés ensemble : aucun intervalle où une entrée serait acceptée sans être livrée.
        CHECK_FALSE((*thread)->running());
        CHECK_FALSE((*thread)->pushInput({10, "late", 1}));
        (*thread)->stop();
    }

    // Test concurrent : la simulation tourne à 1000 Hz (ManualClock avancée par ce thread) ou en N pas par image pendant que ce thread joue le
    // rôle du rendu (lit les instantanés, vide les événements, pousse des entrées) ; aucun GPU (Mesa sous TSan donnerait des
    // faux positifs). Un instantané déchiré, un événement perdu ou une course (TSan) font échouer le test.
    TEST_CASE("SimulationThread: concurrent simulation and snapshot reader (TSan)")
    {
        for (Cadence cadence : {Cadence::kFixedRate, Cadence::kStepsPerFrame})
        {
            CAPTURE(cadenceName(cadence));
            Simulation sim({.step = 1ms, .maxStepsPerFrame = 4, .seed = 11});
            constexpr int kEntities = 64;
            for (int i = 0; i < kEntities; ++i)
                REQUIRE(sim.world().setTransform(sim.world().create(), {.position = {0.0, static_cast<double>(i), 0.0}}));
            sim.world().updateTransforms();
            std::uint64_t applied = 0; // touché seulement par le thread de simulation, lu après stop
            REQUIRE(sim.addSystem({"stamp", Phase::kUpdate, 0, [&applied](StepContext &ctx) {
                                       applied += ctx.inputs.size();
                                       auto *scratch = static_cast<double *>(ctx.scratch.allocate(sizeof(double) * kEntities, alignof(double)));
                                       if (scratch == nullptr)
                                           return;
                                       ctx.world.view(ctx.world.transformType()).each([&](scene::Entity e, scene::Transform &t) {
                                           t.position.x = static_cast<double>(ctx.step + 1); // état = fonction du pas
                                           scratch[e.index % kEntities] = t.position.x;
                                           ctx.world.markTransformDirty(e);
                                       });
                                       ctx.emit("tick", ctx.step);
                                   }}));
            core::ManualClock clock; // horloge pilotée par ce thread (« rendu ») : test sans horloge murale
            auto started = SimulationThread::start(sim, clock, {.cadence = cadence, .stepsPerFrame = 3, .inputCapacity = 64, .eventCapacity = 256});
            REQUIRE(started);
            SimulationThread &thread = **started;

            std::uint64_t nextEvent = 0;
            std::uint64_t lastSeen = 0;
            std::uint64_t pushed = 0;
            bool torn = false;
            bool lostEvent = false;
            // Sans sync() avant chaque lecture : le thread publie pendant que ce thread lit. Un sync() tous les 64 tours garantit
            // la progression (fin du test) même si le thread de simulation manque de temps processeur (TSan).
            for (std::uint64_t turn = 0; thread.stepIndex() < 400; ++turn)
            {
                clock.advance(1ms);
                thread.frame();
                if (turn % 64 == 63)
                    thread.sync();
                const RenderSnapshot &snapshot = thread.acquire();
                if (snapshot.step < lastSeen || snapshot.entities.size() != kEntities)
                    torn = true;
                lastSeen = snapshot.step;
                // Colonnes de même longueur que `entities` avant toute lecture : un instantané incohérent est signalé sans
                // accès hors limites.
                const bool columnsOk = snapshot.transforms.size() == snapshot.entities.size() &&
                                       (snapshot.step == 0 || snapshot.previous.size() == snapshot.entities.size());
                if (!columnsOk)
                    torn = true;
                for (std::size_t i = 0; columnsOk && i < snapshot.entities.size(); ++i)
                {
                    if (snapshot.transforms[i].position.x != static_cast<double>(snapshot.step) ||
                        snapshot.transforms[i].position.y != static_cast<double>(i))
                        torn = true;
                    if (snapshot.step > 0 && snapshot.previous[i].position.x != static_cast<double>(snapshot.step - 1))
                        torn = true;
                }
                const double alpha = snapshot.alpha(clock.now());
                if (alpha < 0.0 || alpha > 1.0)
                    torn = true;
                while (auto event = thread.popEvent())
                {
                    if (event->step != nextEvent || event->data.get<std::uint64_t>() != nextEvent)
                        lostEvent = true;
                    ++nextEvent;
                }
                if (thread.pushInput({.step = thread.stepIndex() + 2, .name = "noop", .data = {}}))
                    ++pushed;
                std::this_thread::yield();
            }
            REQUIRE(thread.stepIndex() >= 400);
            thread.stop();
            while (auto event = thread.popEvent())
            {
                if (event->step != nextEvent)
                    lostEvent = true;
                ++nextEvent;
            }
            CHECK_FALSE(torn);
            CHECK_FALSE(lostEvent);
            CHECK(nextEvent == sim.stepIndex());
            // Toute entrée poussée est livrée, refusée (datée trop tôt) ou encore en attente.
            CHECK(applied + thread.rejectedInputs() + sim.pendingInputs().size() == pushed);
            CHECK(sim.inputLog().size() == applied);
        }
    }
}
