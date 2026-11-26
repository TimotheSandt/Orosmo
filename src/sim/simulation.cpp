// SIM-01 (A79 lot L3) : boucle de simulation à pas fixe. Voir include/orosmo/sim/simulation.hpp.
#include <orosmo/sim/simulation.hpp>

#include <orosmo/core/hash.hpp>
#include <orosmo/core/log.hpp>
#include <orosmo/scene/fingerprint.hpp>

#include <algorithm>
#include <chrono>
#include <tuple>
#include <utility>

namespace orosmo::sim
{

using core::ErrorCode;
using core::Json;
using core::Result;

namespace
{
constexpr int kStateVersion = 1;
constexpr int kReplayVersion = 1;

Json inputToJson(const Input &input) { return Json{{"step", input.step}, {"name", input.name}, {"data", input.data}}; }

Result<Input> inputFromJson(const Json &j, const std::string &path)
{
    if (!j.is_object() || !j.contains("step") || !j["step"].is_number_unsigned() || !j.contains("name") || !j["name"].is_string())
        return core::makeError(ErrorCode::kParse, path + ": expected {step: unsigned, name: string, data}");
    Input input;
    input.step = j["step"].get<std::uint64_t>();
    input.name = j["name"].get<std::string>();
    if (j.contains("data"))
        input.data = j["data"];
    return input;
}

Result<std::vector<Input>> inputsFromJson(const Json &j, const std::string &path)
{
    if (!j.is_array())
        return core::makeError(ErrorCode::kParse, path + ": expected array");
    std::vector<Input> out;
    for (std::size_t i = 0; i < j.size(); ++i)
    {
        auto input = inputFromJson(j[i], path + "[" + std::to_string(i) + "]");
        if (!input)
            return std::unexpected(input.error());
        out.push_back(std::move(*input));
    }
    return out;
}

Result<std::uint64_t> readU64(const Json &j, const char *key, const std::string &path)
{
    if (!j.contains(key) || !j[key].is_number_unsigned())
        return core::makeError(ErrorCode::kParse, path + "." + key + ": expected unsigned integer");
    return j[key].get<std::uint64_t>();
}
} // namespace

std::string_view phaseName(Phase phase) noexcept
{
    switch (phase)
    {
    case Phase::kInput: return "input";
    case Phase::kUpdate: return "update";
    case Phase::kPostUpdate: return "postUpdate";
    }
    return "update";
}

std::optional<Phase> parsePhase(std::string_view name) noexcept
{
    for (Phase p : {Phase::kInput, Phase::kUpdate, Phase::kPostUpdate})
        if (phaseName(p) == name)
            return p;
    return std::nullopt;
}

Json replayToJson(const Replay &replay)
{
    Json inputs = Json::array();
    for (const Input &input : replay.inputs)
        inputs.push_back(inputToJson(input));
    Json data{{"seed", replay.seed}, {"firstStep", replay.firstStep}, {"lastStep", replay.lastStep}, {"inputs", std::move(inputs)}};
    return Json{{"version", kReplayVersion}, {"data", std::move(data)}};
}

Result<Replay> replayFromJson(const Json &json)
{
    if (!json.is_object() || !json.contains("version") || !json["version"].is_number_integer() || !json.contains("data") ||
        !json["data"].is_object())
        return core::makeError(ErrorCode::kParse, "replay must be {\"version\": int, \"data\": {...}}");
    if (json["version"].get<std::int64_t>() != kReplayVersion)
        return core::makeError(ErrorCode::kVersionMismatch, "replay: unsupported version");
    const Json &data = json["data"];
    Replay replay;
    auto seed = readU64(data, "seed", "replay");
    auto first = readU64(data, "firstStep", "replay");
    auto last = readU64(data, "lastStep", "replay");
    if (!seed || !first || !last)
        return std::unexpected(!seed ? seed.error() : !first ? first.error() : last.error());
    replay.seed = *seed;
    replay.firstStep = *first;
    replay.lastStep = *last;
    if (!data.contains("inputs"))
        return core::makeError(ErrorCode::kParse, "replay.inputs: missing");
    auto inputs = inputsFromJson(data["inputs"], "replay.inputs");
    if (!inputs)
        return std::unexpected(inputs.error());
    replay.inputs = std::move(*inputs);
    return replay;
}

Simulation::Simulation(SimulationSettings settings)
    : settings_(settings), dt_(std::chrono::duration<double>(settings.step).count()), stepper_(settings.step, settings.maxStepsPerFrame),
      random_(settings.seed, settings.stream)
{
}

// ── Systèmes ──────────────────────────────────────────────────────────────────

Result<> Simulation::addSystem(SystemDesc system)
{
    if (stepping_)
        return core::makeError(ErrorCode::kInvalidArgument, "addSystem: not allowed during a step");
    if (system.name.empty() || !system.run)
        return core::makeError(ErrorCode::kInvalidArgument, "addSystem: a system needs a name and a function");
    if (hasSystem(system.name))
        return core::makeError(ErrorCode::kAlreadyExists, "addSystem: a system named " + system.name + " already exists");
    System s{std::move(system.name), system.phase, system.order, std::move(system.run)};
    const auto key = [](const System &x) { return std::tie(x.phase, x.order, x.name); };
    auto at = std::upper_bound(systems_.begin(), systems_.end(), s, [&](const System &a, const System &b) { return key(a) < key(b); });
    systems_.insert(at, std::move(s));
    return {};
}

bool Simulation::removeSystem(std::string_view name)
{
    if (stepping_)
        return false;
    auto it = std::find_if(systems_.begin(), systems_.end(), [&](const System &s) { return s.name == name; });
    if (it == systems_.end())
        return false;
    systems_.erase(it);
    return true;
}

bool Simulation::hasSystem(std::string_view name) const noexcept
{
    return std::any_of(systems_.begin(), systems_.end(), [&](const System &s) { return s.name == name; });
}

std::vector<std::string> Simulation::systemOrder() const
{
    std::vector<std::string> out;
    out.reserve(systems_.size());
    for (const System &s : systems_)
        out.push_back(s.name);
    return out;
}

// ── Entrées ───────────────────────────────────────────────────────────────────

Result<> Simulation::queueInput(Input input)
{
    if (input.step < step_)
        return core::makeError(ErrorCode::kInvalidArgument,
                               "queueInput: step " + std::to_string(input.step) + " is already played (now " + std::to_string(step_) + ")");
    auto at = std::upper_bound(pending_.begin(), pending_.end(), input.step, [](std::uint64_t s, const Input &x) { return s < x.step; });
    pending_.insert(at, std::move(input));
    return {};
}

// ── Pas ───────────────────────────────────────────────────────────────────────

void Simulation::step()
{
    current_.clear();
    std::size_t due = 0;
    while (due < pending_.size() && pending_[due].step == step_)
        ++due;
    current_.assign(std::make_move_iterator(pending_.begin()), std::make_move_iterator(pending_.begin() + static_cast<std::ptrdiff_t>(due)));
    pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(due));

