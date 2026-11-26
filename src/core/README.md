# core

Couche 0 de `docs/03` : ne dépend d'aucun autre module du moteur (bibliothèque standard, `glm`, `nlohmann-json`). Code
dans `include/orosmo/core/*.hpp`, `namespace orosmo::core`, `camelCase` / `kPascalCase`. Le `Logger` repris de l'ancien
`Utilities` garde ses noms de méthodes (`Logger::Log`, niveaux `L_INFO`...) et ses macros `LOG_*` : seul son emplacement
a changé. Le module n'appelle pas le système directement (voir `system.hpp`). Archive : `libcore.a`.

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
| `reflection.hpp`, `serialization.hpp` | CORE-12 | `Reflect<T>` + `field(...)`, JSON et MessagePack, documents versionnés + migrations ; `describeFields<T>()` : `FieldDesc` autonome (nom, type, plage, texte, et depuis M8 L2 décalage, taille, sous-champs) pour lire une structure sans son type C++ (`scene`) |
| `parse.hpp` | — | `parseFloat`, `parseDouble` : syntaxe de `std::from_chars`, indépendants de la locale et du système (`strtod_l` en locale « C » sur macOS, où `from_chars` des flottants manque pour les cibles anciennes) |
| `file_watcher.hpp` | CORE-13 | `FileWatcher` par interrogation (fichier ou dossier récursif). `setPollInterval(ms)` : cadence sur l'horloge réelle (un `poll()` trop proche du précédent ne fait rien, la démo l'appelle à chaque image avec 500 ms) ; `setFullScanEvery(n)` : un dossier n'est parcouru en entier (fichiers créés) qu'un `poll()` sur `n`, les autres ne vérifient que les fichiers suivis (modifiés, supprimés) ; `stats()` compte les interrogations |
| `log.hpp` | CORE-01 | `Logger` et macros `LOG_INFO`, `LOG_WARNING`, `LOG_ERROR(code, ...)`, `SET_LOG_FILE`, `FLUSH_LOG_TO_FILE` : console colorée, répétitions regroupées, fichier de journal, pile d'appels en DEBUG (`<stacktrace>` : sous GCC 13, lier `-lstdc++_libbacktrace`, GCC 14+ `-lstdc++exp`, fait dans `tools/engine-sdk/engine.mk`) ; mémoire bornée (`Logger::kMaxKeptLogs` messages, les plus anciens écrits dans le fichier puis oubliés), doublon cherché parmi les `kDuplicateSearchDepth` derniers messages, date mise en cache à la seconde |
| `paths.hpp` | — | `GET_RESOURCE_PATH` (préfixe `res/` hors release), `setWorkingDirectoryToExecutable`, `validateAssets` |
| `profiler.hpp`, `gpu_timer.hpp` | CORE-05 | `Profiler` (repris de l'ancien module `Profiler`, noms inchangés) : `Profile(nom, f)`, `ProfileGPU(nom, f)` (boucle chaude : `Profiler::Key k = MakeKey(nom)` une fois, puis `Profile(k, f)` / `ProfileGPU(k, f)` sans chaîne ni recherche de table ; mêmes noms affichés), `SetGpuTimingsEnabled(bool)` (coupées : `ProfileGPU` mesure sur le CPU et ne demande aucune minuterie au fournisseur), `GetAverageTime(nom)` ; minuteries GPU par une `IGpuTimerFactory` que le renderer installe (`SetGpuTimerFactory`), sinon horloge CPU ; historique : un point toutes les 3 s, `kProfilerMaxHistory` points au plus ; une minuterie sans mesure depuis 3 s est vidée |
| `fps_counter.hpp`, `ring_buffer.hpp` | — | `FPSCounter` (images par seconde, limiteur de cadence) et `RingBuffer<T>` (moyenne, min, max) |
| `random.hpp` | M8 (A79 L1) | `Random` : PCG32 (XSH RR, état 64 bits, flux), entièrement `constexpr` : `nextU32`, `nextU64`, `nextBelow(n)` (sans biais), `range(lo, hi)` entier ou réel, `nextFloat` / `nextDouble` dans [0, 1) par conversion exacte, `advance(n)` en O(log n), `state()` / `fromState` pour la rejouabilité. Même suite sur toutes les plateformes (règle 9) ; `Random(42)` = suite de référence `pcg32-demo`, 1 000 valeurs figées dans `tests/core/random_seed42_table.inc` |
| `spsc_queue.hpp` | M8 (A79 L1) | `SpscQueue<T>` : file bornée sans verrou à un producteur et un consommateur (simulation → rendu) : `tryPush` / `emplace`, `front` / `pop`, `tryPop`. Capacité arrondie à une puissance de deux, aucune allocation après la construction, indices sur des lignes de cache séparées. Testée entre deux threads (1e6 éléments, ordre gardé), job CI TSan |
| `benchmark.hpp` | — | `orosmo::core::bench` : mesures reproductibles (`BenchmarkConfig`, `ScenarioConfig`, `Scenario`, `ScenarioRegistry`, `CounterSink`, `Runner`, `Statistics`, `computeStatistics`, `BenchmarkReport`, `writeReport`) |
| `system.hpp` | M1 bis | Services du système (largeur de console, chemin de l'exécutable, dossier de données, attente précise) : **déclarés ici, définis par `platform`** (`src/platform/system.cpp`). `core` n'inclut aucun en-tête système |

## Règles d'usage

- **Erreurs** : une fonction qui peut échouer renvoie `Result<T>` ; pas d'exception dans le flux normal. Une exception qui s'échappe d'une tâche est journalisée et la tâche compte comme terminée.
- **Temps** : la simulation reçoit des deltas (`FixedStepper::advance`), jamais l'horloge murale ; tests avec `ManualClock`.
- **Positions** : `WorldPosition` (double) ne se convertit pas implicitement en `Vec3` ; passer par `toLocal(position, origine)`.
- **Réflexion** : décrire une structure une fois (`Reflect<T>::fields()`), la sérialisation et l'interface automatique (UI-12) en dérivent. Champ absent à la lecture : valeur par défaut gardée ; champ inconnu : ignoré ; type erroné ou hors plage : `Error` qui nomme le champ (`layers[0].weight`).
- **Fils d'exécution** : `TaskSystem`, `ResourceCache`, `TraceRecorder`, `Logger` sont thread-safe ; `EventBus`, `Config`, `Vfs`, `FileWatcher`, `ArenaAllocator`, `ObjectPool` ne le sont pas (un propriétaire, ou verrou extérieur).
- **Aucun appel graphique** dans ce module (règle 4), ni `<windows.h>` / `getenv` : les services du système passent par `system.hpp`. `std::filesystem` (bibliothèque standard, portable) reste utilisé par `Vfs`, `FileWatcher`, `Config` et `TraceRecorder` (`docs/QUESTIONS.md`, A24).

## Tests et démo

Tests : `tests/core/*.cpp` (le logger : `tests/core/log_tests.cpp` ; le Profiler : `profiler_tests.cpp`, `profiler_gpu_timer_tests.cpp` ; le benchmark : `benchmark_tests.cpp`) ; `make demo-test`.
Pas de cible `make` dédiée à une démo du noyau : il est exercé par les tests (`make demo-test-unit`, sans GPU ni fenêtre) et par la démo unique (`make run`).
