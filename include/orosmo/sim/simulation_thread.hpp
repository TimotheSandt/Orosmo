// SIM-01, extension (A79 lot L4, `docs/05` M8) : thread de simulation optionnel. Le mode mono-thread (`Simulation` appelée
// par son propriétaire) reste le défaut ; `SimulationThread` fait tourner une `Simulation` sur un thread dédié.
//
//   - Cadence (`ThreadSettings`, lue dans `core::Config` par `readSimulationConfig`) : `kStepsPerFrame`, N pas par image (le
//     rendu appelle `frame()` à chaque image ; il attend la fin du lot de l'image précédente, une image de latence au plus) ;
//     ou `kFixedRate`, fréquence fixe indépendante du rendu (1 / `SimulationSettings::step`, par ex. 20, 50 ou 120 Hz) : le
//     thread lit l'horloge (`core::IClock`, `ManualClock` en test) et joue les pas dus par `core::FixedStepper`.
//   - Un seul point d'échange pour l'état : `RenderSnapshot`, instantané immuable des transformations monde (et des
//     composants listés dans `ThreadSettings::renderComponents`) publié après chaque lot de pas par `SnapshotBuffer` (sans
//     verrou). Il garde l'état du pas courant et celui du pas d'avant, pour l'interpolation (`alpha(now)`, fraction du
//     `FixedStepper`).
//   - Entrées du joueur : `pushInput` (file `core::SpscQueue` vers la simulation), datées par numéro de pas comme en
//     mono-thread : même graine et mêmes entrées => même empreinte (rejouable). Événements émis par les systèmes
//     (`StepContext::emit`) : `popEvent` (file `SpscQueue` vers le rendu, dans l'ordre d'émission, aucun perdu).
//   - Aucune donnée non sûre entre threads n'est partagée : pendant que le thread tourne (entre `start` et `stop`), seul lui
//     touche la `Simulation` (monde, systèmes, arène de pas `StepContext::scratch`) ; `EventBus`, `Vfs`, `Config` restent au
//     thread principal. Les systèmes ne doivent toucher qu'au contexte du pas.
//
// Côté rendu, toutes les méthodes publiques (sauf `stepIndex`, `rejectedInputs`, `settings`) sont appelées par UN thread,
// celui qui a appelé `start` (consommateur des événements, producteur des entrées, lecteur des instantanés).
#pragma once

#include <orosmo/core/config.hpp>
#include <orosmo/core/error.hpp>
#include <orosmo/core/math.hpp>
#include <orosmo/core/spsc_queue.hpp>
#include <orosmo/core/time.hpp>
#include <orosmo/scene/entity.hpp>
#include <orosmo/sim/simulation.hpp>
#include <orosmo/sim/snapshot_buffer.hpp>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

namespace orosmo::sim
{

enum class Cadence : std::uint8_t
{
    kStepsPerFrame, // « frame » : N pas à chaque appel de `frame()`
    kFixedRate,     // « rate » : pas à fréquence fixe, horloge lue par le thread
};
std::string_view cadenceName(Cadence cadence) noexcept;
std::optional<Cadence> parseCadence(std::string_view name) noexcept;

struct ThreadSettings
{
    Cadence cadence = Cadence::kFixedRate;
    int stepsPerFrame = 1;            // kStepsPerFrame : 1 à kMaxStepsPerFrame
    std::size_t inputCapacity = 1024; // file des entrées (rendu -> simulation)
    std::size_t eventCapacity = 4096; // file des événements (simulation -> rendu)
    // Composants copiés dans l'instantané en plus des transformations (par nom, comme le monde les enregistre).
    std::vector<std::string> renderComponents{};

