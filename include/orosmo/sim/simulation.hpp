// SIM-01 (A79 lot L3) : boucle de simulation à pas fixe, déterministe et rejouable, découplée du rendu.
//
//   - Pas fixe : `advance(deltaImage)` passe par `core::FixedStepper` et joue 0..maxStepsPerFrame pas ; tant qu'aucun appel
//     n'atteint ce plafond (le surplus est alors abandonné), le nombre de pas ne dépend que du temps cumulé, pas du découpage
//     en images. `step()` joue exactement un pas (tests, mode sans fenêtre).
//   - Systèmes enregistrés PAR NOM (règle 13 : rien ne garde l'identité d'une fonction de la bibliothèque de jeu), avec une
//     phase et un ordre ; ordre d'exécution : phase, puis ordre, puis nom (indépendant de l'ordre d'enregistrement).
//   - Entrées datées par pas : `queueInput({.step = n, ...})` est livrée au pas n (dans l'ordre d'ajout), et enregistrée dans
//     `inputLog()` ; un `Replay` (graine, pas, entrées) rejoue la même partie. La charge est un JSON sans réel infini ni NaN.
//   - Pas imbriqués : un système qui appelle `step`, `run`, `advance`, `restore`, `loadState` ou `play` sur sa simulation est
//     refusé (erreur journalisée ou rendue, état inchangé).
//   - Capture et restauration : `Snapshot` = monde (copie), générateur `core::Random`, numéro de pas, entrées en attente.
//     `saveState` / `loadState` l'écrivent en JSON (scène par `scene/scene_file.hpp`).
//   - Empreinte : `fingerprint()` hache le monde (`scene::fingerprint`), l'état du générateur, le pas et les entrées en attente.
//
// Déterminisme (règle 9) : même graine, mêmes systèmes, mêmes entrées => même état, sur toutes les plateformes, à condition
// que les systèmes n'utilisent ni horloge, ni `rand()`, ni fonction transcendante de la bibliothèque C (`sin`, `exp`… diffèrent
// entre glibc, macOS et MinGW) et soient compilés avec `-ffp-contract=off` (`mk/engine.mk`, `tools/engine-sdk/engine.mk`).
// Le seul hasard est `StepContext::random`. Thread : un propriétaire ; le thread de simulation et l'instantané en double
// tampon lu par le rendu viennent avec le lot L4 (le `Snapshot` d'ici en est la base).
#pragma once