    StepContext context{world_, random_, step_, dt_, current_};
    stepping_ = true;
    for (System &system : systems_)
        system.run(context);
    stepping_ = false;
    world_.updateTransforms();

    log_.insert(log_.end(), current_.begin(), current_.end());
    ++step_;
}

void Simulation::run(std::uint64_t steps)
{
    for (std::uint64_t i = 0; i < steps; ++i)
        step();
}

int Simulation::advance(core::Nanoseconds frameDelta)
{
    const int steps = stepper_.advance(frameDelta);
    for (int i = 0; i < steps; ++i)
        step();
    return steps;
}

// ── Capture et restauration ───────────────────────────────────────────────────

Snapshot Simulation::capture() const { return Snapshot{world_, random_, step_, pending_}; }

void Simulation::restore(const Snapshot &snapshot)
{
    world_ = snapshot.world;
    random_ = snapshot.random;
    step_ = snapshot.step;
    pending_ = snapshot.pending;
    std::erase_if(log_, [&](const Input &input) { return input.step >= step_; });
    stepper_ = core::FixedStepper(settings_.step, settings_.maxStepsPerFrame);
}

Json Simulation::saveState() const
{
    Json pending = Json::array();
    for (const Input &input : pending_)
        pending.push_back(inputToJson(input));
    Json data{{"step", step_},
              {"random", Json{{"state", random_.state()}, {"increment", random_.increment()}}},
              {"pending", std::move(pending)},
              {"scene", scene::saveScene(world_)}};
    return Json{{"version", kStateVersion}, {"data", std::move(data)}};
}