    static constexpr int kMaxStepsPerFrame = 64;
};

// Réglages lus au démarrage dans `core::Config` (déclarés par `declareSimulationConfig`) :
//   sim.thread        bool    false   thread dédié (sinon mono-thread, le défaut de SIM-01)
//   sim.cadence       string  "rate"  "rate" (fréquence fixe) ou "frame" (N pas par image)
//   sim.rateHz        number  60      fréquence des pas (donne aussi le pas en mode « frame »), ]0, 10000]
//   sim.stepsPerFrame int     1       pas par image en mode « frame », 1 à 64
struct SimulationConfig
{
    bool threaded = false;
    core::Nanoseconds step{16'666'667};
    ThreadSettings thread;
};
void declareSimulationConfig(core::Config &config);
// kInvalidArgument si une valeur est hors de sa plage ou si la cadence est inconnue ; kParse si un type est faux.
core::Result<SimulationConfig> readSimulationConfig(const core::Config &config);
// Durée d'un pas pour une fréquence en Hz, arrondie à la nanoseconde (120 Hz : 8 333 333 ns) ; 0 si la fréquence n'est pas
// positive ou si la durée ne tient pas dans `Nanoseconds`.
core::Nanoseconds stepForRate(double hz) noexcept;

// Transformation monde d'une entité dans un instantané (cache monde de `scene::Transform`).
struct RenderTransform
{
    core::DVec3 position{0.0};
    core::DMat3 linear{1.0};
    friend bool operator==(const RenderTransform &, const RenderTransform &) = default;
};
// Interpolation entre deux états : position et échelle linéaires, rotation par quaternion normalisé (nlerp, chemin court).
// alpha <= 0 rend `a`, alpha >= 1 rend `b` exactement. Matrice avec réflexion ou échelle nulle : interpolation terme à terme.
RenderTransform interpolate(const RenderTransform &a, const RenderTransform &b, double alpha);
// Matrice locale -> monde en float, translation relative à `origin` (comme `scene::relativeMatrix`).
core::Mat4 relativeMatrix(const RenderTransform &transform, const core::WorldPosition &origin);

// Copie d'un composant pour toutes les entités de l'instantané (`size` octets par entité, dans l'ordre de `entities`).
struct RenderComponentColumn
{
    std::string name;
    std::size_t size = 0; // 0 : type inconnu du monde au moment de l'instantané
    scene::ComponentDesc desc; // description enregistrée (taille, alignement, champs) : contrôle de `read<T>`
    std::vector<std::byte> bytes;
    std::vector<std::uint8_t> present; // 1 si l'entité a le composant
};

// Instantané immuable lu par le rendu. Entités : celles qui ont un `Transform`, dans l'ordre dense du monde.
struct RenderSnapshot
{
    std::uint64_t step = 0; // état après `step` pas joués
    core::Nanoseconds stepDuration{0};
    core::Nanoseconds time{0}; // kFixedRate : instant (horloge du thread) où cet état devient l'état courant
    bool timed = false;        // vrai en kFixedRate : `alpha` suit l'horloge ; faux : alpha = 1 (état courant)
    std::vector<scene::Entity> entities;
    std::vector<RenderTransform> transforms; // état après `step` pas
    std::vector<RenderTransform> previous;   // même entité après `step - 1` pas (= transforms[i] si elle vient d'apparaître)
    std::vector<RenderComponentColumn> components;

    // Fraction [0, 1] du pas en cours à l'instant `now` (même horloge que le thread) : 0 montre `previous`, 1 `transforms`.
    // Le rendu a donc un pas de retard sur la simulation, sans jamais extrapoler.
    double alpha(core::Nanoseconds now) const noexcept;
    RenderTransform transformAt(std::size_t index, double alpha) const { return interpolate(previous[index], transforms[index], alpha); }
    std::optional<std::size_t> column(std::string_view name) const noexcept;
    // Copie la valeur du composant de l'entité `index` dans `out`. Faux si elle est absente, ou si T ne correspond pas au type
    // enregistré : taille et alignement, et champs (nom, type, décalage) si T est réfléchi, comme `World::componentType<T>`.
    // Un T non réfléchi n'est contrôlé que par taille et alignement : préférer un type réfléchi (ou `readBytes`).
    template <typename T>
        requires scene::ComponentData<T>
    bool read(std::size_t columnIndex, std::size_t index, T &out) const
    {
        if (columnIndex >= components.size() || index >= entities.size())
            return false;
        const RenderComponentColumn &c = components[columnIndex];
        if (c.size == 0 || c.present[index] == 0u)
            return false;
        std::vector<core::FieldDesc> fields;
        if constexpr (core::Reflected<T>)
            fields = core::describeFields<T>();
        if (!scene::detail::checkComponentType(c.desc, sizeof(T), alignof(T), core::Reflected<T> ? &fields : nullptr))
            return false;
        std::memcpy(&out, c.bytes.data() + index * c.size, sizeof(T));
        return true;
    }
    // Copie les `size` octets du composant (taille enregistrée exigée) ; faux si absent ou si la taille diffère.
    bool readBytes(std::size_t columnIndex, std::size_t index, void *out, std::size_t size) const noexcept
    {
        if (columnIndex >= components.size() || index >= entities.size())
            return false;
        const RenderComponentColumn &c = components[columnIndex];
        if (c.size == 0 || c.size != size || c.present[index] == 0u)
            return false;
        std::memcpy(out, c.bytes.data() + index * c.size, size);
        return true;
    }
};

class SimulationThread
{
  public:
    // Démarre le thread sur `simulation` (qui doit survivre au thread, comme `clock`). Met à jour le cache monde
    // (`updateTransforms`) et publie tout de suite l'instantané de l'état présent. kInvalidArgument : `stepsPerFrame` hors de
    // [1, 64], capacité nulle, pas de durée nulle ; kBusy : le système refuse un thread de plus (rester en mono-thread).
    static core::Result<std::unique_ptr<SimulationThread>> start(Simulation &simulation, const core::IClock &clock,
                                                                  ThreadSettings settings = {});
    ~SimulationThread();
    SimulationThread(const SimulationThread &) = delete;
    SimulationThread &operator=(const SimulationThread &) = delete;

