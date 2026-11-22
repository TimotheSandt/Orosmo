# 05 · Plan par jalons

L'ordre est choisi pour que le générateur de terrain existe tôt, sans reconstruire quand le moteur change de backend. Chaque jalon finit par une **démo** et une **définition de terminé** vérifiable. L'IA ne passe au suivant qu'après validation.

## M00 · Nettoyage du projet et du dépôt (tout premier travail)

Détail complet dans `docs/06-WORKFLOW_GIT_ET_NETTOYAGE.md` §6. Résumé : sauvegarde (tag, branche, bundle), référence de comportement dans `docs/BASELINE/`, inventaire en lecture seule, `docs/CLEANUP_PLAN.md` soumis à l'auteur, puis exécution sur une branche `chore/cleanup-*` : `.gitignore`, `.gitattributes`, `.editorconfig`, `.clang-format`, `README.md` racine, `third_party/` documenté, déplacements propres avec `git mv`, build mis à jour à chaque étape. L'historique actuel est **conservé**.

**Démo** : le projet compile et se comporte comme avant le nettoyage, à partir d'un clone propre.
**Terminé** : fin de M00 de `docs/06` ; branche `dev` créée ; l'auteur fusionne vers `main` s'il est satisfait.

## M0 · Audit et base solide

- M00 est terminé et l'audit de `docs/04` est terminé et validé avant de commencer (l'audit se fait **après** M00, sur le dépôt rangé).
- **Portage Windows → Linux** : le projet a été commencé sous Windows et le développement continue sous Linux. Corriger la casse des `#include`, les fins de ligne (`.gitattributes`), les chemins, les dépendances et les options de lien ; documenter la commande de build Linux exacte dans `CLAUDE.md`.
- Build reproductible sur les trois systèmes ; CI qui compile et lance un test vide.
- Framework de tests (TEST-01 : **framework et exécution en CI** ; la couverture de `core` est suivie en M1 et celle de `world` en M3, sous le même identifiant, `Partiel` jusque-là), formatage, avertissements en erreur, ASan/UBSan.
- Scripts de vérification des couches et d'isolation des API (§8 de `docs/03`), en mode « avertissement » tant que le code n'est pas migré.
- Images et sorties de référence de l'application actuelle, **avec la capture d'écran (TOOL-03, P0)** et un outil de comparaison d'images.

**Démo** : l'application actuelle tourne via la CI, avec ses captures de référence.
**Terminé** : CI verte sur Windows et Linux (macOS si possible, sinon noté) ; `docs/STATUS.md` créé.

## M1 · Noyau

- CORE-02 à CORE-13 selon l'audit : erreurs, tâches, temps, maths avec `double`, ressources à handles, VFS, sérialisation **avec réflexion**, configuration, observateur de fichiers, profilage de base.
- Le logger existant est conservé et testé.
- Aucun appel graphique ajouté.

**Démo** : outil qui lance 10 000 tâches, recharge un fichier de configuration modifié sans redémarrer et affiche des mesures.
**Terminé** : tests unitaires verts, sanitizers propres, README par module ; couverture de tests de `core` (TEST-01, première moitié).

## M1 bis · Remise en ordre des couches (préparation de M2)

Décidé par l'auteur le 2026-11-17 : ce qui doit être nettoyé **avant** M2 mais qui n'appartient pas au noyau est fait ici, juste après M1, pour que M2 démarre sur des couches propres. Aucune fonctionnalité nouvelle.

