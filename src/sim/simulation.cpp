// SIM-01 (A79 lot L3) : boucle de simulation à pas fixe. Voir include/orosmo/sim/simulation.hpp.
#include <orosmo/sim/simulation.hpp>

#include <orosmo/core/hash.hpp>
#include <orosmo/core/log.hpp>
#include <orosmo/scene/fingerprint.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <string_view>
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

// Entier positif ou nul, quelle que soit sa représentation : un document construit en C++ (`{"step", 0}`) stocke un entier
// signé, le même document relu depuis le texte un entier non signé. Fractions et négatifs refusés.
bool isNonNegativeInteger(const Json &j)
{
    return j.is_number_unsigned() || (j.is_number_integer() && j.get<std::int64_t>() >= 0);
}

// Suite d'octets UTF-8 bien formée (pas de surrogates, forme la plus courte) : `dump()` lève sur une chaîne invalide.
bool isValidUtf8(std::string_view text)
{
    std::size_t i = 0;
    while (i < text.size())
    {
        const auto c = static_cast<unsigned char>(text[i]);
        std::size_t extra = 0;
        std::uint32_t cp = 0;
        if (c < 0x80u)
        {
            ++i;
            continue;
        }
        if (c >= 0xC2u && c <= 0xDFu)
        {
            extra = 1;
            cp = c & 0x1Fu;
        }
        else if (c >= 0xE0u && c <= 0xEFu)
        {
            extra = 2;
            cp = c & 0x0Fu;
        }
        else if (c >= 0xF0u && c <= 0xF4u)
        {
            extra = 3;
            cp = c & 0x07u;
        }
        else
            return false;
        if (i + extra >= text.size())
            return false;
        for (std::size_t k = 1; k <= extra; ++k)
        {
            const auto cc = static_cast<unsigned char>(text[i + k]);
            if ((cc & 0xC0u) != 0x80u)
                return false;
            cp = (cp << 6u) | (cc & 0x3Fu);
        }
        if ((extra == 2 && (cp < 0x800u || (cp >= 0xD800u && cp <= 0xDFFFu))) || (extra == 3 && (cp < 0x10000u || cp > 0x10FFFFu)))
            return false;
        i += extra + 1;
    }
    return true;
}

// Charge représentable sans perte en JSON texte : aucun réel infini ni NaN (écrits `null` par `dump()`, ce qui changerait le
// type reçu par le système après une sauvegarde et confondrait deux charges dans l'empreinte), aucune valeur binaire (écrite
// comme un objet {bytes, subtype} : relue objet), chaînes et clés en UTF-8 valide (`dump()` lèverait).
const char *payloadProblem(const Json &j)
{
    if (j.is_discarded()) // valeur « rejetée » d'un filtre de lecture : `dump()` écrit `<discarded>`, illisible
        return "a discarded value";
    if (j.is_number_float())
        return std::isfinite(j.get<double>()) ? nullptr : "an infinite or NaN number";
    if (j.is_binary())
        return "a binary value";
    if (j.is_string())
        return isValidUtf8(j.get_ref<const std::string &>()) ? nullptr : "a string that is not valid UTF-8";
    if (j.is_object())
    {
        for (auto it = j.begin(); it != j.end(); ++it)
        {
            if (!isValidUtf8(it.key()))
                return "an object key that is not valid UTF-8";
            if (const char *problem = payloadProblem(it.value()))
                return problem;
        }
        return nullptr;
    }
    if (j.is_array())
        for (const Json &x : j)
            if (const char *problem = payloadProblem(x))
                return problem;
    return nullptr;
}

Result<> checkPayload(const Input &input, const char *where)
{
    if (!isValidUtf8(input.name))
        return core::makeError(ErrorCode::kInvalidArgument, std::string(where) + ": input name at step " + std::to_string(input.step) +
                                                                " is not valid UTF-8");
    if (const char *problem = payloadProblem(input.data))
        return core::makeError(ErrorCode::kInvalidArgument, std::string(where) + ": input " + input.name + " at step " +
                                                                std::to_string(input.step) + " has " + problem);
    return {};
}

