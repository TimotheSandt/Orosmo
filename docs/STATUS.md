# STATUS

Mis à jour pour la distribution du SDK (`v0.6.2`, A76, 2026-11-25), après le jalon M14 (site vitrine et wiki, 2026-11-24, intégré à `dev` le 2026-11-25), après M6 (`v0.6.0`, 2026-11-24), après M5 (`v0.5.0`, 2026-11-22), après la `v0.4.1` (2026-11-21), après la fin de M4, après M3 (2026-11-19) et le nettoyage « Orosmo » (2026-11-20). États détaillés et preuves de l'audit : [AUDIT_REPORT.md](AUDIT_REPORT.md) (instantané du 2026-11-17, non régénéré). Liste de travail : [GAPS.md](GAPS.md).

## Jalons

| Jalon | État | Preuve |
| --- | --- | --- |
| M00 Nettoyage du projet et du dépôt | Fait | `docs/releases/M00.md` : clone propre de `dev`, build et 102 tests verts, journal de démarrage identique à la référence, `tools/check-repo.sh` OK |
| Audit | Fait | `docs/AUDIT_REPORT.md` (152 identifiants, preuve par ligne), `PORTABILITY_REPORT.md`, `UI_LANGUAGE_AUDIT.md` (compilateur exécuté sur 5 entrées) |
| **M0 Audit et base solide** | **Livré côté code ; CI jamais observée sur GitHub** | `docs/releases/M0.md` : clone propre de `dev`, build sans avertissement, 118 cas doctest verts, ASan+UBSan propres, contrôles, capture identique à la référence |
| **M1 Noyau** | **Livré côté code ; CI Windows/macOS à observer au push** | `docs/releases/M1.md` : module `core` (13 fichiers), 208 cas doctest verts (90 de plus qu'en M0), ASan+UBSan propres (208 cas), capture identique à la référence (écart max 0/255), `make core-demo` |
| **M1 bis Remise en ordre des couches** | **Livré côté code ; Windows/macOS à observer à la CI** | `docs/releases/M1bis.md` : `check_layers.py --strict` : 0 violation (5 avant), `check_headers.py --strict` : 0 (1 avant), `<windows.h>` et `getenv` hors de `core`, G12 et G13 traités, 224 cas doctest verts (10 de plus qu'en M1), ASan+UBSan propres, capture identique à la référence (écart max 0/255) |
| **M2 lot A : interface RHI et backend OpenGL** | **Lot A livré côté code ; M2 en cours (lots B et C à faire)** | `docs/releases/M2-lotA.md` : `include/orosmo/rhi/` + `src/rhi/opengl/`, 256 cas doctest verts (32 de plus), ASan+UBSan propres, `make rhi-demo` (calcul + MRT + instanciation, 0 message de validation) ; Windows/macOS : tests GPU non exécutés (G67) |
| **M2 lot B1 + B2 : pipeline de shaders, fenêtre du RHI** | **B1 et B2 livrés côté code ; B3 à B6 et lot C à faire** | `docs/releases/M2-B2.md` : `include/orosmo/rhi/shader_compiler.hpp`, adoption d'une fenêtre par le périphérique OpenGL ; 269 cas doctest verts (+13), ASan+UBSan propres, capture de référence identique ; plan : `docs/05` (tableau des sous-lots) |
| **M2 lot B3 : couche de ressources `orosmo::renderer`** | **B3 livré côté code ; B4 à B6 et lot C à faire** | `docs/releases/M2-B3.md` : `GpuBuffer`, `Texture`, `RenderTarget`, `ShaderLibrary`, `PipelineCache`, `Image`, `GpuTimer` sur le RHI ; 280 cas doctest verts (+11), ASan+UBSan propres, capture de référence identique (code hérité non touché) |
| **M2 lot B4 : couche de scène `orosmo::renderer`** | **B4 livré côté code ; B5, B6 et lot C à faire** | `docs/releases/M2-B4.md` : `Camera`, `LightManager`, `Mesh`, `SceneRenderer`, shaders `shaders/rhi/` ; la scène de terrain graine 1 rendue par la nouvelle couche est **identique pixel pour pixel** à l'image de référence du rendu hérité (tolérance 0, Intel Arc) ; 290 cas doctest verts (+10) |
| **M2 lot B5 : dessin 2D et interface par listes de dessin** | **B5 livré côté code ; B6 et lot C à faire** | `docs/releases/M2-B5.md` : `DrawList`, `CanvasRenderer`, `UI::Manager::Collect` ; **`ui.png` de l'ancien rendu (scène + panneau + boîtes + texte figé) reproduit sans aucun pixel d'écart** ; 304 cas doctest verts (+14) |
| **M2 lot B6 : bascule de la démo, retrait de l'ancien rendu, isolation bloquante** | **Lot B livré côté code ; lot C à faire** | `docs/releases/M2-B6.md` : la démo présente via le RHI (`scene.png` identique à la référence, écart max 0/255) ; `IGraphicsDevice`, backends OpenGL/Vulkan/Metal et façades supprimés (−16 800 lignes, étiquette `archive/pre-M2-B6-legacy-graphics`) ; `check_api_isolation.py --graphics-only --strict` : **0 infraction** ; 259 cas doctest verts |
| **M2 lot C : plateforme (fenêtre, entrées, mode sans fenêtre), isolation totale** | **Lot C livré côté code ; M2 terminé côté code, à proposer à `main`** | `docs/releases/M2.md` : `Window` et `InputManager` dans `src/platform/` avec GLFW caché (poignée `void *`), DPI (`GetContentScale`, tampon en pixels réels), molette (le `TODO` disparaît), `--headless`, `--vsync` ; `check_api_isolation.py --strict` **sans baseline : 0 infraction** (GLFW et `<windows.h>` compris) ; capture sans fenêtre **identique** à la référence ; 264 cas doctest verts |
| **M3 Premier générateur de terrain et interface de base (lots A à F)** | **Fait : fusionné dans `main` (PR #8, 2026-11-21 ; pas de release, décision de l'auteur : la première release publiée est M3 bis, v0.3.1)** | `docs/releases/M3.md` : module `orosmo::world` (bruit déterministe CPU et GPU, tuiles, niveaux de détail cousus, streaming, export, lancer de rayon), repère Vulkan du RHI et reverse-Z, dessin de debug, rendu du terrain, module `orosmo::ui` (widgets, mise en page, thèmes JSON, paramètres réfléchis), compilateur du langage vers `orosmo::ui`, `terrain-gen`, visualiseur, paquet et `orosmo init` vérifiés de bout en bout ; 386 cas doctest verts depuis un clone propre, ASan + UBSan propres, `make check`, `cui-test`, `terrain-gen-check`, `viewer-capture`, `e2e-init` verts (Linux) |
| **Nettoyage « Orosmo » (après la revue de M3)** | **Fait, dans `dev`** | `docs/releases/M3.md` § Nettoyage : modules hérités `Utilities`, `Profiler`, `Benchmark`, `Noise`, `Graphics`, `UI` retirés ou déplacés dans `orosmo::core` / la démo ; démo sur `orosmo::world` et `orosmo::ui` ; 404 cas doctest verts, ASan + UBSan propres, `make check`, `cui-test`, `terrain-gen-check`, `e2e-init` verts ; capture de la démo identique à la nouvelle référence (0 pixel d'écart, llvmpipe) |
| **M3 bis : release cœur générique (A34), jalon M11, affichage de la compilation** | **Livré côté code, dans `dev` ; à proposer à `main`** | `docs/releases/M3bis.md` : `world`, `TerrainRenderer`, shaders `terrain.*` et exemples terrain exclus de `dist/orosmo-pkg-*.zip` (gardés dans le dépôt via `ENGINE_EXTRA_MODULES`), `make e2e-init` le vérifie ; modèle `orosmo init` et `apps/minimal` génériques ; jalon **M11** (`MOD-01..04`) ; barre de progression de `make` (`tools/build_progress.py`, 5 tests Python) ; `sync-demo` sans recopie des dates |
| **M4 Backend Vulkan** | **Fusionné dans `main` (PR #10, `v0.4.0`)** | `docs/releases/M4.md` : backend `src/rhi/vulkan/` (volk, VMA, Vulkan 1.3, barrières déduites, cache de pipelines sur disque), `--backend` avec repli sur OpenGL, tests communs aux deux backends, `make demo-vulkan-check` (15 captures OpenGL/Vulkan dans la tolérance, `scene.png` identique bit à bit, validation à 0 message), `docs/PERF_OPENGL_VS_VULKAN.md`, `docs/ADD_A_BACKEND.md` ; démo unique sans procédural (A37), API de `platform` renommée (A31). Preuves chiffrées : clone propre de `dev` : 413 / 413 cas (13 521 assertions), 125 / 125 en Vulkan (suites GPU), ASan + UBSan propres, `make check`, `make demo-vulkan-check`, `make e2e-init`, `make cui-test`, `make demo-capture` verts. CI de la PR #10 verte sur Linux x64, ASan, arm64, macOS ; Windows corrigé (motif `grep` de `e2e-init`) |
| **v0.4.1 Outillage DX et SDK** | **Livré côté code ; à proposer à `main`** | `docs/releases/M4-1.md` : `orosmo doctor`, `compile_commands.json`, support CMake optionnel (`OrosmoConfig.cmake`), empreinte du compilateur `cui` dans le cache (G113). Clone de la branche : `make check`, 422 / 422 cas (13 586 assertions), `cui-test`, `demo-capture`, `demo-vulkan-check`, `e2e-init` (doctor, Make, CMake, compile_commands), 14 tests Python verts |
| **M6 Assets, matériaux et animation** | **Livré côté code, sur `milestone/M6` ; à proposer à `main`** | `docs/releases/M6.md` : modules `assets` et `anim`, import et export glTF 2.0 (cgltf), textures sRGB ou linéaires, KTX2 (BC7, ASTC, Basis), cuisson de maillages (meshoptimizer, LOD, `.omesh`), rechargement à chaud des textures, matériaux PBR avec variantes et instances, IBL (harmoniques sphériques, cube préfiltré, sondes), skinning GPU, clips, machine à états, courbes, foules instanciées (1000 agents, un draw call par primitive) ; RHI : formats compressés `kBC7*` et `kASTC4x4*`. Linux x64, g++ 13.3, Mesa 26.1.6, Intel Arc, clone propre (`b54f89d`) : `make all-debug` sans avertissement ; `make demo-test` **679 cas** (678 verts, 1 ignoré, 1 rouge : le cas de fenêtre `PLAT-01` connu instable, suite relancée seule 6 / 6) ; ASan + UBSan : même résultat, aucun signalement ASan, **3 messages UBSan** de `platform/window.hpp:34` (booléen non initialisé, code non touché par M6), **corrigé** par `fix/M6-window-params` (G201) : ASan + UBSan relancés, 0 message, 679 / 680 (1 rouge : cas de fenêtre instable) ; `make demo-test` 680 / 680 ; `make check`, `make demo-capture`, `make demo-vulkan-check` (validation à 0), `make cui-test`, tests Python verts ; démo `--scene anim` : captures `anim/scene` et `anim/ui` identiques OpenGL / Vulkan à 2/255. 10 identifiants passent à Fait, 2 à Partiel (REN-11, ASSET-05) (G195 à G201, A70 à A72) |
| **M5 Cœur du rendu** | **Livré côté code, sur `milestone/M5` ; à proposer à `main`** | `docs/releases/M5.md` : graphe de passes et plugins, `FrameData`, forward+ clusterisé, ombres en cascade avec cache, HDR et post-traitements, anticrénelage (FXAA, SMAA 1x, TAA), AOV à la demande, instancing, culling CPU et GPU (Hi-Z, multi-draw indirect), LOD, transparents triés, modes de fusion, batch 2D, mélange 2D/3D, interface économe, roue de couleur ; RHI : ajouts `Capabilities::drawIndirectFirstInstance` et `CommandEncoder::id()` (revue). Linux x64, g++ 13.3, Mesa, Intel Arc : `make demo-test` **545 / 545**, suites GPU en Vulkan **229 / 229** (316 cas hors GPU ignorés), `make check`, `make demo-vulkan-check` (validation à 0 message), `make demo-capture` verts ; ASan + UBSan : 545 / 545 sans signalement ; clone propre (`df005c7`) : tout vert. Après la revue de la PR #12 (`9423484`) : `make demo-test` **582 / 582**, GPU Vulkan **244 / 244**, GPU sur rasteriseurs logiciels **244 / 244**, `make check`, `make demo-capture`, `make demo-vulkan-check` verts. Après les optimisations (A61) : `make demo-test` **611 / 611**, GPU Vulkan **265 / 265**, GPU logiciel **265 / 265**, tout vert ; Vulkan par défaut ; 0,49 → 0,26 ms par image (Vulkan, release). 17 identifiants passent à Fait, 5 d'Absent à Partiel (G151 à G188) |
| **M14 Site vitrine et wiki** | **Livré côté code, sur `milestone/M14` ; rien n'est publié (publication après M6, au premier tag `v*` sur `main`)** | `docs/releases/M14.md` : `site/` (Astro 7 + Starlight ; **après la refonte : 64 pages**, couvrant M6 ; première livraison : 16 pages), `npm ci && npm run build && npm run check-links && npm run check-anchors` (après la refonte : 5375 liens internes, 1659 ancres, 0 cassé ; première livraison : 470 liens), `docker build` + `docker run` + `curl` (200, santé `healthy`), `.github/workflows/site.yml` (actionlint 0 erreur, jamais exécuté sur GitHub), `docs/DEPLOIEMENT_SITE.md` ; hébergement proposé en A74 |
| **M15 Façade du projet** | **Planifié (A78, 2026-11-26)** : site vitrine soigné et documentation simplifiée, en fin de plan, après la charte et le wireframe de l'auteur ; la simplicité de l'API est planifiée en M8 (A77) | `docs/05` M15 et M8 |
| **SDK (`v0.6.2`, A76)** | **Livré côté code, dans `dev` ; revue CodeRabbit de la PR #15 traitée sur `fix/SDK-review1` ; Windows et macOS non essayés (G216)** | `docs/releases/v0.6.2.md` : paquet avec `deps/`, outil `orosmo` (`tools/engine-init-rs/src/`), gabarit à `OROSMO_VERSION` (`apps/demo/Makefile`), `Generated/` dans le projet. **État final (2026-11-26, `fix/SDK-review1`)** : `cargo test` 28 + 16 verts sans avertissement ; `make e2e-init` **vert** de bout en bout (G220 corrigé : `doctor` 13 ok, 0 erreur ; 2e build `orosmo build --json` sur moteur en lecture seule) ; `orosmo migrate` essayé à la main (échec annulé, succès) ; `make demo-test-unit` 426 / 426 ; `make check` OK. *Historique (2026-11-25, `788c332`)* : `e2e-init` rouge à `doctor` (G220), 14 tests `cargo` |


## M8 lot L13 : façade d'application et rendu de scène (A79, A75.1, reste d'A75.9, A73 point 3)

Branche `feat/M8-L13-application` (depuis `dev` avec L3, L5, L10, L12a, L12b). Module `app` (`src/app/README.md`) :
`Application`, composants de rendu définis par le moteur et leur système. Décisions sous A79 « Lot L13 ». Linux x64 seulement
en local (g++ 13.3, Intel Arc, Mesa, OpenGL et Vulkan) ; Windows et macOS par la CI (G350). La démo `apps/demo` n'est pas
touchée (elle garde l'API bas niveau) ; le gabarit et le tutoriel passent à la façade en L14.

| Élément | Fichier et symbole | Commande | Résultat |
| --- | --- | --- | --- |
| A75.1 Façade d'application | `include/orosmo/app/application.hpp` (`Application::create`, `main`, `run`, `frame`, `spawn`, `addMesh`, `loadAnimatedModel`, `ApplicationSettings`, `parseArguments`) | `make demo-test` (suite `app/application-gpu`, cas « Application without a window renders the same image ») | sur OpenGL et Vulkan, l'image d'une application sans fenêtre (cube, ambiante, directionnelle, caméra en composants) est **identique** (tolérance 1/255, 0 pixel hors tolérance) à la même scène construite avec `SceneRenderer`, `LightManager`, `Mesh` et `Camera` ; 0 message de validation |
| A75.1 Repli de backend | `Application::Impl::createGraphics`, `ApplicationSettings::backendFallback`, `createDevice` | cas « Backend fallback » | Vulkan qui échoue : OpenGL (`fellBack()`, deux essais) ; `--strict-backend` : `kUnsupported` qui cite l'erreur, un essai ; pas de repli depuis OpenGL |
| A75.1 Ordre des rappels | `onUpdate`, `onUi`, `onRender`, `RenderFrame` | cas « Callbacks » | `update`, `ui`, `render` à chaque image ; jeu lié (`GameSource::fromApi`) : système et action enregistrés, 3 pas en 3 images ; pause : 0 pas ; un `onRender` qui échoue rend l'erreur |
| A75.9 (reste) Temps fourni | `FrameTime` (`dt`, `fixedStep`, `steps`, `alpha`, `simulationStep`), `maxFrameDelta`, `settings.clock` | cas « Time (A75.9) » | horloge manuelle : 0, 40 ms (2 pas), 1 s ramenée à 250 ms (8 pas, plafond) ; sans fenêtre : `dt` = un pas fixe |
| Composants de rendu | `include/orosmo/app/render_components.hpp` (`MeshRenderer`, `LightComponent`, `CameraComponent`, `AnimatedModelComponent`), `registerRenderComponents`, `ResourcePool` (`core::Handle<T>`) | suite `app/application` (sans GPU) | trivialement copiables, enregistrés par nom, sauvegardés et rechargés avec l'état de la simulation (même empreinte) ; poignée retirée jamais réutilisée |
| A73 point 3 Mode de dessin | `DrawMode`, `SceneRenderSystem::draw` | cas « Draw modes » | 3 cubes `kSingle` = 3 dessins ; `kInstanced` = 1 lot de 3, même image (tolérance 2) ; poignée périmée ignorée ; modèle animé `kSingle` (lecteur avancé) et foule `kCrowd` (2 agents, clip absent ignoré) |
| A75.2 Dispatch compute | `SceneRenderSystem::dispatch`, appelé par `Application::frame` avant `beginFrame` | cas « Draw modes » | skinning enregistré hors passe ; 0 message de validation |
| Ombres et HDR | `ApplicationSettings::shadows`, `hdr` ; casters des composants | cas « Shadows and HDR » | un caster par maillage `castShadows`, oublié quand le maillage est retiré ; lumière `kDynamic` ; caster retiré aussi quand aucune entité ne le demande (cas « Shadow casters ») |
| Revue PR #32 : arrêt et erreurs | `Application::Impl::~Impl`, `Application::frame` | cas « Destruction », « beginFrame », « present », « Capture without a window » (`tests::CountingDevice` injecte les erreurs) | rappels libérés avant le jeu et le périphérique ; erreurs de `beginFrame`/`present` propagées (kBusy toléré) ; capture au-delà de 3 images écrite ; limite avant la capture = échec |

Vérifications (Linux x64, g++ 13.3, Intel Arc, Mesa ; après fusion de `dev` avec L4 et L16) : `make all-debug` sans
avertissement ; `make check` vert ; tests **879 / 879** (1 ignoré, 372 174 assertions) et `test_game_static: OK` ; ASan +
UBSan sur les suites `app/*`, `scene/*`, `sim/*` (66 cas, 212 514 assertions, avant la fusion) sans signalement ; démo
inchangée, `--headless --capture` en Vulkan (validation : 0 message) et en OpenGL sans erreur. Non vérifié : Windows,
macOS (G350), `clang-format` absent de la machine.

## M8 lot L16 : performances décidées pour M8 (A61 G et C)

Branche `feat/M8-L16-perf-a61` (A79). Preuves sous Linux x64 (g++ 13.3, Mesa 26.1.6, Intel Arc, OpenGL et Vulkan). Aucune ligne
de `docs/02` ne change d'état (A61 n'a pas d'identifiant de checklist). Décisions : `docs/QUESTIONS.md`, A79 « Lot L16 ».

| Proposition | État | Fichier et symbole | Preuve |
| --- | --- | --- | --- |
| A61 G : images en vol réglables (1 à 3), deux par défaut | **Fait** (Windows, macOS, Metal non vérifiés : G343) | `include/orosmo/rhi/device.hpp` `DeviceDesc::framesInFlight`, `kDefaultFramesInFlight`, `kMaxFramesInFlight`, `Device::framesInFlight` ; `src/rhi/opengl/gl_device.cpp`, `src/rhi/vulkan/vk_swapchain.cpp` (`framesInFlight_`) ; `SceneRendererConfig::framesInFlight` = 0 (celle du périphérique) ; démo `--frames-in-flight` | `tests/rhi/frames_in_flight_tests.cpp` (refus de 0 et 4 ; valeur rendue ; libération après exactement N présentations ; image k finie au retour de `beginFrame` k + N ; N = 1, 2, 3, deux backends) et suite `renderer/frames-in-flight` : verts ; démo Vulkan fenêtrée et hors écran avec 1, 2, 3 images : validation 0 message. Mesure : pas de gain stable de 3 images, défaut gardé à 2 (`docs/PERF_OPENGL_VS_VULKAN.md`, section M8) |
| A61 C : graphe de passes gardé tant que plugins, cibles et tailles ne changent pas | **Fait** pour les plugins avec signature (post-traitements, GPU-driven, vues sans signature : G340) | `render_feature.hpp` `RenderFeature::passSignature`, `mixPassSignature` ; `render_graph.hpp` `RenderGraph::setImportedTexture`, `setImportedBuffer`, `compileCount` ; `scene_renderer.hpp` `GraphKey`, `setRenderGraphCaching`, `graphBuildCount`, `graphReused` ; signatures des ombres, du forward+, de l'IBL, de la couche 2D, des transparents, de la vue de debug, de `demo::FrameOverlay` ; démo `--graph-cache` | `tests/renderer/graph_cache_tests.cpp` (suite `renderer/graph-cache` : graphe construit une fois sur 6 images, sortie changée sans reconstruction et vue par la passe, taille, couleur d'effacement et plugin ajouté ou retiré : une reconstruction, signature changée ou absente, cache coupé, image en échec) et cas A61 C de `render_graph_tests.cpp` : verts ; démo : 2 à 3 reconstructions sur 600 images au lieu de 600 ; `CPU/Graph` 0,60 → 0,50 ms (scène lourde, OpenGL hors écran) |

Non-régression : 16 captures (8 configurations × OpenGL et Vulkan : HUD, ombres, widgets, post-traitements, scène mixte avec
ombres tramées, scène mixte avec post-traitements, animation, forward+) faites avec le binaire d'avant puis d'après :
les 16 `scene.png` identiques au pixel près (`tools/compare_png.py`, tolérance 0), avec 2 et avec 3 images en vol ; les `ui.png` sans HUD identiques, celles avec HUD diffèrent seulement par les temps affichés (deux exécutions du binaire d'avant diffèrent de la même façon).

Vérifications : `make all-debug` sans avertissement ; `make demo-test` (après fusion d'`origin/dev`, L3) : 830 cas, 830 verts, 1 ignoré ; `make check` vert ; démo debug Vulkan et OpenGL (`--ui full --scene mixed --shadows on`, `--frames-in-flight 1` et `3`, hors écran, et Vulkan fenêtré avec 1, 2, 3) : validation Vulkan 0 message, aucune erreur GL_KHR_debug ; ASan + UBSan (`make fclean && make demo-fclean && make demo-test SANITIZE=address,undefined TEST_ARGS='--test-suite=*frames-in-flight*,renderer/*,rhi/backend*'`) : 249 cas verts, aucun rapport. Démo : `apps/demo/bin/debug/main --frames-in-flight 3 --graph-cache on` (journal « Perf: framesInFlight=3 graphBuilds=… »). Windows et macOS non vérifiés (G343).

## M8 lot L4 : thread de simulation optionnel (A79, extension de SIM-01)

Branche `feat/M8-L4-thread-simulation` (depuis `feat/M8-L3-sauvegarde-simulation`). Module `sim` (`src/sim/README.md`) :
`simulation_thread.hpp`, `snapshot_buffer.hpp` ; `Simulation` reçoit événements et arène de pas ; `core::ManualClock` devient
lisible depuis un autre thread. Choix de réalisation sous A79 « L4 » ; limites G300 à G306. Le mode mono-thread reste le défaut.

| Élément | Fichier et symbole | Commande | Résultat |
| --- | --- | --- | --- |
| Thread dédié, même résultat qu'en mono-thread | `include/orosmo/sim/simulation_thread.hpp` (`SimulationThread::start`, `frame`, `sync`, `stop`) | `make demo-test-unit` (suite `sim/thread`) | 12 agents, 300 pas, 6 entrées (apparitions, destructions, impulsions) poussées par la file : **même empreinte et mêmes événements** qu'en mono-thread, en 1, 3 et 4 pas par image et en fréquence fixe (`ManualClock`) |
| Cadence (`Config`) | `Cadence`, `ThreadSettings`, `declareSimulationConfig`, `readSimulationConfig`, `stepForRate` | suite `sim/thread` | 20, 50 et 120 Hz pendant 2 s d'images à 60 Hz (`ManualClock`) : 40, 100 et 240 pas, nombre exact après chaque image, `alpha` = fraction du `FixedStepper` à 1e-9 ; 1, 2 et 4 pas par image : 50 N pas, au plus une image de retard ; réglages `sim.*` lus (fichier et ligne de commande), valeurs hors plage refusées |
| Instantané immuable en double tampon, interpolation | `SnapshotBuffer`, `RenderSnapshot` (`alpha`, `transformAt`, `column`, `read`), `interpolate`, `relativeMatrix` | suite `sim/thread` | dernier publié lu, inchangé jusqu'au prochain `acquire` ; état d'avant apparié par entité (nouvelle entité : état courant) ; composants listés copiés ; interpolation exacte aux bornes, rotation de 90° à mi-chemin = 45° sans perte d'échelle |
| Événements et entrées par `SpscQueue`, arène par thread | `StepContext::emit`, `Simulation::takeEvents`, `StepContext::scratch`, `SimulationThread::pushInput`, `popEvent`, `rejectedInputs` | suite `sim/thread` | file d'événements de 4 places, 400 événements : aucun perdu, dans l'ordre ; entrée d'un pas déjà joué refusée et comptée ; entrées en file rendues à la simulation par `stop` ; arène vide au début de chaque pas |
| Test concurrent simulation + lecteur (rôle du rendu), sans GPU | `tests/sim/simulation_thread_tests.cpp` (« concurrent simulation and snapshot reader ») | `setarch -R make demo-test-unit SANITIZE=thread` (après `make fclean demo-fclean`, sans affichage ni pilote Vulkan, comme le job `tsan`) | 1000 Hz (horloge réelle) puis 3 pas par image, 64 entités, au moins 400 pas : aucun instantané déchiré, aucun événement perdu, toute entrée livrée, refusée ou en attente |

Vérifications (Linux x64, g++ 13.3, Intel Xe, Mesa ; avant fusion de `origin/dev`) : `make all-debug` sans avertissement ;
`make demo-test` **798 cas verts** (1 ignoré ; 13 nouveaux, suite `sim/thread`) ; `make demo-test-unit` 505 cas ; **TSan** : 505 cas
unitaires verts, **0 signalement** ; **ASan + UBSan** (`make demo-test SANITIZE=address,undefined`, arbre propre) : 798 cas verts,
aucun message ; `make check` OK (118 en-têtes autonomes, 0 violation de couche, chemin rejoué propre). Windows MinGW et macOS : par
la CI seulement. Non formaté par `tools/format_sources.sh` (clang-format absent) : style voisin suivi à la main.

## M8 lot L5 : module de jeu rechargeable, version finale statique (A79, GAME-01, GAME-02, GAME-07)

Branche `feat/M8-L5-jeu-rechargeable` (depuis `feat/M8-L3-sauvegarde-simulation`). Nouveau module `app` (niveau `app` de
`tools/layers.json`, A79 choix 3, provisoire), archive `libapp.a` (`src/app/README.md`). Choix de réalisation sous A79 « L5 ».
Linux x64 seulement en local (g++ 13.3) ; Windows et macOS par la CI (G310, G311).

| Élément | Fichier et symbole | Commande | Résultat |
| --- | --- | --- | --- |
| GAME-01 Interface C | `include/orosmo/app/game_api.hpp` (`OrosmoGameApi`, `orosmoGameApi`, `kGameAbiVersion`, `OROSMO_GAME_EXPORT`) | suite `app/game_module` | table lue par son nom ; `abiVersion` différente refusée au chargement et au rechargement (`kVersionMismatch`, ancienne version gardée) |
| GAME-01 Chargement et rechargement | `include/orosmo/app/game_module.hpp` (`GameModule::load`, `update`, `reload`), `src/app/game_reloader.cpp` | suite `app/game_module` avec `tests/app/game_module/game.cpp` construit en v1, v2, badabi, failinit | v1 puis v2 copiée sur le fichier surveillé : rechargée à l'`update` suivant, comportement changé (+1 → +10 par pas), état du composant intact (5 → 25), `onReload` appelé ; copie de la v1 effacée ; `init` qui échoue après avoir écrit dans le monde, fichier tronqué : ancienne version active, enregistrements remis, empreinte de la simulation identique (monde et générateur remis) |
| GAME-02 État côté moteur | `include/orosmo/app/game_context.hpp` (`GameContext::registerComponent`, `addSystem`, `addAction`) | suite `app/game_module` | systèmes et actions de l'ancienne version absents après rechargement (`Simulation::systemOrder`, `ActionRegistry::contains`) ; `stop` : `shutdown`, plus aucun système ni action, données des composants gardées ; nom déjà pris par l'hôte refusé |
| GAME-02 Règle 13 sur le binaire | `tools/check_game_static.py` (`make check`, CI, après chaque édition de liens d'une bibliothèque de jeu) | `make check` | auto-test : six formes d'état refusées (global `.bss`/`.data`, `thread_local` `.tbss`/`.tdata`, `static` local et `static` d'une fonction `inline`), constantes et vtables acceptées ; bibliothèque de test v1 et v2 sans état |
| GAME-07 Version finale | `GameModule::fromApi`, `OROSMO_GAME_STATIC` ; `bin/tests/test_game_static` (`tests/app/game_module/static_main.cpp`) | `make demo-test` (lien + `nm` + exécution) | aucun symbole `platform::DynamicLibrary`, `GameModule::load(` ni `DynamicReloader` dans l'exécutable ; `test_game_static: OK` |
| Gabarit | `apps/demo/Makefile` (`GAME_MODULE_DIR`, `GAME_MODULE`, `.DEFAULT_GOAL := all`), `tools/engine-sdk/template/src/game/` (`Spin`, système `spin`), `main.cpp` (`startGame`) | `make e2e-init` | `make debug` : `bin/debug/game.so` (contrôle de la règle 13 après l'édition de liens) ; `orosmo run -- --headless` : `game: started, version 1` puis `game: stopped` ; `make release` : jeu lié, aucun symbole de rechargement (`nm`) ; à la main, programme lancé puis `src/game/game.cpp` modifié et `make debug` : `game: reloaded, version 2` (un premier essai sur le fichier à moitié écrit a échoué proprement, G315) |

Vérifications (Linux x64, g++ 13.3, Intel Xe, Mesa ; machine chargée par d'autres agents) : `make all-debug` sans avertissement ;
`make demo-test` **793 cas verts** (1 ignoré ; 8 nouveaux, suite `app/game_module`) ; après la revue de la PR #28 (fusion de `dev`) : **827 cas verts**, 9 dans `app/game_module`, 31 tests Python puis `test_game_static: OK` ;
`make demo-test-unit` 500 cas verts ; `make check` OK (119 en-têtes autonomes, 0 violation de couche, `check_game_static` OK) ;
`make e2e-init` OK ; ASan + UBSan (`make fclean && make demo-test-unit SANITIZE=address,undefined`) : suite `app/game_module` 8 / 8 et `test_game_static` sans aucun message ; le passage complet s'arrête sur une erreur **antérieure au lot** (`alloc-dealloc-mismatch` dans `libshaderc` du système, appelé par le test de foule `anim` qui crée un périphérique graphique alors qu'il est dans `demo-test-unit`), et le passage complet avec `ASAN_OPTIONS=alloc_dealloc_mismatch=0` n'a pas fini en 15 min sur la machine chargée. Pas de rendu touché (démo inchangée). Non formaté par `tools/format_sources.sh`
(clang-format absent de la machine) : style du code voisin suivi à la main.

## M8 lot L10 : outils de debug et inspecteur (A79, UI-04, UI-10)

Branche `feat/M8-L10-inspecteur-console` (depuis `feat/M8-L2-ecs`). Outils construits sur `orosmo::ui`, sans Dear ImGui (A79
choix 6) ; `ui` dépend désormais de `scene` (couche inférieure) pour l'inspecteur d'entités. Décisions : `docs/QUESTIONS.md`,
A79, « Lot L10 » ; limites G295 à G299. Démo : `apps/demo/bin/debug/main` puis F12, ou `--debug-tools [--debug-tab
console|widgets|entities|profiler]`.

| Élément | Fichier et symbole | Commande | Résultat |
| --- | --- | --- | --- |
| UI-04 Console | `include/orosmo/ui/debug_console.hpp` (`debug::Console::add`, `execute`, `pollLog`, `splitCommandLine`) ; `core::Logger::RecordsSince`, `LastSequence` | suite `ui/debug` (« console runs a registered command… », « console mirrors the engine log », « the console input runs the typed line ») ; `Logger` (« RecordsSince… ») | commande exécutée par nom avec arguments entre guillemets ; commande inconnue : `kNotFound` et ligne d'erreur ; journal recopié (avertissement, erreur) ; saisie + Entrée exécute et vide le champ |
| UI-04 Inspecteur d'entités | `include/orosmo/ui/reflect.hpp` (`reflectedPanel(ObjectLocator, span<const FieldDesc>)`), `debug_tools.hpp` (`entityPanel`, `describeEntity`) | « the runtime reflected panel edits fields… », « the entity inspector modifies a component » | entier, double, uint8 (hors plage refusé), booléen modifiés par l'interface aux bons décalages ; composant modifié dans le `World`, y compris après déplacement de son stockage ; panneau refait à la destruction |
| UI-04 Profileur | `include/orosmo/ui/frame_history.hpp` (`FrameHistory`, `FpsStats`, repris de la démo), `core::Profiler::Names`, onglet Profiler | « frame history computes the frame rate », `Profiler` (« Names lists… »), suite `apps/demo_performance` (inchangée) | FPS moyenne / min / max identiques au calcul de la démo |
| UI-10 Inspecteur d'interface | `debug_tools.hpp` (`describeWidgetTree`, `describeWidget`, `DebugTools::widgetTree`, `highlight`) | « the widget inspector lists the tree with the layout sizes » | arbre dans l'ordre, rectangles égaux à ceux de la mise en page (bouton 120×30 en 5,5), widget masqué signalé ; survol d'une ligne = surlignage du widget |
| UI-13 Rien quand fermé | `DebugTools::update`, `draw` | « closed debug tools neither lay out nor draw » | fermés : liste de dessin vide, aucune mise en page ; F12 ouvre et ferme |

Vérifications (Linux, g++ 13.3, Intel Xe, Mesa) : `make all-debug` sans avertissement ; `make demo-test` **772 cas verts** (1
ignoré ; 10 nouveaux : 8 dans `ui/debug`, 1 `Logger`, 1 `Profiler`) ; `make check` OK (113 en-têtes autonomes, 0 violation de
couche, 0 appel d'API graphique hors zone) ; captures `--debug-tab widgets|console|entities|profiler` en OpenGL et Vulkan sans
fenêtre : code 0, « Vulkan validation: 0 error/warning » ; outils fermés : `ui.png` de `--tab controls --focus detailMedium`
identique pixel pour pixel à `docs/BASELINE/reference/ui-opengl-controls-focus-1280x720.png` ; ASan + UBSan (`make fclean`, `make demo-test-unit SANITIZE=address,undefined`) : 487 cas verts sans GPU (dont les 10 nouveaux), aucun message ; le `make demo-test` complet sous sanitizers n'a pas été relancé (lot sans code GPU).
`make demo-capture` ajoute les quatre captures des outils. `clang-format` absent de la machine : sources non reformatées.

Revue de la PR #26 (CI MinGW et CodeRabbit, après fusion de `dev`) : `Result` ignoré corrigé dans les tests (libstdc++ 15 ;
reproduit sous g++ 13.3 avec une copie de `<expected>` marquée `[[nodiscard]]`) ; `Console` non copiable, état des commandes gardé
(« a console command keeps its state… ») ; surlignage gardé lié au widget (« the pinned highlight follows its widget… ») ;
direction de caméra parallèle à `up` refusée (suite `apps/demo-debug-world`) ; plein écran des builds debug déplacé de F12 à F10
(`static_assert` dans `Game/debug_world.hpp`). `make all-debug`, `make demo-test` (**806 cas verts**, 1 ignoré) et `make check` OK.
Après fusion de `dev` (PR #25, lot L3) : faux positif `-Warray-bounds` de GCC 16 (MinGW) désactivé dans `debug_tools_tests.cpp`
(G34 ; non reproductible sous g++ 13.3, à confirmer par la CI) ; `make demo-test` **832 cas verts**, 1 ignoré, `make check` OK.

## M8 lot L3 : sauvegarde de scène et boucle à pas fixe (A79, ECS-03, SIM-01)

Branche `feat/M8-L3-sauvegarde-simulation` (depuis `feat/M8-L2-ecs`). Sauvegarde et empreinte dans `scene`
(`src/scene/README.md`), nouveau module `sim` (`src/sim/README.md`), couche 9, archive `libsim.a`. Choix de réalisation sous
A79 « L3 ». Linux x64 seulement en local (g++ 13.3) ; arm64, Windows et macOS par la CI (G280).

| Élément | Fichier et symbole | Commande | Résultat |
| --- | --- | --- | --- |
| ECS-03 Sauvegarde JSON et binaire | `include/orosmo/scene/scene_file.hpp` (`saveScene`, `loadScene`, `saveSceneBinary`, `loadSceneBinary`, `saveSceneFile`, `loadSceneFile`) ; `World::entityTable`, `resetEntities`, `componentEntities` | `make demo-test-unit` (suite `scene/file`) | aller-retour JSON et MessagePack **exact** (même empreinte, même texte, mêmes octets ; -0, infinis, dénormaux, `int64` min, `uint64` max, références `Entity`, ordre dense, ordre des enfants, indices libres, `create` suivants identiques) ; cache monde recalculé |
| ECS-03 Versions | `SceneMigrations` (format et par type), `kSceneFormatVersion` | suite `scene/file` | migration de composant v1 -> v2 ; sans migration ou version plus récente : `kVersionMismatch`, monde inchangé ; champ manquant = défaut ; champ inconnu et type non enregistré ignorés avec avertissement ; documents invalides refusés (tout ou rien) |
| Empreinte | `include/orosmo/scene/fingerprint.hpp` (`fingerprint`, `hashWorld`), `include/orosmo/core/hash.hpp` (`Fnv1a`) | suite `scene/file` | ignore remplissage et cache monde ; sensible à un ulp, au signe de zéro, à l'ordre dense ; indépendante de l'ordre d'enregistrement des types |
| SIM-01 Pas fixe, systèmes | `include/orosmo/sim/simulation.hpp` (`Simulation::advance`, `step`, `run`, `addSystem`, `systemOrder`) | suite `sim/simulation` | nombre de pas égal au temps cumulé / pas pour trois découpages d'images ; `maxStepsPerFrame` ; ordre phase, ordre, nom ; pas d'ajout pendant un pas |
| SIM-01 Entrées, rejeu | `Simulation::queueInput`, `inputLog`, `recording`, `play`, `Replay`, `replayToJson` | suite `sim/simulation` | entrées livrées à leur pas, dans l'ordre ; enregistrement rejoué (aussi via JSON) : même empreinte ; autre graine refusée |
| SIM-01 Capture et restauration | `Simulation::capture`, `restore`, `saveState`, `loadState`, `Snapshot` | suites `sim/simulation`, `demo/simulation` | 500 agents, graine 42 : capture au pas 500 puis restauration, et état JSON/MessagePack rechargé dans une nouvelle simulation : même empreinte au pas 1000 |
| SIM-01 Mode sans fenêtre de la démo | `apps/demo/src/Sim/agent_simulation.cpp` (`runSimulationMode`), options `--sim-agents`, `--seed`, `--sim-steps` | `apps/demo/bin/debug/main --sim-agents 500 --seed 42 --sim-steps 1000` | `Simulation: agents=500 seed=42 steps=1000 alive=500 fingerprint=0xa3f0f4166622ae05` en 84 ms, sans fenêtre ni GPU ; **même valeur en debug et en release**, figée par la suite `demo/simulation` (deux exécutions identiques, autre graine différente) |
| Chemin rejoué déterministe | `tools/check_deterministic_math.py` (`make check`, CI), `-ffp-contract=off` (`mk/engine.mk`, `tools/engine-sdk/engine.mk`) | `make check` ; `python3 -m unittest discover -s tests/tools` | OK ; 5 tests du contrôle (refus de `sin`, `glm::angleAxis`, `rand`, `<random>`, horloge ; commentaires ignorés) |

Vérifications (Linux x64, g++ 13.3, Intel Xe, Mesa) : `make all-debug` sans avertissement ; `make demo-test` **777 cas verts**
(1 ignoré ; 16 nouveaux : 6 `scene/file`, 6 `sim/simulation`, 4 `demo/simulation`) ; `make demo-test-unit` 492 cas verts ;
`make check` OK (114 en-têtes autonomes, 0 violation de couche, chemin rejoué propre) ; `python3 -m unittest discover -s
tests/tools` 26 tests OK. ASan + UBSan (`-fsanitize=address,undefined -fno-sanitize-recover=all`, `-O1`) **sur les sources et
tests du lot seulement** (`src/scene/`, `src/sim/`, `apps/demo/src/Sim/`, `launch_options.cpp`, suites `scene/*`, `sim/*`,
`demo/simulation`, `core/serialization`, liés aux archives release du reste du moteur) : 43 cas verts, aucun message,
empreinte figée retrouvée ; le `make demo-test SANITIZE=address,undefined` complet n'a pas été relancé (machine partagée, lot
sans code GPU). Non formaté par `tools/format_sources.sh` (clang-format absent de la machine) : style du code voisin suivi à la
main.

## M8 lot L2 : entités et transformations (A79, ECS-01, ECS-02)

Branche `feat/M8-L2-ecs` (depuis `feat/M8-L1-fondations`). Nouveau module `scene` (`src/scene/README.md`), couche 6, archive
`libscene.a`, dépend seulement de `core`. ECS maison (A79 choix 1), composants trivialement copiables (choix 2) ; choix de
réalisation sous A79 « L2 ». Linux seulement en local (g++ 13.3) ; macOS et Windows par la CI (G264).

| Élément | Fichier et symbole | Commande | Résultat |
| --- | --- | --- | --- |
| ECS-01 Entités | `include/orosmo/scene/entity.hpp` (`Entity`), `world.hpp` (`World::create`, `destroy`, `alive`) | `make demo-test` (suite `scene/world`) | une entité détruite est refusée par sa génération, même après réutilisation de l'indice, et n'atteint pas les composants de la nouvelle |
| ECS-01 Composants par nom | `component.hpp` (`ComponentDesc`, `validateComponentDesc`, `describeComponent<T>`), `World::registerComponent`, `componentId`, `componentType<T>`, `add(e, nom)` | suite `scene/world` | description copiée (survit à l'original), valeur par défaut, version ; composant inconnu refusé par nom (`kNotFound`) ; disposition différente `kAlreadyExists` ; descriptions invalides refusées ; retrait d'un type et identifiant périmé |
| ECS-01 Stockage contigu, requêtes | `component_pool.hpp` (`ComponentPool`), `World::view`, `forEach`, `forEachComponent` | suite `scene/world` | stockage contigu aligné (64 octets), dense après retraits ; vues typées et non typées concordantes ; copie profonde ; **100 000 entités parcourues** (temps dans `src/scene/README.md`) |
| Réflexion avec décalages | `include/orosmo/core/reflection.hpp` (`FieldDesc::offset`, `size`, `fields`, `kDVec3`, `kQuat`, `kDQuat`) | suite `core/reflection` | décalages et tailles égaux à `offsetof` / `sizeof`, sous-champs, type sans constructeur par défaut |
| ECS-02 Hiérarchie, transformations | `transform.hpp` (`Transform`, `composeWorld`, `relativeMatrix`), `World::setParent`, `updateTransforms` | suite `scene/hierarchy` | ordre des enfants, changement de parent, cycle refusé, destruction du sous-arbre ; propagation (rotation, échelle, ancêtre sans `Transform`) ; seules les entités sales et leurs descendants recalculées ; **précision à 10^7 m : erreur < 1 mm relative à la caméra** ; 100 000 entités en hiérarchie |

Vérifications (Linux, g++ 13.3, Intel Xe, Mesa) : `make all-debug` sans avertissement ; `make demo-test` **757 cas verts**
(1 ignoré ; 30 cas dans les suites `scene/*`, `core/reflection` et `core/serialization`, dont 18 nouveaux pour `scene` et 3 pour la
réflexion) ; `make check` OK (110 en-têtes autonomes, 0 violation de couche) ; ASan + UBSan (`-fsanitize=address,undefined
-fno-sanitize-recover=all`, debug) **sur les tests du lot seulement** (sources `src/scene/` et tests `tests/scene/`,
`tests/core/reflection_layout_tests.cpp`, `serialization_tests.cpp`, liés à `libcore.a`) : 30 cas verts, aucun message ; le
`make demo-test SANITIZE=address,undefined` complet n'a pas été relancé (machine partagée, lot sans code GPU).

Après la revue de la PR #22 (CodeRabbit) : décalages mesurés sur un objet construit (`FieldDesc::kUnknownOffset` sinon),
vues par `ComponentId`, `kBusy` pour un nouveau type pendant un parcours, garde de parcours sûre face aux exceptions, copie de
la valeur avant agrandissement du stockage, taille bornée (`ComponentDesc::kMaxSize`), textes d'aide hors de la disposition,
remontée des ancêtres mémorisée dans `updateTransforms`, valeur par défaut sans `resize` + `memcpy` (faux positif
`-Warray-bounds` de GCC 16 sous MinGW) ; 7 cas de régression. Horloges retirées des tests : mesures dans `apps/demo/benchmark`
(`scene-view`, `scene-transforms`, `make -C apps/demo ENGINE_DIR=orosmo TEST_DIR=../../tests benchmark`). Vérifications :
`make all-debug` sans avertissement, `make demo-test` **776 cas verts** (1 ignoré), `make check` OK, ASan + UBSan sur les tests
du lot (37 cas) sans message.

## M8 lot L1 : fondations (A79)

Branche `feat/M8-L1-fondations`. Aucune ligne de `docs/02` ne change d'état : ce lot prépare GAME-01, UI-08 (chargement
dynamique), SIM-01 bis (file entre threads, TSan) et ECS/SIM (couches). Linux seulement en local (g++ 13.3) ; Windows MinGW et
macOS par la CI (G230).

| Élément | Fichier et symbole | Commande | Résultat |
| --- | --- | --- | --- |
| Bibliothèque dynamique | `include/orosmo/platform/dynamic_library.hpp`, `src/platform/dynamic_library.cpp` (`DynamicLibrary::load`, `symbol`, `function<Sig>`, `unload`, copie sous un nom unique) | `make demo-test` (suite `platform/dynamic_library`) | 5 cas verts : fichier absent `kNotFound`, symbole absent `kNotFound`, appel d'une fonction, copie unique effacée au déchargement, deux versions chargées ensemble |
| Bibliothèque liée au moteur de l'hôte | `tools/engine-sdk/engine.mk` (`engine_host_ldflags`, `engine_module_ldflags`, `ENGINE_MODULE_EXT`, `engine_count_exports`), `apps/demo/Makefile` (`TEST_MODULE`), `tests/platform/test_module/test_module.cpp` | même commande ; `nm -DC bin/tests/test_module.so` | le message écrit par la bibliothèque arrive dans le `Logger` de l'hôte et la bibliothèque lit le même compte ; `Logger::AddLog` et `KeptLogCount` indéfinis (`U`) dans la bibliothèque, contrôle `nm` au lien ; **10 109** symboles exportés par l'exécutable des tests (limite PE 65 535, G231) |
| Générateur PCG32 | `include/orosmo/core/random.hpp` (`Random`) | suite `core/random` | 5 cas verts : 1 000 valeurs de la graine 42 égales à `tests/core/random_seed42_table.inc` (référence Python indépendante, début = `pcg32-demo`), `advance`, `fromState`, bornes |
| File SPSC sans verrou | `include/orosmo/core/spsc_queue.hpp` (`SpscQueue<T>`) | suite `core/spsc_queue` ; sous TSan : `setarch -R` + `-fsanitize=thread` | 3 cas verts, 1e6 éléments entre deux threads dans l'ordre ; propre sous TSan ; une version cassée (`relaxed` à la publication) y est signalée (data race) |
| Couches `scene`, `sim`, `app`, `game` | `tools/layers.json` (niveau `app` entre `sim` et `game`, A79 à confirmer), `docs/03` §1 annoté | `make check` ; `python3 -m unittest discover -s tests/tools` | OK ; 5 tests de `tests/tools/test_check_layers.py` (arbre factice : `scene -> app` et `sim -> game` refusés) |
| Job CI TSan | `.github/workflows/ci.yml` (job `tsan` : `make demo-test-unit SANITIZE=thread`, `vm.mmap_rnd_bits=28`) | actionlint 1.7.12 ; en local `make fclean && setarch -R make demo-test-unit SANITIZE=thread`, puis le binaire sans affichage ni pilote Vulkan (G233) | actionlint : 0 erreur ; **439 cas unitaires verts et propres sous TSan** ; le job lui-même s'observe au push |

Vérifications (Linux, g++ 13.3, Intel Xe, Mesa) : `make demo-test` **724 cas verts** (13 nouveaux) ; `make fclean && make demo-test
SANITIZE=address,undefined` : aucun message ASan ni UBSan, 723 cas sur 724 (le seul échec est `PLAT-01 the framebuffer size…`,
instable sous un gestionnaire de fenêtres qui redimensionne la fenêtre de test, déjà noté sous G201 ; 1 échec sur 3 en le relançant
seul) ; `make all-debug` sans avertissement ; `make check` OK.

## M8 lot L11 : API de rendu simplifiée (A75.3 à A75.7, A75.15)

Sur `feat/M8-L11-api-rendu` (A79). Preuves sous Linux x64 (g++ 13.3, Mesa, OpenGL et Vulkan). Les anciennes signatures restent (choix 10) et sont encore exercées par les tests. Tests : `tests/renderer/render_api_tests.cpp` (suites `renderer/render-api`, `renderer/mesh-builder`, sur les deux backends).

| Proposition | État | Fichier et symbole | Preuve |
| --- | --- | --- | --- |
| A75.3 définitions et programme résolu au dessin | **Fait** (récepteurs d'ombres par `mesh.frag` : G240 ; dispositions texturée et instanciée : G243) | `scene_renderer.hpp` `SceneRenderer::shaderDefines`, `revision`, `resolveProgram` ; `render_feature.hpp` `RenderFeature::shaderDefines` ; `ForwardPlusLighting::shaderDefines` | cas « SceneRenderer::shaderDefines and revision », « drawMesh(mesh) (A75.3, A75.6) » (recompilé pour un AOV ajouté, mêmes pixels que l'ancien chemin) : verts |
| A75.4 `RenderContext` | **Fait** | `render_context.hpp` `RenderContext` ; surcharges `create(const RenderContext &…)` de 17 fabriques, `enableForwardPlus` et `enableGpuDriven` | cas « RenderContext (A75.4) » : contexte possédant et contexte qui référence, ancienne signature : verts |
| A75.5 matériau porté par le maillage, `MeshBuilder` | **Fait** | `mesh.hpp` `Mesh::setMaterial`, `material` ; `mesh_builder.hpp` `MeshBuilder` (`computeTangents`, `addBox`, `addPlane`) | cas « MeshBuilder (A75.5) » et « Mesh::setMaterial » (matériau gardé vivant, groupe suivi, mêmes pixels que l'ancien chemin) : verts |
| A75.6 `drawMesh(Mesh &)`, `setWireframe` | **Fait** | `SceneRenderer::drawMesh(Mesh &)`, `setWireframe` | cas « setWireframe (A75.6) » : verts ; démo migrée (`apps/demo/src/Scene/demo_scene.cpp`) |
| A75.7 `endFrame(backbuffer)` | **Partiel** (passe « upscale » non renommée : G241) | `SceneRenderer::endFrame(rhi::TextureHandle)` | cas « endFrame(texture) (A75.7) » : mêmes pixels, sortie étirée, texture inconnue refusée : verts ; démo migrée (`Game::render`) |
| A75.15 `kBack` pour le contenu glTF | **Fait** pour les matériaux (défaut global gardé : G242) | `Mesh::setMaterial` ; `assets::RenderModel` (depuis M6) | cas « Mesh::setMaterial » : `kBack`, `kNone` en double face |

Non-régression : 24 captures (12 configurations × OpenGL et Vulkan : HUD, ombres, forward+, forward+ et ombres, widgets, scène mixte, ombre tramée, animation, TAA, debug normales, fil de fer, 200 instances) faites avant puis après : les 24 `scene.png` identiques au pixel près (`tools/compare_png.py --tolerance 0`), `ui.png` identiques sauf le texte des mesures en direct du HUD (FPS, ms) ; couches de validation Vulkan : 0 message sur chaque exécution.

Vérifications : `make all-debug` sans avertissement ; `make demo-test` : 718 cas, 718 verts (7 nouveaux), 1 ignoré ; ASan + UBSan sur les suites `renderer/*` et `assets/*` (`make demo-test SANITIZE=address,undefined TEST_ARGS='--test-suite=renderer/*,assets/*'`) : 260 cas verts, aucun rapport ; `make check` : OK ; `make demo-capture` : OK (références comparées) ; `make demo-vulkan-check` : OK, validation Vulkan silencieuse. Non vérifié : Windows et macOS (compilés par la CI au push).


## M8 lot L12a : API des contenus (A75.2, A75.11)

Sur `feat/M8-L12a-api-contenus` (A79), fondé sur L11. Preuves sous Linux x64 (g++ 13.3, Mesa, OpenGL et Vulkan). Anciennes API gardées (`SkinnedModel`, `CrowdRenderer::create(…, render)`, `setAgents(worlds, poses)`). Tests : `tests/anim/animated_model_tests.cpp` (suite `anim/animated-model`, sur les deux backends) et `tests/apps/demo_anim_scene_tests.cpp`.

| Proposition | État | Fichier et symbole | Preuve |
| --- | --- | --- | --- |
| A75.2 `assets::ModelLoader` | **Fait** | `model_loader.hpp` `ModelLoader::create`, `load`, `createRenderModel`, `reloadChanged` | cas « ModelLoader: a missing file is an error naming it… » : vert |
| A75.2 `anim::AnimatedModel` (modèle animé en 5 lignes) | **Fait** (événements de clip : G251) | `animated_model.hpp` `AnimatedModel::create`, `play`, `crossFade`, `update`, `setPose`, `dispatch`, `draw`, `rebuild`, `reloadChanged` | cas « an animated glTF model is loaded and played in five lines » et « play and crossFade give the same poses as a StateMachine » (égalité exacte) : verts |
| A75.2 repli CPU (`skinVertices`) | **Fait** (maillage partagé par plusieurs nœuds : G250) | `AnimatedModel` (`SkinningPath::kCpu`, `forceCpuSkinning`) | cas « the CPU fallback deforms the vertices like the compute skinning, to 1e-4 » (au milieu d'un fondu, sommets relus sur le GPU) : vert sur OpenGL et Vulkan |
| A75.11 foule depuis un modèle | **Fait** | `crowd.hpp` `CrowdRenderer::create(context, animatedModel)`, `setAgents(worlds, clips, times)` | cas « a crowd created from an animated model… draws like a hand-built crowd » (images identiques) : vert |
| Démo `--scene anim` migrée | **Fait** | `apps/demo/src/Scene/anim_scene.cpp` `AnimScene::init`, `update`, `updateCrowd`, `reloadChanged` | suite `apps/demo-anim` : verte ; captures avant/après (ci-dessous) |

Non-régression : `--scene anim` capturée avant (anciens `anim_scene.*` de `dcbadf8` sur le même moteur) puis après, OpenGL et Vulkan, à 30 images et à 240 images (4 s : le fondu de 3 s vers « wave » est passé) : les 4 `scene.png` identiques au pixel près (`tools/compare_png.py --tolerance 0`) ; couches de validation Vulkan : 0 message.

Vérifications : `make all-debug` sans avertissement ; `make demo-test` : 726 cas verts avant la fusion de `origin/dev`, 736 cas verts (1 ignoré) après ; ASan + UBSan sur les suites `anim/*`, `assets/*` et `apps/demo-anim` : 83 cas verts, aucun rapport ; `make check` : OK. Non vérifié : Windows et macOS (compilés par la CI au push) ; `clang-format` absent de la machine (formatage non appliqué).


## M8 lot L0 : `--shadow-quality` de la démo

Option de la démo, sans changement du moteur (`docs/05` M8, A79). `apps/demo/src/launch_options.hpp` : `ShadowQuality`, `parseShadowQuality`, `shadowSettingsForQuality` (préréglages `low`, `medium`, `high` = `ShadowSettings{}`), `shadowQualityLabel` ; `Game::setShadowSettings` passe les réglages à `ShadowFeature::create` ; le panneau de performances affiche « shadows on <qualité> ». Tests : `tests/demo/launch_options_tests.cpp` (lecture, défaut `high`, valeur invalide refusée, préréglages, `high` égal à `ShadowSettings{}`). Démo : `apps/demo/bin/debug/main --shadows on --shadow-quality low`.

## M8 lot L12b : petites fabriques et plateforme (A75.10, A75.12, A75.16, part `platform` d'A75.8)

Sur `feat/M8-L12b-fabriques-platform` (A79). Preuves sous Linux x64 (g++ 13.3, Mesa, OpenGL et Vulkan). Tests : `tests/renderer/light_camera_api_tests.cpp` (sans GPU), cas « A75.10 descriptor light factories give the same image… » de `tests/renderer/forward_plus_tests.cpp` (deux backends), cas A75.8 et A75.16 de `tests/platform/window_input_tests.cpp`.

| Proposition | État | Fichier et symbole | Preuve |
| --- | --- | --- | --- |
| A75.10 une convention de direction, portée, degrés | **Fait** (limites : G272) | `light.hpp` `DirectionalLightDesc`, `PointLightDesc`, `SpotLightDesc`, `directionalLight(desc)`, `pointLight(desc)`, `spotLight(desc)`, `lightDirection` ; anciennes fabriques gardées | cas « A75.10 directionalLight(desc) / spotLight(desc) / pointLight(desc) » (portée = `lightRange`) et « same image as the old ones » (tolérance 0, OpenGL et Vulkan) : verts |
| A75.12 `setForward`, `lookAt` | **Fait** (verticale : G273) | `camera.hpp` `Camera::setForward`, `lookAt`, `forward` | cas « A75.12 setForward… », « A75.12 lookAt… » (cible au centre de l'écran, même vue que `glm::lookAt`) : verts |
| A75.16 `pollEvents` séparé de `newFrame` | **Fait** | `window.hpp` `Window::pollEvents`, `newFrame` (documenté ; faux sans fenêtre au lieu d'un pointeur nul) ; gabarit : `break` au lieu de `continue` | cas « A75.16 pollEvents… » et « a window that was never created behaves safely » : verts |
| A75.8 (part `platform`) `init` → `Result<>`, `Window::input()` | **Fait** pour `init` et `input` (changement incompatible : G270 ; reste : G271) | `Window::init` (kInvalidArgument, kAlreadyExists, kUnsupported ; `glfwInit` en échec ne quitte plus), `Window::input` | cas « A75.8 Window::init returns a Result… », « a second init… », « Window::input() is the InputManager of getInstance » : verts |

## SDK (v0.6.2)

Distribution du SDK, décision A76 (`docs/QUESTIONS.md`), contrat `docs/03` §16. Aucun code C++ du moteur touché ; aucune ligne de `docs/02` ne change d'état (BLD-01, BLD-02 : Partiel, Windows et macOS non vérifiés). Preuves sous Linux x64 (Ubuntu 24.04, g++ 13.3), branche `docs/SDK` fondée sur `feat/SDK-integ` (`788c332`) :

| Élément | Fichier et symbole | Commande | Résultat |
| --- | --- | --- | --- |
| Moteurs dans `OROSMO_HOME` | `tools/engine-init-rs/src/home.rs` (`home_from`, `normalize_version`, `remove_version`), `install.rs` (installation atomique, chemins refusés), `main.rs` (`install_version`, `check_download`) | `cargo test --manifest-path tools/Cargo.toml -p orosmo` | 14 tests verts |
| `build --json` | `tools/engine-init-rs/src/makerun.rs` (`Progress::feed`, `diagnostic_json`, `done_json`) | même commande ; puis `orosmo build --json -C <projet>` | test `json_events` vert ; build réel : code 0, dernière ligne `done` avec `failed: []` |
| `doctor` sans vcpkg, `--fix` | `tools/engine-init-rs/src/doctor.rs` (`check_project_layout`, `fix_command`) | même commande ; `orosmo doctor <projet>` | tests verts ; 13 ok, 0 erreur avec `OROSMO_HOME` isolé |
| Paquet avec `deps/`, lecture seule, sans vcpkg | `tools/package-deps.sh`, `tools/engine-sdk/engine.mk` (`ENGINE_DEPS_DIR`, `ENGINE_GEN_DIR`), `Makefile` (`e2e-init`) | `make -j8 e2e-init` | vert jusqu'à `orosmo doctor` ; **rouge à cette étape** (G220 : `OROSMO_HOME` non transmis) ; `compile-commands` et CMake relancés à la main : verts |
| Projet sans moteur copié | `apps/demo/Makefile` (résolution de `ENGINE_DIR`), `apps/demo/config.mk` (`OROSMO_VERSION`) | `orosmo run -C <projet> -- --api opengl --headless --capture <dir>` | code 0, `scene.png` et `ui.png` |
| Contrôles | — | `make check` ; `cd site && npm run build && npm run check-links && npm run check-anchors` | OK ; 65 pages, 5524 liens, 1729 ancres, 0 cassé |

Après la revue de la PR #15 (`fix/SDK-review1`, 2026-11-26) : `make e2e-init` vert de bout en bout (G220 corrigé, `doctor` 13 ok, 2e build sur moteur en lecture seule) ; `orosmo migrate` essayé à la main sur deux projets `--vendor` (build cassé : migration annulée, fichiers et `orosmo/` restaurés ; build sain : `orosmo.old/` supprimé) ; `cargo test` 28 verts. Les lignes du tableau ci-dessus datent de `788c332` (2026-11-25).

Non prouvé : Windows et macOS (G215, G216 ; `install.ps1` jamais exécuté), installeurs et `orosmo install` depuis GitHub (après publication), `update` de bout en bout, chemins avec espaces (G214, atténué), redirections hors curl (G221). Limites : G214 à G220.

## Identifiants de `docs/02` touchés par M8 lot L9 (UI-09)

Preuves sous Linux x64 (g++ 13.3, Mesa 26.1.6, Intel Arc), branche `feat/M8-L9-ui-focus`. Windows et macOS non vérifiés (CI).
Décisions sur les ambiguïtés : `docs/QUESTIONS.md`, A79, « Lot L9 ».

| ID | Avant | Après | Preuve |
| --- | --- | --- | --- |
| UI-09 | Absent | **Fait** | Ordre de tabulation : `include/orosmo/ui/widget.hpp` `Widget::setTabIndex`, `setFocusable`, `canTakeFocus` ; `src/ui/ui.cpp` `Ui::focusNext` (positif croissant, puis 0 dans l'ordre de l'arbre, négatif exclu). Flèches : `Ui::moveFocus` (voisin le plus proche, sans boucler), `Widget::usesArrowKey` (curseur, champ de texte, liste, menus gardent leurs flèches). Manette : `include/orosmo/ui/gamepad_navigation.hpp` `GamepadNavigator` → `InputFrame::navigation`. Focus visible : style de thème `focusRing` (`res/themes/base.json`, jetons `color.focus`, `border.focus`, `focus.ringGap`), `Ui::drawFocusRing`, `Ui::focusVisible` (clavier ou manette, pas la souris). Langage : `.tabIndex(n)`, `.focusable(false)`, moins unaire (`tools/cui-rs/src/engine.rs`, `parser.rs`). Tests : `tests/ui/navigation_tests.cpp` (6 cas : ordre de tabulation, grille 3 x 3 aux flèches, flèches gardées par un curseur, manette simulée avec branchement, répétition, A / B / LB / RB, liste déroulante fermée par B, anneau de focus du thème), `tests/ui/cui_widgets_tests.cpp` (`tabIndex` et `focusable` compilés depuis `tests/ui/cui/test_widgets.cui`), `tools/cui-rs/src/tests.rs` `ui09_tab_index_and_focusable_describe_the_keyboard_order`. Démo : `apps/demo/bin/debug/main --api opengl --headless --ui full --tab controls --focus detailMedium --capture <dir>` ; `make demo-capture` compare à `docs/BASELINE/reference/ui-opengl-controls-focus-1280x720.png`. Résultats : `make demo-test` 717 / 717 (1 ignoré) ; `make demo-test-unit SANITIZE=address,undefined` (après `make fclean demo-fclean`) 432 / 432 sans signalement ; `make cui-test` vert (17 tests `cargo` de `cui`, 18 cas d'erreur) ; `make check` vert ; captures sans `--focus` identiques avant / après (0 pixel, onglets `controls`, `widgets`, `windows`, liste ouverte) ; capture avec focus identique OpenGL / Vulkan (0 pixel), validation Vulkan 0 message. Reste : G222 (défilement vers le widget focalisé) |
| PLAT-03 | Absent | Partiel | `include/orosmo/platform/gamepad.hpp` `Gamepads` (`poll` par GLFW, `simulate`, `events`, `first`), `GamepadState`, `GamepadButton`, `GamepadAxis` ; tests par `Gamepads::simulate` dans `tests/ui/navigation_tests.cpp` ; démo : manette branchée sur l'interface complète (`apps/demo/src/Game/Game.cpp`). **Manque** : vibrations, remappage, joysticks sans disposition connue, aucune manette réelle essayée (G223) |

Totaux de `docs/02` : Fait +1 (UI-09), Partiel +1 (PLAT-03), Absent −2.

## Identifiants de `docs/02` touchés par M14

Preuves sous Linux (Node 24.20.0, npm 11.19.0, Docker 29.1.3), sur `feat/M14-site` puis depuis un clone propre (`docs/releases/M14.md`).
Aucun code C++ touché (`make check` vert). Hébergement : A74 (proposé, appliqué par défaut, à confirmer).

| ID | Avant | Après | Preuve |
| --- | --- | --- | --- |
| WEB-01 | Absent | **Fait** | `site/` (Astro 7.4.13, Starlight 0.43.1, versions exactes, `package-lock.json`) : « Utiliser le moteur » (`site/src/content/docs/utiliser/` : installation, `orosmo init`/`doctor`/`update`, Make et CMake, `.cui`, thèmes, exemples) et « Architecture interne » (`architecture/` : couches, RHI, renderer, ui, compilateur `cui`, shaders), chaque page citant ses sources ; `(cd site && npm ci && npm run build && npm run check-links && npm run check-anchors)` : première livraison 16 pages, 470 liens, 0 cassé ; **après la refonte** (`docs/releases/M14-MR.md`) : 64 pages, 5375 liens internes, 1659 ancres, 0 cassé ; mise à jour par jalon : `docs/DEPLOIEMENT_SITE.md` ; CI GitHub non observée (G203) |
| WEB-02 | Absent | Partiel | `index.mdx` (vision de `docs/01`, captures de référence, fonctions **Fait** seulement, M6 « en préparation », liens releases et wiki), `histoire.md` (texte de transparence de WEB-02, lié depuis l'accueil, la barre latérale et le pied de page `src/components/Footer.astro`), `etat.md` ; **manque** : publication (G203, G204) ; le contenu de M6 est couvert par la refonte (à relire à chaque jalon, G206) |
| WEB-03 | Absent | Partiel | `site/Dockerfile` (node 24.20.0 alpine → nginx 1.30.4 alpine, `HEALTHCHECK`), `site/nginx.conf` ; `docker build -t orosmo-site:test site/` puis `docker run -p 8089:80` : accueil 200, `/architecture/rhi/` 200, `/histoire/` 200, page absente 404, santé `healthy`, image 104 Mo ; registre GHCR (A74) ; **manque** : première publication (G203) |
| WEB-04 | Absent | Partiel | `.github/workflows/site.yml` : PR sur `site/**` → build, liens et image sans publication ; tag `v*` (refusé hors de `main`) ou lancement manuel → publication `ghcr.io/<propriétaire>/orosmo-site` (`GITHUB_TOKEN`, `packages: write`) ; actionlint 1.7.7 : 0 erreur ; procédure : `docs/DEPLOIEMENT_SITE.md` ; **manque** : jamais exécuté sur GitHub, déploiement serveur manuel (G203, G204) |
| WEB-05 | Absent | Absent | optionnel, non retenu pour l'instant (A74 point 4, G205) |

Totaux de `docs/02` (167 lignes), M6 et M14 intégrés dans `dev` : **Fait 53, Partiel 44, Absent 51, Archivé 11, Hors moteur 7, Incompatible 1.** Variation due à M14 (depuis les totaux de M6 : Fait 52, Partiel 41, Absent 55) : Fait +1 (WEB-01), Partiel +3 (WEB-02 à WEB-04), Absent −4. Somme : 53 + 44 + 51 + 11 + 7 + 1 = 167.

## Identifiants de `docs/02` touchés par M6

Preuves sous Linux (g++ 13.3, Mesa 26.1.6, Intel Arc), mesurées sur un clone propre de `chore/M6-release` (`b54f89d`, `docs/releases/M6.md`). Windows, macOS et arm64 : non vérifiés pour le code de M6. Chaque « Partiel » écrit son manque dans la cellule de `docs/02`. **Résultat des tests de chaque ligne « Fait »** : les fichiers de test cités (`tests/assets/`, `tests/anim/`, `tests/renderer/ibl_tests.cpp`) font partie de `make demo-test` (679 cas, 678 verts, 1 rouge connu instable (fenêtre), 1 ignoré, 0 échec).

| ID | Avant | Après | Preuve |
| --- | --- | --- | --- |
| ASSET-01 | Absent | **Fait** | `include/orosmo/assets/model.hpp` `Model`, `loadGltf`, `parseGltf`, `saveGltf` (cgltf, A70) ; `tests/assets/gltf_tests.cpp` : aller-retour `.glb` et `.gltf` + `.bin` champ par champ, `.gltf` écrit à la main (data URI, bande de triangles, matrice de nœud), erreurs `kIo`, `kParse`, `kUnsupported` ; limites : `src/assets/README.md`, `docs/FORMATS.md` |
| ASSET-02 | Partiel | **Fait** | `image_loader.hpp` `TextureRole` → `textureFormatFor`, `texture_cache.hpp` `TextureCache` ; `tests/assets/texture_loading_tests.cpp` sur OpenGL et Vulkan (mip sRGB 188 contre 128 en linéaire) ; limite : 8 bits par canal |
| ASSET-03 | Absent | **Fait** | `ktx2.hpp` `decodeKtx2`, `encodeKtx2` ; `rhi::Format::kBC7*`, `kASTC4x4*` ; `tests/assets/ktx2_tests.cpp` sur OpenGL et Vulkan ; replis ASTC et RGBA8 vérifiés sur CPU seulement (G200) |
| ASSET-04 | Absent | **Fait** | `bake.hpp` `bakePrimitive`, `writeBakedMesh`, `readBakedMesh` ; `tests/assets/bake_tests.cpp` (ACMR non dégradé, LOD1 ≤ 60 %, déterminisme octet pour octet, fichiers corrompus refusés) ; **manque** assumé : pas d'exécutable de cuisson (G200) |
| ASSET-05 | Absent | Partiel | `TextureCache::reloadChanged` ; `ktx2_tests.cpp` (PNG et KTX2, en place, taille changée, fichier corrompu) ; **manque** : modèles glTF non rechargés (G200) |
| REN-10 | Absent | **Fait** | `renderer/material.hpp` `PbrMaterial`, `shaders/rhi/mesh_pbr.vert` et `mesh_pbr.frag`, `assets/render_model.hpp` `blendModeForAlpha` ; `tests/assets/pbr_tests.cpp` sur OpenGL et Vulkan ; G195 |
| REN-11 | Absent | Partiel | variantes par combinaison de cartes (`PbrShaderCache`), instances (`PbrMaterial::instantiate`) ; `pbr_tests.cpp` ; **manque** : spécialisation de constantes (A71, G196) |
| REN-13 | Absent | **Fait** | `renderer/ibl.hpp` `projectEquirect`, `computeBrdfLut`, `ProbeSet`, `IblFeature`, `shaders/rhi/ibl_prefilter.comp` ; `tests/renderer/ibl_tests.cpp` sur OpenGL et Vulkan ; G197 |
| ANIM-01 | Absent | **Fait** | `anim/skinning.hpp` `SkinnedModel`, `shaders/rhi/skin.comp` ; `tests/anim/skinning_tests.cpp` sur OpenGL et Vulkan (compute = référence CPU) ; G198 |
| ANIM-02 | Absent | **Fait** | `anim/clip.hpp`, `pose.hpp`, `state_machine.hpp` ; `tests/anim/anim_tests.cpp` ; G198 |
| ANIM-03 | Absent | **Fait** | `anim/curves.hpp` `Curve<T>`, `PropertyAnimation` ; `anim_tests.cpp` (caméra, lumière, paramètre de matériau) |
| ANIM-04 | Absent | **Fait** | `anim/crowd.hpp` `CrowdRenderer`, `shaders/rhi/mesh_pbr_crowd.vert` ; `tests/anim/crowd_tests.cpp` sur OpenGL et Vulkan (1000 agents = 1 draw call) ; mesure : `docs/PERF_OPENGL_VS_VULKAN.md` ; G199 |

Totaux de `docs/02` (167 lignes comptées par script sur le tableau) : **Fait 52, Partiel 41, Absent 55, Archivé 11, Hors moteur 7, Incompatible 1.** Avant M6 (`dev`) : Fait 42, Partiel 40, Absent 66, Archivé 11, Hors moteur 7, Incompatible 1. Variation : Fait +10 (9 d'Absent, 1 de Partiel : ASSET-02), Partiel +1 (+2 venus d'Absent : REN-11 et ASSET-05, −1 passé à Fait : ASSET-02), Absent −11 (9 vers Fait, 2 vers Partiel). Somme : 52 + 41 + 55 + 11 + 7 + 1 = 167.

## Identifiants de `docs/02` touchés par M5

Preuves sous Linux (g++ 13.3, Mesa, Intel Arc ; la CI exécute aussi sous llvmpipe + xvfb), mesurées sur `milestone/M5` (`docs/releases/M5.md`). Windows, macOS et arm64 : non vérifiés pour le code de M5 (G164). Chaque « Partiel » écrit son manque dans la cellule de `docs/02`. **Résultat des tests de chaque ligne « Fait »** : les fichiers de test cités font partie de `make demo-test` (dernier passage complet sur `dev` : voir la ligne M5 du tableau des jalons, tous les cas verts, 0 échec) ; les suites GPU passent aussi en Vulkan et sur rasteriseurs logiciels (CI).

| ID | Avant | Après | Preuve |
| --- | --- | --- | --- |
| REN-01 | Absent | **Fait** | `render_graph.hpp` `RenderGraph`, `render_feature.hpp` `RenderFeature` ; `tests/renderer/render_graph_tests.cpp` (ordre déduit des accès, passe inutile écartée, cycle refusé, transitoires réutilisés, `read` / `readFinal`, durée GPU par passe) ; limites G151, G152 |
| REN-03 | Partiel | **Fait** | `frame_data.hpp` `FrameData` : 640 octets, 19 `offsetof` figés par `static_assert` ; `shaders/rhi/frame_data.glsl` ; test « FrameData (REN-03): the C++ block and frame_data.glsl share one std140 layout, read by a shader through the graph » |
| REN-04 | Absent | **Fait** | `forward_plus.hpp` `ForwardPlusLighting`, `shaders/rhi/light_clusters.comp`, `SceneRenderer::setDepthPrepass` ; `tests/renderer/forward_plus_tests.cpp` (1000 lumières : même image que le forward, budgets appliqués) ; un pixel d'écart sur la démo (G161) ; limites G153 |
| REN-05 | Partiel | Partiel | ponctuelles et projecteurs rendus (`lights.glsl`, `mesh.frag`) ; **manque** : lumières de surface, projecteur testé comme une sphère dans les clusters (G166) |
| REN-06 | Absent | **Fait** | `shadows.hpp` `resolveShadowBehavior`, `ShadowFeature` ; `tests/renderer/shadow_tests.cpp` (toutes les combinaisons mobilité × mode, violations refusées) |
| REN-07 | Absent | **Fait** | `ShadowFeature` (cascades, PCF `shadow_receiver.glsl`, translation au texel) ; `shadow_tests.cpp` (`cascadeSplits`, `fitCascade`, image identique en rendu relatif à la caméra) ; démo `--shadows on` |
| REN-08 | Absent | Partiel | atlas, budget, priorité, cache (0 redessin quand rien ne bouge), projecteurs, fondu à distance ; **manque** : ombres de contact, fondu entre cascades, cache d'une cascade redessiné dès que la caméra bouge d'un texel (G156) |
| REN-17 | Absent | **Fait** | `setSceneColorFormat` (RGBA16F), `tonemap.hpp`, `TonemapFeature` ; `tests/renderer/post_effects_tests.cpp` (`post/math` sans GPU, `renderer/post` sur chaque backend) ; G158 |
| REN-18 | Absent | **Fait** | `post_effects.hpp` : `BloomFeature`, `AutoExposureFeature` (histogramme en compute), `ColorGradingFeature`, `VignetteFeature` ; `post_effects_tests.cpp` ; démo `--post full` |
| REN-19 | Partiel | Partiel | FXAA, TAA, SMAA 1x analytique (`antialiasing.hpp`) ; **manque** : SMAA sans diagonales ni coins arrondis ni seuil prédiqué, MSAA absent, résolution dynamique sans contrôleur (G159) |
| REN-21 | Partiel | Partiel | AOV à la demande (`aov.hpp`, `declareAov`, `requiredAovs`) ; **manque** : objets instanciés et GPU sans AOV, valeurs provisoires jusqu'à REN-09, règle MSAA non exercée (G169) |
| REN-22 | Absent | Partiel | `shaders/include/orosmo/` (7 fichiers), `shader_toolbox_tests.cpp` ; **manque** : l'atmosphère du critère (module REN-24 : A39, A56, G168) |
| REN-28 | Partiel | **Fait** | `instance_batch.hpp`, `instance_renderer.hpp`, `object_set.hpp` ; `tests/renderer/instancing_tests.cpp` ; démo : 10 000 instances en 2 draw calls |
| REN-29 | Absent | **Fait** | `gpu_driven.hpp` `GpuDrivenFeature`, `gpu_scene.hpp`, `gpu_cull.comp`, `hiz_build.comp` ; `tests/renderer/gpu_driven_tests.cpp` (même ensemble visible et même image que le culling CPU, Hi-Z, OpenGL et Vulkan) ; G154 |
| REN-30 | Absent | **Fait** | `culling.hpp` (`Frustum`, `Bvh`, petits objets) ; `tests/renderer/visibility_tests.cpp` ; BVH : 279 tests d'objets contre 10 000 en linéaire |
| REN-31 | Absent | Partiel | `lod.hpp` `selectLod`, fondu tramé sans trou ; **manque** : imposteurs automatiques (G167) |
| REN-32 | Absent | **Fait** | `transparent_feature.hpp` `sortTransparents` ; `tests/renderer/transparency_tests.cpp` ; démo `--scene mixed` |
| REN-35 | Partiel | Partiel | `debug_view.hpp` : normals, albedo, overdraw, lod, wireframe, motion, gel du frustum ; **manque** : cascades, clusters, instances sur albedo, overdraw et normals (G170) |
| REN-37 | Absent | Partiel | 8 `BlendMode` (`blend_mode.hpp`), `tests/renderer/blend_mode_tests.cpp` (image de référence par mode) ; **manque** : `kMaskA2C` = `kMask`, `kTransmission` = `kBlend` (G171) |
| REN-39 | Absent | **Fait** | `transparent_shadow.hpp`, `ShadowFeature::setCasterTransparency` ; `tests/renderer/transparent_shadow_tests.cpp` ; instances : ombre pleine seulement ; G157 |
| REN-41 | Absent | Partiel | `Mesh::setAovMaterial` ; `aov_tests.cpp` ; **manque** : instances sans AOV (G169) |
| R2D-01 | Partiel | **Fait** | `renderer2d.hpp` `Renderer2D`, `canvas_renderer.hpp` ; `tests/renderer/renderer2d_tests.cpp`, `canvas_tests.cpp` ; interface 143 → 18 draw calls ; un matériau = une texture (G160) |
| R2D-02 | Partiel | **Fait** | `camera2d.hpp` `Camera2D`, mode pixel parfait ; `renderer2d_tests.cpp` |
| R2D-03 | Partiel | **Fait** | `Renderer2D` : rectangles, cercles, lignes, polygones ; `renderer2d_tests.cpp` ; G160 |
| R2D-05 | Partiel | **Fait** | `layer2d_feature.hpp` `Layer2DFeature` (monde, superposition, vers texture) ; `tests/renderer/mixed_2d3d_tests.cpp` ; démo `--scene mixed` |
| R2D-06 | Absent | **Fait** | `mixed_2d3d_tests.cpp` « several views place their own cameras in zones of the output; a view shows in the interface » ; exercé par les tests seulement, pas dans la démo |
| UI-13 | Partiel | Partiel | cachés et hors découpage éliminés (`tests/ui/lean_render_tests.cpp`, 4 cas) ; **manque** : cache par couche (A54, G165) |
| UI-03 | Fait | Fait | le carré d'un `ColorPicker` ouvre la roue `ColorEditor` (`tests/ui/widgets_tests.cpp` « ColorPicker: clicking the swatch opens a ColorEditor popup… ») |

Totaux de `docs/02` (167 lignes comptées : 162 de M5 + WEB-01 à WEB-05 de M14, A43) : **Fait 42, Partiel 40, Absent 66, Archivé 11, Hors moteur 7, Incompatible 1.** Avant M5 : Fait 25, Partiel 41, Absent 77 (le fichier compte une ligne Partiel de plus que l'annonce de M4, 161 identifiants avec Partiel 40 : écart non identifié). Variation : Fait +17 (11 d'Absent, 6 de Partiel), Partiel −1 (+5 venus d'Absent, −6 passés à Fait), Absent −16.

## Identifiants de `docs/02` touchés par M4

Preuves sous Linux (g++ 13.3, Mesa 26.1.6, Intel Arc, ANV, Vulkan 1.4 ; la CI exécute aussi sous llvmpipe + xvfb), depuis un clone propre de `dev`
(`docs/releases/M4.md`). Windows et macOS : jamais observés pour Vulkan (G142).

| ID | Avant | Après | Preuve |
| --- | --- | --- | --- |
| RHI-03 | Absent | **Fait** (Linux, un GPU) | `src/rhi/vulkan/` (`vk_device.cpp`, `vk_encoder.cpp`, `vk_pipelines.cpp`, `vk_resources.cpp`, `vk_swapchain.cpp`, VMA dans `vk_vma.cpp`) ; tests `tests/rhi/vulkan_device_tests.cpp`, `vulkan_backend_tests.cpp`, `rhi_device_tests.cpp` (mêmes cas sur les deux backends, `validationErrorCount() == 0`) ; suites GPU en Vulkan : 125 cas réussis ; `make demo-vulkan-check` : validation à 0 sur la démo |
| RHI-04 | Partiel | Partiel | `--backend`/`--api`, `--strict-backend`, repli sur OpenGL (`demo::resolveBackend`, `tests/demo/launch_options_tests.cpp`), `Capabilities` lisible. **Manque** : Metal (reconnu et refusé ; M9) |
| RHI-06 | Partiel | **Fait** | `PipelineCache` en mémoire (`tests/renderer/resources_tests.cpp`), cache de pipelines Vulkan sur disque (`vk_pipelines.cpp` ; `tests/rhi/vulkan_backend_tests.cpp`, « the pipeline cache is written at shutdown and read back… » : écrit puis relu, fichier d'un autre GPU ignoré) |
| RHI-14 | Partiel | Partiel | `tools/demo_parity.py` (comparaison automatisée, image de différence `--diff-out`), `make demo-vulkan-check`. **Manque** : une seule scène, une seule référence (OpenGL) pour les deux backends, outil hors de `demo-test` et de la CI (G143) |
| TER-01 à TER-04, TER-07, TER-11 à TER-13, TOOL-01, TOOL-02 | Fait / Partiel | **Archivé** | retirés du dépôt (A37), branche `archive/procedural-world` |
| TER-05, TER-06, TER-08, TER-09, TER-10, SIM-02, SIM-03 | Absent | **Hors moteur** | A39 ; addons éventuels après M11 |
| REN-14, REN-23 à REN-27, REN-33, REN-38 | Absent | Absent (module préinstallé, M13) | A39 |
| MOD-05 à MOD-09 | — | Absent | registre d'addons (M12) et modèles `orosmo init` (M11) ; A38, A40 |
| WEB-01 à WEB-05 | — | Absent | site vitrine et wiki (M14) ; A43 |

Totaux de `docs/02` (166 identifiants) : **Fait 25, Partiel 40, Absent 82, Archivé 11, Hors moteur 7, Incompatible 1** (WEB-01 à WEB-05 ajoutés, A43 ; total 166). (Fait −6 :
8 archivés, +2 nouveaux ; Partiel −3.)

## Identifiants de `docs/02` touchés par M3 bis

Ajoutés à la checklist (section M, jalon M11) : `MOD-01`, `MOD-02`, `MOD-03`, `MOD-04`, tous **Absent**. BLD-01 et BLD-02 restent
Partiel (Windows et macOS : G126). Totaux : Fait 31, Partiel 43, Absent 81, Incompatible 1.

## Identifiants de `docs/02` touchés par M3

Preuves sous Linux (g++ 13.3, Mesa 26.1.6, Intel Arc ; la CI exécute aussi sous llvmpipe + xvfb), depuis un clone propre de `dev`
(`docs/BASELINE/tests-M3.txt`). Windows et macOS : jamais observés (G110).

| ID | Avant | Après | Preuve |
| --- | --- | --- | --- |
| TER-01 | Partiel | **Fait** | `include/orosmo/world/noise.hpp` : `perlin2/3`, `simplex2/3`, `worley2/3`, `fbm2/3`, `warpedFbm2` sur hachage entier et opérations IEEE de base (`-ffp-contract=off`) ; `tests/world/noise_tests.cpp` (5 cas, sommes de contrôle de référence) ; GPU : `world/gpu_noise.hpp` `GpuNoise`, `tests/world/gpu_noise_tests.cpp` : écart max 0,5 mm pour 150 m d'amplitude (tolérance `kGpuTolerance` 1e-4 relative), chaque base, avec et sans distorsion |
| TER-02 | Partiel | **Fait** | `terrain.hpp` `generateTile`, `TileKey`, positions depuis des indices entiers globaux ; `tests/world/terrain_tests.cpp` « TER-02: a tile is a pure function… », somme de contrôle de tuile ; asynchrone : `TileStreamer` (TER-04) |
| TER-03 | Absent | **Fait** | `lod.hpp` `selectTiles` (quadtree équilibré, `LodConfig::distanceFactor` = distance de vue), `buildTileMesh` (bords cousus) ; cas « TER-03: the selection covers the roots without overlap… », « TER-03: no crack… » |
| TER-04 | Absent | Partiel | `streaming.hpp` `TileStreamer` (tâches de `core`, budget mémoire, éviction LRU) ; cas « TER-04: streaming generates asynchronously (same bytes as synchronous), respects the memory budget… » ; **manque** : mesure de saccade (G108) |
| TER-07 | Absent | **Fait** (OpenGL) | `include/orosmo/renderer/terrain_renderer.hpp` `TerrainRenderer` (tableau de 4 couches, triplanaire, pente et altitude), `shaders/rhi/terrain.*` ; `tests/renderer/terrain_render_tests.cpp` (2 cas : sable, herbe, neige, roche aux bons endroits) |
| TER-11 | Absent | **Fait** | `world/export.hpp` : `writeHeightPng16`, `writeHeightExr`/`readHeightExr`, `writeObj`, `writeGltf`, `writeStl` (fermé), `writeBiomePng` ; `tests/world/export_tests.cpp` : chaque fichier relu (stb, EXR, JSON + base64, variété fermée orientée) |
| TER-12 | Absent | **Fait** | `apps/terrain_viewer/src/TerrainPanel.cui` + `viewer_state.cpp` (graine, hasard journalisé, préréglages chargés et « Save », thème, `ParamPanel` des `TerrainParams`) ; `tests/world/preset_tests.cpp` (2 cas) ; `make viewer-capture` : panneau rendu, thèmes sombre et clair différents |
| TER-13 | Absent | **Fait** | `world/raycast.hpp` `raycastTerrain`, `meshHeightAt` ; cas « TER-13: consistent with rendering: the rendered depth at the image centre gives the same point as the ray » (5 cm près) |
| TOOL-01 | Partiel | Partiel | `apps/terrain_viewer` (caméra libre, streaming, panneau, export, `--headless --capture`) ; `make viewer-capture` vert ; **manque** : caméra et export non exercés automatiquement (G109) |
| TOOL-02 | Absent | **Fait** | `apps/terrain_gen/main.cpp` ; `make terrain-gen-check` : mêmes octets avec 1 et 4 threads, somme de référence `docs/BASELINE/terrain-gen-seed42-256.txt`, codes 0/1/2 ; 4096² en 0,75 s |
| REN-02 | Partiel | **Fait** | `camera.hpp` : reverse-Z à plan lointain infini, orthographique, `relativeViewMatrix` (rendu relatif à la caméra), jitter ; `tests/renderer/scene_tests.cpp` « Camera (REN-02)… » ; repère du RHI (A26) : `tests/rhi/opengl_state_tests.cpp` |
| REN-34 | Absent | **Fait** | `debug_draw.hpp` `DebugDraw` (lignes, boîtes, sphères, repères ; désactivé par défaut hors debug), `SceneRenderer::drawDebug` ; `tests/renderer/debug_draw_tests.cpp` (2 cas) |
| UI-03 | Absent | **Fait** | `include/orosmo/ui/widgets.hpp` : `Button`, `Slider`, `Checkbox`, `Dropdown`, `TextField`, `NumberField`, `ColorPicker`, `Plot`, `Tabs`, `ScrollView`, `Section`, `Separator`, infobulles ; `tests/ui/widgets_tests.cpp` (18 cas, entrées simulées) ; `tests/ui/ui_render_tests.cpp` (GPU, police réelle, thème sombre). Ajouts (`feat/UI-03-widgets-more`) : `controls.hpp` (`RadioButton`, `RangeSlider`, `SpinBox`, `TextArea`, sélection et presse-papiers de `TextField`), `color.hpp` (roue, éditeur RGB / HSV / HSL / hexadécimal, dégradé, courbe), `menus.hpp` (`Dialog` modale, `MenuBar` / `Menu` / `MenuItem`, `ContextMenu`, `Notifications`, `DynamicImage` / `ImageView`) ; `tests/ui/{controls,color,menus,cui_widgets}_tests.cpp` (17 cas) ; galerie `make gallery-capture`. Limites levées (`feat/UI-03-widget-limits`) : annuler / rétablir, retour à la ligne automatique, menus au clavier, raccourcis installés (`MenuItem`, `Button::setShortcut`, `InputFrame::shortcuts`), touches répétées et entrées communes (`window_input.hpp`) ; `tests/ui/editing_tests.cpp` (5 cas), `tests/platform/text_input_tests.cpp` |
| UI-05 | Partiel | **Fait** | `tools/cui-rs/src/engine.rs` (cible `orosmo/ui`) : arbre compilé en C++, événements `onClick`/`onChange`/`onHover`/`onFocus` nommant des fonctions C++ (appel direct ou action nommée), liaisons `$`, `@cui-cpp`, erreurs `fichier:ligne:colonne`, `#line` ; dépendances de build (`engine.mk`) ; `cargo test` (8), `make cui-test` (18 entrées invalides dont une erreur C++ ramenée au `.cui`), `tests/ui/cui_tests.cpp` (vue compilée et exécutée) |
| UI-06 | Partiel | **Fait** | `layout.hpp` : `VBox`, `HBox`, `Grid` (`columnSpan`), `Overlay`, `ScrollView` ; tailles fixes, ajustées, de remplissage pondérées, min et max ; marges ; alignement ; `tests/ui/layout_tests.cpp` (8 cas, valeurs calculées à la main) |
| UI-07 | Partiel | Partiel | `theme.hpp` `Theme` (jetons, alias, propriétés par widget et par état, `extends`), `ThemeHost` (rechargement à chaud, dernier thème valide gardé), `res/themes/{base,dark,light}.json`, `.style(...)` du langage ; `tests/ui/theme_tests.cpp` (7 cas) ; canevas : coins arrondis, bordures, ombres (`tests/renderer/canvas_tests.cpp`) ; **manque** : jeton de famille de police (G111) |
| UI-11 | Partiel | **Fait** | `docs/UI_LANGUAGE.md` (grammaire, composants, événements, thèmes, erreurs, limites) ; tests valides (`tests/ui/cui/test_panel.cui`) et invalides avec messages attendus (`tests/ui/cui_errors/`, `tools/check_cui_errors.py`, `tools/cui-rs/src/tests.rs`) |
| UI-12 | Absent | **Fait** | `reflect.hpp` `reflectedPanel` (booléen, énumération nommée par `core::EnumNames`, entiers et flottants bornés ou non, chaîne, couleur, vecteurs, structures imbriquées) ; `tests/ui/reflect_tests.cpp` (3 cas) ; `ParamPanel` du langage |
| PLAT-05 | Absent | Partiel | `InputManager::GetTextInput` (rappel de caractères, UTF-8), `AppendUtf8` ; `tests/platform/text_input_tests.cpp` ; presse-papiers du système (`Window::GetClipboardText` / `SetClipboardText`, `ui::useSystemClipboard`) ; **manque** : composition IME (G107) |
| TEST-03 | Partiel | Partiel | sommes de contrôle de référence (bruit, tuile, région, `terrain-gen`) vérifiées sous Linux x64 (CI : Linux x64 et arm64, Windows et macOS non observés) |
| BLD-02 | Partiel | Partiel | `make e2e-init` (paquet, `orosmo init --package`, compilation, lancement sans fenêtre, exemples) en CI Linux ; Windows et macOS rapportés, jamais observés |
| BLD-01 | Partiel | Partiel | `docs/USING_THE_ENGINE.md` (prérequis par système, `orosmo init`, `ENGINE_DIR`) ; Windows et macOS non vérifiés |
| DOC-01 | Partiel | Partiel | `docs/USING_THE_ENGINE.md`, `docs/UI_LANGUAGE.md`, README de `src/world`, `src/ui`, `apps/terrain_gen`, `apps/terrain_viewer` ; manquent toujours le schéma des modules et `ADD_A_BACKEND.md` |

| UI-07 (ajouts) | Partiel | Partiel | polices par thème (section `fonts`, propriété `font`, `.style(font:)`) : `tests/ui/text_tests.cpp` ; images en neuf parties : `tests/ui/nine_slice_tests.cpp` ; le reste comme ci-dessus ; **reste** (revue de M3) : des tailles en dur dans les widgets (largeur minimale d'un champ, pas de défilement : G111) |
| REN-36 (ajouts) | Partiel | Partiel | texte UTF-8 (Latin-1, Latin étendu A, ponctuation) : `tests/ui/text_tests.cpp` ; parité `ui.png` inchangée ; **manque** : autres écritures, mise en forme complexe (G106) |

Totaux après M3 et ses ajouts : Fait 31, Partiel 43, Absent 77, Incompatible 1 (UI-07 reste Partiel : G111) (avant M3 : 16 / 47 / 88 / 1).

## Identifiants de `docs/02` touchés par la revue de la PR #7 (G97 à G100)

| ID | Avant | Après | Preuve |
| --- | --- | --- | --- |
| RHI-15 | Fait (OpenGL) | Fait (OpenGL) | rendu vers `Capabilities::maxColorAttachments` cibles (8 sur Mesa Intel Arc et llvmpipe), une valeur distincte par cible : `tests/rhi/opengl_state_tests.cpp`, cas « MRT up to Capabilities::maxColorAttachments » |
| RHI-03 | Partiel | Absent | l'ancien backend Vulkan est retiré du build et archivé (A1) ; `--api vulkan` répond « not available yet (Vulkan: M4) » (`tests/suites/graphics_api_tests.cpp`) |
| RHI-08 | Partiel | Partiel | destruction différée gardée par une clôture GPU par image (`GlDevice::present`, `flushReleases`) : cas « destroyed resources are released only after the frames in flight AND the GPU fence » |
| RHI-09 | Fait (OpenGL) | Fait (OpenGL) | barrière de stockage entre deux dessins d'une passe qui lie un tampon ou une image de stockage (`GlEncoder::beforeDraw`) ; limite des SSBO exposée (`Capabilities::maxStorageBufferBindings`) |
| RHI-12 | Fait | Fait | réflexion des tableaux de ressources et des chevauchements, includes absents surveillés, étages validés ensemble (`tests/rhi/shader_compiler_tests.cpp`, `tests/renderer/resources_tests.cpp`) |
| UI-02 | Partiel | Partiel | couches prémultipliées, couches gardées (découpage, dépendances), redessin partiel dans l'ordre (`tests/renderer/canvas_tests.cpp`, `ui_collect_tests.cpp`) ; parité `ui.png` toujours dans la tolérance (`parity_tests.cpp`) |

## Identifiants de `docs/02` touchés par M2 lot C

| ID | Avant | Après | Preuve |
| --- | --- | --- | --- |
| PLAT-01 | Partiel | Partiel | `include/orosmo/platform/window.hpp`, `src/platform/window.cpp` : poignée opaque, `GetFramebufferSize`, `GetContentScale`, `RequestClose`, curseur ; `tests/platform/window_input_tests.cpp` (3 cas fenêtre). **Manque** : DPI ×2 non observé (G94), plein écran et sans bordure non testés, `Window` pas encore dans `orosmo::platform` |
| PLAT-02 | Partiel | Partiel | molette cumulée par rappel et exposée par `GetMouseScroll` (cas `PLAT-02 the mouse wheel…`, `TODO` retiré) ; `InputManager` dans `src/platform/`. **Manque** : manettes (PLAT-03), remappage persistant, nommage `orosmo::platform` |
| PLAT-04 | Absent | Partiel | `--headless` (`GraphicsLaunchOptions::headless`, `Game(bool)`) : aucune fenêtre créée par la démo, `scene.png` **identique** à la référence (0/255) ; cas `launch options parse --headless`. **Manque** : exige encore un serveur d'affichage (GLFW crée un contexte caché, G93), vérifié sous Linux seulement |
| RHI-08 | Partiel | Partiel | `--vsync` règle `Device::setVSync` ; **manque** : swapchain Vulkan (M4) |
| TEST-05 | (inchangé) | (inchangé) | isolation des API : bloquante pour toutes les API, baseline supprimée |

## Identifiants de `docs/02` touchés par M2 B6

Preuves sous Linux (g++ 13.3, Mesa 26.1.6, Intel Arc ; la CI exécute aussi sous llvmpipe + xvfb). Windows et macOS : tests GPU et démo non exécutés (G67).

| ID | Avant | Maintenant | Fichier / symbole, test, résultat |
| --- | --- | --- | --- |
| RHI-02 | Partiel | **Fait** (OpenGL) | la démo tourne sur le RHI : `apps/demo/src/Game/Game.cpp` (`SceneRenderer`, `CanvasRenderer`, périphérique adoptant la fenêtre) ; `apps/demo/bin/debug/main --api opengl --capture <dir>` : `scene.png` **identique à `docs/BASELINE/reference/scene-opengl-seed1-1280x720.png`** (0 pixel d'écart) ; `tests/renderer/parity_tests.cpp` (scène et `ui.png`, 0 pixel d'écart) ; plus aucun `gl*`/`GL_*` hors `src/rhi/opengl/` |
| RHI-14 | Absent | Partiel | tests d'images de référence : `Image`, `compareImages`, `parity_tests.cpp` (scène, interface), capture de la démo comparée en CI (`tools/compare_png.py`, tolérance pour llvmpipe). **Manque** : référence par backend (Vulkan M4), rapport de différence publié en CI |
| RHI-04 | Partiel | Partiel | `--api opengl` seul disponible, `--api vulkan` / `metal` refusés avec la raison (`GraphicsAPI.cpp`, test `GraphicsAPI`) ; `--list-apis`, `--choose-api`. **Manque** : option `--backend`, repli documenté, Vulkan (M4) |
| RHI-18 | Partiel | **Fait** | l'ancien backend Vulkan, qui annonçait `rayQuery = true` à tort, est supprimé ; `Capabilities` du RHI : toutes les capacités optionnelles `false`, testé (`tests/rhi/opengl_device_tests.cpp`) |
| RHI-11 | Partiel | Partiel | `ProfilerGpuTimerFactory` : le `Profiler` de la démo mesure en GPU par horodatages du RHI (testé : `tests/renderer/resources_tests.cpp` ; démo : durées non nulles après 200 images). **Manque** : noms de debug non relus (G69), Vulkan (M4) |
| REN-05 | Partiel | Partiel | l'éclairage de la démo passe par `LightManager` (parité de `scene.png`) ; ponctuelles et projecteurs non rendus (G82) |
| REN-36 | Partiel | Partiel | le texte de la démo passe par la liste de dessin et le RHI (parité de `ui.png`) ; UTF-8 (G27) |
| UI-01 | Partiel | Partiel | la démo utilise `Manager::Collect` ; le singleton `UI::Manager::Instance()` reste (G85) |
| UI-02 | Partiel | Partiel | l'interface ne contient plus aucun chemin de dessin immédiat (`Draw`, `Sprite`, `RenderTarget` supprimés) ; **manque** : batch 2D (R2D-01, M5), contexte explicite à la place du singleton (G85) |
| PLAT-01 | Partiel | Partiel | `Window` ne connaît plus aucune API graphique (rappel `beforeCreate`) ; **manque** : `platform::Window`, GLFW encore dans `Window`, `InputManager`, la démo (lot C) |

Totaux après B6 : Fait 16, Partiel 47, Absent 88, Incompatible 1 (après B5 : 14 / 48 / 89 / 1 ; RHI-02 et RHI-18 passent à Fait, RHI-14 d'Absent à Partiel).

## Identifiants de `docs/02` touchés par M2 B5

Preuves sous Linux (g++ 13.3, Mesa 26.1.6, Intel Arc ; la CI exécute aussi sous llvmpipe + xvfb). La démo n'utilise pas encore ce chemin (bascule en B6).

| ID | Avant | Maintenant | Fichier / symbole, test, résultat |
| --- | --- | --- | --- |
| UI-02 | Absent | Partiel | `include/orosmo/renderer/draw_list.hpp` `DrawList` (données pures) ; `ComponentBase::Collect`, `ContainerBase::Collect` (couches, découpage, défilement), `TextWidgetBase::Collect`, `Manager::Collect` ; `tests/renderer/ui_collect_tests.cpp` (6 cas, sans GPU) ; `CanvasRenderer` : `tests/renderer/canvas_tests.cpp` (7 cas). **Manque** : l'interface connaît encore le chemin hérité (`Draw`, `Sprite`, `RenderTarget`, G84) ; pas de batch R2D-01 (M5, A21) |
| REN-36 | Partiel | Partiel | texte par RHI : `TextRenderer::setDrawListSink`, shader MSDF en GLSL 450 (`ui_glyph`), atlas mis en cache ; `tests/renderer/parity_tests.cpp` : six lignes de texte identiques à la référence (0 pixel d'écart). **Manque** : UTF-8 (G27), mise en page avancée non testée par liste (G87) |
| UI-01 | Partiel | Partiel | `Manager::Collect` ; le singleton et l'ancien chemin restent (G84, G85) |
| RHI-02 | Partiel | Partiel | parité de `ui.png` (en plus de `scene.png`, B4) : `docs/BASELINE/reference/ui-opengl-fixedtext-1280x720.png` ; reste la bascule de la démo |

Totaux après B5 : Fait 14, Partiel 48, Absent 89, Incompatible 1 (UI-02 : Absent → Partiel ; avant : 14 / 47 / 90 / 1).

## Identifiants de `docs/02` touchés par M2 B4

Preuves sous Linux (g++ 13.3, Mesa 26.1.6, Intel Arc ; la CI exécute aussi sous llvmpipe + xvfb). La démo applicative n'utilise pas encore la nouvelle couche (bascule en B6) : RHI-02 et les identifiants de migration restent donc ouverts.

| ID | Avant | Maintenant | Fichier / symbole, test, résultat |
| --- | --- | --- | --- |
| RHI-02 | Partiel | Partiel | `tests/renderer/parity_tests.cpp` : terrain graine 1 (250 000 sommets, 500 000 triangles), caméra, ambiante + directionnelle, rendus par `SceneRenderer` sur le RHI : **0 pixel d'écart à la tolérance 0** avec `docs/BASELINE/reference/scene-opengl-seed1-1280x720.png`. **Manque** : la démo n'utilise pas encore cette couche, l'interface et le texte ne sont pas migrés (B5, B6), `ui.png` non couvert |
| REN-05 | Partiel | Partiel | `LightManager` / `Light` : ambiante, directionnelle, ponctuelle, projecteur ; disposition std430 vérifiée par `static_assert` et relue octet par octet (`tests/renderer/scene_tests.cpp`) ; tampon qui grandit. **Manque** : ponctuelles et projecteurs non rendus par le shader (G82) ; clusterisation (M5) |
| REN-02 | Absent | Absent | `Camera` reprend la perspective de l'ancienne caméra (GL, profondeur −1..1). Le reverse-Z reste à faire (M3, A26) |
| RHI-15 | Fait (OpenGL) | Fait (OpenGL) | `SceneRenderer` s'en sert : cible de scène RGBA8 + RG16F (vecteurs de mouvement) + profondeur, testée |
| RHI-10 | Partiel | Partiel | `Mesh` instancié dessiné par `drawIndexed` avec `instanceCount` (attributs par instance) |

Totaux inchangés : Fait 14, Partiel 47, Absent 90, Incompatible 1.

## Identifiants de `docs/02` touchés par M2 B3

Preuves sous Linux (g++ 13.3, Mesa 26.1.6, Intel Arc ; la CI exécute aussi sous llvmpipe + xvfb). Windows et macOS : tests GPU non exécutés (G67). Les nouvelles classes ne sont **pas encore branchées** sur la démo (B4, B5) : les états de migration (RHI-02, REN-05, REN-36, UI-01, UI-02) ne changent donc pas.

| ID | Avant | Maintenant | Fichier / symbole, test, résultat |
| --- | --- | --- | --- |
| RHI-05 | Partiel | Partiel | `orosmo::renderer::Texture`, `GpuBuffer` : création, texels, mips selon le filtre, `resize`, relecture, libération RAII sans fuite (`stats()` revient à 0) ; `tests/renderer/resources_tests.cpp`. **Manque** : cube, tableau et 3D échantillonnés dans un shader, façade 2D seulement (G79) |
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
| RHI-01 | Incompatible | Partiel | `include/orosmo/rhi/{handle,types,capabilities,device}.hpp` : `Device`, `CommandEncoder`, handles opaques ; `check_headers --strict` (102 en-têtes, 0 non autonome) ; `tests/rhi/handle_tests.cpp`. **Manque** : aucune façade (`Mesh`, `Texture`…) ne l'utilise encore (lot B) |
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
| CORE-02 | Absent | Partiel | `include/orosmo/core/error.hpp` `Result<T>`, `makeError`, `ENGINE_ASSERT` ; `tests/core/error_tests.cpp` verts. **Partiel** : le code existant lance encore des exceptions (G51) |
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
