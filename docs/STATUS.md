# STATUS

Mis à jour à la fin de M1 (2026-11-17). États détaillés et preuves de l'audit : [AUDIT_REPORT.md](AUDIT_REPORT.md) (instantané du 2026-11-17, non régénéré). Liste de travail : [GAPS.md](GAPS.md).

## Jalons

| Jalon | État | Preuve |
| --- | --- | --- |
| M00 Nettoyage du projet et du dépôt | Fait | `docs/releases/M00.md` : clone propre de `dev`, build et 102 tests verts, journal de démarrage identique à la référence, `tools/check-repo.sh` OK |
| Audit | Fait | `docs/AUDIT_REPORT.md` (152 identifiants, preuve par ligne), `PORTABILITY_REPORT.md`, `UI_LANGUAGE_AUDIT.md` (compilateur exécuté sur 5 entrées) |
| **M0 Audit et base solide** | **Livré côté code ; CI jamais observée sur GitHub** | `docs/releases/M0.md` : clone propre de `dev`, build sans avertissement, 118 cas doctest verts, ASan+UBSan propres, contrôles, capture identique à la référence |
| **M1 Noyau** | **Livré côté code ; CI Windows/macOS à observer au push** | `docs/releases/M1.md` : module `core` (13 fichiers), 208 cas doctest verts (90 de plus qu'en M0), ASan+UBSan propres (208 cas), capture identique à la référence (écart max 0/255), `make core-demo` |
| **M1 bis Remise en ordre des couches** | **Livré côté code ; Windows/macOS à observer à la CI** | `docs/releases/M1bis.md` : `check_layers.py --strict` : 0 violation (5 avant), `check_headers.py --strict` : 0 (1 avant), `<windows.h>` et `getenv` hors de `core`, G12 et G13 traités, 224 cas doctest verts (10 de plus qu'en M1), ASan+UBSan propres, capture identique à la référence (écart max 0/255) |

## Identifiants de `docs/02` touchés par M1

Preuves mesurées sous Linux (g++ 13.3). Windows et macOS : la CI du push les exercera (limite notée, `docs/GAPS.md` G28) ; un identifiant `Fait` ici l'est sur cette preuve, hors portabilité observée.

| ID | Avant | Maintenant | Fichier / symbole, test, résultat |
| --- | --- | --- | --- |
| CORE-01 | Partiel | **Fait** | `include/Utilities/Logger.h` `Logger` ; `tests/suites/logger_tests.cpp` (6 cas : fichier, flush, dédoublonnage, `Clear`, 4 fils) verts. Limites : couleurs et `<stacktrace>` non testés ; G50 (exception de `Initialize`) |
| CORE-02 | Absent | Partiel | `include/engine/core/error.hpp` `Result<T>`, `makeError`, `ENGINE_ASSERT` ; `tests/core/error_tests.cpp` verts. **Partiel** : le code existant lance encore des exceptions (G51) |
| CORE-03 | Absent | **Fait** | `memory.hpp` `ArenaAllocator`, `ObjectPool<T>` ; `tests/core/memory_tests.cpp` (alignement, plein, recyclage, `liveCount()`) ; usage documenté dans `src/core/README.md` ; pas encore employé par le moteur (G57) |
| CORE-04 | Absent | **Fait** | `tasks.hpp` `TaskSystem`, `TaskGroup` ; `tests/core/tasks_tests.cpp` : 10 000 tâches, dépendances, attente dans une tâche avec 1 fil, arrêt propre ; 30 exécutions consécutives sans échec ; `make core-demo` : 10 000 tâches en ~17 ms |
| CORE-05 | Partiel | Partiel | `trace.hpp` `TraceRecorder`, `ENGINE_TRACE_SCOPE`, export Chrome trace ; `tests/core/trace_tests.cpp` ; trace de 10 002 événements relue en JSON. **Manque** : minuteries GPU branchées sur le RHI (G54) |
| CORE-06 | Partiel | **Fait** | `config.hpp` `Config` ; `tests/core/config_tests.cpp` (couches, CLI, rechargement, aide) ; la démo principale utilise encore son analyseur (G52) |
| CORE-07 | Partiel | **Fait** | `time.hpp` `FrameTimer`, `FixedStepper`, `ManualClock` ; `tests/core/time_tests.cpp` (même temps de simulation pour deux séquences de deltas) |
| CORE-08 | Absent | **Fait** | `events.hpp` `EventBus` ; `tests/core/events_tests.cpp` : `publish` sans allocation (compteur d'`operator new`), abonnement pendant la diffusion |
| CORE-09 | Partiel | **Fait** | `math.hpp` `WorldPosition` (double), `toLocal`/`toWorld` ; `tests/core/math_tests.cpp` : précision de 2 mm à 10 000 km de l'origine ; pas encore utilisé par le terrain (M3) |
| CORE-10 | Absent | **Fait** | `resources.hpp` `Handle<T>`, `ResourceCache<T>` ; `tests/core/resources_tests.cpp` : génération, comptage, chargement asynchrone, rechargement à chaud, échec |
| CORE-11 | Partiel | **Fait** | `vfs.hpp` `Vfs` ; `tests/core/vfs_tests.cpp` (montages superposés, `..` refusé) ; macros héritées non migrées (G53) |
| CORE-12 | Absent | **Fait** | `reflection.hpp`, `serialization.hpp` ; `tests/core/serialization_tests.cpp` : JSON et MessagePack exacts, version, migration, erreurs qui nomment le champ |
| CORE-13 | Absent | Partiel | `file_watcher.hpp` `FileWatcher` ; `tests/core/file_watcher_tests.cpp` verts sous Linux. **Manque** : preuve sur Windows et macOS (CI) ; interrogation, pas d'API native (G55) |
| TEST-01 | Partiel | Partiel | `tests/core/*.cpp` (83 cas) ; manque la mesure de couverture (G58) |
| A12 / G36 | — | Traité | `Noise()` et `PCGRandom()` supprimés (`static_assert` dans `tests/suites/noise_tests.cpp`) ; la démo tire et journalise sa graine |

Total après M1 : Fait 11, Partiel 44, Absent 94, Incompatible 3 (après M0 : 1 / 47 / 101 / 3).

## Identifiants de `docs/02` touchés par M0

Seul `REPO-01` est `Fait` : un identifiant n'est `Fait` que si tout son critère est exercé, et la CI (qui exerce TEST-01, BLD-02, TEST-02) n'a jamais tourné sur GitHub.

| ID | Avant (audit) | Maintenant | Preuve |
| --- | --- | --- | --- |
| TEST-01 | Partiel (framework maison) | Partiel | `tests/support/test_support.hpp`, `tests/suites/*.cpp` ; `make demo-test` : 118 cas / 647 assertions verts (`docs/BASELINE/tests-M0.txt`) ; manque : exécution en CI, couverture de `core` |
| TEST-02 | Partiel | Partiel | `mk/engine.mk` (`WERROR`, `SANITIZE`) ; build `-Wall -Wextra -Wpedantic -Werror` : 0 avertissement ; `make demo-test SANITIZE=address,undefined` : verts, aucun rapport ASan/UBSan (leaks désactivés) ; manque : clang-tidy / clang-format, CI |
| BLD-02 | Partiel | Partiel | `.github/workflows/ci.yml` (YAML valide ; étapes Linux rejouées localement) ; jamais exécuté sur GitHub |
| TOOL-03 | Absent (P2) | Partiel (**P0**) | `src/Graphics/Screenshot.cpp`, `Renderer::SaveColorPng`, `main --capture` ; 3 captures identiques ; `tools/compare_png.py` (6 tests Python) ; OpenGL seulement |
| TEST-03 | Partiel | Partiel | séquence PCG figée (`noise_tests.cpp`) + image de terrain reproductible ; pas de checksum multi-OS |
| RHI-14 | Absent | Absent | outil de comparaison et image de référence prêts ; pas de test automatisé par backend |
| BLD-01 | Partiel | Partiel | build Linux documenté dans `CLAUDE.md` et `README.md` ; Windows/macOS non vérifiés |
| DOC-01 | Partiel | Partiel | README racine, README par module, `CLAUDE.md` (commandes) ; manquent schéma des modules et `ADD_A_BACKEND.md` |
| REPO-01 | Fait | Fait (confirmé par l'auteur le 2026-11-17) | `sh tools/check-repo.sh` : OK ; 102 tests et journal de démarrage identiques avant/après M00 ; capture reproductible depuis M0 ; l'auteur accepte ces preuves d'identité et l'absence de `third_party/` (rien de copié) |

Total après M0 : Fait 1, Partiel 47, Absent 101, Incompatible 3 (audit initial : 1 / 46 / 102 / 3 ; seul TOOL-03 change, d'Absent à Partiel).

Commandes (Linux) : voir `CLAUDE.md` § Commandes. Démo : `make all-debug && make run`. Tests : `make demo-test`. Contrôles : `make check`.
