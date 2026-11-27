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
//     refusé (erreur journalisée ou rendue, état inchangé) ; de même pendant l'`init` d'un module de jeu (`setFrozen`).
//   - Capture et restauration : `Snapshot` = monde (copie), générateur `core::Random`, numéro de pas, entrées en attente.
//     `saveState` / `loadState` l'écrivent en JSON (scène par `scene/scene_file.hpp`). Seul un état ENTRE deux pas se
//     restaure : une capture ou un `saveState` fait par un système pendant un pas est marqué partiel et refusé au chargement.
//   - Exception d'un système pendant `step()` : le pas n'est pas compté, ses entrées reviennent en tête de la file (rien n'est
//     journalisé) et un nouvel appel le rejoue avec elles ; les entrées mises en file et les événements émis par la tentative
//     sont abandonnés (une entrée mise en file pendant un pas n'apparaît dans `pendingInputs()` qu'à la fin du pas) ; les
//     modifications du monde déjà faites ne sont PAS annulées. Exception pendant `play` : le rejeu reste actif jusqu'à son
//     `lastStep` (reprise par `step()`, sans doublon des entrées du rejeu) ; ce filtre fait partie de l'état capturé
//     (`Snapshot::replayEnd`, `"replayEnd"` de `saveState`) : reprendre après `restore` / `loadState` filtre de même.
//   - Empreinte : `fingerprint()` hache le monde (`scene::fingerprint`), l'état du générateur, le pas et les entrées en attente.
//
// Déterminisme (règle 9) : même graine, même flux, même durée de pas (`ctx.dt`), mêmes systèmes, mêmes entrées et état de
// départ équivalent => même état, sur toutes les plateformes, à condition
// que les systèmes n'utilisent ni horloge, ni `rand()`, ni fonction transcendante de la bibliothèque C (`sin`, `exp`… diffèrent
// entre glibc, macOS et MinGW) et soient compilés avec `-ffp-contract=off` (`mk/engine.mk`, `tools/engine-sdk/engine.mk`).
// Le seul hasard est `StepContext::random`. Thread : un propriétaire. Pour faire tourner la boucle sur un thread dédié
// (instantané immuable lu par le rendu, entrées et événements par files sans verrou) : `simulation_thread.hpp` (lot L4).
//   - Événements : un système appelle `ctx.emit(nom, données)` ; le propriétaire les reprend par `takeEvents()` (en mode
//     thread, ils passent au rendu par une `core::SpscQueue`). Ils ne font pas partie de l'état (ni capture ni empreinte).
//   - Mémoire temporaire : `ctx.scratch`, une arène propre à la simulation (donc au thread qui la fait tourner), remise à
//     zéro au début de chaque pas (`SimulationSettings::scratchBytes`).
#pragma once

