# sim

Couche 9 de `docs/03` §1 : boucle de simulation à pas fixe, déterministe et rejouable (SIM-01). Archive : `libsim.a`
(`src/sim/`), API publique `include/orosmo/sim/`, namespace `orosmo::sim`. Dépend de `scene` (monde, sauvegarde, empreinte)
et de `core` (`FixedStepper`, `Random`, sérialisation, `SpscQueue`, `Config`). Créé en M8 (A79 lot L3) ; thread de simulation
optionnel au lot L4.

| En-tête | Rôle |
| --- | --- |
| `simulation.hpp` | `Simulation` (monde, générateur, pas fixe, systèmes, entrées datées, événements, arène de pas, capture et restauration, état JSON, rejeu, empreinte), `SystemDesc` / `StepContext` (`emit`, `scratch`) / `Phase`, `Input`, `Event`, `Snapshot`, `Replay` (`replayToJson`, `replayFromJson`) |
| `simulation_thread.hpp` | `SimulationThread` (thread dédié : `start`, `frame`, `sync`, `pushInput`, `acquire`, `popEvent`, `stop`), `ThreadSettings`, `Cadence`, `RenderSnapshot` / `RenderTransform` / `interpolate`, `SimulationConfig` (`declareSimulationConfig`, `readSimulationConfig`, `stepForRate`) (lot L4) |
| `snapshot_buffer.hpp` | `SnapshotBuffer<T>` : échange sans verrou d'un instantané entre un écrivain et un lecteur (trois emplacements) (lot L4) |

Exemple :

```cpp
orosmo::sim::Simulation sim({.step = std::chrono::milliseconds(10), .seed = 42});
auto velocity = *sim.world().registerComponent<Velocity>("Velocity");
sim.addSystem({"move", orosmo::sim::Phase::kUpdate, 0, [velocity](orosmo::sim::StepContext &ctx) {
    ctx.world.view(ctx.world.transformType(), velocity).each([&](auto e, orosmo::scene::Transform &t, Velocity &v) {
        t.position += orosmo::core::widen(v.value) * ctx.dt;
        ctx.world.markTransformDirty(e);
    });
}});
sim.queueInput({.step = 30, .name = "jump", .data = 2.5});  // livrée au pas 30
sim.advance(frameDelta);                                     // 0..maxStepsPerFrame pas, selon le temps cumulé
auto snapshot = sim.capture();                               // monde + Random + pas + entrées en attente
sim.restore(snapshot);
auto replay = sim.recording();                               // graine + pas + entrées : rejoue la même partie
```

## Règles

- **Pas fixe** : `advance(delta)` passe par `core::FixedStepper` ; le nombre de pas ne dépend que du temps cumulé (au plus
  `maxStepsPerFrame` par appel, le surplus est abandonné). `step()` et `run(n)` jouent des pas sans horloge (tests, mode sans
  fenêtre de la démo). Après chaque pas, `World::updateTransforms()` est appelé.
- **Systèmes** : enregistrés par nom (règle 13), exécutés par phase (`kInput`, `kUpdate`, `kPostUpdate`), puis ordre, puis
  nom : l'ordre ne dépend pas de l'ordre d'enregistrement. Pas d'ajout ni de retrait pendant un pas (refusé).
- **Entrées** : datées par pas, livrées dans l'ordre d'ajout à `StepContext::inputs`, enregistrées dans `inputLog()`. Une entrée
  d'un pas déjà joué est refusée.
- **Capture** : `Snapshot` est une copie profonde (le monde est copiable). `restore` garde le journal des entrées d'avant le
  pas restauré et remet à zéro le temps accumulé. Les systèmes ne font pas partie de l'état. `saveState` / `loadState` : même
  état en JSON (scène par `scene_file.hpp`, générateur par `state`/`increment`), tout ou rien.
- **Empreinte** : `fingerprint()` = `scene::hashWorld` (champs décrits, ordre dense, table des entités) + état du générateur +
  pas + entrées en attente.
- **Déterminisme** (règle 9) : même graine, mêmes systèmes, mêmes entrées => même empreinte, sur toutes les plateformes, si les
  systèmes n'utilisent ni horloge, ni `rand()`, ni fonction transcendante (`sin`, `exp`, `pow`… diffèrent entre glibc, macOS et
  MinGW) et sont compilés avec `-ffp-contract=off` (moteur, démo et gabarit : `mk/engine.mk`, `tools/engine-sdk/engine.mk`,
  `OrosmoConfig.cmake`). `make check` (`tools/check_deterministic_math.py`) le vérifie sur `src/sim`, `src/scene` et le
  scénario de la démo. Seul hasard : `StepContext::random` (`core::Random`, PCG32).