- **Couches** : amener `tools/baseline/layers.txt` à **zéro** violation (`python3 tools/check_layers.py --strict`). Aujourd'hui : `Profiler` (core) inclut `Graphics/Core/GraphicsRuntime.h` ; `Window.cpp` (platform) inclut des en-têtes rhi (`GraphicsDiagnostics`, `RenderState`, `GraphicsRuntime`, `WindowContext`). Inverser ces dépendances par des interfaces (le `Profiler` reçoit un fournisseur de minuteries GPU ; la fenêtre ne connaît plus le contexte graphique).
- **Appels système dans `core`** : sortir `<windows.h>` de `Logger`, `Utilities` et `FPSCounter` (derrière une interface `platform`), sans changer le comportement. Ajouté après la revue CodeRabbit de M1 (G59) : `std::getenv` de `Vfs::defaultUserDirectory` et l'accès direct à `std::filesystem` de `FileWatcher`/`TraceRecorder` passent aussi par cette interface.
- **En-têtes autonomes** : `check_headers.py --strict` à zéro (aujourd'hui `Graphics/Upscaling/IFrameGenerationMode.h`).
- **Nommage hérité** : `utilities.h` / `Utilities.cpp` (G12), dossier d'en-têtes `Graphics/Backend/` contre `Backends/` (G13).
- **Pas de changement de comportement** : mêmes tests, même capture de référence (`docs/BASELINE/reference/`).

**Démo** : `make check` sans aucun avertissement de couches ni d'en-tête ; capture identique à la référence.
**Terminé** : baselines `layers.txt` vide, `check_headers.py --strict` vert, 0 régression de test, README des modules mis à jour.

## M2 · RHI et backend OpenGL (migration de l'existant)

- Définir l'interface (`docs/03` §3), la couche plateforme (PLAT-01, PLAT-04).
- Écrire le backend OpenGL 4.3+ avec MRT (RHI-15), textures 3D et images de stockage (RHI-16), compute (RHI-09), dessin instancié et indirect (RHI-10), `Capabilities` incluant les capacités optionnelles réservées (RHI-18).
- **Migrer le rendu actuel** (éclairage, texte, UI) derrière le RHI.
- Pipeline de shaders (RHI-12) : GLSL → SPIR-V → GLSL cible ; réflexion ; inclusions ; rechargement à chaud.
- Tests d'images de référence (RHI-14).
- L'interface produit des listes de dessin (UI-02) ; le texte passe par le RHI.

### Découpage de M2 en lots et sous-lots (décidé par l'auteur le 2026-11-18)

Trois lots, chacun fusionné dans `dev` puis arrêt pour validation. Le **lot B** est une **réécriture explicite** des façades de rendu sur `Device` / `CommandEncoder` (passes, groupes de liaison, pipelines à état fixe), sans adaptateur d'état implicite : l'ancienne abstraction `IGraphicsDevice` et les backends associés disparaissent. L'API publique des façades **change** ; `Game`, `World` et l'interface suivent. Chaque sous-lot est une branche, fusionnée quand compilation, tests et contrôles sont verts ; **la démo peut être temporairement incomplète entre deux sous-lots** (notée dans leur compte rendu) et la capture de référence ne redevient identique qu'à B6.

| Lot | Contenu | État |
| --- | --- | --- |
| A | Interface RHI, backend OpenGL hors écran, tests GPU, `make rhi-demo` | **Fait** (`docs/releases/M2-lotA.md`) |
| B1 | **Pipeline de shaders** (RHI-12) : GLSL 450 → SPIR-V → GLSL 430, réflexion, `#include`, cache mémoire et disque, rechargement à chaud | **Fait**, fusionné dans `dev` (branche `feat/RHI-12-shader-pipeline`) : `tests/rhi/shader_compiler_tests.cpp` (compilation, réflexion, includes, cache, rechargement, rendu OpenGL), résultats `docs/BASELINE/tests-M2-B2.txt` et `docs/releases/M2-B2.md` |
| B2 | **Surface de fenêtre du RHI** : le périphérique OpenGL adopte une fenêtre GLFW créée par la couche appelante (backbuffer = framebuffer par défaut, `present` = échange des tampons, vsync, `resize`) ; le contexte et les attributs de contexte restent dans `src/rhi/opengl/`. Test de présentation sous xvfb | **Fait**, fusionné dans `dev` (branche `feat/RHI-08-window-surface`) : `tests/rhi/window_surface_tests.cpp` (adoption, présentation, redimensionnement, vsync, backbuffer non échantillonnable, sous xvfb en CI), résultats `docs/BASELINE/tests-M2-B2.txt` et `docs/releases/M2-B2.md` |
| B3 | **Couche de ressources** `orosmo::renderer` (`include/orosmo/renderer/`, `src/renderer/`, archive `librenderer.a`) : `GpuBuffer`, `Texture`, `RenderTarget` (MRT, profondeur lisible, `passDesc`), `ShaderLibrary` / `ShaderProgram` (rechargement à chaud branché), `PipelineCache`, `Image` (relecture, PNG, comparaison), `GpuTimer` ; ajouts au RHI : `textureDesc`, `timestampsReady`. **Écart au plan initial (noté le 2026-11-18)** : ces classes sont **nouvelles** et coexistent avec les anciennes façades jusqu'à ce que leurs consommateurs migrent (B4, B5) ; les supprimer maintenant aurait cassé la compilation à chaque commit. La suppression de `Buffer`, `Texture`, `RenderTarget`, `ShaderProgram`, `ShaderLibrary`, `Screenshot`, `GPUTimestampQuery` et de `GraphicsRenderState` (G38) passe donc en B6 | **Fait**, fusionné dans `dev` (branche `feat/RHI-02-render-resources`) |
| B4 | **Couche de scène** `orosmo::renderer` : `Camera` (sans entrées), `LightManager` (SSBO vérifié), `Mesh` (sommets, instances, transformation, textures, constantes), `FullscreenBlit`, extensions d'upscale / post-traitement / génération d'images, `SceneRenderer` (échelle de rendu, jitter, historique, MRT) ; shaders `shaders/rhi/` en GLSL 450 ; **parité avec l'image de référence** du rendu hérité (terrain graine 1, 1280x720). **Écart au plan (noté le 2026-11-18)** : `Sprite` et `Renderer2D` appartiennent au côté interface et passent en B5 ; `Game` et `World` ne basculent sur la nouvelle couche (et la démo ne présente via le RHI) qu'en B6, avec l'interface, car l'ancien et le nouveau runtime graphique ne peuvent pas dessiner dans la même fenêtre | **Fait**, fusionné dans `dev` (branche `feat/RHI-02-scene-renderer`) |
| B5 | **Dessin 2D et interface par listes de dessin** (UI-02) : `DrawList` (données pures) et `CanvasRenderer` sur le RHI (rectangles, glyphes MSDF, couches hors écran, découpages), shaders `ui_*` en GLSL 450 ; `TextRenderer` produit des séries de glyphes dans la liste (`setDrawListSink`) ; l'interface produit sa liste par `Collect` (composant, conteneur avec couches et découpage, texte, `Manager`) ; **parité avec `ui.png` de l'ancien rendu** (texte figé, `docs/BASELINE/reference/ui-opengl-fixedtext-1280x720.png`) : 0 pixel d'écart. **Écart au plan (noté le 2026-11-18)** : comme en B3 et B4, le chemin d'interface hérité (`Draw`, `Sprite`, `Renderer2D`, `Manager::Render`) est **conservé** jusqu'à la bascule de la démo ; `Sprite`, `Renderer2D` et le singleton `UI::Manager` (A10) sont supprimés ou remplacés en B6 | **Fait**, fusionné dans `dev` (branche `feat/UI-02-draw-lists`) |
| B6 | **Bascule, retrait et vérification** (**Fait**, branches `feat/RHI-02-demo-switch` et `chore/RHI-02-remove-legacy-graphics`) : `Window` sans graphique (rappel avant création, GLFW initialisé par elle), `Game` / `World` / `main` sur `SceneRenderer`, le périphérique RHI de fenêtre (B2), `CanvasRenderer` et `Manager::Collect` ; pilotage de la caméra par `CameraController` (G80) ; minuteries GPU du Profiler sur le RHI ; suppression de l'ancienne abstraction (`IGraphicsDevice`, backends OpenGL, Vulkan, Metal, `GraphicsRenderState`, façades, `Sprite`, `Renderer2D`, upscale, shaders d'origine ; étiquette `archive/pre-M2-B6-legacy-graphics`) ; `--api vulkan` : « pas avant M4 » ; isolation des API **bloquante** pour `gl*`, `Vk*`, `MTL*` (`--graphics-only --strict`, 0 infraction) ; `docs/RHI.md`. **Reste** (déplacé ou non fait) : singleton `UI::Manager` (G85), images de référence par backend et rapport de différence publié (RHI-14 Partiel) | **Fait**, fusionné dans `dev` |
| C | `platform::Window` (GLFW caché, DPI, plein écran), mode sans fenêtre (PLAT-04), entrées (PLAT-02, `Camera::Inputs`), isolation des API **intégralement bloquante** (GLFW compris), vsync | **Fait** (`docs/releases/M2.md` ; branches `feat/PLAT-01-platform-window`, `feat/PLAT-04-headless`, `feat/PLAT-01-dpi`) |