#include <orosmo/core/error.hpp>
#include <orosmo/core/memory.hpp>
#include <orosmo/core/random.hpp>
#include <orosmo/core/serialization.hpp>
#include <orosmo/core/time.hpp>
#include <orosmo/scene/scene_file.hpp>
#include <orosmo/scene/world.hpp>
#include <orosmo/sim/system.hpp> // Phase, StepContext, SystemDesc (en-tête léger, A79 lot L7)

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace orosmo::sim
{

// Entrée datée : livrée au pas `step`. `data` : charge libre (JSON, sauvegardée telle quelle dans un rejeu).
struct Input
{
    std::uint64_t step = 0;
    std::string name;
    core::Json data;
    friend bool operator==(const Input &, const Input &) = default;
};

// Événement émis par un système pendant un pas (son, effet, message pour l'interface) : sortie de la simulation, lue par le
// rendu ou le jeu. `step` : pas qui l'a émis.
struct Event
{
    std::uint64_t step = 0;
    std::string name;
    core::Json data;
    friend bool operator==(const Event &, const Event &) = default;
};

struct SimulationSettings
{
    core::Nanoseconds step{16'666'667}; // 60 Hz
    int maxStepsPerFrame = 8;           // anti « spirale de la mort » (FixedStepper)
    std::uint64_t seed = 0;
    std::uint64_t stream = core::Random::kDefaultStream;
    std::size_t scratchBytes = 64u * 1024u; // taille de `StepContext::scratch`
};

struct Snapshot
{
    scene::World world;
    core::Random random;
    std::uint64_t step = 0;
    std::vector<Input> pending;
    // Fin du rejeu (`play`) interrompu en cours à la capture (0 sinon) : la reprise après `restore` filtre comme une reprise
    // directe (une entrée du rejeu déjà en file n'est pas remise par un système).
    std::uint64_t replayEnd = 0;
    // Vrai si capturé par un système PENDANT un pas : le monde est à moitié mis à jour et les entrées du pas déjà prélevées.
    // Un tel instantané n'est pas restaurable (`restore` le refuse) ; capturer entre deux pas.
    bool partial = false;
};

// Partie rejouable : à partir du pas `firstStep` (0 : état initial construit par le jeu avec la graine `seed`), jouer jusqu'au
// pas `lastStep` exclu avec ces entrées (toutes les entrées livrées dans l'intervalle, y compris celles qu'un système a mises
// en file pendant un pas). `stepNanoseconds` et `stream` : réglages de la partie enregistrée (`recording`), vérifiés par
// `play` ; `stepNanoseconds` à 0 ou `stream` vide (rejeu écrit à la main, document sans ce champ) ne sont pas vérifiés.
struct Replay
{
    std::uint64_t seed = 0;
    std::uint64_t firstStep = 0;
    std::uint64_t lastStep = 0;
    std::vector<Input> inputs;
    std::int64_t stepNanoseconds = 0;
    std::optional<std::uint64_t> stream = core::Random::kDefaultStream; // vide : document sans flux, non vérifié
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
    // déjà prélevées) ou si la charge n'est pas représentable sans perte en JSON texte (réel infini ou NaN, valeur binaire,
    // chaîne, clé ou nom qui n'est pas de l'UTF-8 valide).
    core::Result<> queueInput(Input input);
    // Remplace les seules entrées en attente (retour arrière d'une mise en file, p. ex. `init` d'un module de jeu rejeté) :
    // monde, générateur, pas, journal, événements et temps accumulé inchangés. kInvalidArgument (file intacte) pendant un pas,
    // pour une entrée d'un pas déjà joué ou une charge refusée par `queueInput`. Triées par pas (ordre gardé à pas égal).
    core::Result<> restorePending(std::vector<Input> pending);
    // Entrées livrées depuis `firstLoggedStep()`, dans l'ordre de livraison (une restauration retire celles du pas restauré et
    // d'après).
    const std::vector<Input> &inputLog() const noexcept { return log_; }
    const std::vector<Input> &pendingInputs() const noexcept { return pending_; }
    // Premier pas couvert par le journal : 0, ou le pas chargé par `loadState` (le journal d'une autre partie est vidé), ou
    // le pas d'un instantané restauré que le journal ne couvre pas (antérieur à lui, ou postérieur au pas courant).
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

    // ── Événements ────────────────────────────────────────────────────────────
    // Événements émis depuis la dernière reprise, dans l'ordre d'émission ; `takeEvents` les rend et vide la liste.
    const std::vector<Event> &events() const noexcept { return events_; }
    std::vector<Event> takeEvents();
    const core::ArenaAllocator &scratch() const noexcept { return scratch_; }

    // ── Capture et restauration ───────────────────────────────────────────────
    // Pendant un pas (appel d'un système) : instantané marqué `partial`, non restaurable, et avertissement au journal.
    Snapshot capture() const;
    // Remet l'état capturé (monde, générateur, pas, entrées en attente, filtre du rejeu) ; le journal des entrées garde celles
    // d'avant ce pas s'il couvre ce pas (sinon il repart de ce pas) ; un instantané `partial` est refusé (avertissement, état inchangé) ;
    // le temps accumulé par `advance` repart de zéro ; les événements non repris sont abandonnés. Les systèmes ne font pas partie de l'état (enregistrés par le jeu).
    void restore(const Snapshot &snapshot);
    // État en JSON : {"version", "data": {"step", "random": {"state", "increment"}, "pending": [...], "scene": <scène>}}, plus
    // "replayEnd" seulement après un rejeu interrompu qui filtre encore.
    core::Json saveState() const;
    // Les types de composants doivent être enregistrés dans le monde. Tout ou rien ; kParse si une entrée en attente précède le
    // pas sauvegardé ; kInvalidArgument pour un état sauvegardé pendant un pas (`"partial": true`). Le journal des entrées est vidé (il décrivait une autre partie) : `firstLoggedStep()` = pas chargé.
    core::Result<scene::SceneLoadReport> loadState(const core::Json &state, const scene::SceneMigrations &migrations = {});

    // ── Rejeu et empreinte ────────────────────────────────────────────────────
    // Partie jouée depuis le pas `firstStep` jusqu'à maintenant, avec les entrées du journal. kInvalidArgument si `firstStep`
    // précède `firstLoggedStep()` (entrées inconnues) ou dépasse `stepIndex()`.
    core::Result<Replay> recording(std::uint64_t firstStep = 0) const;
    // Met en file les entrées du rejeu puis joue jusqu'à `lastStep`. Dans [firstStep, lastStep), le rejeu fait foi : les
    // entrées déjà en attente dans cet intervalle (venues d'un instantané) sont remplacées, et celles qu'un système met en
    // file pendant le rejeu pour un pas de l'intervalle sont ignorées (le rejeu les contient déjà, dans leur ordre de
    // livraison) ; celles d'après `lastStep` restent en file. kInvalidArgument (file intacte) si la simulation n'est pas au
    // pas `firstStep`, si la graine, le flux ou la durée du pas diffèrent, si une entrée sort de l'intervalle ou a une charge
    // refusée par `queueInput`.
    core::Result<> play(const Replay &replay);
    std::uint64_t fingerprint() const;

    // ── Gel ───────────────────────────────────────────────────────────────────
    // Posé par `app::GameModule` pendant l'`init` d'une version : step, run, advance, play, restore, loadState et takeEvents
    // sont refusés (avertissement ou kInvalidArgument, état inchangé). Un `init` rejeté n'a ainsi que le monde, le générateur et
    // la file d'entrées à remettre : pas, journal, événements et temps accumulé n'ont pas bougé.
    void setFrozen(bool frozen) noexcept { frozen_ = frozen; }

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
    std::vector<Event> events_;
    core::ArenaAllocator scratch_;
    bool stepping_ = false;
    std::vector<Input> queued_;   // mises en file par les systèmes pendant le pas : publiées à sa fin, abandonnées s'il lève
    // Fin du dernier rejeu (`play`) : pendant un pas, une entrée mise en file pour un pas avant elle est ignorée (le rejeu la
    // contient). Reste posée après un rejeu interrompu par une exception ; capturée avec l'instantané (`Snapshot::replayEnd`)
    // et remise par `restore`, sauvegardée par `saveState` tant qu'elle filtre encore.
    std::uint64_t replayEnd_ = 0;
    bool frozen_ = false;
};

} // namespace orosmo::sim
