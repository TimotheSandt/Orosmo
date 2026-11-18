# core

Couche 0 de `docs/03` : ne dépend d'aucun autre module du moteur (bibliothèque standard, `glm`, `nlohmann-json`, et le
logger existant `Utilities/Logger.h`). Code neuf dans `include/engine/core/*.hpp`, `namespace engine::core`, `camelCase` /
`kPascalCase` ; le code hérité (`Logger`, `Profiler`, `Utilities`) migre en M1 bis. Archive : `libcore.a`.

| Fichier | ID | Rôle |
| --- | --- | --- |
| `error.hpp` | CORE-02 | `Error`, `ErrorCode`, `Result<T>` (= `std::expected<T, Error>`), `makeError`, `logError`, `ENGINE_ASSERT` |
| `memory.hpp` | CORE-03 | `ArenaAllocator` (une image : `allocate`, puis `reset`), `ObjectPool<T>` (liste libre, `liveCount()` pour détecter les fuites) |
| `tasks.hpp` | CORE-04 | `TaskSystem` (pool, `submit` avec dépendances, `wait`, `waitIdle`, `parallelFor`), `TaskGroup` |
| `trace.hpp` | CORE-05 | `TraceRecorder`, `ENGINE_TRACE_SCOPE`, statistiques, export Chrome trace (Perfetto) |
| `config.hpp` | CORE-06 | `Config` : défauts < fichier JSON < ligne de commande, diff au rechargement, `applyTo(structure réfléchie)` |
| `time.hpp` | CORE-07 | `IClock`, `SteadyClock`, `ManualClock`, `FrameTimer` (delta variable), `FixedStepper` (pas fixe) |
| `events.hpp` | CORE-08 | `EventBus` typé, `Subscription` RAII, `publish` sans allocation |
| `math.hpp` | CORE-09 | alias `float` (glm), `WorldPosition` en `double`, `toLocal` / `toWorld` (origine flottante) |
| `resources.hpp` | CORE-10 | `Handle<T>` avec génération, `ResourceCache<T>` (comptage, chargement asynchrone, rechargement à chaud) |
| `vfs.hpp` | CORE-11 | `Vfs` : `res://`, `user://`, montages superposés, `..` refusé |
| `reflection.hpp`, `serialization.hpp` | CORE-12 | `Reflect<T>` + `field(...)`, JSON et MessagePack, documents versionnés + migrations |
| `file_watcher.hpp` | CORE-13 | `FileWatcher` par interrogation (fichier ou dossier récursif) |

## Règles d'usage

- **Erreurs** : une fonction qui peut échouer renvoie `Result<T>` ; pas d'exception dans le flux normal. Une exception qui s'échappe d'une tâche est journalisée et la tâche compte comme terminée.
- **Temps** : la simulation reçoit des deltas (`FixedStepper::advance`), jamais l'horloge murale ; tests avec `ManualClock`.
- **Positions** : `WorldPosition` (double) ne se convertit pas implicitement en `Vec3` ; passer par `toLocal(position, origine)`.
- **Réflexion** : décrire une structure une fois (`Reflect<T>::fields()`), la sérialisation et l'interface automatique (UI-12) en dérivent. Champ absent à la lecture : valeur par défaut gardée ; champ inconnu : ignoré ; type erroné ou hors plage : `Error` qui nomme le champ (`layers[0].weight`).
- **Fils d'exécution** : `TaskSystem`, `ResourceCache`, `TraceRecorder`, `Logger` sont thread-safe ; `EventBus`, `Config`, `Vfs`, `FileWatcher`, `ArenaAllocator`, `ObjectPool` ne le sont pas (un propriétaire, ou verrou extérieur).
- **Aucun appel graphique** dans ce module (règle 4).

## Tests et démo

Tests : `tests/core/*.cpp` (+ `tests/suites/logger_tests.cpp` pour le logger existant) ; `make demo-test`.
Démo sans fenêtre : `make core-demo` (10 000 tâches, rechargement à chaud d'une configuration, mesures) ;
`make core-demo CORE_DEMO_ARGS="--help"` liste les réglages.
