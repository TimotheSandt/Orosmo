// SIM-01, extension (A79 lot L4) : thread de simulation. Voir include/orosmo/sim/simulation_thread.hpp.
#include <orosmo/sim/simulation_thread.hpp>

#include <orosmo/core/log.hpp>
#include <orosmo/scene/transform.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <limits>
#include <system_error>
#include <utility>

namespace orosmo::sim
{

using core::ErrorCode;
using core::Json;
using core::Nanoseconds;
using core::Result;

// ── Réglages ──────────────────────────────────────────────────────────────────

std::string_view cadenceName(Cadence cadence) noexcept { return cadence == Cadence::kStepsPerFrame ? "frame" : "rate"; }

std::optional<Cadence> parseCadence(std::string_view name) noexcept
{
    if (name == "frame")
        return Cadence::kStepsPerFrame;
    if (name == "rate")
        return Cadence::kFixedRate;
    return std::nullopt;
}

Nanoseconds stepForRate(double hz) noexcept
{
    if (!(hz > 0.0))
        return Nanoseconds{0};
    // Arrondi au plus proche sans fonction de la bibliothèque C (règle 9, `make check`) : + 0,5 puis troncature. Une durée qui
    // ne tient pas dans le compteur (fréquence infime) rend 0 au lieu d'une conversion indéfinie.
    const double ns = 1.0e9 / hz + 0.5;
    if (!(ns < static_cast<double>(std::numeric_limits<Nanoseconds::rep>::max())))
        return Nanoseconds{0};
    return Nanoseconds(static_cast<Nanoseconds::rep>(ns));
}

void declareSimulationConfig(core::Config &config)
{
    config.declare("sim.thread", false, "run the simulation on a dedicated thread (default: single-thread)");
    config.declare("sim.cadence", "rate", "thread cadence: \"rate\" (fixed frequency) or \"frame\" (N steps per frame)");
    config.declare("sim.rateHz", 60.0, "simulation steps per second (]0, 10000])");
    config.declare("sim.stepsPerFrame", 1, "steps per rendered frame with the \"frame\" cadence (1 to 64)");
}

Result<SimulationConfig> readSimulationConfig(const core::Config &config)
{
    auto threaded = config.get<bool>("sim.thread");
    auto cadence = config.get<std::string>("sim.cadence");
    auto rate = config.get<double>("sim.rateHz");
    auto perFrame = config.get<int>("sim.stepsPerFrame");
    if (!threaded)
        return std::unexpected(threaded.error());
    if (!cadence)
        return std::unexpected(cadence.error());
    if (!rate)
        return std::unexpected(rate.error());
    if (!perFrame)
        return std::unexpected(perFrame.error());
    const auto parsed = parseCadence(*cadence);
    if (!parsed)
        return core::makeError(ErrorCode::kInvalidArgument, "sim.cadence: expected \"rate\" or \"frame\", got \"" + *cadence + "\"");
    if (!(*rate > 0.0) || *rate > 10000.0)
        return core::makeError(ErrorCode::kInvalidArgument, "sim.rateHz: expected a frequency in ]0, 10000]");
    if (*perFrame < 1 || *perFrame > ThreadSettings::kMaxStepsPerFrame)
        return core::makeError(ErrorCode::kInvalidArgument, "sim.stepsPerFrame: expected 1 to 64");
    SimulationConfig out;
    out.threaded = *threaded;
    out.step = stepForRate(*rate);
    if (out.step.count() <= 0)
        return core::makeError(ErrorCode::kInvalidArgument, "sim.rateHz: step duration out of range");
    out.thread.cadence = *parsed;
    out.thread.stepsPerFrame = *perFrame;
    return out;
}

// ── Instantané ────────────────────────────────────────────────────────────────

namespace
{
RenderTransform lerpTerms(const RenderTransform &a, const RenderTransform &b, double alpha)
{
    return {a.position + (b.position - a.position) * alpha, a.linear + (b.linear - a.linear) * alpha};
}

// Échelle par colonne et rotation pure ; faux si une colonne est nulle ou si la matrice contient une réflexion.
bool decompose(const core::DMat3 &linear, core::DVec3 &scale, core::DQuat &rotation)
{
    for (int c = 0; c < 3; ++c)
    {
        scale[c] = glm::length(linear[c]);
        if (!(scale[c] > 1e-12))
            return false;
    }
    if (glm::determinant(linear) <= 0.0)
        return false;
    const core::DMat3 pure(linear[0] / scale[0], linear[1] / scale[1], linear[2] / scale[2]);
    // Cisaillement (parent à échelle non uniforme, A79 L2) : colonnes non orthogonales, que quaternion et échelles ne rendent pas.
    constexpr double kOrthogonal = 1e-9;
    if (glm::abs(glm::dot(pure[0], pure[1])) > kOrthogonal || glm::abs(glm::dot(pure[0], pure[2])) > kOrthogonal ||
        glm::abs(glm::dot(pure[1], pure[2])) > kOrthogonal)
        return false;
    rotation = glm::quat_cast(pure);
    return true;
}
} // namespace

RenderTransform interpolate(const RenderTransform &a, const RenderTransform &b, double alpha)
{
    if (alpha <= 0.0)
        return a;
    if (alpha >= 1.0)
        return b;
    core::DVec3 scaleA, scaleB;
    core::DQuat rotationA, rotationB;
    if (!decompose(a.linear, scaleA, rotationA) || !decompose(b.linear, scaleB, rotationB))
        return lerpTerms(a, b, alpha);
    if (glm::dot(rotationA, rotationB) < 0.0)
        rotationB = -rotationB;
    const core::DQuat rotation = glm::normalize(rotationA * (1.0 - alpha) + rotationB * alpha);
    const core::DVec3 scale = scaleA + (scaleB - scaleA) * alpha;
    core::DMat3 linear = glm::mat3_cast(rotation);
    for (int c = 0; c < 3; ++c)
        linear[c] *= scale[c];
    return {a.position + (b.position - a.position) * alpha, linear};
}

core::Mat4 relativeMatrix(const RenderTransform &transform, const core::WorldPosition &origin)
{
    scene::Transform t;
    t.worldPosition = transform.position;
    t.worldLinear = transform.linear;
    return scene::relativeMatrix(t, origin);
}

double RenderSnapshot::alpha(Nanoseconds now) const noexcept
{
    if (!timed || stepDuration.count() <= 0)
        return 1.0;
    const double a = static_cast<double>((now - time).count()) / static_cast<double>(stepDuration.count());
    return std::clamp(a, 0.0, 1.0);
}

std::optional<std::size_t> RenderSnapshot::column(std::string_view name) const noexcept
{
    for (std::size_t i = 0; i < components.size(); ++i)
        if (components[i].name == name)
            return i;
    return std::nullopt;
}

// ── Thread ────────────────────────────────────────────────────────────────────

Result<std::unique_ptr<SimulationThread>> SimulationThread::start(Simulation &simulation, const core::IClock &clock, ThreadSettings settings)
{
    if (settings.stepsPerFrame < 1 || settings.stepsPerFrame > ThreadSettings::kMaxStepsPerFrame)
        return core::makeError(ErrorCode::kInvalidArgument, "SimulationThread: stepsPerFrame must be 1 to 64");
    if (settings.inputCapacity == 0 || settings.eventCapacity == 0)
        return core::makeError(ErrorCode::kInvalidArgument, "SimulationThread: queue capacities must not be zero");
    if (simulation.settings().step.count() <= 0)
        return core::makeError(ErrorCode::kInvalidArgument, "SimulationThread: the simulation step must be positive");
    std::unique_ptr<SimulationThread> thread(new SimulationThread(simulation, clock, std::move(settings)));
    // Cache monde à jour (un `setTransform` du propriétaire n'est appliqué qu'au prochain `updateTransforms`).
    simulation.world().updateTransforms();
    // Premier instantané et départ de l'horloge sur ce thread, avant celui de simulation (sa création synchronise).
    thread->captureState(thread->currentState_);
    thread->previousState_ = thread->currentState_;
    thread->lastSample_ = clock.now();
    thread->publish(thread->lastSample_);
    thread->stepIndex_.store(simulation.stepIndex(), std::memory_order_release);
    try
    {
        thread->thread_ = std::thread([raw = thread.get()] { raw->run(); });
    }
    catch (const std::system_error &e) // plus de thread disponible : l'appelant reste en mono-thread
    {
        return core::makeError(ErrorCode::kBusy, std::string("SimulationThread: cannot create the thread: ") + e.what());
    }
    return thread;
}

SimulationThread::SimulationThread(Simulation &simulation, const core::IClock &clock, ThreadSettings settings)
    : simulation_(simulation), clock_(clock), settings_(std::move(settings)),
      stepper_(simulation.settings().step, simulation.settings().maxStepsPerFrame),
      perFrame_(settings_.cadence == Cadence::kStepsPerFrame), inputs_(settings_.inputCapacity), events_(settings_.eventCapacity)
{
}

SimulationThread::~SimulationThread() { stop(); }

void SimulationThread::stop()
{
    if (!thread_.joinable())
        return;
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    work_.notify_all();
    thread_.join();
    // Le thread est rejoint : ses données sont à nous. Entrées encore en file rendues à la simulation, événements en
    // arriéré gardés pour popEvent.
    drainInputs();
    flushEvents();
}

void SimulationThread::frame()
{
    std::unique_lock lock(mutex_);
    if (perFrame_)
    {
        done_.wait(lock, [this] { return completed_ >= requested_ || exited_; });
        requested_ += static_cast<std::uint64_t>(settings_.stepsPerFrame);
    }
    else
        wake_ = true;
    lock.unlock();
    work_.notify_one();
}

void SimulationThread::sync()
{
    std::unique_lock lock(mutex_);
    if (exited_ || !thread_.joinable())
        return;
    const std::uint64_t ticket = ++syncRequested_;
    wake_ = true;
    work_.notify_one();
    done_.wait(lock, [&] { return syncDone_ >= ticket || exited_; });
}

bool SimulationThread::pushInput(Input input)
{
    if (!thread_.joinable()) // arrêté : aucun thread ne la livrerait
        return false;
    return inputs_.tryPush(std::move(input));
}

core::Result<> SimulationThread::status() const
{
    std::lock_guard lock(mutex_);
    if (failure_)
        return std::unexpected(*failure_);
    return {};
}

std::optional<Event> SimulationThread::popEvent()
{
    if (auto event = events_.tryPop())
        return event;
    if (!thread_.joinable() && !backlog_.empty()) // arrêté : l'arriéré nous appartient
    {
        Event event = std::move(backlog_.front());
        backlog_.pop_front();
        return event;
    }
    return std::nullopt;
}

// ── Corps du thread ───────────────────────────────────────────────────────────

void SimulationThread::drainInputs()
{
    while (auto input = inputs_.tryPop())
    {
        const std::uint64_t step = input->step;
        if (!simulation_.queueInput(std::move(*input)))
        {
            rejectedInputs_.fetch_add(1, std::memory_order_relaxed);
            LOG_WARNING("SimulationThread: input for step ", step, " arrived after that step was played (now ", simulation_.stepIndex(),
                        "); date inputs at stepIndex() + latency");
        }
    }
}

void SimulationThread::playSteps(std::uint64_t count)
{
    // L'instantané n'a besoin que des deux derniers états : capture après l'avant-dernier pas (état d'avant) et le dernier.
    for (std::uint64_t i = 0; i < count; ++i)
    {
        playStep();
        if (i + 2 == count)
            captureState(previousState_);
        else if (i + 1 == count)
        {
            if (count == 1)
                std::swap(previousState_, currentState_);
            captureState(currentState_);
        }
    }
}

void SimulationThread::playStep()
{
    drainInputs();
    simulation_.step();
    stepIndex_.store(simulation_.stepIndex(), std::memory_order_release);
    for (Event &event : simulation_.takeEvents())
        backlog_.push_back(std::move(event));
    flushEvents();
}

void SimulationThread::flushEvents()
{
    while (!backlog_.empty() && events_.tryPush(std::move(backlog_.front())))
        backlog_.pop_front();
}

void SimulationThread::captureState(State &out)
{
    scene::World &world = simulation_.world();
    out.step = simulation_.stepIndex();
    out.entities.clear();
    out.transforms.clear();
    world.view(world.transformType()).each([&](scene::Entity e, scene::Transform &t) {
        out.entities.push_back(e);
        out.transforms.push_back({t.worldPosition, t.worldLinear});
    });
}

void SimulationThread::publish(Nanoseconds time)
{
    RenderSnapshot &s = snapshots_.back();
    const State &current = currentState_;
    const State &before = previousState_;
    s.step = current.step;
    s.stepDuration = simulation_.settings().step;
    s.time = time;
    s.timed = !perFrame_;
    s.entities = current.entities;
    s.transforms = current.transforms;

    // Même entité à l'état d'avant (indice et génération), sinon l'état courant (entité nouvelle).
    s.previous = current.transforms;
    if (before.step + 1 == current.step)
    {
        std::uint32_t maxIndex = 0;
        for (const scene::Entity &e : before.entities)
            maxIndex = std::max(maxIndex, e.index + 1u);
        if (slots_.size() < maxIndex)
            slots_.resize(maxIndex);
        for (std::size_t i = 0; i < before.entities.size(); ++i)
            slots_[before.entities[i].index] = {before.entities[i].generation, static_cast<std::uint32_t>(i + 1)};
        for (std::size_t i = 0; i < current.entities.size(); ++i)
        {
            const scene::Entity &e = current.entities[i];
            if (e.index < maxIndex && slots_[e.index].position != 0u && slots_[e.index].generation == e.generation)
                s.previous[i] = before.transforms[slots_[e.index].position - 1u];
        }
        for (const scene::Entity &e : before.entities)
            slots_[e.index].position = 0u;
    }

    // Composants listés : copie octet par octet (données trivialement copiables du monde).
    scene::World &world = simulation_.world();
    s.components.resize(settings_.renderComponents.size());
    for (std::size_t c = 0; c < settings_.renderComponents.size(); ++c)
    {
        RenderComponentColumn &column = s.components[c];
        column.name = settings_.renderComponents[c];
        column.size = 0;
        column.desc = {};
        column.present.assign(current.entities.size(), 0u);
        column.bytes.clear();
        auto id = world.componentId(column.name);
        const scene::ComponentDesc *desc = id ? world.componentDesc(*id) : nullptr;
        if (desc == nullptr)
            continue;
        column.size = desc->size;
        column.desc.name = desc->name;
        column.desc.size = desc->size;
        column.desc.alignment = desc->alignment;
        column.desc.version = desc->version;
        column.desc.fields = desc->fields;
        column.bytes.assign(current.entities.size() * column.size, std::byte{0});
        for (std::size_t i = 0; i < current.entities.size(); ++i)
            if (const void *data = std::as_const(world).get(current.entities[i], *id))
            {
                std::memcpy(column.bytes.data() + i * column.size, data, column.size);
                column.present[i] = 1u;
            }
    }
    snapshots_.publish();
}

void SimulationThread::run()
{
    // Un système qui lève ne termine pas le processus : l'erreur est gardée pour `status()`, les attentes sont réveillées et
    // `stop()` rejoint le thread. Le pas interrompu n'est pas rejoué (le monde a pu changer).
    try
    {
        loop();
    }
    catch (const std::exception &e)
    {
        fail(std::string("SimulationThread: a system threw: ") + e.what());
    }
    catch (...)
    {
        fail("SimulationThread: a system threw an unknown exception");
    }
    {
        std::lock_guard lock(mutex_);
        exited_ = true;
    }
    done_.notify_all();
}

void SimulationThread::fail(std::string message)
{
    LOG(::orosmo::core::L_ERROR, message);
    std::lock_guard lock(mutex_);
    failure_ = core::makeError(ErrorCode::kUnknown, std::move(message)).error();
}

void SimulationThread::loop()
{
    const Nanoseconds step = simulation_.settings().step;
    Nanoseconds wait = step;
    std::uint64_t completed = 0;
    for (;;)
    {
        std::uint64_t target = 0;
        std::uint64_t ticket = 0;
        {
            std::unique_lock lock(mutex_);
            const auto ready = [this, completed] { return stop_ || wake_ || (perFrame_ && requested_ > completed); };
            if (perFrame_)
                work_.wait(lock, ready);
            else
                work_.wait_for(lock, wait, ready);
            if (stop_ && !(perFrame_ && requested_ > completed))
                break; // un lot accepté par frame() est joué avant l'arrêt
            wake_ = false;
            target = requested_;
            ticket = syncRequested_;
        }

        drainInputs();
        if (perFrame_)
        {
            const std::uint64_t count = target - completed;
            playSteps(count);
            completed = target;
            if (count > 0)
                publish(clock_.now());
        }
        else
        {
            const Nanoseconds now = clock_.now();
            const int count = stepper_.advance(now - lastSample_);
            lastSample_ = now;
            playSteps(static_cast<std::uint64_t>(count));
            completed += static_cast<std::uint64_t>(count);
            // Instant où l'état courant a commencé : maintenant moins la fraction déjà écoulée du pas suivant.
            // alpha = accumulateur / pas : produit arrondi au plus proche, l'accumulateur entier est retrouvé exactement.
            const auto elapsed = static_cast<Nanoseconds::rep>(stepper_.alpha() * static_cast<double>(step.count()) + 0.5);
            if (count > 0)
                publish(now - Nanoseconds(elapsed));
            // Prochain pas dû à `now + step - elapsed` : le temps passé à jouer et publier est retranché de l'attente.
            const Nanoseconds spent = clock_.now() - now;
            wait = std::clamp(step - Nanoseconds(elapsed) - spent, Nanoseconds{0}, step);
        }
        flushEvents(); // l'arriéré passe dans la file même sans nouveau pas (rendu qui a vidé la file puis sync())

        {
            std::lock_guard lock(mutex_);
            completed_ = completed;
            syncDone_ = ticket;
        }
        done_.notify_all();
    }
}

} // namespace orosmo::sim
