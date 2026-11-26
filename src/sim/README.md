# sim

Couche 9 de `docs/03` §1 : boucle de simulation à pas fixe, déterministe et rejouable (SIM-01). Archive : `libsim.a`
(`src/sim/`), API publique `include/orosmo/sim/`, namespace `orosmo::sim`. Dépend de `scene` (monde, sauvegarde, empreinte)
et de `core` (`FixedStepper`, `Random`, sérialisation). Créé en M8 (A79 lot L3).

| En-tête | Rôle |
| --- | --- |
| `simulation.hpp` | `Simulation` (monde, générateur, pas fixe, systèmes, entrées datées, capture et restauration, état JSON, rejeu, empreinte), `SystemDesc` / `StepContext` / `Phase`, `Input`, `Snapshot`, `Replay` (`replayToJson`, `replayFromJson`) |

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
- **Thread** : un propriétaire. Le thread de simulation, l'instantané immuable en double tampon lu par le rendu et
  l'interpolation viennent avec le lot L4 ; `Snapshot` (copie du monde) en est la base, `alpha()` donne déjà la fraction.

## Démo et tests

- `apps/demo/bin/debug/main --sim-agents 500 --seed 42 --sim-steps 1000` : 500 agents sans fenêtre ni GPU
  (`apps/demo/src/Sim/agent_simulation.cpp`), affiche l'empreinte. `tests/demo/agent_simulation_tests.cpp` la compare à une
  valeur figée (`make demo-test-unit`, donc chaque job de CI).
- `tests/sim/simulation_tests.cpp` (pas fixe, ordre des systèmes, entrées, capture et restauration, état JSON, rejeu),
  `tests/scene/scene_file_tests.cpp` (sauvegarde, migration, empreinte).

Limites : voir `docs/GAPS.md` (G280 et suivants).
