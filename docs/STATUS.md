# STATUS

Mis à jour à la fin du sous-lot B3 de M2 (2026-11-18). États détaillés et preuves de l'audit : [AUDIT_REPORT.md](AUDIT_REPORT.md) (instantané du 2026-11-17, non régénéré). Liste de travail : [GAPS.md](GAPS.md).

## Jalons

| Jalon | État | Preuve |
| --- | --- | --- |
| M00 Nettoyage du projet et du dépôt | Fait | `docs/releases/M00.md` : clone propre de `dev`, build et 102 tests verts, journal de démarrage identique à la référence, `tools/check-repo.sh` OK |
| Audit | Fait | `docs/AUDIT_REPORT.md` (152 identifiants, preuve par ligne), `PORTABILITY_REPORT.md`, `UI_LANGUAGE_AUDIT.md` (compilateur exécuté sur 5 entrées) |
| **M0 Audit et base solide** | **Livré côté code ; CI jamais observée sur GitHub** | `docs/releases/M0.md` : clone propre de `dev`, build sans avertissement, 118 cas doctest verts, ASan+UBSan propres, contrôles, capture identique à la référence |
| **M1 Noyau** | **Livré côté code ; CI Windows/macOS à observer au push** | `docs/releases/M1.md` : module `core` (13 fichiers), 208 cas doctest verts (90 de plus qu'en M0), ASan+UBSan propres (208 cas), capture identique à la référence (écart max 0/255), `make core-demo` |
| **M1 bis Remise en ordre des couches** | **Livré côté code ; Windows/macOS à observer à la CI** | `docs/releases/M1bis.md` : `check_layers.py --strict` : 0 violation (5 avant), `check_headers.py --strict` : 0 (1 avant), `<windows.h>` et `getenv` hors de `core`, G12 et G13 traités, 224 cas doctest verts (10 de plus qu'en M1), ASan+UBSan propres, capture identique à la référence (écart max 0/255) |
| **M2 lot A : interface RHI et backend OpenGL** | **Lot A livré côté code ; M2 en cours (lots B et C à faire)** | `docs/releases/M2-lotA.md` : `include/engine/rhi/` + `src/rhi/opengl/`, 256 cas doctest verts (32 de plus), ASan+UBSan propres, `make rhi-demo` (calcul + MRT + instanciation, 0 message de validation) ; Windows/macOS : tests GPU non exécutés (G67) |
| **M2 lot B1 + B2 : pipeline de shaders, fenêtre du RHI** | **B1 et B2 livrés côté code ; B3 à B6 et lot C à faire** | `docs/releases/M2-B2.md` : `include/engine/rhi/shader_compiler.hpp`, adoption d'une fenêtre par le périphérique OpenGL ; 269 cas doctest verts (+13), ASan+UBSan propres, capture de référence identique ; plan : `docs/05` (tableau des sous-lots) |
| **M2 lot B3 : couche de ressources `engine::renderer`** | **B3 livré côté code ; B4 à B6 et lot C à faire** | `docs/releases/M2-B3.md` : `GpuBuffer`, `Texture`, `RenderTarget`, `ShaderLibrary`, `PipelineCache`, `Image`, `GpuTimer` sur le RHI ; 280 cas doctest verts (+11), ASan+UBSan propres, capture de référence identique (code hérité non touché) |

## Identifiants de `docs/02` touchés par M2 B3

Preuves sous Linux (g++ 13.3, Mesa 26.1.6, Intel Arc ; la CI exécute aussi sous llvmpipe + xvfb). Windows et macOS : tests GPU non exécutés (G67). Les nouvelles classes ne sont **pas encore branchées** sur la démo (B4, B5) : les états de migration (RHI-02, REN-05, REN-36, UI-01, UI-02) ne changent donc pas.

| ID | Avant | Maintenant | Fichier / symbole, test, résultat |
| --- | --- | --- | --- |
| RHI-05 | Partiel | Partiel | `engine::renderer::Texture`, `GpuBuffer` : création, texels, mips selon le filtre, `resize`, relecture, libération RAII sans fuite (`stats()` revient à 0) ; `tests/renderer/resources_tests.cpp`. **Manque** : cube, tableau et 3D échantillonnés dans un shader, façade 2D seulement (G79) |
| RHI-06 | Partiel | Partiel | `PipelineCache` (un pipeline par programme, révision et état ; périmés détruits au rechargement) et cache SPIR-V disque relu par une seconde bibliothèque (testé). **Manque** : cache de pipelines natifs du pilote |
| RHI-11 | Partiel | Partiel | `GpuTimer` (`isReady`, `elapsed` sans blocage) et `Device::timestampsReady` (testés). **Manque** : relier au `Profiler` (G78), noms de debug relus (G69), Vulkan (M4) |
| RHI-12 | Partiel | Partiel | `ShaderLibrary::pollHotReload` : une modification recompile et reconstruit le pipeline, une édition cassée garde l'ancien shader (testé de bout en bout par un rendu vert puis bleu). **Manque** : shaders du dépôt encore en GLSL 430 (B4), sortie MSL (M9) |
| RHI-14 | Absent | Absent | `Image`, `readImage`, `savePng` / `loadPng`, `compareImages` (tolérance, écart max et moyen) sont prêts ; le test d'images de référence par backend en CI reste à écrire (B6) |
| TOOL-03 | Partiel | Partiel | `savePng(device, texture, chemin)` sur le RHI (testé, aller-retour PNG exact) ; la capture de la démo passe encore par l'ancien `Screenshot` |

Totaux inchangés : Fait 14, Partiel 47, Absent 90, Incompatible 1.

## Identifiants de `docs/02` touchés par M2 B1 et B2

Preuves sous Linux (g++ 13.3, Mesa 26.1.6, Intel Arc ; la CI exécute aussi sous llvmpipe + xvfb). Fenêtre : invisible dans les tests. Windows et macOS : tests GPU et fenêtre non exécutés (G67).

| ID | Avant | Maintenant | Fichier / symbole, test, résultat |
| --- | --- | --- | --- |
| RHI-12 | Partiel | Partiel | `shader_compiler.hpp` : `compileGlsl` (shaderc), `translateToGlsl430` (SPIRV-Cross), `ShaderCache`, `createShaderFromGlsl`, `ShaderHotReloader` ; `tests/rhi/shader_compiler_tests.cpp` (8 cas CPU : réflexion, traduction `set*8+binding`, erreurs avec fichier:ligne, `#include`, cache disque, rechargement à chaud ; 2 cas GPU : rendu MRT avec push constants, compute). **Manque** : compilation « hors ligne » au sens d'une étape de build (ici à l'exécution avec cache), sortie MSL (M9), shaders du dépôt encore en GLSL 430 (B4) |
| RHI-06 | Partiel | Partiel | cache disque de **SPIR-V** (`ShaderCache`, testé : survit à un autre cache, fichier corrompu ignoré). **Manque** : cache de pipelines natifs du pilote |
| RHI-08 | Partiel | Partiel | `prepareWindowHints`, `SurfaceDesc::nativeWindow`, `beginFrame`/`present`/`resize`/`setVSync` sur une fenêtre adoptée ; `tests/rhi/window_surface_tests.cpp` (5 cas : adoption, rendu et relecture sur 3 images, usages interdits du backbuffer, redimensionnement). **Manque** : vsync mesuré, fenêtre réduite, fenêtre visible (G73, G74) ; swapchain Vulkan (M4) |
| PLAT-01 | Partiel | Partiel | la surface de fenêtre côté RHI existe ; `platform::Window` reste à écrire (lot C) |

Totaux inchangés : Fait 14, Partiel 47, Absent 90, Incompatible 1.

## Identifiants de `docs/02` touchés par M2 lot A

Preuves mesurées sous Linux (g++ 13.3, Mesa 26.1.6, Intel Arc, OpenGL 4.6 core ; la CI exécute aussi les tests sous Mesa llvmpipe + xvfb). Le RHI n'est branché sur **aucun** code de rendu existant : la migration (éclairage, texte, UI) est le lot B, donc RHI-02 et les identifiants de migration restent inchangés. Windows et macOS : tests GPU non exécutés (G67).

| ID | Avant | Maintenant | Fichier / symbole, test, résultat |
| --- | --- | --- | --- |
| RHI-01 | Incompatible | Partiel | `include/engine/rhi/{handle,types,capabilities,device}.hpp` : `Device`, `CommandEncoder`, handles opaques ; `check_headers --strict` (102 en-têtes, 0 non autonome) ; `tests/rhi/handle_tests.cpp`. **Manque** : aucune façade (`Mesh`, `Texture`…) ne l'utilise encore (lot B) |
| RHI-04 | Partiel | Partiel | `createDevice`, `isBackendCompiled` (`src/rhi/device.cpp`) : Vulkan et Metal refusés avec `kUnsupported` (testé). **Manque** : l'option `--backend` de la démo, le repli documenté |
| RHI-05 | Partiel | Partiel | `createTexture` 2D/tableau/cube/3D, mips (`generateMips`), échantillonneur ; `tests/rhi/opengl_device_tests.cpp` : 12 formats aller-retour octet pour octet, régions, niveaux, erreurs. **Manque** : cube et tableau jamais échantillonnés dans un shader (seulement cibles de rendu) |
| RHI-06 | Partiel | Partiel | `createGraphicsPipeline` / `createComputePipeline` : état explicite par pipeline, validé contre la passe (testé). **Manque** : cache de pipelines **sur disque** (lot B) |
| RHI-07 | Absent | Partiel | `BindGroupDesc`, `setBindGroup` ; groupes 0/1/2 aplatis `groupe × 8 + binding` ; tests : UBO à décalage dynamique, texture + échantillonneur, SSBO, image de stockage, erreurs. **Manque** : table de réflexion des shaders (lot B), traduction Vulkan (M4) |
| RHI-08 | Partiel | Partiel | `beginRenderPass`, `LoadOp`/`StoreOp`, `beginFrame`/`present`/`resize` (handle conservé) ; testé hors écran. **Manque** : swapchain de fenêtre et vsync (lot C) |
| RHI-09 | Absent | **Fait** (OpenGL) | `CommandEncoder::dispatch`, `Capabilities::maxComputeWorkGroup*` ; `tests/rhi/opengl_device_tests.cpp` « compute writes a storage buffer… » : 64 éléments exacts ; barrières calculées par le RHI (conservatrices, G70) |
| RHI-10 | Partiel | Partiel | `draw`/`drawIndexed` instanciés avec `firstInstance`, `drawIndirect`/`drawIndexedIndirect` multi-draw (testés). **Manque** : compteur lu sur le GPU (G71) |
| RHI-11 | Partiel | Partiel | `writeTimestamp`, `readTimestamps` (testé : valeurs croissantes) ; `glObjectLabel`/`glPushDebugGroup` appelés. **Manque** : noms non relus ni observés dans RenderDoc (G69) ; Vulkan (M4) |
| RHI-15 | Partiel | **Fait** (OpenGL) | `RenderPassDesc` : cibles RGBA8 + RG16F + R32UI dans un seul shader, `LoadOp`/`StoreOp` par cible, limite lue dans `Capabilities::maxColorAttachments` (test) ; seules 3 cibles ont été rendues, la limite (8 ici) est vérifiée par refus |
| RHI-16 | Absent | Partiel | Texture 3D R32F écrite par un shader de calcul et relue tranche par tranche (testé) ; RG16F, RGBA16F, R32F, R32UI créables et aller-retour exacts. **Manque** : `imageLoad` en compute, chaque format garanti utilisé comme image de stockage |
| RHI-17 | Absent | **Fait** (OpenGL) | `dispatchIndirect` (test : 2 groupes lus dans un tampon), décalages dynamiques de tampons uniformes (test) |
| RHI-18 | Incompatible | Partiel | `Capabilities` du RHI : `rayQuery`, `accelerationStructure`, `bindless`, `meshShader`, `bufferDeviceAddress`, `sparseResources` toutes `false` (testé). **Manque** : l'ancien backend Vulkan annonce encore `true` à tort jusqu'à son retrait (lot B) |

Total après M2 lot A : Fait 14, Partiel 47, Absent 90, Incompatible 1 (après M1 : 11 / 44 / 94 / 3).

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