- **Événements et arène** : `ctx.emit(nom, données)` ajoute un `Event` (pas, nom, JSON) repris par `takeEvents()` ; ils ne
  font pas partie de l'état (`restore` abandonne ceux non repris). `ctx.scratch` : arène de la simulation
  (`SimulationSettings::scratchBytes`, 64 Kio), remise à zéro au début de chaque pas.
- **Thread** (lot L4, optionnel ; le mono-thread reste le défaut) : `SimulationThread::start(sim, clock, settings)` fait
  tourner la simulation sur un thread dédié ; entre `start` et `stop`, seul ce thread touche la `Simulation` (monde,
  systèmes, arène). Cadence : `kStepsPerFrame` (le rendu appelle `frame()` à chaque image : il attend le lot de l'image
  précédente puis demande N pas, une image de latence au plus) ou `kFixedRate` (le thread lit l'horloge, `FixedStepper`,
  fréquence 1 / pas). Réglages par `core::Config` : `sim.thread`, `sim.cadence` (`rate` / `frame`), `sim.rateHz`,
  `sim.stepsPerFrame` (`declareSimulationConfig`, `readSimulationConfig`).
- **Instantané** : `RenderSnapshot` (immuable, publié après chaque lot de pas par `SnapshotBuffer`, sans verrou) : entités
  qui ont un `Transform` (ordre dense), transformations monde du pas courant et du pas d'avant (`previous`, même entité par
  indice et génération), composants listés dans `ThreadSettings::renderComponents` (copie d'octets, `read<T>`).
  `alpha(now)` : fraction du pas en cours en `kFixedRate` (1 en `kStepsPerFrame`) ; `transformAt(i, alpha)` interpole
  (position et échelle linéaires, rotation nlerp ; le rendu a un pas de retard, sans extrapolation).
- **Files** : entrées du rendu vers la simulation et événements de la simulation vers le rendu par `core::SpscQueue` ; une
  entrée datée d'un pas déjà joué à son arrivée est refusée et comptée (`rejectedInputs`) : dater à `stepIndex() + latence`.
  Aucun événement n'est perdu (arriéré côté simulation si la file est pleine). Côté rendu, un seul thread appelle les
  méthodes de `SimulationThread`. Même graine et mêmes entrées : même empreinte qu'en mono-thread.

Exemple (thread) :

```cpp
orosmo::core::SteadyClock clock;
auto thread = *orosmo::sim::SimulationThread::start(sim, clock, {.cadence = orosmo::sim::Cadence::kFixedRate,
                                                                .renderComponents = {"Tint"}});
thread->pushInput({.step = thread->stepIndex() + 2, .name = "jump", .data = 2.5});
const auto &snapshot = thread->acquire();          // inchangé jusqu'au prochain acquire
const double alpha = snapshot.alpha(clock.now());
for (std::size_t i = 0; i < snapshot.entities.size(); ++i)
    draw(relativeMatrix(snapshot.transformAt(i, alpha), cameraPosition));
while (auto event = thread->popEvent()) play(*event);
thread->stop();                                    // la simulation revient au thread appelant
```

## Démo et tests

- `apps/demo/bin/debug/main --sim-agents 500 --seed 42 --sim-steps 1000` : 500 agents sans fenêtre ni GPU
  (`apps/demo/src/Sim/agent_simulation.cpp`), affiche l'empreinte. `tests/demo/agent_simulation_tests.cpp` la compare à une
  valeur figée (`make demo-test-unit`, donc chaque job de CI).
- `tests/sim/simulation_tests.cpp` (pas fixe, ordre des systèmes, entrées, capture et restauration, état JSON, rejeu),
  `tests/sim/simulation_thread_tests.cpp` (lot L4 : même empreinte et mêmes événements qu'en mono-thread, 20 / 50 / 120 Hz
  et 1 / 2 / 4 pas par image avec `ManualClock`, instantané, files, test concurrent simulation + lecteur pour TSan),
  `tests/scene/scene_file_tests.cpp` (sauvegarde, migration, empreinte).

Limites : voir `docs/GAPS.md` (G280 et suivants ; thread : G300 et suivants).