Result<scene::SceneLoadReport> Simulation::loadState(const Json &state, const scene::SceneMigrations &migrations)
{
    if (!state.is_object() || !state.contains("version") || !state["version"].is_number_integer() || !state.contains("data") ||
        !state["data"].is_object())
        return core::makeError(ErrorCode::kParse, "simulation state must be {\"version\": int, \"data\": {...}}");
    if (state["version"].get<std::int64_t>() != kStateVersion)
        return core::makeError(ErrorCode::kVersionMismatch, "simulation state: unsupported version");
    const Json &data = state["data"];
    auto step = readU64(data, "step", "state");
    if (!step)
        return std::unexpected(step.error());
    if (!data.contains("random") || !data["random"].is_object())
        return core::makeError(ErrorCode::kParse, "state.random: expected object");
    auto rngState = readU64(data["random"], "state", "state.random");
    auto rngIncrement = readU64(data["random"], "increment", "state.random");
    if (!rngState || !rngIncrement)
        return std::unexpected(!rngState ? rngState.error() : rngIncrement.error());
    if (!data.contains("pending") || !data.contains("scene"))
        return core::makeError(ErrorCode::kParse, "state: missing pending or scene");
    auto pending = inputsFromJson(data["pending"], "state.pending");
    if (!pending)
        return std::unexpected(pending.error());
    scene::World world = world_;
    auto report = scene::loadScene(world, data["scene"], migrations);
    if (!report)
        return std::unexpected(report.error());
    std::stable_sort(pending->begin(), pending->end(), [](const Input &a, const Input &b) { return a.step < b.step; });
    restore(Snapshot{std::move(world), core::Random::fromState(*rngState, *rngIncrement), *step, std::move(*pending)});
    return report;
}

// ── Rejeu et empreinte ────────────────────────────────────────────────────────

Replay Simulation::recording(std::uint64_t firstStep) const
{
    Replay replay{settings_.seed, firstStep, step_, {}};
    for (const Input &input : log_)
        if (input.step >= firstStep)
            replay.inputs.push_back(input);
    return replay;
}

Result<> Simulation::play(const Replay &replay)
{
    if (replay.seed != settings_.seed)
        return core::makeError(ErrorCode::kInvalidArgument, "play: the replay was recorded with another seed");
    if (replay.firstStep != step_)
        return core::makeError(ErrorCode::kInvalidArgument, "play: the simulation is at step " + std::to_string(step_) +
                                                                 ", the replay starts at step " + std::to_string(replay.firstStep));
    if (replay.lastStep < replay.firstStep)
        return core::makeError(ErrorCode::kInvalidArgument, "play: lastStep is before firstStep");
    for (const Input &input : replay.inputs)
        if (auto r = queueInput(input); !r)
            return r;
    run(replay.lastStep - replay.firstStep);
    return {};
}

std::uint64_t Simulation::fingerprint() const
{
    core::Fnv1a hash;
    scene::hashWorld(hash, world_);
    hash.u64(random_.state());
    hash.u64(random_.increment());
    hash.u64(step_);
    hash.u64(pending_.size());
    for (const Input &input : pending_)
    {
        hash.u64(input.step);
        hash.string(input.name);
        hash.string(input.data.dump());
    }
    return hash.value();
}

} // namespace orosmo::sim