Result<Input> inputFromJson(const Json &j, const std::string &path)
{
    if (!j.is_object() || !j.contains("step") || !isNonNegativeInteger(j["step"]) || !j.contains("name") || !j["name"].is_string())
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

// Tri stable par pas (ordre d'origine gardé à pas égal), sans insertions quadratiques ni std::stable_sort, dont le tampon
// temporaire de libstdc++ 13 (`get_temporary_buffer`) est signalé par ASan (alloc-dealloc-mismatch) : tri des indices par
// (pas, position d'origine), puis un seul déplacement par entrée.
void sortByStep(std::vector<Input> &inputs)
{
    std::vector<std::size_t> order(inputs.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::sort(order.begin(), order.end(),
              [&](std::size_t a, std::size_t b) { return std::tie(inputs[a].step, a) < std::tie(inputs[b].step, b); });
    std::vector<Input> sorted;
    sorted.reserve(inputs.size());
    for (std::size_t index : order)
        sorted.push_back(std::move(inputs[index]));
    inputs = std::move(sorted);
}

Result<std::uint64_t> readU64(const Json &j, const char *key, const std::string &path)
{
    if (!j.contains(key) || !isNonNegativeInteger(j[key]))
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
    Json data{{"seed", replay.seed},
              {"firstStep", replay.firstStep},
              {"lastStep", replay.lastStep},
              {"stepNanoseconds", replay.stepNanoseconds},
              {"inputs", std::move(inputs)}};
    if (replay.stream)
        data["stream"] = *replay.stream;
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
    // Réglages de la partie : absents d'un document plus ancien (non vérifiés).
    replay.stream.reset();
    if (data.contains("stepNanoseconds"))
    {
        auto stepNs = readU64(data, "stepNanoseconds", "replay");
        if (!stepNs)
            return std::unexpected(stepNs.error());
        if (*stepNs > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
            return core::makeError(ErrorCode::kOutOfRange, "replay.stepNanoseconds: too large");
        replay.stepNanoseconds = static_cast<std::int64_t>(*stepNs);
    }
    if (data.contains("stream"))
    {
        auto stream = readU64(data, "stream", "replay");
        if (!stream)
            return std::unexpected(stream.error());
        replay.stream = *stream;
    }
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
      random_(settings.seed, settings.stream), scratch_(settings.scratchBytes)
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
    // Pendant un pas, ses entrées sont déjà prélevées : le pas en cours compte comme joué.
    if (input.step < step_ || (stepping_ && input.step == step_))
        return core::makeError(ErrorCode::kInvalidArgument,
                               "queueInput: step " + std::to_string(input.step) + " is already played (now " + std::to_string(step_) + ")");
    if (auto r = checkPayload(input, "queueInput"); !r)
        return r;
    // Rejeu (`play`, ou reprise par `step` d'un rejeu interrompu) : une entrée qu'un système met en file pour un pas du rejeu y
    // est déjà (journalisée à sa livraison) ; la remettre la livrerait deux fois.
    if (stepping_ && input.step < replayEnd_)
        return {};
    // Pendant un pas : gardée à part jusqu'à la fin du pas, pour qu'un pas interrompu ne laisse rien en file.
    if (stepping_)
        queued_.push_back(std::move(input));
    else
        insertPending(std::move(input));
    return {};
}

Result<> Simulation::restorePending(std::vector<Input> pending)
{
    if (stepping_)
        return core::makeError(ErrorCode::kInvalidArgument, "restorePending: not allowed during a step");
    for (const Input &input : pending)
    {
        if (input.step < step_)
            return core::makeError(ErrorCode::kInvalidArgument, "restorePending: input " + input.name + " at step " +
                                                                    std::to_string(input.step) + " is already played (now " +
                                                                    std::to_string(step_) + ")");
        if (auto r = checkPayload(input, "restorePending"); !r)
            return r;
    }
    sortByStep(pending);
    pending_ = std::move(pending);
    return {};
}

void Simulation::insertPending(Input input)
{
    auto at = std::upper_bound(pending_.begin(), pending_.end(), input.step, [](std::uint64_t s, const Input &x) { return s < x.step; });
    pending_.insert(at, std::move(input));
}

// ── Pas ───────────────────────────────────────────────────────────────────────

namespace
{
// Remet `stepping_` à faux en sortie de pas, même si un système lève une exception.
struct SteppingScope
{
    bool &flag;
    explicit SteppingScope(bool &f) : flag(f) { flag = true; }
    ~SteppingScope() { flag = false; }
    SteppingScope(const SteppingScope &) = delete;
    SteppingScope &operator=(const SteppingScope &) = delete;
};
} // namespace

void StepContext::emit(std::string name) { events.push_back(Event{step, std::move(name), core::Json{}}); }

void StepContext::emit(std::string name, core::Json data) { events.push_back(Event{step, std::move(name), std::move(data)}); }

bool Simulation::refuseNested(const char *what) const
{
    if (!stepping_)
        return false;
    LOG_WARNING("Simulation::", what, ": called from a system during a step (refused, state unchanged)");
    return true;
}

void Simulation::step()
{
    if (refuseNested("step"))
        return;
    SteppingScope scope(stepping_);
    current_.clear();
    std::size_t due = 0;
    while (due < pending_.size() && pending_[due].step == step_)
        ++due;
    current_.assign(std::make_move_iterator(pending_.begin()), std::make_move_iterator(pending_.begin() + static_cast<std::ptrdiff_t>(due)));
    pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(due));

    queued_.clear();
    const std::size_t eventsBefore = events_.size();

    scratch_.reset();
    StepContext context{world_, random_, step_, dt_, {current_.data(), current_.size()}, scratch_, events_};
    try
    {
        for (System &system : systems_)
            system.run(context);
    }
    catch (...)
    {
        // Pas interrompu : non compté, ses entrées reviennent en tête de file (aucune entrée d'un pas antérieur n'y est) et
        // ne sont pas journalisées ; un nouvel appel le rejoue avec elles. Les entrées mises en file et les événements émis
        // par la tentative sont abandonnés (le nouvel essai les reproduit). Le monde n'est pas restauré (voir l'en-tête).
        pending_.insert(pending_.begin(), std::make_move_iterator(current_.begin()), std::make_move_iterator(current_.end()));
        current_.clear();
        queued_.clear();
        if (events_.size() > eventsBefore)
            events_.erase(events_.begin() + static_cast<std::ptrdiff_t>(eventsBefore), events_.end());
        throw;
    }
    world_.updateTransforms();

    // Entrées mises en file par les systèmes : publiées dans leur ordre d'ajout (même file qu'une insertion immédiate).
    for (Input &input : queued_)
        insertPending(std::move(input));
    queued_.clear();
    log_.insert(log_.end(), current_.begin(), current_.end());
    ++step_;
}

void Simulation::run(std::uint64_t steps)
{
    if (refuseNested("run"))
        return;
    for (std::uint64_t i = 0; i < steps; ++i)
        step();
}

int Simulation::advance(core::Nanoseconds frameDelta)
{
    if (refuseNested("advance"))
        return 0; // temps non consommé
    const int steps = stepper_.advance(frameDelta);
    for (int i = 0; i < steps; ++i)
        step();
    return steps;
}

// ── Capture et restauration ───────────────────────────────────────────────────

Snapshot Simulation::capture() const
{
    if (stepping_)
        LOG_WARNING("Simulation::capture: called during a step, the snapshot is partial and cannot be restored");
    return Snapshot{world_, random_, step_, pending_, stepping_};
}

void Simulation::restore(const Snapshot &snapshot)
{
    if (refuseNested("restore"))
        return;
    if (snapshot.partial)
    {
        LOG_WARNING("Simulation::restore: the snapshot was captured during a step (partial), refused, state unchanged");
        return;
    }
    // Le journal couvre [logStart_, step_) : il ne reste valable que si le pas restauré est dans cette couverture (retour en
    // arrière dans le préfixe connu). Sinon (pas antérieur, ou pas futur dont les entrées ont été effacées ou jamais vues),
    // il repart du pas restauré.
    if (snapshot.step < logStart_ || snapshot.step > step_)
    {
        log_.clear();
        logStart_ = snapshot.step;
    }
    world_ = snapshot.world;
    random_ = snapshot.random;
    step_ = snapshot.step;
    pending_ = snapshot.pending;
    std::erase_if(log_, [&](const Input &input) { return input.step >= step_; });
    stepper_ = core::FixedStepper(settings_.step, settings_.maxStepsPerFrame);
    events_.clear();
    replayEnd_ = 0; // un rejeu interrompu ne s'applique plus à l'état restauré
}

std::vector<Event> Simulation::takeEvents()
{
    // Pendant un pas, la liste porte encore les événements de la tentative (retirés si elle est interrompue).
    if (refuseNested("takeEvents"))
        return {};
    std::vector<Event> out;
    out.swap(events_);
    return out;
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
    if (stepping_)
    {
        LOG_WARNING("Simulation::saveState: called during a step, the state is partial and cannot be loaded");
        data["partial"] = true;
    }
    return Json{{"version", kStateVersion}, {"data", std::move(data)}};
}

Result<scene::SceneLoadReport> Simulation::loadState(const Json &state, const scene::SceneMigrations &migrations)
{
    if (stepping_)
        return core::makeError(ErrorCode::kInvalidArgument, "loadState: not allowed during a step");
    if (!state.is_object() || !state.contains("version") || !state["version"].is_number_integer() || !state.contains("data") ||
        !state["data"].is_object())
        return core::makeError(ErrorCode::kParse, "simulation state must be {\"version\": int, \"data\": {...}}");
    if (state["version"].get<std::int64_t>() != kStateVersion)
        return core::makeError(ErrorCode::kVersionMismatch, "simulation state: unsupported version");
    const Json &data = state["data"];
    if (data.contains("partial") && data["partial"] != false)
        return core::makeError(ErrorCode::kInvalidArgument, "loadState: the state was saved during a step (partial)");
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
    for (const Input &input : *pending)
    {
        // Une entrée d'un pas déjà joué ne serait jamais livrée et bloquerait celles qui la suivent.
        if (input.step < *step)
            return core::makeError(ErrorCode::kParse, "state.pending: input " + input.name + " at step " + std::to_string(input.step) +
                                                          " is before the saved step " + std::to_string(*step));
        if (auto r = checkPayload(input, "state.pending"); !r)
            return std::unexpected(r.error());
    }
    scene::World world = world_;
    auto report = scene::loadScene(world, data["scene"], migrations);
    if (!report)
        return std::unexpected(report.error());
    sortByStep(*pending);
    restore(Snapshot{std::move(world), core::Random::fromState(*rngState, *rngIncrement), *step, std::move(*pending)});
    // Autre partie : le journal d'avant ne la décrit pas. Un rejeu ne peut commencer qu'au pas chargé.
    log_.clear();
    logStart_ = *step;
    return report;
}

// ── Rejeu et empreinte ────────────────────────────────────────────────────────

Result<Replay> Simulation::recording(std::uint64_t firstStep) const
{
    if (firstStep < logStart_ || firstStep > step_)
        return core::makeError(ErrorCode::kInvalidArgument, "recording: the input log covers steps " + std::to_string(logStart_) +
                                                                " to " + std::to_string(step_) + ", not step " + std::to_string(firstStep));
    Replay replay{settings_.seed, firstStep, step_, {}, settings_.step.count(), settings_.stream};
    for (const Input &input : log_)
        if (input.step >= firstStep)
            replay.inputs.push_back(input);
    return replay;
}

Result<> Simulation::play(const Replay &replay)
{
    if (stepping_)
        return core::makeError(ErrorCode::kInvalidArgument, "play: not allowed during a step");
    if (replay.seed != settings_.seed)
        return core::makeError(ErrorCode::kInvalidArgument, "play: the replay was recorded with another seed");
    if (replay.stream && *replay.stream != settings_.stream)
        return core::makeError(ErrorCode::kInvalidArgument, "play: the replay was recorded with another random stream");
    if (replay.stepNanoseconds != 0 && replay.stepNanoseconds != settings_.step.count())
        return core::makeError(ErrorCode::kInvalidArgument, "play: the replay was recorded with another step duration (" +
                                                                 std::to_string(replay.stepNanoseconds) + " ns, now " +
                                                                 std::to_string(settings_.step.count()) + " ns)");
    if (replay.firstStep != step_)
        return core::makeError(ErrorCode::kInvalidArgument, "play: the simulation is at step " + std::to_string(step_) +
                                                                 ", the replay starts at step " + std::to_string(replay.firstStep));
    if (replay.lastStep < replay.firstStep)
        return core::makeError(ErrorCode::kInvalidArgument, "play: lastStep is before firstStep");
    // Tout le lot est validé avant la première insertion : un rejeu refusé laisse la file intacte.
    for (const Input &input : replay.inputs)
    {
        if (input.step < replay.firstStep || input.step >= replay.lastStep)
            return core::makeError(ErrorCode::kInvalidArgument, "play: input " + input.name + " at step " + std::to_string(input.step) +
                                                                    " is outside the replay [" + std::to_string(replay.firstStep) +
                                                                    ", " + std::to_string(replay.lastStep) + ")");
        if (auto r = checkPayload(input, "play"); !r)
            return r;
    }
    // Le rejeu fait foi dans son intervalle : les entrées déjà en attente (capturées avec l'instantané) y sont remplacées,
    // sinon elles seraient livrées deux fois ; celles d'après `lastStep` restent.
    std::erase_if(pending_, [&](const Input &input) { return input.step >= replay.firstStep && input.step < replay.lastStep; });
    for (const Input &input : replay.inputs)
        insertPending(input);
    // Le filtre du rejeu reste actif jusqu'à `lastStep`, même si un système lève : la reprise par `step()` ne remet pas en
    // file une entrée que le rejeu contient déjà. `restore` / `loadState` l'abandonnent.
    replayEnd_ = replay.lastStep;
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
