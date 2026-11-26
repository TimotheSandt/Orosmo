# GAPS : liste de travail

Générée par l'audit du 2026-11-17 à partir de `docs/AUDIT_REPORT.md`, **mise à jour à la fin de M0** (TEST-01/02/03, BLD-02, RHI-14, TOOL-03 ; TOOL-03 est passé en P0 sur décision de l'auteur). Triée par priorité, puis `Incompatible` > `Partiel` > `Absent`. La colonne **Jalon** est le jalon de `docs/05` qui traite l'identifiant ; « (proposé) » = absent de `docs/05`, jalon proposé (voir `docs/QUESTIONS.md`). Aucun identifiant n'est sans jalon.


## P0

| ID | Fonctionnalité | État | Ce qui manque | Jalon |
| --- | --- | --- | --- | --- |
| RHI-01 | Interface RHI | Incompatible | Existe une abstraction (`IGraphicsDevice`, `IBufferResource`…), mais elle ne suit pas `docs/03` §3 : objets `unique_ptr` avec `Bind()/Unbind()`, uniformes par emplacement entier, `AttachToFramebuffer(std::uint32_t … | M2 |
| ASSET-02 | Textures | Partiel | PNG/JPG via stb, mips : oui. Pas d'espace colorimétrique (aucun format sRGB). | M6 |
| BLD-01 | Construction multiplateforme | Partiel | Makefile + vcpkg, types `debug/dev/release`, Linux vérifié (apt + vcpkg). Windows et macOS non vérifiés ici. Dépendances non épinglées (pas de `builtin-baseline`). Pas de CMake (proposition à faire, voir QUESTIONS). | M0 |
| BLD-02 | Intégration continue | Partiel | `.github/workflows/ci.yml` écrit (Linux : build, tests, contrôles, capture ; ASan/UBSan ; Windows/macOS en `continue-on-error`) ; **jamais exécuté sur GitHub**. Les contrôles de couches/API sont bloquants seulement contre une baseline (pas de migration encore). | M0 (observer la CI) |
| CORE-01 | Logger (pile d'appels, dédoublonnage, couleurs, `<stacktrace>`) | Partiel | Niveaux, fichier, mutex, dédoublonnage, couleurs, pile (`<stacktrace>`, DEBUG seulement) : présents. **Aucun test** (le mot `Logger` n'apparaît que dans tests/test_main.cpp). `logs` (vecteur statique) n'est jamais purgé hors … | M1 |
| CORE-07 | Temps | Partiel | Horloge et delta variable : oui. Pas de pas fixe, pas de test déterministe du temps. | M1 |
| CORE-09 | Mathématiques | Partiel | Vecteurs et matrices en `float` ; **aucune position monde en `double`**, pas de conversion explicite. Repère : main droite, Y haut (`glm::lookAt`), à confirmer. | M1 |
| DOC-01 | Documentation | Partiel | README par module (M00) et STATUS existent. Manquent : schéma des modules, `docs/ADD_A_BACKEND.md`, mise à jour par jalon. | M0 puis continu ; ADD_A_BACKEND en M4 |
| GIT-01 | Flux de branches | Partiel | Flux appliqué depuis M00 (branches `chore/`, `fix/`, `docs/`, fusions `--no-ff`). Protections côté hébergeur non activées par l'auteur ; avant M00, 92 commits sur 178 hors format `type(ID): résumé`. | Continu |
| PLAT-01 | Fenêtre | Partiel | Création, redimensionnement, plein écran, bordure, vsync (`parameters.vsync`, faux par défaut) : oui. **GLFW est exposé** (`#include <GLFW/glfw3.h>` dans Window.h, `GLFWwindow*` dans WindowContext.h, InputManager.h, Camera.h). … | M2 |
| PLAT-02 | InputManager | Partiel | Dispositions QWERTY/QWERTZ/AZERTY/DVORAK, remappage par actions, états `Release/PressBegin/Pressed/PressEnd` : présents. Détection de la disposition : Windows (`GetKeyboardLayout`, non vérifié ici) et, depuis `fix/keyboard-layout-detection`, Linux/macOS par les noms de touches de GLFW (`DetectKeyLayoutFromKeyNames`, testé ; vérifié sur AZERTY/Linux) ; autres dispositions (Colemak...) : QWERTY ; molette « … | M2 |
| R2D-01 | Batch de sprites | Partiel | Un sprite = un dessin ; pas de batch instancié, d'atlas, de tri par couche et matériau. | M5 |
| R2D-02 | Caméra 2D | Partiel | Passe en coordonnées pixel et filtrage « plus proche » disponible ; pas de classe de caméra 2D ni d'échelle entière. | M5 |
| REN-02 | Caméra | Fait (M3) | Reverse-Z, plan lointain infini, perspective et orthographique, rendu relatif à la caméra : voir STATUS « M3 ». | — |
| REN-03 | Données par image (groupe 0) | Partiel | Un UBO par caméra avec position et viewProj seulement ; pas de matrices inverses, image précédente, temps, résolution, graine, soleil, atmosphère. Pas de `static_assert` sur tailles/offsets. | M5 |
| REN-05 | Types de lumières | Partiel | Ambiante, directionnelle, ponctuelle, spot via SSBO : oui, sur l'abstraction actuelle (pas le RHI cible). Pas de `static_assert` de disposition (un commentaire documente un bug de décalage corrigé). `LightManager::SetLight` … | M2 (migration) ; M5 |
| REN-28 | Instancing | Partiel | Données par instance possibles dans un maillage ; un draw call par `Mesh`, pas de regroupement par (maillage, matériau). | M5 |
| REN-36 | Rendu du texte | Partiel | FreeType + atlas MSDF (msdfgen), ancrage, retour à la ligne, alignements, dépassement : oui. Passe par `Texture`/`Mesh` de l'abstraction actuelle, pas le RHI cible. Un octet à la fois (`unsigned char c`, `FT_Load_Char`, … | M2 |
| RHI-02 | Backend OpenGL 4.3+ | Partiel | Le rendu actuel **est** OpenGL 4.3 core, mais n'est pas derrière le RHI cible et **aucune image de référence** n'existe (RHI-14). Fuites GL hors backend : `src/Graphics/Core/RenderState.cpp` (11 occurrences `GL_*`), … | M2 |
| RHI-03 | Backend Vulkan | Fait (Linux, un GPU) | Traité en M4 : `src/rhi/vulkan/` (volk, VMA, Vulkan 1.3), validation à 0 (`validationErrorCount() == 0`, `tests/rhi/vulkan_device_tests.cpp`, `vulkan_backend_tests.cpp`, `rhi_device_tests.cpp` sur chaque backend), `make demo-vulkan-check` OK. Limites : G129 à G134. Windows et macOS non vérifiés (G142). | M4 |
| RHI-04 | Choix du backend à l'exécution | Partiel | `--backend` / `--api`, `--strict-backend`, repli sur OpenGL (`resolveBackend`, `tests/demo/launch_options_tests.cpp`), `Capabilities` : faits en M4. **Manque** : Metal (reconnu, refusé, M9). | M9 |
| RHI-05 | Buffers, textures (2D, tableau, cube, 3D, mips), échantillonneurs | Partiel | Buffers (vertex/index/uniform/storage/staging) et textures **2D** avec mips (OpenGL `glGenerateMipmap`) ; formats RGBA8, BGRA8, D24S8, D32F, R8, R32UI, RG16F, RGB8. Pas de tableau, cube, 3D ; l'échantillonneur n'est pas un … | M2 |
| RHI-06 | Pipelines graphiques et de calcul | Fait | Cache mémoire (`PipelineCache`, `tests/renderer/resources_tests.cpp`), cache disque Vulkan (`tests/rhi/vulkan_backend_tests.cpp`, écrit puis relu, fichier étranger ignoré), calcul (`rhi_device_tests.cpp`, « compute writes a storage buffer »). | M4 |
| RHI-08 | Passes de rendu et swapchain | Partiel | Swapchain Vulkan et cibles de rendu avec `Resize` existent. Pas de passes décrites par attachements load/store ; `vsync` faux par défaut. | M2 |
| RHI-10 | Dessin instancié et indirect | Partiel | Dessin instancié (attributs par instance) oui ; `drawIndirect` / multi-draw : grep `DrawIndirect\|MultiDraw` : 0. | M2 (instancié) ; M5 (indirect) |
| RHI-12 | Pipeline de shaders | Partiel | Source GLSL 430 compilée directement par OpenGL ; pour Vulkan : réécriture par chaînes (`#version 450`, `set = 0`) puis shaderc **à l'exécution**. Absents : compilation hors ligne, SPIRV-Cross (grep : 0), réflexion des … | M2 |
| RHI-15 | Rendu vers plusieurs cibles (MRT) | Partiel | Plusieurs cibles couleur (jusqu'à `maxColorAttachments`) et profondeur lisible : oui (vecteurs de mouvement RG16F ajoutés à la demande). Pas d'opérations load/store par cible ; limite 8 codée en dur côté Vulkan … | M2 |
| TER-01 | Bruit | Archivé (A37) | `orosmo::world` : Perlin, Simplex, Worley, fBm, distorsion ; GPU égal au CPU dans la tolérance : voir STATUS « M3 ». | — |
| TER-02 | Génération par tuiles | Archivé (A37) | Tuiles déterministes, génération asynchrone : voir STATUS « M3 ». | — |
| TEST-01 | Tests unitaires | Partiel | doctest en place (117 cas verts, clone propre) ; CI écrite mais jamais observée sur GitHub ; couverture de `core` partielle (Logger, InputManager, Light, TextRenderer, compilateur `.cui` : 0 test). | M0 (observer la CI) puis continu |
| TEST-03 | Tests de déterminisme | Partiel | Sommes de contrôle de référence (bruit, tuile, région, terrain-gen) vérifiées en CI Linux ; Windows et macOS jamais observés (G110). | M4 |
| TOOL-01 | Visualiseur de terrain | Archivé (A37) | `apps/terrain_viewer` livré (M3) ; caméra et export vérifiés à la main seulement (G109). | M4 |
| UI-01 | UIManager | Partiel | Composant, conteneur, VBox/HBox, `DeferredValue<T>` à double tampon, mixin CRTP : présents et testés. « Migré sur le RHI » : non (dépend de `Graphics` actuel, cycle `UI`↔`Graphics`). `Manager` est un singleton … | M2 |
| UI-05 | Langage d'interface : compilateur vers C++ | Fait (M3) | Cible `orosmo/ui` du compilateur `cui` : voir STATUS « M3 » et `docs/UI_LANGUAGE.md`. | — |
| UI-06 | Mise en page complète | Fait (M3) | `orosmo::ui` : VBox, HBox, Grid, Overlay, ScrollView, tailles fit/fixed/fill, marges, alignement. Preuve (fichier, test, résultat) : `docs/STATUS.md`, section « M3 ». | — |
| UI-07 | Thèmes en JSON | Partiel (M3) | Jetons, états, variantes, polices, images en neuf parties, rechargement, surcharge locale : voir STATUS « M3 ». **Reste** des tailles en dur dans les widgets (largeur minimale d'un champ = 10 caractères, pas de défilement…) : G111. | M5 |
| UI-11 | Spécification et tests du langage | Fait (M3) | `docs/UI_LANGUAGE.md`, cargo test, `make cui-test`. Preuve (fichier, test, résultat) : `docs/STATUS.md`, section « M3 ». | — |
| ASSET-01 | Import glTF 2.0 | Absent | Aucun import de modèle. | M6 |
| CORE-02 | Gestion d'erreurs | Absent | Aucune erreur attendue ne passe par `std::expected` ; plusieurs `throw` dans le flux normal (violation de la règle 10). Pas d'assertion avec message. | M1 |
| CORE-04 | Système de tâches | Absent | Pas de pool, de dépendances ni d'attente de groupe. | M1 |
| CORE-10 | Gestionnaire de ressources | Absent | Aucun handle avec génération, aucun chargement asynchrone ni comptage ni rechargement à chaud (grep `reload\|hot` : 0 hors commentaire). | M1 |
| PLAT-04 | Mode sans fenêtre | Absent | Aucun mode sans fenêtre. | M2 |
| REN-04 | Éclairage forward+ clusterisé | Absent | Éclairage forward simple, sans clusters ni budget. | M5 |
| REN-07 | Ombres en cascade | Absent | Absent. | M5 |
| REN-10 | Matériaux PBR | Absent | Pas de PBR (grep `metallic\|roughness\|albedo` : 0) ; textures sur `Mesh::AddTexture` mais pas de modèle de matériau. | M6 |
| REN-17 | HDR et tonemapping | Absent | Pas de cible HDR ni de tonemapping. | M5 |
| REN-30 | Culling CPU | Absent | Absent. | M5 |
| REN-34 | Dessin de debug | Fait (M3) | `DebugDraw`, `SceneRenderer::drawDebug`. Preuve (fichier, test, résultat) : `docs/STATUS.md`, section « M3 ». | — |
| REN-37 | Modes de fusion des matériaux | Absent | Pas de `BlendMode` par matériau ; seulement deux interrupteurs d'état global. | M5 |
| RHI-07 | Groupes de liaison | Absent | Pas de groupes 0/1/2 ; le Vulkan « patche » `layout(binding)` en `set = 0` (une seule table). | M2 |
| RHI-09 | Compute | Absent | Aucun appel de calcul, aucun tampon de stockage utilisé hors SSBO des lumières. | M2 |
| RHI-14 | Tests de rendu | Partiel | `tools/demo_parity.py` (`make demo-vulkan-check`, 15 captures, rapport et image de différence `--diff-out`) compare OpenGL et Vulkan. **Manque** : une référence par backend (une seule, OpenGL, sert aux deux), une seule scène, hors `demo-test` et CI (G143), un GPU (G138). | M5 |
| RHI-16 | Textures 3D et images de stockage | Absent | Pas de textures 3D, d'images de stockage ; RGBA16F/R32F absents des formats (seuls R32UI et RG16F). | M2 |
| TER-03 | Maillage à niveaux de détail | Archivé (A37) | Quadtree équilibré, bords cousus. Preuve (fichier, test, résultat) : `docs/STATUS.md`, section « M3 ». | — |
| TER-04 | Streaming | Archivé (A37) | Streaming asynchrone sous budget (M3) ; absence de saccade non mesurée (G108). | M4 |
| TER-07 | Texturage du terrain | Archivé (A37) | `TerrainRenderer` : pente/altitude, triplanaire, tableau de textures. Preuve (fichier, test, résultat) : `docs/STATUS.md`, section « M3 ». | — |
| TER-11 | Export | Archivé (A37) | PNG 16 bits, EXR, OBJ, glTF, STL fermé, biomes, relus par les tests. Preuve (fichier, test, résultat) : `docs/STATUS.md`, section « M3 ». | — |
| TER-12 | Interface de réglage | Archivé (A37) | Panneau du visualiseur en langage d'interface, préréglages enregistrables, réflexion. Preuve (fichier, test, résultat) : `docs/STATUS.md`, section « M3 ». | — |
| TOOL-02 | Générateur en ligne de commande | Archivé (A37) | `apps/terrain_gen`, `make terrain-gen-check`. Preuve (fichier, test, résultat) : `docs/STATUS.md`, section « M3 ». | — |
| UI-02 | Rendu indépendant du backend | Absent | Les composants dessinent directement via sprites ; aucune liste de dessin intermédiaire, pas d'adaptateur vers un batch 2D. | M2 |
| UI-03 | Widgets de base | Fait (M3) | `orosmo::ui` : tous les widgets de la liste, infobulles. Preuve (fichier, test, résultat) : `docs/STATUS.md`, section « M3 ». | — |
| TOOL-03 | Capture d'écran | Partiel | Fait en M0 pour OpenGL (`--capture`, `Screenshot::SavePng`, `tools/compare_png.py`) ; Vulkan refusé jusqu'en M4 ; `ui.png` non reproductible (FPS affichés) ; pas de capture sans fenêtre (PLAT-04). | M0 (fait) ; M2 (usage RHI-14) |

## P1

| ID | Fonctionnalité | État | Ce qui manque | Jalon |
| --- | --- | --- | --- | --- |
| RHI-18 | Capacités optionnelles réservées | Incompatible | Champs `supportsRayQueries`, `supportsAccelerationStructures`, `supportsRayTracingPipelines`, `supportsTemporalUpscaling`, `supportsFrameGeneration` **mis à `true` sans condition** côté Vulkan, avec une ressource sans … | M2 |
| CORE-05 | Profilage | Partiel | Minuteries CPU nommées et moyennées : oui. Minuteries GPU : réelles en OpenGL (`glQueryCounter`), **factices en Vulkan** (`VulkanTimestampQueryResource` mesure l'horloge CPU `steady_clock`). Pas d'export (grep … | M1 (CPU, export) ; M2/M4 (GPU) ; (proposé : placement de CORE-05) |
| CORE-06 | Configuration | Partiel | Seul `--api`/`--choose-api`/`--list-apis`/`--help` existe. Pas de lecture/écriture JSON (grep `json` : seulement Benchmark), pas de valeurs par défaut documentées. | M1 |
| CORE-11 | Système de fichiers virtuel | Partiel | Chemins relatifs `res/...` par macro ; pas de schémas logiques `res://` / `user://`. Les chemins utilisateur sont déjà par OS (APPDATA, XDG_DATA_HOME, HOME). | M1 |
| R2D-03 | Formes, lignes, texte | Partiel | Texte et rectangles (sprites) ; pas de cercles, lignes, polygones. | M5 |
| R2D-05 | Mélange 2D/3D | Partiel | Seul le mode « superposition » existe (UI après le rendu 3D) ; ni 2D monde avec profondeur, ni 2D vers texture. | M5 |
| REN-19 | Anticrénélage | Partiel | Infrastructure temporelle (jitter, historique, vecteurs de mouvement RG16F, mode d'upscaling bilinéaire `BilinearBlitUpscaleMode`) ; aucun SMAA, TAA, MSAA ni résolution dynamique par effet. | M5 |
| REN-21 | Sorties auxiliaires (AOV) | Partiel | Vecteurs de mouvement et profondeur lisible **activés à la demande** ; identifiants R32UI lisibles. Pas de normales octaédriques, rugosité/métal, profondeur linéaire, émission ; pas de règle MSAA. | M5 |
| REN-35 | Modes d'affichage de debug | Partiel | Un seul mode (filaire) sur huit. | M5 |
| RHI-11 | Minuteries GPU et noms de debug | Partiel | Les `debugName` sont conservés mais **jamais transmis** à l'API (grep `glObjectLabel\|glPushDebugGroup\|SetDebugUtilsObjectName` : 0). Minuterie Vulkan = horloge CPU (stub qui prétend marcher, règle 3). | M2 (OpenGL) ; M4 (Vulkan) |
| TEST-02 | Assainisseurs et analyse | Partiel | `-Wpedantic -Werror` : 0 avertissement ; ASan + UBSan : tests verts (voir `docs/releases/M0.md`) ; manquent clang-tidy et clang-format (outils absents ici), `detect_leaks` désactivé, et exécution en CI. | M0 (observer la CI) |
| ANIM-01 | Squelettes et skinning GPU | Absent | Absent. | M6 |
| ANIM-02 | Clips, mélange, machine à états | Absent | Absent. | M6 |
| ANIM-03 | Animation de propriétés | Absent | Absent. | M6 |
| ANIM-04 | Foules instanciées | Absent | Absent. | M6 |
| ASSET-03 | Textures compressées | Absent | Absent. | M6 |
| ASSET-04 | Outil de cuisson | Absent | Absent. | M6 |
| ASSET-05 | Rechargement à chaud des assets | Absent | Absent. | M6 |
| CORE-03 | Allocateurs | Absent | Aucun allocateur. | M5 (proposé : première utilisation, données par image) |
| CORE-08 | Événements | Absent | Pas de bus d'événements typé. | M3 (proposé : premier usage, événements de l'interface) |
| CORE-12 | Sérialisation et réflexion | Absent | Ni sérialisation, ni réflexion, ni versionnage. | M1 |
| CORE-13 | Observateur de fichiers | Absent | Aucun observateur de fichiers. | M1 |
| ECS-01 | Entités et composants | Absent | Absent ; choix ECS maison/EnTT à trancher. | M8 |
| ECS-02 | Transformations hiérarchiques | Absent | Absent (transformations dans `Mesh`). | M8 |
| ECS-03 | Chargement et sauvegarde de scène | Absent | Absent. | M8 |
| GAME-01 | Module de jeu rechargeable | Absent | Absent. | M8 |
| GAME-02 | État côté moteur | Absent | Absent. | M8 |
| GAME-03 | Migration d'état | Absent | Absent. | M8 |
| GAME-04 | Registre des gestionnaires | Absent | Absent. | M8 |
| GAME-05 | Paramètres de gameplay en JSON | Absent | Absent. | M8 |
| GAME-06 | Construction incrémentale | Absent | Absent. | M8 |
| GAME-07 | Version finale | Absent | Absent. | M8 |
| PLAT-05 | Saisie de texte | Partiel | Texte Unicode en UTF-8 (`InputManager::GetTextInput`, M3) ; presse-papiers du système (`Window::GetClipboardText` / `SetClipboardText`) ; appuis avec répétition et raccourcis (`InputManager::GetKeyPresses` / `GetShortcuts`) ; pas de composition IME affichée (G107). | M5 |
| R2D-04 | Cartes de tuiles | Absent | Absent. | M5 (proposé : non cité dans docs/05) |
| R2D-06 | Plusieurs vues | Absent | Absent (un `Renderer3D` et un `Renderer2D` par fenêtre). | M5 |
| REN-01 | Graphe de passes et plugins de rendu | Absent | Pas de graphe de passes, de ressources transitoires ni de plugins : le pipeline est une classe `Renderer` avec `Renderer2D`/`Renderer3D` dérivés. Points d'extension existants : passes de post-traitement et modes d'upscaling … | M5 |
| REN-06 | Mobilité et mode d'ombre des lumières | Absent | Absent. | M5 |
| REN-08 | Atlas d'ombres et cache | Absent | Absent. | M5 |
| REN-11 | Variantes et instances de matériaux | Absent | Absent. | M6 |
| REN-13 | IBL et sondes de lumière | Absent | Absent. | M6 |
| REN-14 | Occlusion ambiante | Absent | Absent. | M13 |
| REN-18 | Post-traitement courant | Absent | Interface de passe de post-traitement, aucun effet fourni. | M5 |
| REN-22 | Boîte à outils de shaders | Absent | Pas de boîte à outils GLSL partagée. | M5 |
| REN-23 | Textures de bruit en compute | Absent | Absent (pas de compute). | M13 |
| REN-24 | Ciel et atmosphère | Absent | Absent. | M13 |
| REN-25 | Brouillard | Absent | Absent. | M13 |
| REN-29 | Rendu piloté par GPU | Absent | Absent. | M5 |
| REN-31 | LOD et imposteurs | Partiel | LOD par distance et fondu tramé faits ; imposteurs automatiques absents (G167). | M6 |
| REN-32 | Tri et ordre des transparents | Absent | Absent. | M5 |
| REN-33 | Particules et décalques | Absent | Absent. | M13 |
| REN-39 | Ombres des transparents | Absent | Absent. | M5 |
| REN-41 | Transparents et sorties auxiliaires | Absent | Absent. | M5 |
| RHI-17 | Dispatch indirect et groupes dynamiques | Absent | Absent. | M5 (proposé : rendu piloté par GPU) |
| SIM-01 | Boucle de simulation à pas fixe | Absent | Absent. | M8 |
| TER-05 | Érosion | Absent | Absent (seul `GenerateCrater`). | Hors moteur (A39) |
| TER-06 | Biomes | Absent | Absent. | Hors moteur (A39) |
| TER-08 | Végétation et dispersion | Absent | Absent. | Hors moteur (A39) |
| TER-09 | Planètes sphériques | Absent | Absent. | Hors moteur (A39) |
| TER-13 | Lancer de rayon sur le terrain | Archivé (A37) | `raycastTerrain`, cohérent avec la profondeur rendue. Preuve (fichier, test, résultat) : `docs/STATUS.md`, section « M3 ». | — |
| UI-04 | Outils de debug | Absent | Absent. | M8 |
| UI-08 | Rechargement à chaud de l'interface | Absent | Absent. | M8 |
| UI-09 | Navigation et accessibilité de base | Absent | Absent. | M8 |
| UI-10 | Inspecteur d'interface | Absent | Absent. | M8 |
| UI-12 | Paramètres réfléchis → curseurs | Fait (M3) | `reflectedPanel`, `ParamPanel` du langage. Preuve (fichier, test, résultat) : `docs/STATUS.md`, section « M3 ». | — |

## P2

| ID | Fonctionnalité | État | Ce qui manque | Jalon |
| --- | --- | --- | --- | --- |
| EXT-03 | Interface d'accélération de rayons | Incompatible | Une façade existe, **sans construction BLAS/TLAS ni requête de rayon**, et Vulkan annonce `rayQuery = true` sans implémentation (voir RHI-18) : stub qui prétend marcher. | M2 (corriger les capacités) ; M10 |
| ANIM-05 | IK, morph targets, root motion | Absent | Absent. | Après M10 (P2) |
| ASSET-06 | OBJ | Absent | Absent. | Après M10 (P2) |
| EXT-01 | Plugin de rendu voxel | Absent | Absent (pas de graphe de passes, voir REN-01). | M10 |
| EXT-02 | Tracé de rayons logiciel sur voxels | Absent | Absent. | Après M10 (P2) |
| PLAT-03 | Manettes | Absent | Aucune manette. | Après M10 (P2) ; UI-09 la demande en M8 |
| REN-09 | Ombres avancées | Absent | Absent. | Après M10 (P2) |
| REN-12 | Extensions de matériaux | Absent | Absent. | Après M10 (P2) |
| REN-15 | Réflexions et GI | Absent | Absent. | Après M10 (P2) |
| REN-16 | Éclairage précalculé | Absent | Absent (le mode `Baked` n'existe pas). | Après M10 (P2) |
| REN-20 | Effets coûteux optionnels | Absent | Absent. | Après M10 (P2) |
| REN-26 | Nuages volumétriques | Absent | Absent. | M13 |
| REN-27 | Eau | Absent | Absent. | M13 |
| REN-38 | Réfraction et transmission | Absent | Absent. | M13 |
| WEB-01 à WEB-05 | Site vitrine, wiki, Docker et CI/CD | Absent | WEB-01 Fait ; WEB-02, WEB-03, WEB-04 Partiel (site et image prêts, rien de publié : G203, G204, G206) ; WEB-05 Absent, optionnel (A74, G205). | M14 (publication après M6) |
| REN-40 | Transparence indépendante de l'ordre (OIT) | Absent | Absent. | Après M10 (P2) |
| RHI-13 | Backend Metal | Absent | Stub honnête : `IsAvailable()` renvoie faux, message clair. | M9 |
| RHI-19 | Transferts asynchrones | Absent | Absent. | Après M10 (P2) |
| SIM-02 | Accès aux données du monde | Absent | Hors moteur (A39). | — |
| TER-10 | Eau, rivières, lacs | Absent | Absent. | Hors moteur (A39) |

## P3

| ID | Fonctionnalité | État | Ce qui manque | Jalon |
| --- | --- | --- | --- | --- |
| ANIM-06 | Retargeting | Absent | Absent. | Après M10 (P3) |
| EXT-04 | Ray tracing matériel | Absent | Absent (P3). | Après M10 (P3) |
| EXT-05 | Traceur de chemin de référence | Absent | Absent (P3). | Après M10 (P3) |
| R2D-07 | Éclairage 2D | Absent | Absent (P3). | Après M10 (P3) |
| SIM-03 | Agents et écosystème | Absent | Hors moteur (P3). | Après M10 (P3) |
| TER-14 | Grottes et voxels | Absent | Absent (P3). | Après M10 (P3) |

## Hors checklist : limites et dettes

Issues de M00 (G1 à G15, conservées) et de l'audit.

| # | Sujet | Détail | Traité en |
| --- | --- | --- | --- |
| G1 | Windows non vérifié | Chemins du Makefile, de la CI et de l'installeur NSIS adaptés à `apps/demo/`, `res/`, `shaders/` sans test sous Windows (branches `powershell`/`mklink`). | M0 |
| G2 | macOS non vérifié | Aucun test sous macOS. | M0 / M9 |
| G3 | Installeur NSIS | `installer.nsi` lit `res\` et `shaders\` à la racine ; non testé. | M0 |
| G4 | Installeur .deb | `create_linux_installer` copie `SHADER_DIR` ; non exécuté. | M0 |
| G5 | Cycles entre modules | `Graphics` ↔ `UI` ↔ `Profiler` (règle 5). | M1 / M2 (proposé) |
| G6 | `UI.h`, `PresentationController.h` | Aucun fichier ne les inclut ; conservés ; usage à trancher à l'implémentation de l'UI. | M3 (UI) |
| G7 | Module `Benchmark` | Repris de `dev` sans test ni validation. | À décider |
| G8 | Dépendances non épinglées | `vcpkg.json` sans `builtin-baseline` ; apt selon distribution. | M0 |
| G9 | Tests | Framework maison, pas de CI de tests, pas d'ASan/UBSan, release seulement. | M0 |
| G10 | Licence | Aucun `LICENSE` (Apache-2.0 probable) ; licence de Roboto à documenter. | Décision de l'auteur |
| G11 | Référence visuelle | Pas de capture de référence ; parité Vulkan/OpenGL non mesurée. | M0 / M2 |
| G12 | Casse `utilities.h` / `Utilities.cpp` | **Traité en M1 bis** : `include/Utilities/Utilities.h` (`git mv` + `#include` mis à jour ; 224 tests verts). | — |
| G13 | `Graphics/Backend/` (singulier) vs `Backends/` | **Traité en M1 bis** : `GraphicsAPI.h` déplacé dans `include/Graphics/Backends/`, la règle de couche `Backend/*` est supprimée. | — |
| G14 | Espaces en fin de ligne | 3 fichiers. | M0 (formatage) |
| G15 | Installeurs dans `apps/demo/installers/` | Le plan prévoyait `tools/installers/`. | M0 |
| G16 | Capacités Vulkan annoncées à tort | RHI-18. | M2 |
| G17 | Minuterie GPU Vulkan factice | RHI-11. | M4 |
| G18 | Graine par horloge | `Noise::SetSeed()` sans argument, `PCGRandom::init()`. | M1 |
| G19 | Exceptions dans le flux normal | CORE-02. | M1 |
| G20 | Journal non borné | `Logger::logs`. | M1 |
| G21 | Compilateur `.cui` : en-tête généré mal nommé si vue ≠ fichier | `docs/UI_LANGUAGE_AUDIT.md`. | M3 |
| G22 | Avertissements `-Wmissing-field-initializers` | **Traité (nettoyage Orosmo, 2026-11-20)** : code hérité supprimé (`include/UI/`, `src/ui/legacy/`, démo sur `orosmo::ui`). Avant : `BindFunc.h:137`, `BindMethods.h:319`. | — |
| G23 | Dossier `Libraries/` dans le nom des objets | `obj/<type>/Libraries/` dans `mk/engine.mk` (cosmétique). | M0 |
| G24 | 17 stashes et anciennes branches | Stashes analysés le 2026-11-17 : 2 utiles archivés dans `docs/notes/archive/` (LOD du terrain → TER-03, `ClearAttachments` Vulkan → G41). **Supprimés le 2026-11-21 sur accord de l'auteur**, après sauvegarde complète hors dépôt dans `../ProceduralGeneration-backups/stashes/` (17 patchs `stash-<n>.patch`, `index.txt`, et `stashes.bundle`, un paquet git vérifié ; restauration : `git fetch stashes.bundle 'refs/stashbak/*:refs/stashbak/*'`, puis `git stash apply refs/stashbak/<n>`). Branches anciennes (`UI`, `feat/*`, `feature/*`, `fix/makefile-lint-check-format`) : **gardées**, l'auteur ne veut pas de suppression de branches. | Fait (stashes) ; branches gardées
| G25 | Branche ambiguë `main` locale en avance de 44 commits sur `origin/main` | Rien n'est poussé. | Auteur |
| G26 | `TODO` dans le code | **Molette traitée (lot C)** ; reste `taskbarVisible` (`window.hpp`), non utilisé. | M2 (platform) |
| G27 | `TextRenderer` sans UTF-8 | Voir REN-36. | M2 / M3 |


## Ajouts de M0 (G28 à G37)

| # | Sujet | Détail | Traité en |
| --- | --- | --- | --- |
| G28 | CI jamais exécutée | `.github/workflows/ci.yml` : Windows et macOS en `continue-on-error` ; `WERROR=0` hors Linux ; aucun run observé. | M0 (retour de l'auteur) |
| G29 | Capture Vulkan | `--capture --api vulkan` refusé : 33 erreurs de validation (layout d'image `SHADER_READ_ONLY` attendu, `GENERAL` trouvé) et image différente d'OpenGL pour la même graine (fond blanc). | M4 |
| G30 | Capture non reproductible entre machines | La référence (`docs/BASELINE/reference/`) vient d'Intel Arc/Mesa 26.1.6 ; llvmpipe (CI) peut différer de quelques pixels : comparer avec tolérance. | M2 |
| G31 | `ui.png` | Affiche les FPS : non reproductible ; seul `scene.png` sert de référence. | M3 (UI) |
| G32 | Baselines de couches/API | `tools/baseline/*.txt` figent 5 violations de couches et 13 fichiers en infraction d'isolation ; toute nouvelle violation échoue en CI. À vider au fil de la migration. | M1-M4 |
| G33 | En-tête non autonome | `Graphics/Upscaling/IFrameGenerationMode.h` n'inclut pas `GraphicsCapabilities`. | M2 |
| G34 | Faux positifs GCC 13 | `-Warray-bounds` (vecteur à 1 élément, contourné par `assign`) et `-Wmaybe-uninitialized` dans `std::regex` (pragma local, ancien backend Vulkan) sous ASan -O3. | M4 (Vulkan réécrit) |
| G35 | Fuites ASan | `detect_leaks=0` : les singletons statiques et le contexte GL font apparaître des fuites à l'arrêt ; non triées. | M1 |
| G36 | Graine du terrain | **Traité en M1 (A12)** : `Noise()` / `PCGRandom()` supprimés (`static_assert`), `TerrainGenerator()` a la graine fixe 1, la démo tire une graine aléatoire explicite dans `main()` et la journalise. | M1 (fait) |
| G37 | Disque | `/` à 97 % pendant M0 (voir `docs/QUESTIONS.md` A19). | Auteur |

## Ajouts de M1 (G50 à G58)

| # | Sujet | Détail | Traité en |
| --- | --- | --- | --- |
| G50 | `Logger::Initialize` lance une exception | Un dossier de journal impossible à créer (`/proc/x/y.log`) lève `std::filesystem::filesystem_error` (`create_directories` sans `error_code`) ; trouvé en écrivant les tests du logger (CORE-01). Comportement conservé, test retiré. | M1 bis |
| G51 | Exceptions restantes dans le flux normal | CORE-02 est fourni pour le code neuf ; `UI/Core/Bounds.cpp`, `Graphics/Light.cpp`, `Graphics/ShaderProgram.cpp`, `Game.cpp` lancent encore (audit). | M2 (chaque module à sa migration) |
| G52 | `ParseGraphicsLaunchOptions` | Analyse de la ligne de commande maison (`--api`, `--seed`, `--capture`...) ; `Config` (CORE-06) n'est pas encore branché sur la démo. | M2 |
| G53 | Macros de chemins | `GET_RESOURCE_PATH`, `GetUserDataPath` : la démo n'utilise pas encore `Vfs` (`res://`, `user://`). | M2 |
| G54 | GPU dans `TraceRecorder` | `addSample(..., TraceTrack::kGpu)` existe ; le branchement sur les minuteries du RHI (`GPUTimestampQuery`) se fait avec le RHI. Adaptateur Tracy non fait (export Chrome trace/Perfetto suffit). | M2 |
| G55 | `FileWatcher` par interrogation | Portable et prouvé sur Linux ; Windows/macOS attendent la CI du push. Remplaçable par inotify/ReadDirectoryChangesW/FSEvents derrière la même interface si le coût de `poll()` gêne (gros dossiers). | M3 (shaders, thèmes) |
| G56 | `TaskSystem` : file unique, pas de suspension de tâche | Une file protégée par un verrou : suffisant (10 000 tâches en ~20 ms), vol de travail à ajouter si les mesures le demandent (génération de terrain par blocs). **Limite connue** (revue CodeRabbit PR #5) : `wait()` exécute d'abord la tâche attendue si elle est prête (cas « A soumet C puis attend C » corrigé), mais une tâche prête exécutée « par aide » est empilée sur l'appelant ; si elle attend elle-même cet appelant et que la tâche attendue n'est pas prête, le pool peut se bloquer. Une vraie solution suspend les tâches (coroutines ou fibres) : à décider avec le moteur de tâches du terrain. | M3 |
| G57 | Allocateurs non utilisés | `ArenaAllocator` / `ObjectPool` sont testés mais pas encore employés par le moteur (première utilisation proposée en M5). | M5 |
| G58 | Couverture de `core` | Pas de mesure de couverture (`gcov`) : TEST-01 reste `Partiel` ; 83 cas écrits pour `core` + 6 pour le logger. | M1 bis |
| G59 | `core` appelle le système directement | **Traité en M1 bis pour `<windows.h>` et l'environnement** : `Logger`, `Utilities`, `FPSCounter` et `Vfs` passent par `include/orosmo/core/system.hpp`, défini dans `src/platform/system.cpp` (`getenv` et `<windows.h>` ne sont plus dans `core`). **Reste** : `std::filesystem` (bibliothèque standard, portable) dans `Vfs`, `FileWatcher`, `Config` et `TraceRecorder` ; décision à valider, voir `QUESTIONS.md` A24. | M2 (si A24 demande de l'envelopper) |
| G60 | Remarques CodeRabbit de PR #5 non suivies | **`SteadyClock` dans `time_tests`** : un seul cas vérifie que l'horloge réelle est monotone (preuve de CORE-03) ; les autres cas utilisent `ManualClock`. **Test multithread de `trace`** : il utilise l'horloge réelle exprès (concurrence). **`EventBus::publish` et `typeid`** : RTTI conservé, un identifiant de type maison ne se justifie pas avant une mesure. **Démarrage partiel du pool de threads** : la correction (arrêt et jonction des workers lancés) n'est pas testable sans échec de création de thread. **Nettoyage des objets release avant ASan/UBSan** : déjà documenté (`make fclean` avant `SANITIZE=`), pas d'automatisme ajouté. | Aucun |


## Revue CodeRabbit de la PR #4 (2026-11-17) : constats reportés

La PR `dev` → `main` contient tout l'historique de `dev` : plusieurs remarques portent sur du code antérieur à M00. Corrigés dans M0 : voir `docs/releases/M0.md`. Reportés :

| # | Constat | Fichier | Jalon |
| --- | --- | --- | --- |
| G38 | `CaptureFramebufferState` / `RestoreFramebufferState` ne copient pas `scissorBox` : après une restauration, le test de ciseau s'applique avec un rectangle nul et tout est rogné (OpenGL et Vulkan) | `src/Graphics/Core/RenderState.cpp:223,255` | M2 (l'état global disparaît avec le RHI) ; bug réel, à corriger avant si l'on s'en sert |
| G39 | Chaque programme compilé doit avoir une identité de cache distincte | `src/Graphics/ShaderProgram.cpp:139` | M2 (RHI-06, RHI-12) |
| G40 | Les cas d'énumération (`.topLeft`) doivent être résolus selon le type du paramètre du modificateur | `tools/cui-rs/src/generator.rs:902` | M3 (UI-05) |
| G41 | `Renderer::Clear` est un no-op sous Vulkan : contenu périmé | `src/Graphics/Backends/Vulkan/VulkanRenderState.cpp:136` | M4 |
| G42 | Appliquer la limitation de fréquence (`throttle`) avant d'évaluer le contenu du texte | **Traité (nettoyage Orosmo, 2026-11-20)** : code hérité supprimé (`include/UI/`, `src/ui/legacy/`, démo sur `orosmo::ui`). Avant : `src/ui/legacy/Widgets/Text.cpp:191` | — |
| G43 | Appliquer les réglages du renderer avant `BeginPass()` | **Traité (nettoyage Orosmo, 2026-11-20)** : code hérité supprimé (`include/UI/`, `src/ui/legacy/`, démo sur `orosmo::ui`). Avant : `src/ui/legacy/Manager.cpp:90` | — |
| G44 | `Buffer::Initialize` : signaler l'échec et garder `desc` cohérent avec l'absence de ressource | `src/Graphics/Buffer.cpp:36-58` | M2 |
| G45 | Fichier temporaire de `cui check` non unique (`cui_check_${Date.now()}.cui`) : collision à l'ouverture de plusieurs documents | `tools/vscode-cui/main.js:110-120` | M3 (outillage UI) |
| G46 | `LogErrors(std::string context)` copie `context` : `performance-unnecessary-value-param` (clang-tidy) | `src/Graphics/Backends/OpenGL/OpenGLDiagnostics.*`, `Vulkan/VulkanDiagnostics.*` | M2 |
| G47 | Code ajouté récemment (diagnostics, wrappers de backend, `NormalizeOrFallback` dans `Camera.cpp`) en `PascalCase` et sous `include/Graphics/` : `docs/03` demande `camelCase`, `kPascalCase`, `include/orosmo/` | plusieurs | M2 (migration, décision A2/A17) |
| G48 | TEST-01 : le critère couvre `core` et `world`, que le plan traite en M1 et M3 : M0 ne peut pas le clore | `docs/02` | voir `docs/QUESTIONS.md` A20 |
| G49 | UI-02 exige un adaptateur vers le batch R2D-01, planifié en M5, alors qu'UI-02 est en M2 | `docs/02`, `docs/05` | voir `docs/QUESTIONS.md` A21 |

## Table de rattachement : jalon → identifiants

| Jalon | Identifiants |
| --- | --- |
| M00 | REPO-01 |
| M0 | BLD-01, BLD-02, DOC-01, TEST-01, TEST-02, TOOL-03 |
| M1 | CORE-01, CORE-02, CORE-04, CORE-05, CORE-06, CORE-07, CORE-09, CORE-10, CORE-11, CORE-12, CORE-13 |
| M2 | EXT-03, PLAT-01, PLAT-02, PLAT-04, REN-05, REN-36, RHI-01, RHI-02, RHI-05, RHI-06, RHI-07, RHI-08, RHI-09, RHI-10, RHI-11, RHI-12, RHI-14, RHI-15, RHI-16, RHI-18, UI-01, UI-02 |
| M3 | BLD-01, BLD-02, DOC-01 (première release utilisable : guide, test `orosmo init` en CI, API figée), CORE-08, PLAT-05, REN-02, REN-34, TER-01, TER-02, TER-03, TER-04, TER-07, TER-11, TER-12, TER-13, TEST-03, TOOL-01, TOOL-02, UI-03, UI-05, UI-06, UI-07, UI-11, UI-12 |
| M4 | RHI-03, RHI-04 (Metal : M9) |
| M5 | CORE-03, R2D-01, R2D-02, R2D-03, R2D-04, R2D-05, R2D-06, REN-01, REN-03, REN-04, REN-06, REN-07, REN-08, REN-17, REN-18, REN-19, REN-21, REN-22, REN-28, REN-29, REN-30, REN-31, REN-32, REN-35, REN-37, REN-39, REN-41, RHI-17 |
| M6 | ANIM-01, ANIM-02, ANIM-03, ANIM-04, ASSET-01, ASSET-02, ASSET-03, ASSET-04, ASSET-05, REN-10, REN-11, REN-13 |
| M7 | *supprimé (A39)* : REN-14, REN-23 à REN-27, REN-33, REN-38 vont en M13 ; TER-05, TER-06, TER-08, TER-09, TER-10, SIM-02, SIM-03 hors moteur |
| M11 | MOD-01 à MOD-04, MOD-09 |
| M12 | MOD-05 à MOD-08 |
| M13 | REN-23, REN-24, REN-25, REN-26, REN-27, REN-14, REN-33, REN-38 |
| M8 | ECS-01, ECS-02, ECS-03, GAME-01, GAME-02, GAME-03, GAME-04, GAME-05, GAME-06, GAME-07, SIM-01, UI-04, UI-08, UI-09, UI-10 (SIM-02 hors moteur, A39) |
| M9 | RHI-13 |
| M10 | EXT-01 |
| Après M10 | ANIM-05, ANIM-06, ASSET-06, EXT-02, EXT-04, EXT-05, PLAT-03, R2D-07, REN-09, REN-12, REN-15, REN-16, REN-20, REN-40, RHI-19, TER-14 |
| Continu | GIT-01 |

## Ajouts de M1 bis (G61 à G63)

| ID | Sujet | Détail | Jalon |
| --- | --- | --- | --- |
| G61 | Code Windows/macOS de `system.cpp` non compilé | `terminalWidth`, `executablePath`, `userDataDirectory` (`SHGetFolderPathA`), `preciseSleep` (Sleep + QueryPerformanceCounter) et `begin/endHighResolutionTimer` (`timeBeginPeriod`) ont été **déplacés tels quels** depuis `Logger`, `Utilities` et `FPSCounter`, mais seule la branche Linux a été compilée et testée ici. `tests/platform/system_tests.cpp` s'exécutera sur Windows et macOS à la CI. Windows lit désormais le dossier de données par `SHGetFolderPathA` (comme `GetUserDataPath`) au lieu de `%APPDATA%` dans `Vfs` : même dossier en pratique. | CI du push |
| G62 | `XDG_DATA_HOME` vide | Avant : traité comme un dossier valide (`GetUserDataPath` créait `ProceduralGeneration/` dans le dossier courant). Maintenant : comme non défini (spécification XDG), on retombe sur `~/.local/share`. Seul changement de comportement de M1 bis, voulu. | — |
| G63 | Isolation des API : 11 fichiers restent en infraction | **Traité (lot C, voir G88)** : plus aucune infraction. Historique : `GLFW` dans `Window`, `InputManager`, `Camera`, `RenderState`, `WindowContext` et leurs en-têtes ; `<windows.h>` dans `Window.cpp` (barre des tâches) et `InputManager.cpp`. Ces fichiers sont dans la couche `platform` ou `rhi` mais pas encore dans `src/platform/` ni `src/rhi/` : la migration se fait en M2 (baseline `tools/baseline/api_isolation.txt`, 13 → 11). Revue CodeRabbit PR #6 : `IWindowGraphicsBinding` expose encore `GLFWwindow *` (`docs/03` §2 : aucun type d'API graphique dans l'API publique) ; un handle opaque défini par `platform` se fait avec la migration de `Window` et `InputManager`. | M2 |
| G64 | Workflow de release | Corrigé : le job Linux n'installait ni les paquets apt ni `tools/setup-linux-deps.sh` (`stb_image.h` introuvable, reproduit depuis un clone propre) ; Windows et macOS compilent avec `WERROR=0` comme la CI ; un tag hors de `main` est refusé ; runner `ubuntu-24.04`. **Prouvé** : la séquence Linux depuis un clone propre donne `dist/orosmo-linux-x64` et `dist/orosmo-pkg-linux-x64.zip`. **Non prouvé** : Windows, macOS arm64 et macOS x64 (`engine-tool` n'a jamais tourné sur ces runners) ; répétition manuelle `workflow_dispatch` à faire. **Cache vcpkg** : la répétition du 2026-11-18 a passé 18,5 min (Windows) et 9,6 min (macOS arm64) dans l'installation vcpkg car la release avait son propre cache (`vcpkg_installed`, clé `vcpkg-<triplet>-…`) au lieu de celui de la CI (`.vcpkg-archives`) : alignés. **Linux arm64 ajouté** (runner `ubuntu-24.04-arm`, triplet `arm64-linux`, `VCPKG_FORCE_SYSTEM_BINARIES=1` car vcpkg n'a pas d'outils arm64 précompilés) dans `ci.yml` (job `linux-arm64`, `continue-on-error` et `WERROR=0` jusqu'au premier passage vert) et `build-engine.yml` ; **jamais exécuté**, la première preuve est la CI du prochain push. **Windows arm64 non pris en charge** : `mk/engine.mk` fixe `windows-x64` et `-m64`, MSYS2 ARM64 n'a pas GCC (environnement `CLANGARM64`), et la compilation de `shaderc`/`glslang`/`msdfgen` en arm64-mingw n'est pas connue ; à reprendre si le besoin se confirme. | Avant le premier tag |
| G65 | `Window::Close` après `ClearGraphicsRuntime` | Le binding garde l'API du runtime *actif* : après `ClearGraphicsRuntime`, l'API repasse à OpenGL et `Shutdown` n'exécute pas le nettoyage Vulkan. Comportement identique à celui d'avant M1 bis (la fonction `GraphicsWindowContext::Shutdown` dispatchait déjà sur l'API active). Règle d'usage : fermer la fenêtre **avant** `ClearGraphicsRuntime` (c'est l'ordre de `Game`). Une solution propre (le binding retient l'API du contexte créé) arrive avec la migration en M2. | M2 |
| G66 | `SANITIZE` n'invalide pas les objets | `make demo-test SANITIZE=address,undefined` réutilise les objets compilés sans instrumentation (les règles de `mk/engine.mk` ne dépendent pas de `SANITIZE`) : un résultat vert peut masquer des erreurs. Contournement documenté : `make fclean demo-fclean` avant. Correction possible : répertoire d'objets par mode (`obj/<type>-asan/`) ou fichier témoin qui refuse un changement de `SANITIZE` sans nettoyage. | M2 (outillage) |

## Ajouts de M2 lot A (G67 à G72)

| ID | Sujet | Détail | Jalon |
| --- | --- | --- | --- |
| G67 | Tests GPU du RHI : pilotes de CI | Les tests `tests/rhi/opengl_device_tests.cpp` ont besoin d'un contexte OpenGL 4.3. Linux (xvfb + Mesa llvmpipe, `ci.yml`) : exécutés. **macOS** (OpenGL plafonne à 4.1) et **Windows MinGW** (le runner n'a que le GL 1.1 logiciel) : `createDevice` renvoie `kUnsupported`, chaque test s'arrête avec un avertissement : **non vérifiés sur ces deux systèmes**. | M4 (Vulkan/MoltenVK), M9 |
| G68 | Repère du RHI = repère OpenGL | Origine du viewport en bas à gauche, première ligne de `readTexture` = bas, profondeur NDC −1..1. Vulkan/Metal diffèrent ; harmonisation et reverse-Z (REN-02) à décider avec le premier shader commun : voir `docs/QUESTIONS.md` A26. | M2 lot B, M3, M4 |
| G69 | Noms de debug non relus | `glObjectLabel` / `glPushDebugGroup` sont appelés sur chaque objet et passe (aucune erreur de validation), mais aucun test ne relit le nom (`glGetObjectLabel` est interdit hors du backend) ni ne l'observe dans RenderDoc. RHI-11 reste Partiel pour la partie « visible dans RenderDoc ». | M2 lot B |
| G70 | Barrières conservatrices | `glMemoryBarrier(GL_ALL_BARRIER_BITS)` après chaque `dispatch` et fin de passe qui a lié du stockage : correct, pas optimal. Le calcul fin d'après l'usage déclaré viendra avec le premier compute réel (M3). | M3 |
| G71 | Pas de multi-draw avec compteur | `drawIndirect` / `drawIndexedIndirect` (RHI-10) fonctionnent ; la variante `...Count` (`GL_ARB_indirect_parameters`, compteur lu sur le GPU) n'est pas exposée : `Capabilities` n'a pas de champ pour elle. | M5 |
| G72 | Images de stockage et unités plates | `groupe × 8 + binding` peut dépasser `GL_MAX_IMAGE_UNITS` (8 sur certains pilotes) : `setBindGroup` refuse avec un message. Convention : images de stockage dans le groupe 0. À revoir avec la réflexion des shaders (lot B), qui peut compacter. | M2 lot B |

## Ajouts de M2 lot B2 (G73 à G75)

| ID | Sujet | Détail | Jalon |
| --- | --- | --- | --- |
| G73 | Fenêtre minimisée non testée | `beginFrame` renvoie `kBusy` quand le framebuffer est de taille nulle ; le cas n'est pas reproduit par les tests (xvfb ne minimise pas). Même limite pour la présentation réelle (visible, compositeur) : les tests utilisent une fenêtre invisible. | B4 (la démo présente via le RHI) |
| G74 | Vsync non observable | `setVSync` appelle `glfwSwapInterval` sans erreur ; aucun test ne mesure le rythme d'affichage. RHI-08 reste Partiel pour le vsync. | B4 / C |
| G75 | Backbuffer sans profondeur ni MRT | Le backbuffer d'une fenêtre doit être l'unique attachement de sa passe (framebuffer par défaut). Le rendu de scène se fait dans une cible RHI puis est copié ou dessiné dans le backbuffer, comme le fait déjà le code hérité (`BlitToDefault`). | — (voulu) |
| G76 | ASan : `alloc-dealloc-mismatch` dans libshaderc | Le `libshaderc` d'Ubuntu 24.04 (2023.8) libère avec `free` des options allouées par `operator new` (`shaderc::CompileOptions`) : ASan s'arrête dès le premier `compileGlsl`. Le contrôle `alloc_dealloc_mismatch` est désactivé **pour le binaire de tests seulement** (`tests/test_main.cpp`, `__asan_default_options`) ; les autres contrôles ASan/UBSan restent actifs. À retirer si la bibliothèque est corrigée ou remplacée (vcpkg `shaderc`). | B6 (revoir) |

## Ajouts de M2 lot B3 (G77 à G79)

| ID | Sujet | Détail | Jalon |
| --- | --- | --- | --- |
| G77 | Code hérité et nouveau code coexistent | `Buffer`, `Texture`, `RenderTarget`, `ShaderProgram`, `ShaderLibrary` (singleton), `Screenshot`, `GPUTimestampQuery` et `GraphicsRenderState` (G38 : `scissorBox` non restauré) restent en service jusqu'à la migration de leurs consommateurs ; ils sont supprimés en B6. Entre-temps le dépôt a deux couches de ressources (l'ancienne reste la seule branchée sur la démo). | B4, B5, B6 |
| G78 | `GpuTimer` pas encore relié au `Profiler` | `IGpuTimerFactory` (A25) est toujours installée par l'ancien `BindGraphicsRuntime` ; la fabrique sur `orosmo::renderer::GpuTimer` vient avec le renderer. | B4 |
| G79 | Façades limitées au 2D | `Texture` ne crée que des textures 2D et un échantillonneur par texture ; tableaux, cubes, 3D et partage d'échantillonneurs se font par le RHI directement. | M5 |

## Ajouts de M2 lot B4 (G80 à G83)

| ID | Sujet | Détail | Jalon |
| --- | --- | --- | --- |
| G80 | `Camera::Inputs` pas porté | La nouvelle `Camera` ne lit ni clavier, ni souris, ni GLFW (couche `renderer`). Le pilotage (actions `Camera::MoveForward`, rotation à la souris, bascule fil de fer, curseur masqué) revient à l'application, branché sur `InputManager` / `platform` au moment de la bascule de la démo. | B6, C |
| G81 | Constantes de shader brutes | L'ancien `Mesh::SetUniform*("nom", ...)` (uniformes libres par nom) devient des push constants : octets bruts (`Mesh::setPushConstants`), le premier entier étant le drapeau fil de fer. La réflexion ne donne pas encore les noms et décalages des membres du bloc : pas d'écriture par nom. | M3 (UI-12, paramètres réfléchis) |
| G82 | Lumières ponctuelles et projecteurs non rendus | Types créables et envoyés au GPU, mais `mesh.frag` ne traite que l'ambiante et la directionnelle, comme l'ancien shader (REN-05 reste Partiel). | M5 |
| G83 | Chemins des tests de scène | **Corrigé** (`tests/renderer/repo_paths.hpp` : la racine est cherchée en remontant depuis le dossier courant) : les tests tournent depuis n'importe quel dossier. **Reste** : les shaders ne sont pas encore copiés dans le paquet du moteur (SDK) : à faire à la bascule. | B6 |

## Ajouts de M2 lot B5 (G84 à G87)

| ID | Sujet | Détail | Jalon |
| --- | --- | --- | --- |
| G84 | Deux chemins de dessin dans l'interface | **Traité (nettoyage Orosmo, 2026-11-20)** : code hérité supprimé (`include/UI/`, `src/ui/legacy/`, démo sur `orosmo::ui`). Avant : `Draw` (immédiat, via `Sprite` et l'ancien backend) et `Collect` (liste de dessin) coexistent ; les composants gardent un `Sprite` et les conteneurs un `RenderTarget` hérités. UI-02 reste Partiel : « l'interface ne connaît ni le RHI ni le backend » ne sera vrai qu'après suppression de `Draw`. | — |
| G85 | Singleton `UI::Manager::Instance()` | **Traité (nettoyage Orosmo, 2026-11-20)** : code hérité supprimé (`include/UI/`, `src/ui/legacy/`, démo sur `orosmo::ui`). Avant : Conservé : les textes y trouvent leur `TextRenderer`. Remplacement par un contexte explicite (A10 pour ce module) avec la bascule. | — |
| G86 | Pas de batch 2D | Un rectangle, une série de glyphes ou une image de couche = un dessin. Le batch par instances (R2D-01) arrive en M5 (A21). | M5 |
| G87 | Texte | Pas d'UTF-8 (G27), un atlas MSDF par taille de police ; `TextRenderer::init` compile encore le shader hérité (inutile pour la liste de dessin). La parité du texte est prouvée sur une police et six chaînes, pas sur tous les cas de mise en page (retour à la ligne, ellipse, défilement : `renderTextAdvanced` passe par `renderText`, non testé par liste de dessin). | B6, M3 |

## Ajouts de M2 lot B6 (G88 à G91)

| ID | Sujet | Détail | Jalon |
| --- | --- | --- | --- |
| G88 | GLFW hors de `platform` | **Traité (lot C)** : `Window` et `InputManager` sont dans `src/platform/` (GLFW caché derrière `void *`), la démo n'inclut plus GLFW ; `tools/baseline/api_isolation.txt` supprimé, `check_api_isolation.py --strict` bloquant pour toutes les API (0 infraction). | fait |
| G89 | Dépendances Vulkan inutilisées | Le paquet `vulkan` (vcpkg hors Linux, `libvulkan-dev` et `vulkan-validationlayers` en CI et dans le README) n'est plus lié ; il reste installé jusqu'à M4. | M4 |
| G90 | Touches de debug non testées | Les touches 1 à 8 (échelle de rendu de la scène et de l'interface) sont reprises dans `Game` (l'échelle de l'interface passe par une cible réduite) ; seule la scène à l'échelle est couverte par des tests (`renderer/scene`). | C |
| G91 | Démo sous Windows, macOS et CI non observée | La démo adopte maintenant sa fenêtre par le RHI ; le pas « capture de la démo comparée à la référence » de la CI (llvmpipe + xvfb) n'a pas encore tourné avec ce chemin, ni la démo sous Windows et macOS (G67). | prochain push |
| G92 | Tests de l'ancienne couche retirés | Avec les classes, ~45 cas (`graphics_core`, `graphics_foundation`, `renderers`, `shader`, `window_graphics_binding`, une partie de `profiler_gpu_timer`) ont été supprimés ; leurs comportements sont couverts par `tests/rhi/` et `tests/renderer/` (tampons, textures, cibles, shaders et rechargement, pipelines, scène, dessinateur 2D, minuteries, fenêtre). Le test de la liaison `Window` / backend n'a plus d'objet. | — |
| G93 | Mode sans fenêtre : un serveur d'affichage reste nécessaire | `--headless` ne crée aucune fenêtre côté démo, mais le périphérique OpenGL obtient son contexte par une fenêtre GLFW 1×1 invisible : sans X11/Wayland (serveur sans écran), il faut `xvfb-run` ou un contexte EGL/OSMesa, non fait. Vérifié sous Linux (Intel Arc, Mesa) ; Windows et macOS non observés. | M3 (outil en ligne de commande) |
| G94 | DPI ×2 non observé | La démo dessine à la taille réelle du tampon (`Window::GetFramebufferSize`) et l'interface en coordonnées logiques ; seul le facteur 1 est vérifié (capture identique). L'interface grossit en netteté (atlas MSDF) mais le panneau n'est pas mis à l'échelle de `GetContentScale`. Aucune machine à échelle 2 ici. | M3 |
| G95 | Position du curseur non testée | `Window::SetCursorPosition` / `GetCursorPosition` : sous X11 sans focus la position écrite n'est pas relue, donc aucun test automatisé ; vérifié à la main par la caméra de la démo. Idem pour le plein écran et le mode sans bordure. | M3 |
| G96 | Noms globaux `Window` / `InputManager` | Déplacés dans `include/orosmo/platform/` mais pas dans `orosmo::platform` : `UI` et le compilateur `cui` (`generator.rs`) les nomment tels quels ; **Traité au début de M4** : renommage dans `orosmo::platform`. Les constructeurs de `Window` déclarés mais jamais définis (liés à rien) le sont maintenant. | Traité (début de M4) |
| G97 | Revue CodeRabbit PR #7 : corrigé | Corrigé (branche `fix/M2-coderabbit-rhi`, tests ajoutés) : échec d'allocation de tampon et de texture (erreur au lieu d'un handle sans stockage), limites par type (couches, cube), `resize` qui garde l'ancien backbuffer, rappel de validation retiré à la libération d'un contexte adopté, `glDrawBuffer(GL_BACK)` pour la fenêtre, plage de profondeur remise à [0,1] à chaque passe, `readTexture` en passe qui rend la liaison de framebuffer, `dispatch` qui réactive son programme, `drawIndexedIndirect` qui refuse un décalage d'indices non nul, entrée de cache disque invalide recompilée, `ShaderHotReloader` ni copiable ni déplaçable, `check_layers.py` qui contrôlait à tort rien sous `include/orosmo/` et ignorait `<...>` (136 fichiers contrôlés maintenant, 0 violation), capture non validée si la soumission échoue. | fait |
| G98 | Revue CodeRabbit PR #7 : RHI | **Traité** (branche `fix/G98-rhi-opengl`, `tests/rhi/opengl_state_tests.cpp`, `opengl_backend_tests.cpp`, `shader_compiler_tests.cpp`) : unité de texture réservée, VAO détachés, clôture GPU par image, limite des SSBO exposée, `submit` refuse une portée ouverte, plages sans débordement, octets réellement transférés, tranches 3D, valeur d'effacement entière, barrière de stockage entre dessins, requêtes nommées, réflexion des tableaux, rechargement à chaud (includes absents, surveillances conservées, rappel sur des copies), cache disque par processus, include illisible = kIo. **Reste** : accès aux fichiers du compilateur de shaders et des PNG par la couche `platform` (G101). | fait |
| G99 | Revue CodeRabbit PR #7 : rendu et interface | **Traité** (branche `fix/G99-renderer`, tests dans `tests/renderer/`) : identifiant de programme unique, ancien pipeline gardé si une révision ne se lie pas, étages validés ensemble, surveillances retirées, erreur de lot conservée ; modes de mise à l'échelle initialisés/arrêtés et non exécutés sans mise à l'échelle, génération d'images refusée sans perdre l'actif, historique lu avant d'être remplacé, `generatedFrame()`, sans RTTI, jitter selon `needsJitter`, lumières avant le premier dessin, fil de fer `kLessEqual` ; couches prémultipliées (parité UI toujours dans la tolérance), couches gardées (découpage, dépendances), couche manquante signalée et `UI::Manager::InvalidateLayers`, redessin partiel dans l'ordre, découpages et tailles validés, erreur d'encodeur ; normale dans l'espace monde (matrice des normales par objet ; scène de référence inchangée : modèle identité) ; index et décalages des maillages, format de profondeur refusé comme couleur, `setLight` en `Result`, stb sans réglage global ; démo : interface seule réduite (touches 5-8), capture en échec si une image est refusée, erreurs d'initialisation en `Result` ; `rhi_demo` réutilise `savePng`. | fait |
| G100 | Revue CodeRabbit PR #7 : tests et documentation | **Traité** (branche `fix/G100-tests-docs`) : `tests::errorOf` / `tests::valueOf` (plus d'appel à `error()` sur un succès ni de déréférencement d'une erreur), UBO de test dimensionnés par l'alignement réel, recherche d'include dans les racines testée, dossiers temporaires exclusifs par processus (`tests/support/temp_dir.hpp`), minuteries GPU sans horloge (`waitIdle`), `repo_paths` ancré sur l'exécutable, `saveColorPng` testé sans dépendre des droits, `ENGINE_TEST_REQUIRE_GPU` (un test graphique sans pilote échoue en CI au lieu d'être ignoré), `make demo-test-unit` / `demo-test-gpu`, `Gpu` qui adopte un périphérique fenêtré, test MRT à 8 cibles ; `docs/RHI.md` (exemple corrigé), `docs/05` (preuves B1, B2), `docs/02` (preuve de chaque « Fait », RHI-03 Absent), `make check` bloquant et décrit comme tel (`CLAUDE.md`, `README.md`, `Makefile`). **Non fait, avec raison** : comparer `ui.png` en CI (l'image montre les FPS en direct ; la parité de l'interface est prouvée de façon déterministe par `tests/renderer/parity_tests.cpp`). | fait |
| G101 | Fichiers hors de `platform` | Le compilateur de shaders (résolveur d'includes, cache disque, rechargement) et `savePng`/`loadPng` ouvrent les fichiers par la bibliothèque standard (`std::ifstream`, `std::filesystem`, stb) et non par un service de fichiers de `platform`/VFS (docs/03 §6, revue CodeRabbit). Ce ne sont pas des appels système directs, mais le passage par le VFS (CORE-08) est à faire avec les assets (M3+) : l'`IncludeResolver` est déjà le point d'injection. | M3 (assets) |
| G102 | Revue CodeRabbit PR #7, 2e passe : reporté ou refusé, avec raison | (a) **Rechargement à chaud** : seul le fichier finalement choisi par le résolveur d'includes est surveillé (ou, s'il n'y en a aucun, le chemin relatif au fichier qui inclut) ; créer un include plus prioritaire dans une autre racine ne relance pas la compilation. Corriger demande que `IncludeResolver` expose les chemins examinés : changement de contrat, à décider avec les assets (M3). (b) **Entrées retirées du rechargeur** gardées dans son tableau (un booléen testé par entrée et par image) : coût négligeable devant la complexité d'identifiants découplés de l'indice ; refusé. (c) **Test de la clôture GPU** qui échouerait si l'attente disparaissait : il faudrait garder une clôture non signalée de façon déterministe, ce qu'OpenGL ne permet pas ; la libération reste prouvée par le seuil en images et par le code (`flushReleases`). | M3 / refusé |
| G103 | Revue CodeRabbit PR #7, 2e passe : corrigé | RHI (`fix/M2-review2-rhi`) : toute erreur GL de création est un échec, clôture ou attente en échec -> `glFinish`, rappel de validation retiré seulement s'il est à nous, barrière entre dessins selon le programme (stockage), effacements validés avant le premier, `bufferRowLength` borné, rappel de rechargement conservé (`shared_ptr`), `check_headers` multi-triplet, `TempDir` sans boucle infinie, tests sans appel `gl*`. Rendu (`fix/M2-review2-renderer`) : un étage en échec n'installe aucun autre étage, reprise en recompilant tous les étages, push constants de la révision du pipeline dessiné, bloc `ModelBlock` vérifié par `static_assert`, échec de la scène = image non comptée ni capturée, couleur de remplacement prémultipliée dans une couche, découpage dans un parent vide, aucune éviction après une liste en erreur, premier mode supporté d'un nom, `generatedFrame()` et historique invalidés par un échec, erreurs d'encodeur de `FullscreenBlit`, redessin partiel avec les descendants débordants, `docs/RHI.md` traite les erreurs, test de `Manager::InvalidateLayers`. | fait |

## Revue CodeRabbit de la PR #7, 3e passe (2026-11-19, commit `b1462ae`) : relevé, non traité

Relevé à la demande de l'auteur avant la fin de M3 ; **aucune correction faite** (les constats ne sont pas encore vérifiés sur le
code). Chemins de M2 ; depuis M3, `src/UI/` est `src/ui/legacy/`. À traiter sur une branche `fix/M2-review3-*` depuis `dev`.

| ID | Constat (gravité CodeRabbit) | Fichier | Proposition |
| --- | --- | --- | --- |
| G104a | **Majeur** : en mode sans fenêtre SANS dossier de capture, une erreur de scène est effacée (`sceneError.reset()`) sans positionner `captureFailed` : `captureSucceeded()` vaut vrai et le processus réussit sans avoir produit d'image | `apps/demo/src/Game/Game.cpp:412` | mémoriser l'échec dès que `headless`, arrêter la boucle sans fenêtre ; garder la reprise en mode fenêtré |
| G104b | **Majeur** : barrière de stockage omise quand un dessin qui ÉCRIT par `imageStore` est suivi, dans la même passe, d'un dessin qui LIT la texture par `sampler2D` (`p->usesStorage` faux pour le second) | `src/rhi/opengl/gl_encoder.cpp:627` | barrière avant tout dessin qui suit un dessin de stockage de la passe ; test producteur `imageStore` → consommateur `sampler2D` |
| G104c | Mineur : `FullscreenBlit::copy` / `compose` dessinent même si `beginRenderPass` a échoué (une passe déjà ouverte reste ouverte) | `src/renderer/fullscreen_blit.cpp:123, 140` | rendre l'erreur de l'encodeur avant toute commande, puis vérifier `encoder.error()` après `beginRenderPass` |
| G104d | Mineur : `historyValid_` reste vrai si un post-traitement échoue après une image réussie : l'image suivante réutilise un historique périmé | `src/renderer/scene_renderer.cpp:470` | invalider avant les post-traitements, ne revalider qu'après la copie finale |
| G104e | Mineur : le contrat d'erreur du README du RHI promet qu'aucun appel GL ne suit une commande invalide ; `setBindGroup` lie les entrées une à une (effets partiels) | `src/rhi/README.md:44` | documenter les effets partiels ; garder à part la garantie vérifiée des valeurs d'effacement |
| G104f | Mineur : la zone redessinée d'un conteneur suit ses bornes, pas celles de son contenu qui déborde : un enfant qui déborde peut garder une ancienne couleur | **Traité (nettoyage Orosmo, 2026-11-20)** : code hérité supprimé (`include/UI/`, `src/ui/legacy/`, démo sur `orosmo::ui`). Avant : `src/ui/legacy/Core/Container.cpp:157` | — |
| G104g | Mineur : le test G99 passerait aussi avec un redessin complet | **Traité (nettoyage Orosmo, 2026-11-20)** : code hérité supprimé (`include/UI/`, `src/ui/legacy/`, démo sur `orosmo::ui`). Avant : `tests/renderer/ui_collect_tests.cpp:233` | — |
| G104h | Mineur : `check_headers.py` cherche les dépendances dans tous les triplets vcpkg installés, le build dans un seul | `tools/check_headers.py:32` | utiliser `VCPKG_TRIPLET`, exiger `--vcpkg` si plusieurs triplets sans choix |
| G104i | Détail : la disposition de `ModelBlock` n'est vérifiée que côté C++ ; un ordre différent des matrices dans `mesh.vert` passerait | `src/renderer/mesh.cpp:18` | en-tête commun C++/GLSL ou vérification des offsets par réflexion (docs/03 §4) |
| G104j | Contrôle « règles de CLAUDE.md » en échec : des tests incluent GLFW (`tests/suites/input_layout_tests.cpp`, `tests/platform/window_input_tests.cpp`, `tests/rhi/window_surface_tests.cpp`) | `tests/` | (A) passer par l'API de `platform` (rappels injectables, `Window`) ; ou (B) exception écrite de l'auteur dans `docs/QUESTIONS.md` et `tests/` autorisé par le contrôle d'isolation |

## Ajouts de M3 (G105 à G115)

| ID | Sujet | Détail | Jalon |
| --- | --- | --- | --- |
| G105 | API de `platform` en `PascalCase` | `Window`, `InputManager` : noms hors de `docs/03` §7 et hors de `orosmo::platform` (suite de G96). Non renommés en M3 lot F : le langage hérité (`generator.rs`, convention `Get` + nom) et la démo les nomment ; le guide les déclare **provisoires**. Proposition : renommer avec la bascule de la démo vers `orosmo::ui` (A30, A31) **Décision de l'auteur (2026-11-21) : renommage au début de M4** (A31). | Traité (début de M4) |
| G106 | Texte hors de l'alphabet latin | **Accents traités** (branche `feat/REN-36-text-unicode-fonts`) : UTF-8 décodé, Latin-1, Latin étendu A et ponctuation typographique chargés à la demande (`tests/ui/text_tests.cpp`). **Reste** : autres écritures (grec, cyrillique, CJK, emoji), mise en forme complexe (arabe, ligatures : HarfBuzz) | M5 (REN-36) |
| G107 | Saisie de texte partielle | **Traités** : sélection et presse-papiers (`feat/UI-03-widgets-more`) ; annuler / rétablir, retour à la ligne automatique de `TextArea`, répétition des touches tenues, raccourcis (`InputFrame::shortcuts`, Ctrl+A / C / X / V / Z / Y dans la galerie ET le visualiseur par `ui::readWindowInput`), menus au clavier, raccourcis installés des `MenuItem` et des boutons (`feat/UI-03-widget-limits`, `tests/ui/editing_tests.cpp`). **Reste** : composition IME : le texte validé par un IME arrive (rappel de caractères de GLFW), mais la chaîne en cours de composition n'est ni affichée dans le champ ni placée sous le curseur : GLFW 3.3 (celui du dépôt) et 3.4 n'ont pas d'API de pré-édition ; il faudrait une couche native par système (IMM32 / TSF sous Windows, XIM / IBus sous X11, `text-input` sous Wayland, `NSTextInputClient` sous macOS) dans `src/platform/` | M5 |
| G108 | Streaming non mesuré | Pas de mesure de saccade (TER-04) ; un changement de paramètre recrée le `TileStreamer` (toutes les tuiles régénérées) ; le jeu de tuiles affiché change d'un coup quand la sélection est prête | Archivé (`archive/procedural-world`, A37 ; à reprendre si `world` revient en addon) |
| G109 | Visualiseur vérifié à la main | `make viewer-capture` (CI) prouve le rendu, le panneau et le changement de thème ; caméra libre, export PNG et préréglage « Save » ne sont pas exercés automatiquement (lancement fenêtré de 6 s sans erreur sous Linux, X11) | Archivé (`archive/procedural-world`, A37 ; à reprendre si `world` revient en addon) |
| G110 | Windows et macOS | `make e2e-init` en CI Windows et macOS jamais observés verts ; `make cui-test` n'est lancé que par le job Linux x64 (non configuré sous Windows et macOS, pas un résultat attendu) ; **macOS : aucun rendu avant M4** (OpenGL 4.1, le RHI demande 4.3) **Mis à jour le 2026-11-21 :** `e2e-init` est vert en CI Windows et macOS depuis plusieurs runs ; ces jobs sont devenus **bloquants** (plus de `continue-on-error`, avertissements en erreur). | M4 |
| G111 | Thème : constantes | **Police traitée** (section `fonts`, propriété `font`). Reste : quelques constantes de comportement dans le code (décalage de l'infobulle, pas de défilement = 3 lignes du thème, largeur minimale d'un champ = 10 caractères) | M5 |
| G112 | Deux modules d'interface | **En partie traité (2026-11-20)** : l'interface héritée `UI::` et la cible `UI::View` du langage sont supprimées, la démo utilise `orosmo::ui` (`apps/demo/src/Game/Game.cpp`, `tests/apps/demo_performance_tests.cpp`). Reste : `once` n'est pas figé par la cible `orosmo/ui` ; les erreurs d'argument sont placées sur l'élément ou le modificateur | M5 |
| G113 | Cache du code généré | **Corrigé en v0.4.1** (le hachage inclut l'empreinte du compilateur `cui`, `cui --version`). Ancien état : le hachage en tête des `.gen.h` couvre le source et `OUTPUT_FORMAT_VERSION`, pas la version du compilateur `cui` : changer le générateur sans incrémenter cette constante garde d'anciens fichiers | M4 |
| G114 | Projet d'`orosmo init` | **Traité en partie (M4)** : modèle générique en place ; `examples/minimal` déplacé dans `tools/engine-sdk/examples/minimal/`. Reste : choix entre plusieurs modèles, suivi par `MOD-09` | M11 |
| G115 | Correction de `InputManager` | `glfwGetCursorPos` écrivait des `double` dans les `float` de `MouseMoveData` (position fausse, `delta.x` écrasé) : corrigé en M3 (lot E) ; aucun test automatisé (pas de souris simulée, comme G95) | — |
| G116 | Revue CodeRabbit de M3 (PR #8) : performances | **`src/world/lod.cpp` archivé (A37).** Restent, sans effet sur le résultat : `Font::caretIndex` remesure chaque préfixe, et la coupure d'une très longue ligne sans espace dans `TextArea` devient quadratique ou pire (`src/ui/font.cpp`) ; l'éditeur de courbe et de dégradé recalcule l'interpolation pour chaque colonne (`src/ui/color.cpp`) | M5 |
| G117 | Revue de M3 : corrections sans test automatisé | Parties visualiseur et `TerrainRenderer::setLayers` archivées (A37), galerie `ui_gallery` retirée. Restent, vérifiées à la main ou par lecture : `Mesh::swap` échange l'origine envoyée ; préréglage écrit par remplacement atomique ; `.NOTPARALLEL` du Makefile | M4 |
| G118 | Revue de M3 : Windows non vérifié | Paquet Windows vidé avant copie (`mk/engine.mk`, PowerShell), chemins `\\` normalisés par `tools/check_cui_errors.py` : écrits, jamais exécutés sous Windows (G110). Non traité : `renderer::loadPng` passe `path.string()` (page de codes ANSI) à `stbi_load`, un chemin hors de cette page échoue (`kIo`) ; il faudrait lire le fichier en `std::ifstream` puis `stbi_load_from_memory` | M4 |
| G119 | Envoi des tuiles dupliqué | La démo (`apps/demo/src/Game/World.cpp`) et le visualiseur (`apps/terrain_viewer/src/viewer.cpp`) ont chacun ~60 lignes qui envoient les tuiles du `TileStreamer` au `TerrainRenderer` : `world` ne dépend pas de `renderer`, et aucun module ne fait le pont | Archivé (`archive/procedural-world`, A37 ; à reprendre si `world` revient en addon) |
| G120 | Visualiseur : tuile recousue non renvoyée | `Viewer::syncTiles` ne compare que le pointeur de la tuile : un maillage recousu en place par le streamer (voisine qui change de niveau) n'est pas renvoyé au GPU, des fissures peuvent rester (la démo compare aussi la couture). Repéré pendant le nettoyage, non corrigé, sans test | Archivé (`archive/procedural-world`, A37 ; à reprendre si `world` revient en addon) |
| G121 | Panneau de la démo sans fenêtre | En `--headless`, le panneau affiche « FPS: 0 » (aucun compteur de fenêtre) et, pour une capture de moins de 0,25 s, des moyennes nulles | — |
| G122 | Noms repris tels quels | `Logger`, `Profiler`, `FPSCounter`, `RingBuffer` (déplacés dans `orosmo::core` le 2026-11-20) gardent des méthodes en `PascalCase` et des niveaux `L_INFO`… ; `Window` / `InputManager` aussi (G96) | M5 |
| G123 | `make rhi-demo` échoue : identifiant d'objet | **Clos** (A37) : `apps/rhi_demo` supprimée avec le retrait du procédural ; le RHI est couvert par `tests/rhi/`. | — |
| G124 | Windows : rechargement d'un thème invalide | **Traité** (`fix/M3-review3-misc`, signalé par CodeRabbit) : `tests/ui/theme_tests.cpp` réécrivait `light.json` avec un contenu de même taille (`light2` → `light3`) ; sous Windows l'horodatage ne change pas assez vite, `FileWatcher` (date + taille) ne voyait rien. Le nom a maintenant une autre longueur. À confirmer par le job Windows | M3 |
| G125 | Terrain loin de l'origine : coordonnées de texture en float | `shaders/rhi/terrain.vert` reconstruit la position monde absolue (`pc.origin.xyz + aPos`) pour les textures et les bandes d'altitude : loin de l'origine (au-delà de ~10⁵ m) le float perd des centimètres et les textures tremblent. Proposition (CodeRabbit, PR #8) : coordonnées locales et phase de texture calculée sur le CPU en double. Sans effet sur la démo (tuiles près de l'origine) ; à traiter avec la double précision de TER-09 | Archivé (`archive/procedural-world`, A37 ; à reprendre si `world` revient en addon) |
| G126 | M3 bis : paquet générique hors Linux | **Traité** (2026-11-21) : le run manuel du workflow de release sur `dev` (44869256636) a construit les cinq paquets, dont la branche Windows de `make dist` (PowerShell) et macOS Intel (`core::parseFloat`), sans exclusion ni erreur ; `e2e-init` vert sous Windows et macOS. Reste à voir au premier tag (`v0.3.1`) : la réutilisation du build de `main`. | Fait
| G127 | Barre de progression hors Linux | `tools/build_progress.py` (relance de `make` dans un terminal) est vérifié sous Linux seulement. Sous Windows (MSYS2) et macOS, la détection du terminal (`test -t 2`), les séquences ANSI et le serveur de tâches de `make -j` à travers Python ne sont pas vérifiés ; repli : `ORS_NO_PROGRESS=1` ou `V=1` | M4 |
| G128 | Idées de l'ancienne branche `UI` à reprendre | Branche analysée le 2026-11-21 (`docs/notes/archive/README.md`) ; tout l'essentiel est déjà dans `dev`. Restent : un **registre des composants du langage lu dans les en-têtes C++** (annotations `@cui-*`, au lieu de la table écrite dans `tools/cui-rs/src/engine.rs`), utile aux modules et addons (M11) ; et des **fonctions liées avec cache** (recalculées seulement quand leurs arguments changent, `@cui-volatile` sinon), utile si une vue appelle une fonction coûteuse (M8). Branche gardée. | M8 / M11 |


## Ajouts de M4 (G129 à G150)

| ID | Sujet | Détail | Jalon |
| --- | --- | --- | --- |
| G129 | Vulkan : barrières conservatrices | `memoryBarrierAll` avant et après les passes qui lient du stockage (`src/rhi/vulkan/README.md`) : correct, pas optimal (comme G70 côté OpenGL) | M5 |
| G130 | Vulkan : une seule file | Graphique, calcul et transfert sur la même file ; pas de file de calcul asynchrone | M5 |
| G131 | Vulkan : ressource détruite avant `submit` | Une ressource détruite entre l'enregistrement d'un encodeur et son `submit` n'est pas protégée | M5 |
| G132 | Vulkan : limites d'API | Requête de temps écrite deux fois dans une passe : `kUnsupported` ; `drawIndexedIndirect` refuse un décalage d'indices non nul (comme OpenGL) ; limites de descripteurs dynamiques non vérifiées au-delà de celles du pilote | M5 |
| G133 | Différences de comportement Vulkan / OpenGL | Édition de liens stricte en Vulkan (`kParse` / `kInvalidArgument`) ; dessiner sans groupe lié à un emplacement utilisé = `kInvalidArgument` | M5 |
| G134 | Repli de backend | Message de repli affiché en double sur stderr ; le repli se déclenche sur tout `kUnsupported` de `Game::create` en Vulkan, pas seulement l'absence de Vulkan 1.3 | M5 |
| G135 | `clang-format` absent | Non installé ici : formatage non appliqué à `src/rhi/vulkan/` (`sh tools/format_sources.sh` à lancer) | M5 |
| G136 | Présentation Vulkan avec fenêtre | Swapchain vérifiée seulement par deux captures ; redimensionnement et vsync non vérifiés | M5 |
| G137 | Temps GPU par passe | Non comparables entre backends (`docs/PERF_OPENGL_VS_VULKAN.md`) ; journal `Perf:` affiché à chaque sortie de la démo | M5 |
| G138 | Un seul GPU mesuré | Intel Arc (Mesa 26.1.6 ANV) ; rien sur NVIDIA ni AMD | M5 |
| G139 | Référence `scene-opengl-demo-1280x720.png` | Produite sous llvmpipe : 12 pixels hors tolérance 8 sur GPU Intel, dans la tolérance de 2 % | M5 |
| G140 | Calcul GPU « en conditions réelles » | Couverture Vulkan perdue avec les tests du bruit GPU du terrain (archivés, A37) ; reste `tests/rhi/rhi_device_tests.cpp` (dispatch, dispatch indirect, images et tampons de stockage) | M13 |
| G141 | Outils de parité sans test | Pas de test unitaire de `tools/demo_parity.py` ni de `tools/perf_compare.py` | M5 |
| G142 | Windows et macOS pour Vulkan | Non vérifiés (MoltenVK sous macOS : M9) | M9 |
| G143 | `make demo-vulkan-check` hors `demo-test` et CI | Lancé à la main (OK en fin de M4) ; une régression de parité ne serait pas vue par la CI | M5 |
| G144 | Vulkan : fonctions globales au processus | Les pointeurs de fonctions Vulkan (volk) sont globaux : deux périphériques avec des pilotes différents ne sont pas isolés (« un seul pilote par processus », `src/rhi/vulkan/README.md`). À faire : une table de fonctions par périphérique (`VolkDeviceTable`). Revue CodeRabbit de la PR #10 | M5 ou plus tard |
| G145 | Vulkan : dessin indirect sans `drawIndirectFirstInstance` | Corrigé en revue de la PR #12 : nouvelle capacité `Capabilities::drawIndirectFirstInstance` (Vulkan : la fonction du périphérique ; OpenGL : toujours vraie), exigée par `GpuDrivenFeature::supportedBy` et testée (`fix/M5-review-gpu`) | M5 |
| G146 | Vulkan : décalage dynamique sur une entrée de taille 0 | Un décalage non nul sur une entrée `VK_WHOLE_SIZE` est refusé (`kUnsupported`, taille explicite demandée) ; OpenGL l'accepte | M5 |
| G147 | Décalage de tampon de sommets ou d'index égal à la taille | Refusé par Vulkan (`kOutOfRange`), encore accepté par `GlEncoder` (`<=`) : aligner OpenGL | M5 |
| G148 | Vulkan : copies D24S8 sur un GPU sans D24S8 | La texture devient D32S8 (repli) ; `updateTexture` / `readTexture` refusent alors la copie (`kUnsupported`) au lieu de convertir | M5 |
| G149 | Vulkan : sémaphores de l'ancienne chaîne d'échange | Détruits après N présentations (retrait différé par comptage), faute de fence de présentation ; à remplacer par `VK_KHR_swapchain_maintenance1` quand l'extension est activée ; la reconstruction appelle encore `waitIdle` sans lire son résultat (`waitIdleChecked` existe) | M5 |
| G150 | Correctifs de la revue sans test d'échec | Plusieurs correctifs de la PR #10 (soumission refusée, lot abandonné, énumération incomplète, surface sans `TRANSFER_DST`, deux périphériques, limites de descripteurs dynamiques) ne sont pas testés : il faudrait injecter des échecs de pilote ; deux tests ajoutés passaient déjà avant leur correctif | M5 |

## Ajouts de M5 (G151 à G171)

Constatés pendant M5 (`docs/releases/M5.md`) ; chaque ligne a été vérifiée dans le code. « M5 / M8 » : à reprendre avec les ajouts au RHI proposés en A50 (`docs/QUESTIONS.md`).

| ID | Sujet | Détail | Jalon |
| --- | --- | --- | --- |
| G151 | Ressources transitoires du graphe | `RenderGraph` : textures 2D seulement (profondeur 1) ; une texture est réutilisée seulement si sa description est identique (pas d'alias entre formats différents) ; un seul encodeur (`src/renderer/render_graph.cpp`, `acquireTexture`) | M5 / M8 |
| G152 | Sémantique `read` / `readFinal` | `read` voit la version laissée par le dernier écrivain déclaré avant la passe et l'écrivain suivant attend (écriture après lecture) ; `readFinal` voit le contenu final. Décrite dans `render_graph.hpp` et `src/renderer/README.md`, pas dans `docs/03` §11 (A44) | M5 |
| G153 | Forward+ : limites | Un seul `LightManager` par image ; tous les clusters sont remplis même quand ils sont vides ; les dessins `drawCustom` (instances) ne passent pas par la pré-passe de profondeur (`scene_renderer.hpp`, `setDepthPrepass`) | M5 |
| G154 | Rendu piloté par GPU : limites | Culling compute mesuré de 3 à 5,5 ms pour 100 000 objets sur un GPU intégré ; pas de `drawIndexedIndirectCount` ; Hi-Z de l'image précédente (un objet qui se découvre manque une image, G173) ; 8 niveaux de LOD au plus (`kGpuSceneMaxLevels`) ; un seul dessin GPU par image (`GpuDrivenFeature`) ; pools de géométrie copiés à l'enregistrement du maillage (`Mesh::updateVertices` après coup non suivi) ; les ombres gardent le culling CPU | M5 / M8 |
| G155 | Libération de ressources pendant l'enregistrement | `FullscreenBlit::groupFor` (cache de 64 groupes, `kMaxCachedGroups`, vidé en cours d'enregistrement) et `Mesh::setTextures` ont le même risque que G131 ; le plugin de debug-view a été corrigé (`fix/M5-debugview-lifetime`). Correctif de fond : libération différée par série d'encodeur dans le RHI (A50) | M5 / M8 |
| G156 | Ombres : limites | Pas d'ombres de contact ; pas de fondu entre cascades (le fondu à distance existe) ; une seule directionnelle ombrée ; pas de lumières ponctuelles ombrées (REN-09) ; le cache d'une cascade est redessiné dès que la caméra bouge d'un texel ; le RHI n'a pas de `depthBias` (le biais est dans le récepteur, `shadow_receiver.glsl`) ni de copie texture à texture (la recopie du cache passe par `gl_FragDepth`, `shadow_tile.frag`) | M5 / M8 |
| G157 | Ombres des transparents | Opacité et seuil par objet, pas par texel ; tramage visible sur de grosses cartes ; pas d'ombre colorée (REN-38) ; les instances projettent une ombre pleine | M5 |
| G158 | HDR : limites | Les matériaux de la démo sont écrits en sRGB mais traités comme linéaires (l'image HDR est plus claire que la LDR) ; chaque effet qui relit la scène recopie son résultat (pas de ping-pong) ; exposition automatique sans pondération centrale ; pas de sortie HDR vers l'écran (PQ, scRGB) | M5 / M6 |
| G159 | Anticrénelage : limites | TAA sans Catmull-Rom ni dé-jitter de l'image courante ; les objets sans AOV de mouvement (instances, GPU) peuvent traîner ; SMAA partiel (ni diagonales, ni coins arrondis, ni seuil prédiqué) ; MSAA absent (le RHI n'a pas de nombre d'échantillons) ; résolution dynamique sans contrôleur | M5 / M8 |
| G160 | Batch 2D : limites | Pas de tableau de textures (un matériau = une texture) ; à `z` égal, des matériaux différents peuvent s'intercaler ; formes sans anticrénelage ; polygone découpé en O(n²) (`Renderer2D::polygon`) ; un seul dépôt par `Layer2DFeature` et par image | M5 |
| G161 | Forward et forward+ : un pixel d'écart | Sur la démo, les deux chemins diffèrent d'un pixel (cause : la pré-passe de profondeur) ; les tests comparent avec la tolérance 8 | M5 |
| G162 | Référence `scene-opengl-demo-1280x720.png` | Diffère déjà de 1 250 pixels à la tolérance 0 sur la machine de mesure (G139 en comptait 12 hors tolérance 8) ; 332 pixels sur une autre mesure ; les deux restent dans la tolérance de la CI. `make demo-vulkan-check` : `dark-scene` / `ui` à 0,58 % des pixels au-delà de la tolérance 8, accepté par l'outil, antérieur à M5 | M5 |
| G163 | Build : copies datées d'origine | `sync-demo` copie avec `cp -p` (`mk/engine.mk`) : un en-tête modifié pendant une compilation de référence laisse des `.o` périmés (SIGSEGV constaté dans `debug_draw_tests`). Contournement : supprimer `apps/demo/obj/release/tests/renderer/*.o` | M5 |
| G164 | Windows, macOS, arm64 et formatage | Code de M5 non vérifié sous Windows, macOS ni arm64 (la CI le dira) ; `clang-format` non installé : fichiers neufs non formatés (comme G135) | M5 |
| G165 | UI-13 : cache par couche | Le cache par couche de l'interface (un conteneur inchangé n'est ni remis en page ni redessiné) n'est pas fait ; proposition : le reporter à M6 (A54) ; non fait en M6, reporté de M8 à M10 par la validation d'A79 (à confirmer par l'auteur) | M10 |
| G166 | REN-05 : types de lumières | Pas de lumières de surface (P2) ; un projecteur est testé comme une sphère dans les clusters (plus large que son cône) | M6 |
| G167 | REN-31 : imposteurs | LOD par distance et fondu tramé faits ; pas d'imposteurs automatiques pour la végétation lointaine | M6 |
| G168 | REN-22 : atmosphère | La boîte à outils n'a pas d'include d'atmosphère (le bloc `FrameData` en porte les paramètres) ; l'atmosphère est le module REN-24 (A39) ; critère de REN-22 à amender (A56) | M13 |
| G169 | REN-21 et REN-41 : AOV | Les objets instanciés et GPU n'écrivent aucun AOV ; rugosité, métal et émission sont des valeurs provisoires du matériau par défaut jusqu'aux matériaux PBR (REN-10) ; la règle de résolution MSAA (`aovResolveRule`) est documentée et non exercée | M6 (REN-09) |
| G170 | REN-35 : modes de debug | Modes cascades et clusters absents de la démo (clusters : seulement l'API `readClusterLightCounts`) ; les objets instanciés ignorent `albedo`, `overdraw` et `normals` | M6 |
| G171 | REN-37 : modes de fusion | `kMaskA2C` rend comme `kMask` (le RHI n'a ni alpha-to-coverage ni nombre d'échantillons) ; `kTransmission` rend comme `kBlend`, sans réfraction (REN-38, module M13) | M8 |

## Revue de la PR #12 (G172 à G181)

Remarques de CodeRabbit et de la CI sur la PR #12 (M5) vérifiées dans le code et reportées : correctif hors de portée du jalon ou décision de l'auteur. Les remarques corrigées sont dans les branches `fix/M5-review-*` fusionnées dans `dev`.

| ID | Sujet | Détail | Jalon |
| --- | --- | --- | --- |
| G172 | REN-29 : copies de géométrie après une soumission ratée | `GpuDrivenFeature` vide `pendingCopies_` dès l'enregistrement de la passe `gpudriven.cull`. Si la soumission échoue (perte de périphérique, mémoire), les copies des pools de géométrie ne sont rejouées qu'au prochain changement du registre d'`InstanceRenderer`. Le RHI ne signale pas au plugin qu'une soumission a réussi. Correctif prévu : un numéro d'image confirmé par le RHI, les copies étant gardées jusqu'à cette confirmation (A50) | M8 |
| G173 | REN-29 : occlusion en une passe | La Hi-Z de l'image précédente sert de preuve définitive : un objet découvert par un occulteur qui bouge, ou par la caméra qui contourne un mur, manque une image. Correctif prévu : une seconde passe (« two-phase ») qui reteste les objets rejetés contre la Hi-Z de l'image courante | M8 |
| G174 | Ombres de projecteur à cône large | Un projecteur de demi-angle supérieur à 85° n'a pas d'ombre (`kUnsupportedType`), faute de cubemap ou de vues multiples (REN-09) | M6 |
| G175 | Ombres en cascade : marge d'ancrage | Le texel utile d'une cascade vaut `2R / (résolution - 2)` (une marge d'un texel de chaque côté) : environ 1 % de résolution en moins à 256 texels | M5 |
| G176 | Panneau de performances de la démo | Mémoire GPU toujours « n/d » (le RHI ne l'expose pas) ; RSS « n/d » hors Linux ; les dessins de `--scene mixed` ne sont pas comptés ; « dessins de la scène » compte les maillages (`drawMesh`), pas les appels de dessin du RHI (il faudrait une statistique dans l'encodeur) ; en fenêtre, la préchauffe (`Game::warmUp`) fait avancer l'historique du TAA avant la première vraie image | M6 |
| G177 | Premier affichage des onglets de la démo | La préchauffe ne couvre que l'onglet courant : les autres onglets restent paresseux (0,3 à 1,5 s au premier affichage, environ 4 s pour l'atlas étendu de l'onglet Themes). La génération MSDF ajoute 1,3 à 1,5 s au démarrage. Proposition : cache disque des atlas (A58) | M6 |
| G178 | Tests de post-traitement | La remarque 20 de la revue (post) n'a pas de test dédié : il faudrait séparer `declare` de l'exécution dans le harnais de test | M6 |
| G179 | R2D-01 : budget de fusion du canvas | Le budget de 512 comparaisons par commande (`canvas_renderer.cpp`) est fixe, non réglable et sans test dédié | M6 |
| G180 | R2D-03 : polyligne translucide | Quand un segment est plus court que le décalage du coude, `Renderer2D::polyline` garde les quadrilatères d'origine : léger recouvrement possible avec une couleur translucide | M6 |
| G181 | Tests de superposition 2D et texte | Le test de superposition des passes 2D n'utilise pas une vraie commande de glyphes avec un atlas déterministe ; à ajouter avec une aide de test d'atlas partagée. Les nouveaux tests GPU de la revue (deux `render` par encodeur, polyligne) n'ont été lancés que sur le GPU matériel ; la CI les lance sur rasteriseurs logiciels | M6 |

## Optimisations de performance de fin de M5 (G182 à G188)

Constatés pendant les optimisations décidées en A61 (`docs/releases/M5.md`, section « Performances »).

| ID | Sujet | Détail | Jalon |
| --- | --- | --- | --- |
| G182 | Cadence d'images non testée en fenêtre | `FramePacer` (vsync, limite, sans limite) est testé sans fenêtre (états et appels) ; la vsync et le limiteur réels ne sont vérifiés qu'à la main, comme G74 | M6 |
| G183 | Vulkan `kUpload` : suivi par tampon | L'écriture directe d'un tampon `kUpload` n'a lieu que si le dernier lot qui l'a utilisé est fini (`BufferRes::gpuUse`) ; le suivi est par tampon, pas par plage : un tampon unique découpé par image passe par la copie ordonnée (A65) | M8 |
| G184 | Données par image hors de l'anneau | `ShadowFeature` (données et instances), les paramètres de `GpuDrivenFeature`, `InstanceRenderer` et `LightManager::upload` écrivent encore leurs tampons par `updateBuffer` ; inactifs dans la démo par défaut. Proposition : une allocation par image ouverte aux plugins dans l'anneau de `SceneRenderer` (A66) | M6 |
| G185 | Vulkan : copie vers la chaîne d'échange | **Corrigé quand la surface propose RGBA8 UNORM** (`perf/M5-direct-swapchain`, Wayland chez Mesa) : l'image de la chaîne est l'image de fond, sans copie. Reste : surfaces BGRA seulement (X11 chez Mesa, souvent Windows) gardent la copie, faute de pipelines BGRA pour tout ce qui dessine dans l'image de fond ; le chemin direct n'est testé que sous Wayland (`ENGINE_TEST_WAYLAND=1 bin/tests/tests -tc='G185*'`), pas en CI (X11) | M8 |
| G186 | Surveillance des thèmes | Un fichier créé dans un dossier surveillé n'est vu qu'au parcours complet suivant (jusqu'à 2 s dans la démo, `FileWatcher::setFullScanEvery`) | M6 |
| G187 | Build : en-têtes de la démo introuvables | Une fois, `make demo-test` a échoué sur `orosmo/core/system.hpp` introuvable dans `apps/demo/orosmo/includes` pendant des compilations parallèles ; relancé sans changement, il passe. Même famille que G163 (`sync-demo`) | M6 |
| G188 | Mesures de performance | Un seul GPU (Intel Arc, Mesa 26.1.6), machine chargée par d'autres processus pendant la plupart des mesures : les gains CPU de quelques pour cent sont dans le bruit ; seuls les comptages (soumissions, transferts, écritures, copies) sont exacts | M8 |
| G189 | Vsync sous Wayland | Corrigé en partie (A67 : GLFW 3.5 en Wayland natif). Mesuré ici avec la fenêtre lancée par un script (visibilité non contrôlée) : 34 à 52 images/s avec vsync en Wayland, 59 à 107 en XWayland ; le rythme exact avec la fenêtre au premier plan reste à vérifier à la main | M8 |
| G190 | Interface : texte remesuré à chaque image | `Ui::update`/`layout`/`draw` remesurent et reconstruisent chaque libellé à chaque image (`TextRenderer::measureText`, `renderText`) : environ 20 % du temps CPU de la démo, interface complète, en fenêtre | M6 |
| G191 | Wayland : présentation d'une fenêtre cachée | Sous Wayland natif (GLFW 3.4+), une fenêtre jamais affichée (ou réduite, selon le compositeur) ne reçoit pas de rappel d'image : `present` avec vsync (FIFO, Vulkan ou EGL) attend sans fin. Les tests passent par X11 quand XWayland est là (`tests/test_main.cpp`) ; la démo se fige pendant qu'elle est réduite et repart quand elle réapparaît (non vérifié sur tous les compositeurs) | M8 |
| G192 | Vulkan : minuteries GPU trop basses | **Corrigé** (`fix/M5-vk-gpu-timers`) : un pool de requêtes réutilisé gardait la disponibilité de sa mesure précédente jusqu'à ce que le GPU exécute la remise à zéro ; la minuterie se disait prête aussitôt réenregistrée et rendait d'anciennes valeurs (≈ 0,0003 ms par passe). `timestampsReady` exige maintenant que la soumission qui a écrit le pool soit terminée. Après : `Render` 0,75 ms en 2880x1800 sans fenêtre (attente du GPU mesurée 0,57 ms) | M5 |
| G193 | Vulkan : scintillement du dessin direct à très haute cadence | Avec le dessin direct dans l'image de la chaîne (G185) et sans vsync ni limite (~1000 FPS, mode MAILBOX), l'interface disparaît quelques images ou une zone de l'écran reste noire, souvent au changement de focus de la fenêtre (constaté : COSMIC, Wayland natif, Intel Arc, Mesa ANV). Propre avec la vsync, avec `--fps-limit 144`, avec `--direct-present off` (ou `ENGINE_VK_DIRECT_SWAPCHAIN=0`) et en OpenGL. Validation Vulkan avec synchronisation : aucun message. Cause non trouvée (compositeur qui relit une image rendue trop tôt, pilote ou moteur) ; hypothèse : l'image est reprise pendant que le compositeur la lit encore et il affiche une image à moitié dessinée. Contournement : paramètre `DeviceDesc::directPresentation` / `Device::setDirectPresentation`, dans la démo `--direct-present on|off` et la case « Direct present (Vulkan) » | M8 |
| G194 | Wayland : message GLFW au démarrage | GLFW (Wayland natif) écrit `Failed to retrieve layout for key name` pendant la détection de la disposition du clavier ; la disposition AZERTY est quand même détectée. Sans effet constaté | M8 |
| G195 | PBR : chemins instancié et piloté par GPU | Les matériaux PBR (`PbrMaterial`, `mesh_pbr.frag`) passent par `SceneRenderer::drawMesh` (dessins par nœud) ; `InstanceRenderer`, `ObjectSet` et `GpuDrivenFeature` gardent le matériau provisoire `InstanceMaterial` (A52) et `mesh.frag`. Un seul jeu d'UV, un mode d'adressage par texture, tangentes par triangle (pas MikkTSpace), transparents non triés (`src/assets/README.md`). |
| G196 | REN-11 : pas de constantes de spécialisation | Variantes de matériaux par définitions de préprocesseur et cache de programmes (A48, A71) : le RHI n'a pas de constantes de spécialisation. |
| G197 | REN-13 : sondes et environnement | Les sondes ne sont pas capturées depuis la scène (on les remplit à la main ou depuis une image HDR, `projectEquirect`) ; pas de dessin de ciel (skybox) ni de rotation de l'environnement ; le préfiltrage est soumis tout de suite à chaque `setEnvironment` (pas d'étalement sur plusieurs images) ; l'IBL n'atteint que les matériaux PBR de `drawMesh` (G195). | M6 |
| G198 | ANIM-01/02 : limites de l'animation | Pas de cibles de forme, d'IK ni de mouvement de racine (ANIM-05, P2 ; pistes `weights` ignorées) ; un maillage GPU dessiné par plusieurs nœuds skinnés est déformé pour le premier seulement ; normales et tangentes déformées par mat3(palette) (exact sans échelle non uniforme dans les os) ; pendant un fondu de `StateMachine`, aucune autre transition n'est évaluée ; le skinning demande le compute (sans lui `SkinnedModel::create` refuse, pas de repli en vertex shader). | M6 |
| G199 | ANIM-04 : limites des foules | `CrowdRenderer` : pas de culling par agent (tout le lot est dessiné), pas d'AOV ni de vecteurs de mouvement, pas d'ombres projetées par la foule ; palettes calculées sur le CPU à chaque `setAgents` (pas de textures d'animation précalculées ni d'évaluation des clips sur le GPU) ; normales par mat3(modèle × palette), exactes sans échelle non uniforme. Coût GPU mesuré (lot H) sur un personnage glTF généré de 10 147 sommets × 1000 agents : environ 15 ms par image sur Intel Arc iGPU, sur les deux backends (`docs/PERF_OPENGL_VS_VULKAN.md`, « M6 : skinning et foules ») ; coût CPU des palettes non mesuré. | M6 |
| G200 | ASSET-03/04/05 : limites des assets | KTX2 2D seulement (pas de tableaux, cubes ni 3D) ; repli ASTC et RGBA8 vérifiés sur CPU seulement (le GPU de la machine de test prend BC7) ; textures compressées non relisibles (`readTexture` refusé) et sans mips générés. Cuisson : bibliothèque seulement (pas d'exécutable `orosmo-bake`), LOD guidés par les positions (coutures d'UV ou de normales possibles), LOD partageant les sommets du LOD 0, peaux non reconstruites, `.omesh` suppose des flottants IEEE. Rechargement : textures seulement (`TextureCache::reloadChanged`, à appeler par l'application) ; un changement de taille ou de format crée une nouvelle texture et les matériaux qui la lient doivent reconstruire leur groupe ; modèles glTF non rechargés. | M6 |
| G201 | ~~UBSan : `WindowParameters` copiée avec un booléen non initialisé~~ **Corrigé** (`fix/M6-window-params` : valeurs initiales dans `WindowParameters`, test « G201 WindowParameters… ») | Relevé au clone propre de M6 (`make demo-test SANITIZE=address,undefined`) : 3 messages `include/orosmo/platform/window.hpp:34:8: load of value … not a valid value for type 'bool'` pendant un banc d'essai de `platform` (membres `vsync` ou `taskbarVisible` sans valeur initiale). Code non touché par M6 ; non vérifié sur `dev`. Le cas `PLAT-01 the framebuffer size…` (`tests/platform/window_input_tests.cpp:122`) reste instable sous un gestionnaire de fenêtres qui redimensionne la fenêtre de test. | M8 |
| G202 | REN-10/ANIM-04 : limites laissées par la revue CodeRabbit de la PR #13 | `RenderModel::rebuildMaterials` remplace un groupe de matériau posé à la main sur un maillage, et les lots de `CrowdRenderer` gardent l'ancien groupe (à recréer) ; le pipeline instancié des foules ne suit pas la face avant d'une transformation miroir (elle est par instance, pas par pipeline) ; un nœud statique qui dessine une primitive skinnée avant le nœud skinné montre la sortie skinnée (cas antérieur à la revue) ; le bloc IBL est réécrit à chaque image quand la caméra bouge et que des sondes existent ; sans test automatisé : nouvelle tentative de `AnimScene` après un échec de `RenderModel::create`, envoi couche par couche des tableaux compressés OpenGL de plus de 2 Gio, compteurs `.omesh` au-delà de 2^32, disque plein à l'export glTF ; publication de l'export glTF par `rename` (ancienne paire restaurée en cas d'échec, pas atomique au sens strict ; comportement sous Windows non vérifié). | M8 |

## Ajouts de M14 (G203 à G208)

Site vitrine et wiki (`site/`, `docs/releases/M14.md`). Numérotation à partir de G203 : G195 à G202 sont pris par M6 (`v0.6.0`).

| ID | Sujet | Détail | Jalon |
| --- | --- | --- | --- |
| G203 | Site jamais publié | Aucune image `ghcr.io/<propriétaire>/orosmo-site` tant qu'aucun tag `v*` n'est posé sur `main` (A43 : publication après M6) ; `.github/workflows/site.yml` n'a jamais tourné sur GitHub (vérifié localement : `npm ci`, build, `docker build`, actionlint). À la première publication : vérifier les deux jobs, puis vérifier que le paquet GHCR est **public** (un paquet neuf peut être créé privé : réglage « Package settings » sur GitHub ; un paquet privé serait facturé au-delà du quota gratuit). | M14, au premier tag après M6 |
| G204 | Pas de serveur ni de domaine | Le déploiement est manuel (`docs/DEPLOIEMENT_SITE.md`) ; serveur, domaine, HTTPS (proxy inverse) et variable de dépôt `SITE_URL` (URL canoniques, plan du site) sont à choisir par l'auteur. Sans `SITE_URL`, les URL canoniques valent `http://localhost:8080`. | Auteur |
| G205 | WEB-05 : pas de prévisualisation sur PR | Optionnel (P2), non retenu par A74 (il faut un hébergement de prévisualisation). La CI des PR construit le site et l'image sans les publier ; la relecture visuelle se fait en local (`npm run dev` ou `docker run`). | À trancher par l'auteur |
| G206 | Contenu du site à relire à chaque jalon | Le site couvre maintenant M6 (`v0.6.0`). Le contenu est rédigé à la main depuis `docs/` et les `README.md` des modules (pas de génération automatique) : il doit être relu à chaque jalon (`docs/DEPLOIEMENT_SITE.md`, « Mise à jour par jalon »), sinon il diverge. | Chaque jalon (M6 d'abord) |
| G207 | CI principale lancée pour une PR du site seul | `ci.yml` n'a pas de filtre de chemins : une PR qui ne touche que `site/` relance tout le build C++ (coût en minutes, sans risque). Proposition : `paths-ignore: [site/**]` dans `ci.yml` (non appliqué : changement de la CI principale à valider par l'auteur). | À décider |
| G208 | Build du site : un avertissement bénin | `astro build` écrit `Entry docs → 404 was not found` (Starlight cherche une page 404 personnalisée ; la page par défaut, traduite par `site/src/content/i18n/fr.json`, est utilisée). Une page `404.md` déclenche à la place un conflit de route avec Astro 7. Aucun test visuel automatique du site : une capture d'écran de l'accueil a été relue une fois (Chrome sans fenêtre). | Plus tard (P3) |
| G209 | Starter d'`orosmo init` : message faux pour Vulkan | `tools/engine-sdk/template/src/main.cpp:406-410` refuse toute API autre qu'`opengl` avec « Graphics API 'vulkan' is not available yet (use --api opengl) », alors que le backend Vulkan existe (M4, `v0.4.0`) et est le défaut de la démo. Le starter ne sait créer qu'un device OpenGL : le message devrait dire « non géré par ce projet de démarrage », ou le starter devrait choisir Vulkan avec repli (A75.1). Code non modifié (hors périmètre de la branche de documentation). | À décider (M8 ou M10) |
| G210 | `pollHotReload` des shaders jamais appelé | Ni la démo ni le gabarit d'`orosmo init` n'appellent `pollHotReload` : le rechargement à chaud des shaders ne fonctionne que si l'application l'appelle elle-même. | À décider |
| G211 | `Window(title, w, h)` plein écran hors DEBUG | Dans un build sans `DEBUG`, `Window(title, w, h)` ouvre en plein écran (piège pour un débutant). Proposition : `kWindowed` par défaut. | À décider |
| G212 | Ombre `kCached` d'un modèle skinné figée | Avec une lumière d'ombre `kCached`, l'ombre d'un modèle skinné reste figée sur une ancienne pose : le cache ne se redessine que si la matrice du modèle change ; `kDynamic` suit l'animation. Vérifié à la main, pas de test. | À décider |
| G213 | `coords.glsl` : commentaire et conversions en désaccord | `shaders/include/orosmo/coords.glsl` annonce « uv.y = 0 (haut) correspond à ndc.y = +1 » (A26), mais `uvToNdc(uv) = uv * 2 - 1` et `ndcToUv` ne retournent pas y : `uvToNdc(vec2(0, 0))` donne y = -1. Les appelants (`depth.glsl` : `viewPositionFromDepth`, `worldPositionFromDepth`) sont corrects ou non selon l'origine de l'uv qu'ils reçoivent (origine en haut, comme annoncé, ou en bas). Trouvé par la revue CodeRabbit de la PR #14 ; non corrigé (code des shaders, hors du périmètre documentaire). À trancher : corriger le commentaire, ou ajouter le retournement de y et vérifier `tests/renderer/shader_toolbox_tests.cpp`. | À décider |

## Ajouts du SDK v0.6.2 (G214 à G220)

Distribution du SDK (A76, `docs/03` §16) : outil `orosmo`, moteurs dans `OROSMO_HOME`, `deps/` dans le paquet, installeurs.

| ID | Sujet | Détail | Jalon |
| --- | --- | --- | --- |
| G214 | Chemins avec espaces | `OROSMO_HOME` (ou le dossier personnel) contenant une espace n'est pas vérifié : `ENGINE_DIR` vient de `$(shell orosmo path …)` et GNU Make découpe les mots sur les espaces (`$(wildcard …)`, `-I`, `-L`, listes d'archives). Cas probable sous Windows (`C:\Users\Prénom Nom\AppData\Local\Orosmo`). Contournement : `OROSMO_HOME` sans espace. **Atténué (fix/SDK-review1)** : sous Windows, `orosmo path` donne la forme courte 8.3 d'un chemin avec espaces (`GetShortPathNameW`, non essayé : pas de machine Windows) ; sinon le `Makefile` du gabarit refuse le chemin avec un message clair (vérifié sous Linux avec un faux outil). Le dossier du projet lui-même avec des espaces reste non pris en charge. | Avant v0.7 |
| G215 | macOS : chargeur Vulkan non embarqué, `dlopen` non vérifié | `deps/` n'embarque jamais le chargeur Vulkan (A76) ; sous macOS, volk le cherche à l'exécution (MoltenVK, `brew install molten-vk` proposé par `orosmo doctor --fix`). Le chargement par `dlopen` d'une installation Homebrew n'a pas été observé. | M9 |
| G216 | Windows et macOS : paquet, outil et installeurs non vérifiés localement | Le paquet avec `deps/`, `orosmo install/build/run/self-update` et `install.ps1` / `install.sh` sous macOS sont construits ou lancés par la CI seulement ; aucun essai sur une machine Windows ou macOS. `self-update` sous Windows renomme l'exécutable en cours en `.old` (non observé). | Avant v0.7 |
| G217 | Linux : glibc ≥ 2.39 | Le paquet Linux est construit sur Ubuntu 24.04 (glibc 2.39) : l'outil `orosmo` demande `GLIBC_2.39`, les archives du paquet `GLIBC_2.38` (`__isoc23_*`), `tools/cui` `GLIBC_2.34` (mesuré avec `objdump -T` / `nm -u` sur une construction locale, fix/SDK-review1) ; Debian 12, Ubuntu 22.04 refusés à l'édition de liens ou au lancement. Aucun contrôle automatique de cette borne. Version garantie de A76 ; pas de construction sur une glibc plus ancienne (manylinux) pour l'instant. | Plus tard |
| G218 | Paquets natifs et signature reportés | Pas de `.deb`, de formule Homebrew ni de manifeste winget ; outil et paquets non signés (seule l'empreinte `SHA256SUMS` est vérifiée, et elle vient de la même release). Étape 2 de A76. | Plus tard |
| G219 | Moteurs installés modifiables | `orosmo install` n'enlève pas les droits d'écriture de `$OROSMO_HOME/engines/<v>` : rien du build n'y écrit (vérifié par `make e2e-init` sur un paquet en lecture seule), mais une modification à la main touche tous les projets de cette version. `orosmo install <v> --force` le réinstalle. (Un moteur en lecture seule ne casse plus le 2e build : `copy_res` rend la copie inscriptible, prouvé par le 2e build de `make e2e-init`.) | Avant v0.7 |
| G220 | ~~`make e2e-init` : `orosmo doctor` sans l'`OROSMO_HOME` isolé~~ | **Corrigé** (feat/SDK-installers) : toutes les commandes `orosmo` de `e2e-init` reçoivent le même `OROSMO_HOME`. | — |
| G221 | `install.sh` avec wget : protocole des redirections non contrôlé | L'outil `orosmo` (client `https_only`), `install.sh` avec curl (`--proto '=https' --proto-redir '=https'`) et, depuis fix/SDK-review2, `install.ps1` (redirections suivies à la main, `HttpWebRequest` avec `AllowAutoRedirect = $false`, HTTPS seulement ; jamais exécuté, pas de PowerShell ici, G216) refusent `http://`, redirections comprises. `install.sh` avec wget seulement (son `--https-only` ne vaut qu'en mode récursif) refuse une base `http://` mais ne contrôle pas une redirection HTTPS → HTTP décidée par le serveur. | Avant v0.7 |

## Ajouts de M8 lot L1 (G222 à G225)

Fondations de M8 (A79 lot L1) : `platform::DynamicLibrary`, liaison de la bibliothèque avec le moteur de l'hôte, `core::Random`, `core::SpscQueue`, couches `scene`, `sim`, `app`, `game`, job TSan. Numéros pris à la suite de G221 ; un autre lot de la même vague peut avoir pris les mêmes : renuméroter à la fusion.

| ID | Sujet | Détail | Jalon |
| --- | --- | --- | --- |
| G222 | Liaison bibliothèque/hôte sous Windows et macOS : CI seulement | Les options de lien (`engine_host_ldflags`, `engine_module_ldflags` de `tools/engine-sdk/engine.mk` : `--export-all-symbols` et bibliothèque d'import sous MinGW, `-undefined dynamic_lookup -Wl,-no_fixup_chains` sous macOS) et `tests/platform/dynamic_library_tests.cpp` ne sont exécutés sur ces systèmes que par la CI (`make demo-test` des jobs Windows et macOS) ; rien n'a été essayé localement. MSVC n'est pas pris en charge (exporter depuis un `.exe` y demande `__declspec(dllexport)` ou un `.def`) : le projet ne vise que MinGW sous Windows. | M8 (CI du lot L1) |
| G223 | Nombre de symboles exportés par l'hôte (limite PE de 65 535) | Mesuré sous Linux : **10 109** symboles exportés par l'exécutable des tests lié avec `-rdynamic` (`nm -D --defined-only`, moteur et dépendances tierces compris). Sous MinGW, la règle de lien des tests affiche le compte de la table d'exports (`objdump -p`) dans le journal de la CI. Si l'exécutable du jeu (lot L5) approche la limite : exclure les archives tierces des exports (`-Wl,--exclude-libs,<archives>`), la bibliothèque de jeu n'ayant besoin que du moteur. | M8 L5 |
| G224 | Copies laissées après un plantage | `DynamicLibrary` efface sa copie (`<nom>-<pid>-<n><ext>`) au déchargement ; un processus tué la laisse à côté de l'original. Le module de jeu (L5) doit effacer les copies périmées au démarrage (motif `<nom>-*-*<ext>`, pid absent). | M8 L5 |
| G225 | TSan : réglage de l'ASLR sur les noyaux récents | Le runtime TSan de GCC 13 refuse l'entropie mmap de 32 bits des noyaux récents (« unexpected memory mapping ») : le job CI `tsan` passe `vm.mmap_rnd_bits=28` ; en local, même réglage (`sudo sysctl -w vm.mmap_rnd_bits=28`) ou `setarch -R make demo-test-unit SANITIZE=thread`. Sans affichage ni pilote Vulkan (`env -u DISPLAY -u WAYLAND_DISPLAY VK_DRIVER_FILES=/nonexistent.json`, comme le job) : **439 cas unitaires propres sous TSan** (dont `core/spsc_queue` et `platform/dynamic_library`). Avec un affichage, TSan signale une course dans Mesa et libxcb (bibliothèques non instrumentées) : `GPU_TEST_SUITES` du `Makefile` (`demo-test-unit`) n'exclut pas toutes les suites qui ouvrent un périphérique (`anim/*`, `assets/*`, `apps/demo*`…) ; elles s'ignorent seulement faute de GPU. Classement à revoir (lot L4, test concurrent simulation + rendu). | M8 L4 |