#include <orosmo/core/error.hpp>
#include <orosmo/core/random.hpp>
#include <orosmo/core/serialization.hpp>
#include <orosmo/core/time.hpp>
#include <orosmo/scene/scene_file.hpp>
#include <orosmo/scene/world.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace orosmo::sim
{

enum class Phase : std::uint8_t
{
    kInput,      // lecture des entrées du pas
    kUpdate,     // logique
    kPostUpdate, // conséquences (collisions, nettoyage)
};
std::string_view phaseName(Phase phase) noexcept;
std::optional<Phase> parsePhase(std::string_view name) noexcept;

// Entrée datée : livrée au pas `step`. `data` : charge libre (JSON, sauvegardée telle quelle dans un rejeu).
struct Input
{
    std::uint64_t step = 0;
    std::string name;
    core::Json data;
    friend bool operator==(const Input &, const Input &) = default;
};

struct StepContext
{
    scene::World &world;
    core::Random &random;
    std::uint64_t step; // numéro du pas joué (0 pour le premier)
    double dt;          // durée d'un pas en secondes
    std::span<const Input> inputs; // entrées de ce pas, dans l'ordre d'ajout
};

using SystemFn = std::function<void(StepContext &)>;

struct SystemDesc
{
    std::string name;
    Phase phase = Phase::kUpdate;
    int order = 0;
    SystemFn run;
};

struct SimulationSettings
{
    core::Nanoseconds step{16'666'667}; // 60 Hz
    int maxStepsPerFrame = 8;           // anti « spirale de la mort » (FixedStepper)
    std::uint64_t seed = 0;
    std::uint64_t stream = core::Random::kDefaultStream;
};

struct Snapshot
{
    scene::World world;
    core::Random random;
    std::uint64_t step = 0;
    std::vector<Input> pending;
};

// Partie rejouable : à partir du pas `firstStep` (0 : état initial construit par le jeu avec la graine `seed`), jouer jusqu'au
// pas `lastStep` exclu avec ces entrées.
struct Replay
{
    std::uint64_t seed = 0;
    std::uint64_t firstStep = 0;
    std::uint64_t lastStep = 0;
    std::vector<Input> inputs;
    friend bool operator==(const Replay &, const Replay &) = default;
};
core::Json replayToJson(const Replay &replay);
core::Result<Replay> replayFromJson(const core::Json &json);

class Simulation
{
  public:
    explicit Simulation(SimulationSettings settings = {});

    scene::World &world() noexcept { return world_; }
    const scene::World &world() const noexcept { return world_; }
    core::Random &random() noexcept { return random_; }
    const SimulationSettings &settings() const noexcept { return settings_; }
    double stepSeconds() const noexcept { return dt_; }

    // ── Systèmes ──────────────────────────────────────────────────────────────
    // kInvalidArgument : nom vide ou fonction vide, ou appel pendant un pas ; kAlreadyExists : nom déjà pris.
    core::Result<> addSystem(SystemDesc system);
    // Faux si aucun système ne porte ce nom (ou pendant un pas : refusé).
    bool removeSystem(std::string_view name);
    bool hasSystem(std::string_view name) const noexcept;
    // Noms dans l'ordre d'exécution.
    std::vector<std::string> systemOrder() const;

    // ── Entrées ───────────────────────────────────────────────────────────────
    // kInvalidArgument si `input.step` est déjà joué (< stepIndex(), ou == stepIndex() pendant le pas, dont les entrées sont
    // déjà prélevées) ou si la charge contient un réel infini ou NaN (non représentable en JSON texte).
    core::Result<> queueInput(Input input);
    // Entrées livrées depuis `firstLoggedStep()`, dans l'ordre de livraison (une restauration retire celles du pas restauré et
    // d'après).
    const std::vector<Input> &inputLog() const noexcept { return log_; }
    const std::vector<Input> &pendingInputs() const noexcept { return pending_; }
    // Premier pas couvert par le journal : 0, ou le pas chargé par `loadState` (le journal d'une autre partie est vidé), ou
    // le pas d'un instantané antérieur restauré.
    std::uint64_t firstLoggedStep() const noexcept { return logStart_; }

    // ── Pas ───────────────────────────────────────────────────────────────────
    void step();
    void run(std::uint64_t steps);
    // Cumule le delta d'image et joue les pas dus ; rend leur nombre.
    int advance(core::Nanoseconds frameDelta);
    // Fraction du pas suivant déjà écoulée (interpolation du rendu).
    double alpha() const { return stepper_.alpha(); }
    // Nombre de pas joués = numéro du prochain pas.
    std::uint64_t stepIndex() const noexcept { return step_; }

    // ── Capture et restauration ───────────────────────────────────────────────
    Snapshot capture() const;
    // Remet l'état capturé (monde, générateur, pas, entrées en attente) ; le journal des entrées garde celles d'avant ce pas ;
    // le temps accumulé par `advance` repart de zéro. Les systèmes ne font pas partie de l'état (enregistrés par le jeu).
    void restore(const Snapshot &snapshot);
    // État en JSON : {"version", "data": {"step", "random": {"state", "increment"}, "pending": [...], "scene": <scène>}}.
    core::Json saveState() const;
    // Les types de composants doivent être enregistrés dans le monde. Tout ou rien ; kParse si une entrée en attente précède le
    // pas sauvegardé. Le journal des entrées est vidé (il décrivait une autre partie) : `firstLoggedStep()` = pas chargé.
    core::Result<scene::SceneLoadReport> loadState(const core::Json &state, const scene::SceneMigrations &migrations = {});

    // ── Rejeu et empreinte ────────────────────────────────────────────────────
    // Partie jouée depuis le pas `firstStep` jusqu'à maintenant, avec les entrées du journal. kInvalidArgument si `firstStep`
    // précède `firstLoggedStep()` (entrées inconnues) ou dépasse `stepIndex()`.
    core::Result<Replay> recording(std::uint64_t firstStep = 0) const;
    // Met en file les entrées du rejeu puis joue jusqu'à `lastStep`. Dans [firstStep, lastStep), le rejeu fait foi : les
    // entrées déjà en attente dans cet intervalle (venues d'un instantané) sont remplacées, pas doublées. kInvalidArgument
    // (file intacte) si la simulation n'est pas au pas `firstStep`, si la graine diffère, si une entrée sort de l'intervalle
    // ou a une charge infinie ou NaN.
    core::Result<> play(const Replay &replay);
    std::uint64_t fingerprint() const;

  private:
    struct System
    {
        std::string name;
        Phase phase;
        int order;
        SystemFn run;
    };

    void insertPending(Input input);
    bool refuseNested(const char *what) const;

    SimulationSettings settings_;
    double dt_;
    core::FixedStepper stepper_;
    scene::World world_;
    core::Random random_;
    std::uint64_t step_ = 0;
    std::vector<System> systems_; // triés : phase, ordre, nom
    std::vector<Input> pending_;  // triées par pas (ordre d'ajout gardé à pas égal)
    std::vector<Input> log_;
    std::uint64_t logStart_ = 0; // premier pas couvert par log_
    std::vector<Input> current_;
    bool stepping_ = false;
};

} // namespace orosmo::sim