Les identifiants de `docs/02` restent ceux de la table de rattachement (M2). Un sous-lot n'est « Fait » que sur preuve (CLAUDE.md règle 1) ; STATUS est mis à jour à chaque fusion.

**Démo** : l'application actuelle, même image qu'avant, sans aucun appel `gl*` hors de `src/rhi/opengl/`.
**Terminé** : règle d'isolation des API en mode bloquant ; images identiques à la référence dans la tolérance ; documentation du RHI.

## M3 · Premier générateur de terrain et interface de base (la démo qui compte)

- TER-01 (bruit CPU et GPU), TER-02, TER-03, TER-04, TER-07, TER-11, TER-12, TER-13.
- TOOL-01 (visualiseur) et TOOL-02 (ligne de commande, sans fenêtre).
- REN-02 (caméra, reverse-Z), REN-34 (debug), éclairage existant migré.
- TEST-03 : déterminisme.
- **Interface** : fin du langage d'interface pour ce que le visualiseur exige : UI-03 (widgets), UI-05 (compilateur, erreurs lisibles), UI-06 (mise en page), UI-07 (thèmes JSON), UI-11 (spécification et tests), UI-12 (paramètres réfléchis → curseurs).
- **Première release utilisable (décidé par l'auteur le 2026-11-18)** : M3 est le premier jalon dont la release (`vX.Y.Z`) est présentée comme utilisable et à peu près stable ; les releases précédentes (`v0.1.0`…) restent des instantanés pour développeurs. Tâches (rattachées à DOC-01, BLD-01, BLD-02) :
  - **Guide d'utilisation** `docs/USING_THE_ENGINE.md`, relié depuis le `README.md` racine : installer depuis la release par l'exécutable `engine` (`engine init`, `engine update`) **et** par le zip `engine-pkg-<plateforme>.zip` (`ENGINE_DIR`), prérequis par système, projet minimal complet, limites connues, version minimale du compilateur.
  - **Mettre à jour** `tools/engine-sdk/dist-README.md` et le corps de la release (`build-engine.yml`) : archives `librhi.a` et `libplatform.a`, en-têtes `include/orosmo/…`, API du RHI.
  - **Test de bout en bout en CI** : à partir du paquet construit (sans les sources du moteur), créer un projet vide avec `engine init`, le compiler et le lancer (capture ou mode sans fenêtre) sur Linux, Windows et macOS ; échec de la CI si le chemin documenté casse.
  - **Stabilité de l'API** : figer les en-têtes publics `include/orosmo/…` (migration `PascalCase` → `camelCase` terminée pour les modules livrés), table des changements incompatibles dans `docs/releases/`, numéro de version sémantique ; les identifiants de `docs/02` livrés doivent être `Fait` ou limités dans le guide.
  - **Exemple fourni** : exemples compilables depuis le paquet (`examples/minimal`, `examples/ui_gallery` ; note A34 du 2026-11-20 : `world`, `TerrainRenderer`, `terrain-gen` et `terrain-viewer` restent dans le dépôt pour les démos internes et sont exclus de la release cœur générique).

### Découpage de M3 en lots (proposé et appliqué par l'IA le 2026-11-19, `docs/QUESTIONS.md` A29 ; même portée, ordre explicite)

Chaque lot est fusionné dans `dev` quand il est vert ; le rapport de fin de jalon est fait après le lot F. Nouveau module
`orosmo::world` (`include/orosmo/world/`, `src/world/`, archive `libworld.a`, couche `world` de `docs/03`) : il ne dépend pas
de `renderer` (génération sans GPU) ; le calcul GPU passe par le RHI (`src/world/gpu/`).

| Lot | Contenu | IDs |
| --- | --- | --- |
| A | **Bruit déterministe** : Perlin, Simplex, Worley, fBm, distorsion de domaine, sur des entiers hachés et des opérations IEEE de base (pas de `sin`/`exp` de la bibliothèque, pas de contraction FMA) ; sommes de contrôle de référence versionnées | TER-01 (CPU), TEST-03 |
| B | **Tuiles, niveaux de détail, streaming** : hauteur par tuile (graine, coordonnées, paramètres), génération asynchrone (tâches de `core`), quadtree à niveaux de détail sans fissure (bords cousus), chargement/déchargement par distance sous un budget mémoire | TER-02, TER-03, TER-04 |
| C | **Rendu** : repère Vulkan du RHI (A26 : `glClipControl`, origine en haut, profondeur 0..1), caméra reverse-Z à plan lointain infini, dessin de debug, rendu du terrain (mélange pente/altitude, triplanaire, tableau de textures), bruit sur GPU identique au CPU dans une tolérance fixée | REN-02, REN-34, TER-07, TER-01 (GPU) |
| D | **Export et outils** : carte de hauteur PNG 16 bits et EXR, maillage OBJ, glTF, STL, carte de biomes, relus par les tests ; lancer de rayon sur le terrain ; `terrain-gen` (ligne de commande, sans fenêtre, code de retour fiable) | TER-11, TER-13, TOOL-02 |
| E | **Interface** : widgets, compilateur du langage (erreurs fichier:ligne:colonne, événements nommés), mise en page complète, thèmes JSON à jetons et états rechargés à chaud, spécification `docs/UI_LANGUAGE.md` et tests, paramètres réfléchis → curseurs ; panneau de réglage du terrain (graine, préréglages) ; visualiseur | UI-03, UI-05, UI-06, UI-07, UI-11, UI-12, TER-12, TOOL-01 |
| F | **Release utilisable** : API publique figée et nommée selon `docs/03` §7, `docs/USING_THE_ENGINE.md`, paquet et corps de release à jour, test de bout en bout `engine init` + compilation en CI sur les trois systèmes, exemples compilables depuis le paquet | DOC-01, BLD-01, BLD-02 |

**État (2026-11-19)** : les six lots sont fusionnés dans `dev` (branches `feat/TER-01-noise` … `docs/DOC-01-using-the-engine`) ;
bilan, preuves et écarts dans `docs/releases/M3.md`. Écarts au plan : l'API de `platform` reste en `PascalCase` (A31, G105) ;
l'interface héritée `UI::` reste pour la démo (A30) ; Windows n'est pas observé, et sous macOS le parcours complet (`make e2e-init`, qui lance la démo en OpenGL) est **bloqué** jusqu'à M4 : le RHI demande OpenGL 4.3, macOS s'arrête à 4.1 ; la compilation et les tests sans GPU y tournent en CI (G110).

**Démo** : visualiseur avec graine et paramètres modifiables en direct dans une interface décrite dans le langage maison, thème changeable ; `terrain-gen --seed 42 --size 4096 --out relief.png` produit une carte de hauteur identique à chaque lancement.
**Terminé** : somme de contrôle de référence ; pas de fissure entre niveaux de détail ; budget mémoire respecté ; exports vérifiés en les rouvrant ; tests de compilation du langage verts ; `docs/UI_LANGUAGE.md` écrit. La release de fin de M3 est utilisable : `docs/USING_THE_ENGINE.md` écrit, `engine init` + compilation vérifiés en CI sur les trois systèmes, API publique figée.

## M3 bis · Release générique et confort de build (entre M3 et M4)

Décidé par l'auteur le 2026-11-21 : une release intermédiaire, après la fusion de M3 dans `main` et avant M4.

- **Release cœur générique** (A34) : `orosmo-pkg-<plateforme>.zip` ne contient que le moteur générique (`core`, `platform`, `rhi`,
  `renderer` sans `TerrainRenderer`, `ui`, `cui`) ; `world`, `TerrainRenderer`, les shaders `terrain.*`, `terrain-gen` et
  `terrain-viewer` restent dans le dépôt (`ENGINE_EXTRA_MODULES`). Modèle de projet `orosmo init` et exemple `minimal` génériques.
- **Jalon M11** (modules et addons, `MOD-01` à `MOD-04`) ajouté au plan.
- **Affichage de la compilation** : barre de progression dans un terminal (fichiers compilés, en cours, restants, à jour) ;
  `sync-demo` garde les dates des copies (plus de recompilation de la démo à chaque `make all-debug`).

**Démo** : `make all-debug` dans un terminal (barre), puis `make e2e-init` (paquet sans module procédural, projet créé et lancé).
**Terminé** : `e2e-init` vérifie l'absence de `world` dans le paquet ; tests, ASan / UBSan et `make check` verts ; note de version
`docs/releases/M3bis.md`.

## M4 · Backend Vulkan

- Backend complet avec VMA, barrières déduites, pipelines en cache (mémoire et disque, RHI-06).
- Sélection du backend à l'exécution (RHI-04).
- Guide `docs/ADD_A_BACKEND.md`.
- **Début de M4 (décision de l'auteur, 2026-11-21, A31)** : renommer l'API de `platform` (`Window`, `InputManager`) selon `docs/03` §7 (`orosmo::platform`, `camelCase` ; G96, G105), avant d'écrire le backend.
- **`generateRegion` renvoie `Result<HeightField>`** (A33, décision du 2026-11-21) : livré dans `dev`, puis archivé avec le module `world` (`archive/procedural-world`, A37) ; sans objet dans la release M4.
- **Retrait du procédural et démo unique (A37)** : `apps/demo` devient l'unique application ; `world`, `TerrainRenderer`, `terrain_gen`, `terrain_viewer`, `ui_gallery`, `core_demo` et `rhi_demo` sont retirés (archivés).

**Démo** : la démo unique `apps/demo` (scène 3D statique déterministe, A37), lancée avec `--backend=vulkan` et `--backend=opengl` (`--ui off`), donne la même image dans la tolérance (référence `docs/BASELINE/reference/scene-opengl-demo-1280x720.png`).
**Terminé** : couches de validation Vulkan silencieuses ; images de référence dans la tolérance sur les deux backends ; mesure de performance comparée dans `docs/`.

## M5 · Cœur du rendu

- REN-01 (graphe de passes, plugins), REN-03 (données par image), REN-04 (forward+ clusterisé), REN-05, REN-06 à REN-08 (ombres, mobilité, cache), REN-17 à REN-19 (HDR, post-traitement, anticrénélage), REN-21 (AOV et MRT à la demande), REN-22 (boîte à outils), REN-28 à REN-31 (instancing, rendu piloté par GPU, culling, LOD), REN-32, REN-35, REN-37 (modes de fusion), REN-39 (ombres des transparents), REN-41 (transparents et sorties auxiliaires).
- **2D** : R2D-01, R2D-02, R2D-03, R2D-05, R2D-06 ; l'interface passe par le batch 2D.

**Démo** : la scène générique de la démo unique avec ombres en cascade et cache, des milliers d'objets instanciés en quelques draw calls, normales et vecteurs de mouvement activés à la demande par un effet, scène mélangeant 3D, sprites dans le monde et interface.
**Terminé** : tests d'images par backend, profilage GPU par passe visible, nombre de draw calls mesuré et documenté, aucune régression sur les démos précédentes.

## M6 · Contenu importé et animation

- ASSET-01 à ASSET-05 (glTF, textures, KTX2, cuisson, rechargement), REN-10, REN-11 (matériaux PBR et variantes), REN-13 (IBL, sondes).
- ANIM-01 à ANIM-04 (squelettes, clips, machine à états, animation de propriétés, foules instanciées).

**Démo** : un modèle glTF animé, puis mille instances animées avec un seul draw call par lot ; un asset modifié sur disque se met à jour en direct.
**Terminé** : formats vérifiés par import/export de fichiers d'exemple ; mesure du coût GPU du skinning ; README des formats pris en charge.

## M7 · Supprimé (décision de l'auteur, 2026-11-21, A39)

Le moteur reste générique : l'atmosphère, les effets et le « monde riche » ne sont pas du cœur. Le numéro M7 n'est **pas réattribué** (les références de `docs/` restent valables). Où vont ses éléments :

- **Effets de rendu** (`REN-23` bruit en compute, `REN-24` atmosphère, `REN-25` brouillard, `REN-26` nuages, `REN-33` particules, `REN-14` occlusion ambiante, `REN-27` eau, `REN-38` réfraction et transmission) : modules **préinstallés** livrés avec le moteur, après le système de modules (voir **M13**).
- **Terrain et planètes** (`TER-05` érosion, `TER-06` biomes, `TER-08` végétation, `TER-09` planètes sphériques, `TER-10` eau et rivières) : hors du moteur. Ils reviendront, si l'auteur le veut, comme addons construits depuis `archive/procedural-world`, après M11.

## M8 · Interface avancée, scène, simulation et logique rechargeable

- UI-04, UI-08 (rechargement à chaud de l'interface), UI-09, UI-10.
- ECS-01 à ECS-03, SIM-01 (SIM-02 et SIM-03, accès aux données du monde et agents, sont hors moteur : A39).
- GAME-01 à GAME-07 : module de jeu en bibliothèque dynamique, registre de gestionnaires, migration d'état, paramètres JSON, version finale statique.

**Démo** : on modifie un fichier de logique pendant que le programme tourne, on compile, et le comportement change sans redémarrage ni perte d'état ; on modifie un fichier d'interface et l'écran se met à jour ; un petit programme de simulation (quelques centaines d'agents évoluant dans une scène de test) tourne sans fenêtre, rejouable avec la même graine.
**Terminé** : cas de rechargement testés (changement de corps de fonction, ajout de champ, changement de hiérarchie avec repli sur sauvegarde/restauration) ; boucle à pas fixe testée ; rejouabilité vérifiée.

## M9 · macOS et Metal

- Valider Vulkan via MoltenVK, régler les différences.
- Mesurer. **Décision documentée** : backend Metal natif oui ou non (RHI-13).
- Si oui : implémenter `src/rhi/metal/` en suivant `ADD_A_BACKEND.md`.

**Démo** : visualiseur sur un Mac.
**Terminé** : CI macOS verte ; limites connues dans `docs/GAPS.md`.

## M10 · Extensions et finition

- Vérifier que les points d'accroche tiennent : un plugin voxel minimal (EXT-01) s'ajoute **sans modifier** le renderer ni les backends ; l'interface d'accélération de rayons (EXT-03) est présente dans le RHI, désactivée dans OpenGL.
- Revue de documentation, schémas, guide de démarrage.
- Nettoyage : TODO restants, code mort, avertissements.
- Passe de performance : budgets de mémoire et de temps notés par système.
- Emballage : binaires des démos, README principal.

**Terminé** : passe de confirmation de `docs/04` (§« Confirmation de la couverture ») sans écart non justifié.

## M11 · Système de modules et d'addons (décidé par l'auteur le 2026-11-20, A34 ; registre déplacé en M12 le 2026-11-21, A38)

Objectif : garder une release cœur 100 % générique par défaut, tout en rendant trivial l'ajout de modules optionnels (génération procédurale, physique légère, audio, utilitaires métier, etc.), selon **deux modes** : inclus directement dans la release (`bundled`) ou distribués en **addon séparé**. M11 livre le **mécanisme** (manifeste, empaquetage, installation locale ou par URL, liaison au build). L'hébergement des addons et l'interface pour les parcourir sont un **produit à part**, en M12. *(Ce jalon peut être avancé si l'auteur souhaite réutiliser un module en addon plus tôt.)*

- **MOD-01 — Contrat et manifeste de module** : format déclaratif (`module.json`) décrivant un module optionnel : nom, version et plage ABI moteur compatible, en-têtes publics, archives statiques, shaders et ressources à copier, dépendances sur d'autres modules Orosmo et dépendances vcpkg éventuelles, points d'accroche (ex. plugin `RenderFeature`, types réfléchis). Le manifeste porte aussi les champs que le registre de M12 affichera (description, auteur, licence, vignette), pour ne pas le changer ensuite.
- **MOD-02 — Deux modes de distribution (`bundled` ou `addon`)** :
  - *Intégré (`bundled`)* : un module présent dans le dépôt peut être inclus dans la release via une simple déclaration de packaging, sans duplication manuelle dans `mk/engine.mk` ni `tools/engine-sdk/engine.mk`.
  - *Addon externe (`addon`)* : un module peut être empaqueté séparément (`orosmo-addon-<nom>-<plateforme>.zip`), depuis ce dépôt ou un dépôt externe, contre une release du moteur.
- **MOD-03 — Intégration CLI et build (`orosmo` + `engine.mk`)** : commandes `orosmo addon list|add|remove|update`, où `add` accepte un **fichier local, un dossier ou une URL** (le nom d'un registre vient en M12) ; installation dans `orosmo/addons/<nom>/` ; découverte et liaison automatiques par `engine.mk` (chemins `-I`, archives `.a`, copie des shaders/ressources, dépendances vcpkg propres au module) ; verrou des versions installées (`orosmo.lock`).
- **MOD-04 — Preuve de bout en bout (module pilote)** : empaqueter un module pilote via `MOD-01..03` et prouver en CI qu'un projet créé avec `orosmo init` reste 100 % générique par défaut, puis compile et s'exécute avec le module après `orosmo addon add`, sans modifier le cœur du moteur. Candidat naturel : le monde procédural retiré du cœur (branche `archive/procedural-world`), qui devient ainsi le premier addon.

- **MOD-09 — Modèles de démarrage multiples** (A40) : `orosmo init --template minimal|starter|demo|<nom>` ; l'utilisateur peut enregistrer un de ses projets comme modèle par défaut.

**Démo** : `orosmo init mygame` produit un projet générique ; `orosmo addon add ./mon-addon.zip` installe le module, `make debug` lie automatiquement ses archives, shaders et ressources sans édition manuelle du `Makefile`.
**Terminé** : manifeste spécifié et validé par tests, les deux modes (`bundled` et `addon`) vérifiés en CI de bout en bout, guide `docs/MODULES_AND_ADDONS.md` écrit, zéro régression sur la release cœur générique.

## M12 · Registre d'addons et gestionnaire (décidé par l'auteur le 2026-11-21, A38)

Objectif : un **service et une interface à part**, pas du moteur : héberger des addons, les publier, les trouver et les installer depuis le moteur en un geste. Dépend de M11 (manifeste et installation locale) ; ne modifie ni le cœur ni les backends.

- **MOD-05 — Registre d'addons** : hébergement des paquets et d'un **index** (liste des addons, versions, compatibilité moteur, empreinte SHA-256 de chaque archive, signature). Forme à choisir en M12 et à proposer dans `docs/QUESTIONS.md` avant de coder : un dépôt Git avec ses releases (le plus simple, aucune infrastructure à tenir) ou un petit service web. Le moteur ne fait confiance qu'à une archive dont l'empreinte et la signature correspondent à l'index.
- **MOD-06 — Publication** : `orosmo addon publish` (ou un workflow CI fourni) valide le manifeste, construit les archives par plateforme, calcule les empreintes et dépose le paquet dans le registre. Règles de nommage, de versions (SemVer) et de retrait d'une version.
- **MOD-07 — Installation depuis le registre** : `orosmo addon search|info|add <nom>[@version]` résout les versions compatibles avec le moteur du projet, installe les dépendances entre addons, vérifie les empreintes, et refuse proprement en cas d'incompatibilité (`orosmo.lock` mis à jour).
- **MOD-08 — Interface du gestionnaire** : une application (construite avec le moteur et `orosmo::ui`, donc aussi une vitrine) qui liste les addons du registre, les détails (description, versions, compatibilité, vignette, licence), l'état d'installation d'un projet, et permet d'installer, mettre à jour ou retirer. Elle appelle les mêmes fonctions que la CLI, sans logique propre. Registres multiples (un registre officiel et des registres ajoutés par l'utilisateur).

**Démo** : l'application du gestionnaire affiche le registre ; on installe l'addon pilote dans un projet `orosmo init`, le projet compile avec lui, puis on le retire.
**Terminé** : index et archives vérifiés (empreinte altérée → refus, testé), installation et retrait de bout en bout en CI contre un registre de test, publication d'au moins un addon réel (le pilote de M11), `docs/ADDON_REGISTRY.md` (format de l'index, publication, sécurité) écrit, zéro régression sur la release cœur.

## M13 · Modules d'effets préinstallés (décidé par l'auteur le 2026-11-21, A39)

Les effets de rendu retirés de M7 deviennent des **modules** au format de M11, **livrés avec le moteur et préinstallés** (mode `bundled`), activables ou non par le projet. Chacun s'ajoute comme plugin du renderer (`REN-01`), **sans modifier le cœur ni les backends** ; c'est aussi la preuve que le système de modules tient avec des modules « de la maison ». Dépend de M5 (graphe de passes, post-traitement, HDR) et de M11. Indépendant du registre de M12 : l'ordre des deux est libre.

- Bruit en compute (`REN-23`), brouillard (`REN-25`), particules (`REN-33`), occlusion ambiante (`REN-14`), eau (`REN-27`), réfraction et transmission (`REN-38`) ; atmosphère (`REN-24`) et nuages (`REN-26`) si l'auteur les veut aussi (à confirmer au début du jalon).
- Un module = un manifeste, un plugin du renderer, ses shaders, ses paramètres réfléchis (réglables par l'interface), ses tests d'image par backend et sa mesure de coût GPU.

**Démo** : la scène de la démo unique avec chaque effet activé puis désactivé depuis l'interface, sans recompiler le cœur.
**Terminé** : chaque module testé sur les deux backends (images dans la tolérance), coût GPU noté, un projet `orosmo init` peut retirer un module préinstallé sans erreur de compilation, README par module.

## M14 · Site vitrine et Wiki du moteur (décidé par l'auteur le 2026-11-21, A43)

*(Note : le numéro M7 reste volontairement non réattribué selon A39 pour préserver les références historiques ; ce chantier est donc numéroté **M14**.)*

Objectif : fournir un **site de présentation** du moteur Orosmo et un **wiki complet** (guide d'utilisation du SDK **et** explication de l'architecture interne du moteur), construits avec **Astro**, conteneurisés en Docker et publiés par CI.

- **Calendrier et indépendance** :
  - **Peut être commencé avant M6** : ne modifie pas le code C++ du moteur et peut avancer en parallèle.
  - **Publication initiale après M6** : mise en ligne publique une fois M5 (cœur du rendu) et M6 (import glTF / PBR / animation) livrés, puis **mise à jour à chaque jalon**.
  - **Spécifications détaillées** (hébergement, coûts, DevOps, staging) à trancher dans `docs/QUESTIONS.md` au **début du jalon**, avant l'implémentation.
- **WEB-01 — Wiki technique et guide utilisateur (Astro)** : documentation structurée avec Astro (ex. Astro / Starlight) couvrant (a) l'utilisation du moteur (installation, `orosmo init`/`doctor`, Make & CMake, `.cui`, thèmes, exemples) et (b) le fonctionnement interne (couches, RHI, renderer, UI, compilateur `.cui`, pipeline de shaders).
- **WEB-02 — Site vitrine du moteur** : page(s) de présentation d'Orosmo (vision, fonctionnalités clés, captures/démos, liens vers les releases GitHub et le wiki).
- **WEB-03 — Conteneurisation Docker et registre** : `Dockerfile` reproductible servant le site/wiki compilé ; workflow CI qui construit l'image Docker et la publie sur un registre (ex. GHCR ou autre registre choisi selon les coûts) afin de pouvoir la démarrer manuellement ou automatiquement sur un serveur.
- **WEB-04 — CI de publication sur tag `main` et maintenance par jalon** : déclenchement automatique de la construction et publication lors de la pose d'un tag sur `main` ; stratégie de déploiement serveur (manuel ou automatique) définie selon le budget et l'infrastructure retenus au début du jalon.
- **WEB-05 — Prévisualisation / staging sur PR *(optionnel, à trancher au début du jalon)*** : possibilité d'ajouter une CI qui construit et déploie le site/wiki en staging sur un sous-domaine de test à chaque PR pour vérification visuelle avant fusion (faisabilité, coût et spécifications à décider au démarrage de M14).

**Démo** : image Docker construite par la CI, lancée localement ou sur serveur, servant le site vitrine et le wiki Astro complets ; publication déclenchée par un tag sur `main`.
**Terminé** : site et wiki construits sans erreur en CI, image Docker publiée sur le registre, déploiement documenté (coûts et procédure dans `docs/`), processus de mise à jour par jalon en place.

## Fonctions non rattachées à un jalon

Les identifiants de priorité P2 ou P3 non cités ci-dessus (par exemple REN-09, REN-12, REN-15, REN-16, REN-20, REN-40, RHI-19, PLAT-03 ; TOOL-03 est passé en M0) sont traités après M10, par ordre d'intérêt décidé avec l'auteur. RHI-11 (minuteries GPU), RHI-17 (dispatch indirect), CORE-03, CORE-05, CORE-08, PLAT-05 et REN-36 (texte) sont à placer par l'IA dans le jalon où elles servent en premier, et à signaler dans `docs/STATUS.md`. Aucun identifiant ne doit rester sans jalon : l'audit produit la table de rattachement dans `docs/GAPS.md`.

## Règles de conduite pour tous les jalons

1. Un jalon à la fois. Pas de fonctionnalité d'un jalon futur « au passage ».
2. Chaque fonction du jalon est liée à un ID de la checklist ; `docs/STATUS.md` est mis à jour avec la preuve.
3. Si l'ordre ou la portée doivent changer, l'IA le propose dans `docs/QUESTIONS.md` et attend.
4. En fin de jalon : message de synthèse avec (a) la commande de démo, (b) le résultat des tests, (c) ce qui n'est pas fait et pourquoi, (d) le jalon suivant.
5. Git : une branche par fonctionnalité depuis `dev`, fusion dans `dev` quand c'est vert, jamais de commit sur `main`. En fin de jalon, l'IA prépare `docs/releases/<jalon>.md` et la description de la merge request, puis s'arrête : l'auteur teste sur `dev`, signale les bugs, et fusionne vers `main` quand le jalon lui convient (détail dans `docs/06`).
