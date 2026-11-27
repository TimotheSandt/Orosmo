# sim

Couche 9 de `docs/03` §1 : boucle de simulation à pas fixe, déterministe et rejouable (SIM-01). Archive : `libsim.a`
(`src/sim/`), API publique `include/orosmo/sim/`, namespace `orosmo::sim`. Dépend de `scene` (monde, sauvegarde, empreinte)
et de `core` (`FixedStepper`, `Random`, sérialisation, `SpscQueue`, `Config`). Créé en M8 (A79 lot L3) ; thread de simulation
optionnel au lot L4.

| En-tête | Rôle |
| --- | --- |
| `system.hpp` | `Phase`, `StepContext`, `SystemDesc` seuls (en-tête léger de `orosmo/game.hpp`) : `emit(nom)` ; la forme avec charge, `Input` et `Event` demandent `simulation.hpp` |
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
auto replay = sim.recording();                               // Result<Replay> : graine + pas + entrées, rejoue la même partie
```

## Règles

- **Pas fixe** : `advance(delta)` passe par `core::FixedStepper` ; au plus `maxStepsPerFrame` pas par appel, le surplus est
  abandonné. Tant qu'aucun appel n'atteint ce plafond, le nombre de pas ne dépend que du temps cumulé ; sinon il dépend du
  découpage (pas de 10 ms, plafond 4 : `advance(100ms)` joue 4 pas, dix `advance(10ms)` en jouent 10). `step()` et `run(n)` jouent des pas sans horloge (tests, mode sans
  fenêtre de la démo). Après chaque pas, `World::updateTransforms()` est appelé.
- **Systèmes** : enregistrés par nom (règle 13), exécutés par phase (`kInput`, `kUpdate`, `kPostUpdate`), puis ordre, puis
  nom : l'ordre ne dépend pas de l'ordre d'enregistrement. Pas d'ajout ni de retrait pendant un pas (refusé), ni de pas
  imbriqué : `step`, `run`, `advance`, `restore` appelés depuis un système sont ignorés (avertissement journalisé),
  `play` et `loadState` rendent `kInvalidArgument`. Un système qui lève une exception ne laisse pas la simulation « dans un pas » :
  le pas n'est pas compté, ses entrées reviennent en tête de file sans être journalisées (un nouvel `step()` le rejoue avec
  elles) ; les entrées mises en file et les événements émis par la tentative sont abandonnés (une entrée mise en file
  pendant un pas n'entre dans `pendingInputs()` qu'à la fin du pas) ; les modifications du monde déjà faites par les
  systèmes précédents ne sont **pas** annulées. Exception pendant `play` : le filtre du rejeu reste actif jusqu'à son
  `lastStep` (la reprise par `step()` ne livre aucune entrée en double) ; ce filtre fait partie de l'état (`Snapshot::replayEnd`,
  `"replayEnd"` de `saveState`, écrit seulement s'il filtre encore) : reprendre après `restore` ou `loadState` filtre de même.
  Pendant l'`init` d'un module de jeu (`setFrozen`, posé par `app::GameModule`), `step`, `run`, `advance`, `restore`,
  `takeEvents` sont refusés (avertissement) et `play`, `loadState` rendent `kInvalidArgument`.
- **Entrées** : datées par pas, livrées dans l'ordre d'ajout à `StepContext::inputs`, enregistrées dans `inputLog()`. Une entrée
  d'un pas déjà joué est refusée, comme une entrée du pas en cours mise en file par un système (ses entrées sont déjà
  prélevées) ; les pas suivants restent ouverts. La charge (`core::Json`) ne contient ni infini ni NaN (`dump()` les écrit
  `null` : type changé au rechargement, charges confondues dans l'empreinte), ni valeur binaire (relue comme un objet
  `{bytes, subtype}`), ni chaîne ou clé hors UTF-8 (`dump()` lèverait) ; le nom est aussi en UTF-8 : refusée par
  `queueInput`, `play`, `loadState`. Une valeur JSON `discarded` (même imbriquée) est refusée aussi.
  `restorePending(file)` remplace la seule file (monde, journal, événements et temps accumulé inchangés) : retour arrière
  d'une mise en file, p. ex. par un `init` de module de jeu rejeté.
  Les entiers positifs signés (document construit en C++) et non signés (document relu) sont acceptés partout.
- **Capture** : `Snapshot` est une copie profonde (le monde est copiable). Seul un état pris **entre deux pas** se restaure :
  `capture()` / `saveState()` appelés par un système pendant un pas donnent un état partiel (`Snapshot::partial`,
  `"partial": true`), refusé par `restore` (avertissement) et `loadState` (`kInvalidArgument`). `restore` garde le journal des
  entrées d'avant le pas restauré s'il le couvre (retour en arrière dans le préfixe connu) ; sinon (pas antérieur au journal,
  ou postérieur au pas courant) le journal repart du pas restauré. Il remet à zéro le temps accumulé. Les systèmes ne font pas partie de l'état. `saveState` / `loadState` : même
  état en JSON (scène par `scene_file.hpp`, générateur par `state`/`increment`), tout ou rien ; une entrée en attente
  antérieure au pas sauvegardé est refusée (`kParse`). `loadState` vide le journal (autre partie) : `firstLoggedStep()`
  devient le pas chargé.
- **Rejeu** : `recording(first)` rend `kInvalidArgument` si `first` précède `firstLoggedStep()` (entrées inconnues : le
  rejeu serait faux) ou dépasse le pas courant. Un `Replay` porte aussi la durée du pas (`stepNanoseconds`) et le flux du
  générateur (`stream`) ; `play` refuse un rejeu enregistré avec d'autres réglages (durée à 0 : rejeu écrit à la main, non
  vérifiée), valide tout le lot avant d'insérer (entrées dans `[firstStep, lastStep)`, charges acceptées par `queueInput`)
  et, dans cet intervalle, fait foi : il remplace les entrées déjà en attente (venues d'un instantané) et ignore celles
  qu'un système remet en file pendant le rejeu (le journal les contient déjà, dans leur ordre de livraison) ; celles d'après
  `lastStep` restent.
- **Empreinte** : `fingerprint()` = `scene::hashWorld` (champs décrits, ordre dense, table des entités) + état du générateur +
  pas + entrées en attente (tout NaN d'un champ haché comme le NaN canonique : même empreinte après un aller-retour JSON).
- **Déterminisme** (règle 9) : même graine, mêmes réglages (durée du pas, donc `ctx.dt`, et flux du générateur ; `play` les
  vérifie), mêmes systèmes, mêmes entrées et état de départ équivalent au pas de départ => même empreinte, sur toutes les
  plateformes, si les
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
  Aucun événement n'est perdu (arriéré côté simulation si la file est pleine, repassé dans la file à chaque tour, même sans
  pas). `stop()` joue d'abord le lot accepté par le dernier `frame()` ; après l'arrêt, `pushInput` rend faux. Un système qui
  lève sur le thread l'arrête sans terminer le processus : `status()` rend l'erreur, `running()` devient faux et `pushInput`
  rend faux dès la sortie du thread (avant même `stop()`, qui le rejoint). Une entrée refusée par `queueInput` à son arrivée
  est journalisée avec sa vraie cause (pas déjà joué ou charge refusée). `RenderSnapshot::read<T>` contrôle la
  disposition enregistrée (taille, alignement, champs si T est réfléchi) ; `readBytes` copie les octets. Côté rendu, un seul thread appelle les
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

Guide du site : `site/src/content/docs/guides/scene/simulation.md` ; architecture : `site/src/content/docs/interne/scene-sim-app.md`.