    // Arrête et rejoint le thread (idempotent). En kStepsPerFrame, le lot accepté par le dernier `frame()` est joué avant
    // l'arrêt (aucun pas demandé n'est perdu). Les entrées encore en file sont remises à la simulation (`pendingInputs`) ;
    // les événements non lus restent lisibles par `popEvent`. Le propriétaire peut ensuite reprendre la simulation.
    void stop();
    // Vrai tant que le thread joue : faux après `stop()`, et dès que le thread s'est arrêté sur une erreur (`status()`), même
    // s'il reste à le rejoindre par `stop()`.
    bool running() const noexcept;
    // Erreur du thread : un système a levé une exception (le thread s'est arrêté, `frame` et `sync` ne bloquent plus ; le pas
    // interrompu n'est pas rejoué). Sans erreur : succès. L'erreur et l'arrêt sont publiés ensemble : dès qu'elle est
    // visible, `running()` est faux et `pushInput` refuse.
    core::Result<> status() const;

    // ── Côté rendu ────────────────────────────────────────────────────────────
    // kStepsPerFrame : attend la fin des pas demandés à l'image précédente, puis demande `stepsPerFrame` pas (sans attendre
    // qu'ils soient joués). kFixedRate : réveille le thread (sans effet sur le nombre de pas, fixé par l'horloge).
    void frame();
    // Attend que le thread ait fait un tour commencé après cet appel : tous les pas demandés joués (kStepsPerFrame), ou
    // horloge relue et pas dus joués (kFixedRate), instantané publié. Tests et mode sans fenêtre.
    void sync();
    // Entrée datée par pas (voir `Simulation::queueInput`), livrée au thread avant son prochain pas. Faux si la file est
    // pleine ou le thread arrêté, y compris sur une erreur pas encore rejointe (rien n'est ajouté). Une entrée d'un pas déjà joué quand le thread la reçoit est refusée et comptée
    // (`rejectedInputs`, avertissement au journal) : dater au moins à `stepIndex() + latence`.
    bool pushInput(Input input);
    // Dernier instantané publié, inchangé jusqu'au prochain `acquire`.
    const RenderSnapshot &acquire() noexcept { return snapshots_.acquire(); }
    // Prochain événement émis par la simulation, dans l'ordre d'émission.
    std::optional<Event> popEvent();

    // ── Lisibles de tout thread ───────────────────────────────────────────────
    // Pas joués (numéro du prochain pas).
    std::uint64_t stepIndex() const noexcept { return stepIndex_.load(std::memory_order_acquire); }
    std::uint64_t rejectedInputs() const noexcept { return rejectedInputs_.load(std::memory_order_relaxed); }
    const ThreadSettings &settings() const noexcept { return settings_; }

  private:
    struct State
    {
        std::uint64_t step = 0;
        std::vector<scene::Entity> entities;
        std::vector<RenderTransform> transforms;
    };
    struct Slot
    {
        std::uint32_t generation = 0;
        std::uint32_t position = 0; // position + 1 dans l'état précédent ; 0 : absente
    };

    SimulationThread(Simulation &simulation, const core::IClock &clock, ThreadSettings settings);
    void run();
    void loop();
    void fail(std::string message);
    void drainInputs();
    void playSteps(std::uint64_t count);
    void playStep();
    void captureState(State &out);
    void publish(core::Nanoseconds time);
    void flushEvents();

    Simulation &simulation_;
    const core::IClock &clock_;
    const ThreadSettings settings_;
    core::FixedStepper stepper_;
    const bool perFrame_;

    core::SpscQueue<Input> inputs_;
    core::SpscQueue<Event> events_;
    SnapshotBuffer<RenderSnapshot> snapshots_;

    // Thread de simulation seulement (avant `start` du thread et après `join` : thread appelant).
    State previousState_;
    State currentState_;
    std::vector<Slot> slots_;
    std::deque<Event> backlog_; // événements que la file pleine n'a pas pris (aucun n'est perdu)
    core::Nanoseconds lastSample_{0};

    // Commande (sous `mutex_`).
    mutable std::mutex mutex_;
    std::optional<core::Error> failure_;
    std::condition_variable work_; // vers le thread de simulation
    std::condition_variable done_; // vers le rendu
    std::uint64_t requested_ = 0;  // kStepsPerFrame : pas demandés depuis le départ
    std::uint64_t completed_ = 0;  // pas joués depuis le départ (publié à la fin de chaque tour)
    std::uint64_t syncRequested_ = 0;
    std::uint64_t syncDone_ = 0;
    bool wake_ = false;
    bool stop_ = false;
    bool exited_ = false;

    std::atomic<std::uint64_t> stepIndex_{0};
    std::atomic<std::uint64_t> rejectedInputs_{0};
    std::thread thread_;
};

} // namespace orosmo::sim
