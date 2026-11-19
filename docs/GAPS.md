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
| REN-02 | Caméra | Partiel | Perspective seule ; jitter présent. Pas d'orthographique, pas de reverse-Z/plan lointain infini, pas de rendu relatif à la caméra (flottants). | M3 |
| REN-03 | Données par image (groupe 0) | Partiel | Un UBO par caméra avec position et viewProj seulement ; pas de matrices inverses, image précédente, temps, résolution, graine, soleil, atmosphère. Pas de `static_assert` sur tailles/offsets. | M5 |
| REN-05 | Types de lumières | Partiel | Ambiante, directionnelle, ponctuelle, spot via SSBO : oui, sur l'abstraction actuelle (pas le RHI cible). Pas de `static_assert` de disposition (un commentaire documente un bug de décalage corrigé). `LightManager::SetLight` … | M2 (migration) ; M5 |
| REN-28 | Instancing | Partiel | Données par instance possibles dans un maillage ; un draw call par `Mesh`, pas de regroupement par (maillage, matériau). | M5 |
| REN-36 | Rendu du texte | Partiel | FreeType + atlas MSDF (msdfgen), ancrage, retour à la ligne, alignements, dépassement : oui. Passe par `Texture`/`Mesh` de l'abstraction actuelle, pas le RHI cible. Un octet à la fois (`unsigned char c`, `FT_Load_Char`, … | M2 |
| RHI-02 | Backend OpenGL 4.3+ | Partiel | Le rendu actuel **est** OpenGL 4.3 core, mais n'est pas derrière le RHI cible et **aucune image de référence** n'existe (RHI-14). Fuites GL hors backend : `src/Graphics/Core/RenderState.cpp` (11 occurrences `GL_*`), … | M2 |
| RHI-03 | Backend Vulkan | Partiel | Fonctionne sur ce GPU. Écarts : **pas de VMA** (sous-allocateur maison, `VulkanGraphicsResources.cpp:32`) ; GLSL « patché » par chaînes puis compilé à l'exécution par shaderc (`VulkanPipelineCache.cpp:104-191`) ; parité avec … | M4 |
| RHI-04 | Choix du backend à l'exécution | Partiel | `--api opengl\|vulkan\|metal` et `--choose-api` existent ; `GraphicsCapabilities` est interrogeable. Pas de repli documenté (`Game.cpp:16` lance une exception si l'API n'est pas OpenGL/Vulkan) ; Metal annoncé « reconnu mais non … | M4 |
| RHI-05 | Buffers, textures (2D, tableau, cube, 3D, mips), échantillonneurs | Partiel | Buffers (vertex/index/uniform/storage/staging) et textures **2D** avec mips (OpenGL `glGenerateMipmap`) ; formats RGBA8, BGRA8, D24S8, D32F, R8, R32UI, RG16F, RGB8. Pas de tableau, cube, 3D ; l'échantillonneur n'est pas un … | M2 |
| RHI-06 | Pipelines graphiques et de calcul | Partiel | Cache **mémoire** des pipelines Vulkan (clé : programme, attributs, passe, état) et cache **disque** de programmes OpenGL. Pas de cache disque Vulkan, pas de pipelines de calcul, `GraphicsPipelineDesc` défini mais jamais … | M2 (OpenGL) ; M4 (disque Vulkan) |
| RHI-08 | Passes de rendu et swapchain | Partiel | Swapchain Vulkan et cibles de rendu avec `Resize` existent. Pas de passes décrites par attachements load/store ; `vsync` faux par défaut. | M2 |
| RHI-10 | Dessin instancié et indirect | Partiel | Dessin instancié (attributs par instance) oui ; `drawIndirect` / multi-draw : grep `DrawIndirect\|MultiDraw` : 0. | M2 (instancié) ; M5 (indirect) |
| RHI-12 | Pipeline de shaders | Partiel | Source GLSL 430 compilée directement par OpenGL ; pour Vulkan : réécriture par chaînes (`#version 450`, `set = 0`) puis shaderc **à l'exécution**. Absents : compilation hors ligne, SPIRV-Cross (grep : 0), réflexion des … | M2 |
| RHI-15 | Rendu vers plusieurs cibles (MRT) | Partiel | Plusieurs cibles couleur (jusqu'à `maxColorAttachments`) et profondeur lisible : oui (vecteurs de mouvement RG16F ajoutés à la demande). Pas d'opérations load/store par cible ; limite 8 codée en dur côté Vulkan … | M2 |
| TER-01 | Bruit | Partiel | Bruit blanc, bruit lissé (interpolation de valeurs hachées), fBm : oui (CPU, `float`). `PerlinNoise` n'est pas un bruit de Perlin à gradients. Pas de Simplex, Worley, distorsion de domaine ; **pas de version GPU** ni de … | M3 |
| TER-02 | Génération par tuiles | Partiel | Une grille unique déterministe pour une graine. Pas de tuiles, pas d'asynchronisme, pas de dépendance (graine, coordonnées, paramètres). Le code est dans l'**application de démo**, et `TerrainGenerator` possède un `Mesh` … | M3 |
| TEST-01 | Tests unitaires | Partiel | doctest en place (117 cas verts, clone propre) ; CI écrite mais jamais observée sur GitHub ; couverture de `core` partielle (Logger, InputManager, Light, TextRenderer, compilateur `.cui` : 0 test). | M0 (observer la CI) puis continu |
| TEST-03 | Tests de déterminisme | Partiel | Séquence PCG figée par des valeurs d'or (`noise_tests.cpp`) et image de terrain reproductible (3 captures identiques, `docs/BASELINE/reference/`) ; pas de somme de contrôle des sorties de génération sur 3 systèmes ; `Noise()` sans graine reste lié à l'horloge. | M3 |
| TOOL-01 | Visualiseur de terrain | Partiel | Caméra libre et terrain affichés ; pas de réglages (pas de widgets) ni d'export. Ce n'est pas un `terrain-viewer`. | M3 |
| UI-01 | UIManager | Partiel | Composant, conteneur, VBox/HBox, `DeferredValue<T>` à double tampon, mixin CRTP : présents et testés. « Migré sur le RHI » : non (dépend de `Graphics` actuel, cycle `UI`↔`Graphics`). `Manager` est un singleton … | M2 |
| UI-05 | Langage d'interface : compilateur vers C++ | Partiel | Compilation vers C++ sans coût d'exécution supplémentaire, dépendances de build, `@cui-cpp` : oui. Erreurs **ligne:colonne** du source : oui (nom du fichier présent à l'exécution `run`, absent du JSON `check`). **Manque** : … | M3 |
| UI-06 | Mise en page complète | Partiel | VBox, HBox, marges, alignement, tailles px/% : oui. Pas de grille, de superposition (ZStack), de tailles flexibles/minimales. | M3 |
| UI-07 | Thèmes en JSON | Partiel | Thème en C++ (11 couleurs nommées, préréglages clair/sombre, états `HOVER/PRESSED/DISABLED`). Pas de JSON (grep `json` dans src/UI : 0), pas de jetons de rayon/police/ombre, pas de changement à chaud. Des couleurs sont aussi … | M3 |
| UI-11 | Spécification et tests du langage | Partiel | Une spécification existe (exigences, grammaire, plan) mais pas `docs/UI_LANGUAGE.md`, et **aucun test de compilation** (valides/invalides). | M3 |
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
| REN-34 | Dessin de debug | Absent | Absent. | M3 |
| REN-37 | Modes de fusion des matériaux | Absent | Pas de `BlendMode` par matériau ; seulement deux interrupteurs d'état global. | M5 |
| RHI-07 | Groupes de liaison | Absent | Pas de groupes 0/1/2 ; le Vulkan « patche » `layout(binding)` en `set = 0` (une seule table). | M2 |
| RHI-09 | Compute | Absent | Aucun appel de calcul, aucun tampon de stockage utilisé hors SSBO des lumières. | M2 |
| RHI-14 | Tests de rendu | Absent | Outil de comparaison (`tools/compare_png.py`) et image de référence OpenGL existent (M0) ; pas encore de test automatisé « image de référence par backend » ni de rapport de différence dans la CI. | M2 |
| RHI-16 | Textures 3D et images de stockage | Absent | Pas de textures 3D, d'images de stockage ; RGBA16F/R32F absents des formats (seuls R32UI et RG16F). | M2 |
| TER-03 | Maillage à niveaux de détail | Absent | Absent (grille régulière de 498 002 triangles). | M3 |
| TER-04 | Streaming | Absent | Absent. | M3 |
| TER-07 | Texturage du terrain | Absent | Pas de mélange pente/altitude ni texturage triplanaire. | M3 |
| TER-11 | Export | Absent | Aucun export. | M3 |
| TER-12 | Interface de réglage | Absent | Aucune interface de réglage. | M3 |
| TOOL-02 | Générateur en ligne de commande | Absent | Absent (nécessite PLAT-04). | M3 |
| UI-02 | Rendu indépendant du backend | Absent | Les composants dessinent directement via sprites ; aucune liste de dessin intermédiaire, pas d'adaptateur vers un batch 2D. | M2 |
| UI-03 | Widgets de base | Absent | Aucun bouton, curseur, case à cocher, liste, champ de texte, etc. Seuls `Text` et `Label` (alias) existent. | M3 |
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
| PLAT-05 | Saisie de texte | Absent | Pas de saisie de texte, d'IME, de presse-papiers ; pas de décodage UTF-8 vers points de code. | M3 (proposé : champ de texte, UI-03) |
| R2D-04 | Cartes de tuiles | Absent | Absent. | M5 (proposé : non cité dans docs/05) |
| R2D-06 | Plusieurs vues | Absent | Absent (un `Renderer3D` et un `Renderer2D` par fenêtre). | M5 |
| REN-01 | Graphe de passes et plugins de rendu | Absent | Pas de graphe de passes, de ressources transitoires ni de plugins : le pipeline est une classe `Renderer` avec `Renderer2D`/`Renderer3D` dérivés. Points d'extension existants : passes de post-traitement et modes d'upscaling … | M5 |
| REN-06 | Mobilité et mode d'ombre des lumières | Absent | Absent. | M5 |
| REN-08 | Atlas d'ombres et cache | Absent | Absent. | M5 |
| REN-11 | Variantes et instances de matériaux | Absent | Absent. | M6 |
| REN-13 | IBL et sondes de lumière | Absent | Absent. | M6 |
| REN-14 | Occlusion ambiante | Absent | Absent. | M7 |
| REN-18 | Post-traitement courant | Absent | Interface de passe de post-traitement, aucun effet fourni. | M5 |
| REN-22 | Boîte à outils de shaders | Absent | Pas de boîte à outils GLSL partagée. | M5 |
| REN-23 | Textures de bruit en compute | Absent | Absent (pas de compute). | M7 |
| REN-24 | Ciel et atmosphère | Absent | Absent. | M7 |
| REN-25 | Brouillard | Absent | Absent. | M7 |
| REN-29 | Rendu piloté par GPU | Absent | Absent. | M5 |
| REN-31 | LOD et imposteurs | Absent | Absent de `dev`. | M5 |
| REN-32 | Tri et ordre des transparents | Absent | Absent. | M5 |
| REN-33 | Particules et décalques | Absent | Absent. | M7 |
| REN-39 | Ombres des transparents | Absent | Absent. | M5 |
| REN-41 | Transparents et sorties auxiliaires | Absent | Absent. | M5 |
| RHI-17 | Dispatch indirect et groupes dynamiques | Absent | Absent. | M5 (proposé : rendu piloté par GPU) |
| SIM-01 | Boucle de simulation à pas fixe | Absent | Absent. | M8 |
| TER-05 | Érosion | Absent | Absent (seul `GenerateCrater`). | M7 |
| TER-06 | Biomes | Absent | Absent. | M7 |
| TER-08 | Végétation et dispersion | Absent | Absent. | M7 |
| TER-09 | Planètes sphériques | Absent | Absent. | M7 |
| TER-13 | Lancer de rayon sur le terrain | Absent | Absent. | M3 |
| UI-04 | Outils de debug | Absent | Absent. | M8 |
| UI-08 | Rechargement à chaud de l'interface | Absent | Absent. | M8 |
| UI-09 | Navigation et accessibilité de base | Absent | Absent. | M8 |
| UI-10 | Inspecteur d'interface | Absent | Absent. | M8 |
| UI-12 | Paramètres réfléchis → curseurs | Absent | Absent. | M3 |

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
| REN-26 | Nuages volumétriques | Absent | Absent. | M7 |
| REN-27 | Eau | Absent | Absent. | M7 |
| REN-38 | Réfraction et transmission | Absent | Absent. | M7 |
| REN-40 | Transparence indépendante de l'ordre (OIT) | Absent | Absent. | Après M10 (P2) |
| RHI-13 | Backend Metal | Absent | Stub honnête : `IsAvailable()` renvoie faux, message clair. | M9 |
| RHI-19 | Transferts asynchrones | Absent | Absent. | Après M10 (P2) |
| SIM-02 | Accès aux données du monde | Absent | Absent. | M8 |
| TER-10 | Eau, rivières, lacs | Absent | Absent. | M7 |

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
| G22 | Avertissements `-Wmissing-field-initializers` | `BindFunc.h:137`, `BindMethods.h:319`. | M0 |
| G23 | Dossier `Libraries/` dans le nom des objets | `obj/<type>/Libraries/` dans `mk/engine.mk` (cosmétique). | M0 |
| G24 | 17 stashes et anciennes branches | Stashes analysés le 2026-11-17 : 2 utiles archivés dans `docs/notes/archive/` (LOD du terrain → TER-03, `ClearAttachments` Vulkan → G41) ; patchs complets hors dépôt. Branches anciennes (`UI`, `feat/*`, `feature/*`, `fix/makefile-lint-check-format`) non analysées. | Auteur (suppression des stashes) |
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
| G59 | `core` appelle le système directement | **Traité en M1 bis pour `<windows.h>` et l'environnement** : `Logger`, `Utilities`, `FPSCounter` et `Vfs` passent par `include/engine/core/system.hpp`, défini dans `src/platform/system.cpp` (`getenv` et `<windows.h>` ne sont plus dans `core`). **Reste** : `std::filesystem` (bibliothèque standard, portable) dans `Vfs`, `FileWatcher`, `Config` et `TraceRecorder` ; décision à valider, voir `QUESTIONS.md` A24. | M2 (si A24 demande de l'envelopper) |
| G60 | Remarques CodeRabbit de PR #5 non suivies | **`SteadyClock` dans `time_tests`** : un seul cas vérifie que l'horloge réelle est monotone (preuve de CORE-03) ; les autres cas utilisent `ManualClock`. **Test multithread de `trace`** : il utilise l'horloge réelle exprès (concurrence). **`EventBus::publish` et `typeid`** : RTTI conservé, un identifiant de type maison ne se justifie pas avant une mesure. **Démarrage partiel du pool de threads** : la correction (arrêt et jonction des workers lancés) n'est pas testable sans échec de création de thread. **Nettoyage des objets release avant ASan/UBSan** : déjà documenté (`make fclean` avant `SANITIZE=`), pas d'automatisme ajouté. | Aucun |


## Revue CodeRabbit de la PR #4 (2026-11-17) : constats reportés

La PR `dev` → `main` contient tout l'historique de `dev` : plusieurs remarques portent sur du code antérieur à M00. Corrigés dans M0 : voir `docs/releases/M0.md`. Reportés :

| # | Constat | Fichier | Jalon |
| --- | --- | --- | --- |
| G38 | `CaptureFramebufferState` / `RestoreFramebufferState` ne copient pas `scissorBox` : après une restauration, le test de ciseau s'applique avec un rectangle nul et tout est rogné (OpenGL et Vulkan) | `src/Graphics/Core/RenderState.cpp:223,255` | M2 (l'état global disparaît avec le RHI) ; bug réel, à corriger avant si l'on s'en sert |
| G39 | Chaque programme compilé doit avoir une identité de cache distincte | `src/Graphics/ShaderProgram.cpp:139` | M2 (RHI-06, RHI-12) |
| G40 | Les cas d'énumération (`.topLeft`) doivent être résolus selon le type du paramètre du modificateur | `tools/cui-rs/src/generator.rs:902` | M3 (UI-05) |
| G41 | `Renderer::Clear` est un no-op sous Vulkan : contenu périmé | `src/Graphics/Backends/Vulkan/VulkanRenderState.cpp:136` | M4 |
| G42 | Appliquer la limitation de fréquence (`throttle`) avant d'évaluer le contenu du texte | `src/UI/Widgets/Text.cpp:191` | M3 |
| G43 | Appliquer les réglages du renderer avant `BeginPass()` | `src/UI/Manager.cpp:90` | M3 |
| G44 | `Buffer::Initialize` : signaler l'échec et garder `desc` cohérent avec l'absence de ressource | `src/Graphics/Buffer.cpp:36-58` | M2 |
| G45 | Fichier temporaire de `cui check` non unique (`cui_check_${Date.now()}.cui`) : collision à l'ouverture de plusieurs documents | `tools/vscode-cui/main.js:110-120` | M3 (outillage UI) |
| G46 | `LogErrors(std::string context)` copie `context` : `performance-unnecessary-value-param` (clang-tidy) | `src/Graphics/Backends/OpenGL/OpenGLDiagnostics.*`, `Vulkan/VulkanDiagnostics.*` | M2 |
| G47 | Code ajouté récemment (diagnostics, wrappers de backend, `NormalizeOrFallback` dans `Camera.cpp`) en `PascalCase` et sous `include/Graphics/` : `docs/03` demande `camelCase`, `kPascalCase`, `include/engine/` | plusieurs | M2 (migration, décision A2/A17) |
| G48 | TEST-01 : le critère couvre `core` et `world`, que le plan traite en M1 et M3 : M0 ne peut pas le clore | `docs/02` | voir `docs/QUESTIONS.md` A20 |
| G49 | UI-02 exige un adaptateur vers le batch R2D-01, planifié en M5, alors qu'UI-02 est en M2 | `docs/02`, `docs/05` | voir `docs/QUESTIONS.md` A21 |

## Table de rattachement : jalon → identifiants

| Jalon | Identifiants |
| --- | --- |
| M00 | REPO-01 |
| M0 | BLD-01, BLD-02, DOC-01, TEST-01, TEST-02, TOOL-03 |
| M1 | CORE-01, CORE-02, CORE-04, CORE-05, CORE-06, CORE-07, CORE-09, CORE-10, CORE-11, CORE-12, CORE-13 |
| M2 | EXT-03, PLAT-01, PLAT-02, PLAT-04, REN-05, REN-36, RHI-01, RHI-02, RHI-05, RHI-06, RHI-07, RHI-08, RHI-09, RHI-10, RHI-11, RHI-12, RHI-14, RHI-15, RHI-16, RHI-18, UI-01, UI-02 |
| M3 | BLD-01, BLD-02, DOC-01 (première release utilisable : guide, test `engine init` en CI, API figée), CORE-08, PLAT-05, REN-02, REN-34, TER-01, TER-02, TER-03, TER-04, TER-07, TER-11, TER-12, TER-13, TEST-03, TOOL-01, TOOL-02, UI-03, UI-05, UI-06, UI-07, UI-11, UI-12 |
| M4 | RHI-03, RHI-04 |
| M5 | CORE-03, R2D-01, R2D-02, R2D-03, R2D-04, R2D-05, R2D-06, REN-01, REN-03, REN-04, REN-06, REN-07, REN-08, REN-17, REN-18, REN-19, REN-21, REN-22, REN-28, REN-29, REN-30, REN-31, REN-32, REN-35, REN-37, REN-39, REN-41, RHI-17 |
| M6 | ANIM-01, ANIM-02, ANIM-03, ANIM-04, ASSET-01, ASSET-02, ASSET-03, ASSET-04, ASSET-05, REN-10, REN-11, REN-13 |
| M7 | REN-14, REN-23, REN-24, REN-25, REN-26, REN-27, REN-33, REN-38, TER-05, TER-06, TER-08, TER-09, TER-10 |
| M8 | ECS-01, ECS-02, ECS-03, GAME-01, GAME-02, GAME-03, GAME-04, GAME-05, GAME-06, GAME-07, SIM-01, SIM-02, UI-04, UI-08, UI-09, UI-10 |
| M9 | RHI-13 |
| M10 | EXT-01 |
| Après M10 | ANIM-05, ANIM-06, ASSET-06, EXT-02, EXT-04, EXT-05, PLAT-03, R2D-07, REN-09, REN-12, REN-15, REN-16, REN-20, REN-40, RHI-19, SIM-03, TER-14 |
| Continu | GIT-01 |

## Ajouts de M1 bis (G61 à G63)

| ID | Sujet | Détail | Jalon |
| --- | --- | --- | --- |
| G61 | Code Windows/macOS de `system.cpp` non compilé | `terminalWidth`, `executablePath`, `userDataDirectory` (`SHGetFolderPathA`), `preciseSleep` (Sleep + QueryPerformanceCounter) et `begin/endHighResolutionTimer` (`timeBeginPeriod`) ont été **déplacés tels quels** depuis `Logger`, `Utilities` et `FPSCounter`, mais seule la branche Linux a été compilée et testée ici. `tests/platform/system_tests.cpp` s'exécutera sur Windows et macOS à la CI. Windows lit désormais le dossier de données par `SHGetFolderPathA` (comme `GetUserDataPath`) au lieu de `%APPDATA%` dans `Vfs` : même dossier en pratique. | CI du push |
| G62 | `XDG_DATA_HOME` vide | Avant : traité comme un dossier valide (`GetUserDataPath` créait `ProceduralGeneration/` dans le dossier courant). Maintenant : comme non défini (spécification XDG), on retombe sur `~/.local/share`. Seul changement de comportement de M1 bis, voulu. | — |
| G63 | Isolation des API : 11 fichiers restent en infraction | **Traité (lot C, voir G88)** : plus aucune infraction. Historique : `GLFW` dans `Window`, `InputManager`, `Camera`, `RenderState`, `WindowContext` et leurs en-têtes ; `<windows.h>` dans `Window.cpp` (barre des tâches) et `InputManager.cpp`. Ces fichiers sont dans la couche `platform` ou `rhi` mais pas encore dans `src/platform/` ni `src/rhi/` : la migration se fait en M2 (baseline `tools/baseline/api_isolation.txt`, 13 → 11). Revue CodeRabbit PR #6 : `IWindowGraphicsBinding` expose encore `GLFWwindow *` (`docs/03` §2 : aucun type d'API graphique dans l'API publique) ; un handle opaque défini par `platform` se fait avec la migration de `Window` et `InputManager`. | M2 |
| G64 | Workflow de release | Corrigé : le job Linux n'installait ni les paquets apt ni `tools/setup-linux-deps.sh` (`stb_image.h` introuvable, reproduit depuis un clone propre) ; Windows et macOS compilent avec `WERROR=0` comme la CI ; un tag hors de `main` est refusé ; runner `ubuntu-24.04`. **Prouvé** : la séquence Linux depuis un clone propre donne `dist/engine-linux-x64` et `dist/engine-pkg-linux-x64.zip`. **Non prouvé** : Windows, macOS arm64 et macOS x64 (`engine-tool` n'a jamais tourné sur ces runners) ; répétition manuelle `workflow_dispatch` à faire. **Cache vcpkg** : la répétition du 2026-11-18 a passé 18,5 min (Windows) et 9,6 min (macOS arm64) dans l'installation vcpkg car la release avait son propre cache (`vcpkg_installed`, clé `vcpkg-<triplet>-…`) au lieu de celui de la CI (`.vcpkg-archives`) : alignés. **Linux arm64 ajouté** (runner `ubuntu-24.04-arm`, triplet `arm64-linux`, `VCPKG_FORCE_SYSTEM_BINARIES=1` car vcpkg n'a pas d'outils arm64 précompilés) dans `ci.yml` (job `linux-arm64`, `continue-on-error` et `WERROR=0` jusqu'au premier passage vert) et `build-engine.yml` ; **jamais exécuté**, la première preuve est la CI du prochain push. **Windows arm64 non pris en charge** : `mk/engine.mk` fixe `windows-x64` et `-m64`, MSYS2 ARM64 n'a pas GCC (environnement `CLANGARM64`), et la compilation de `shaderc`/`glslang`/`msdfgen` en arm64-mingw n'est pas connue ; à reprendre si le besoin se confirme. | Avant le premier tag |
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
| G78 | `GpuTimer` pas encore relié au `Profiler` | `IGpuTimerFactory` (A25) est toujours installée par l'ancien `BindGraphicsRuntime` ; la fabrique sur `engine::renderer::GpuTimer` vient avec le renderer. | B4 |
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
| G84 | Deux chemins de dessin dans l'interface | `Draw` (immédiat, via `Sprite` et l'ancien backend) et `Collect` (liste de dessin) coexistent ; les composants gardent un `Sprite` et les conteneurs un `RenderTarget` hérités. UI-02 reste Partiel : « l'interface ne connaît ni le RHI ni le backend » ne sera vrai qu'après suppression de `Draw`. | B6 |
| G85 | Singleton `UI::Manager::Instance()` | Conservé : les textes y trouvent leur `TextRenderer`. Remplacement par un contexte explicite (A10 pour ce module) avec la bascule. | B6 |
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
| G96 | Noms globaux `Window` / `InputManager` | Déplacés dans `include/engine/platform/` mais pas dans `engine::platform` : `UI` et le compilateur `cui` (`generator.rs`) les nomment tels quels ; le renommage suit la migration de `UI`. Les constructeurs de `Window` déclarés mais jamais définis (liés à rien) le sont maintenant. | migration `UI` |
| G97 | Revue CodeRabbit PR #7 : corrigé | Corrigé (branche `fix/M2-coderabbit-rhi`, tests ajoutés) : échec d'allocation de tampon et de texture (erreur au lieu d'un handle sans stockage), limites par type (couches, cube), `resize` qui garde l'ancien backbuffer, rappel de validation retiré à la libération d'un contexte adopté, `glDrawBuffer(GL_BACK)` pour la fenêtre, plage de profondeur remise à [0,1] à chaque passe, `readTexture` en passe qui rend la liaison de framebuffer, `dispatch` qui réactive son programme, `drawIndexedIndirect` qui refuse un décalage d'indices non nul, entrée de cache disque invalide recompilée, `ShaderHotReloader` ni copiable ni déplaçable, `check_layers.py` qui contrôlait à tort rien sous `include/engine/` et ignorait `<...>` (136 fichiers contrôlés maintenant, 0 violation), capture non validée si la soumission échoue. | fait |
| G98 | Revue CodeRabbit PR #7 : RHI | **Traité** (branche `fix/G98-rhi-opengl`, `tests/rhi/opengl_state_tests.cpp`, `opengl_backend_tests.cpp`, `shader_compiler_tests.cpp`) : unité de texture réservée, VAO détachés, clôture GPU par image, limite des SSBO exposée, `submit` refuse une portée ouverte, plages sans débordement, octets réellement transférés, tranches 3D, valeur d'effacement entière, barrière de stockage entre dessins, requêtes nommées, réflexion des tableaux, rechargement à chaud (includes absents, surveillances conservées, rappel sur des copies), cache disque par processus, include illisible = kIo. **Reste** : accès aux fichiers du compilateur de shaders et des PNG par la couche `platform` (G101). | fait |
| G99 | Revue CodeRabbit PR #7 : rendu et interface | **Traité** (branche `fix/G99-renderer`, tests dans `tests/renderer/`) : identifiant de programme unique, ancien pipeline gardé si une révision ne se lie pas, étages validés ensemble, surveillances retirées, erreur de lot conservée ; modes de mise à l'échelle initialisés/arrêtés et non exécutés sans mise à l'échelle, génération d'images refusée sans perdre l'actif, historique lu avant d'être remplacé, `generatedFrame()`, sans RTTI, jitter selon `needsJitter`, lumières avant le premier dessin, fil de fer `kLessEqual` ; couches prémultipliées (parité UI toujours dans la tolérance), couches gardées (découpage, dépendances), couche manquante signalée et `UI::Manager::InvalidateLayers`, redessin partiel dans l'ordre, découpages et tailles validés, erreur d'encodeur ; normale dans l'espace monde (matrice des normales par objet ; scène de référence inchangée : modèle identité) ; index et décalages des maillages, format de profondeur refusé comme couleur, `setLight` en `Result`, stb sans réglage global ; démo : interface seule réduite (touches 5-8), capture en échec si une image est refusée, erreurs d'initialisation en `Result` ; `rhi_demo` réutilise `savePng`. | fait |
| G100 | Revue CodeRabbit PR #7 : tests et documentation | **Traité** (branche `fix/G100-tests-docs`) : `tests::errorOf` / `tests::valueOf` (plus d'appel à `error()` sur un succès ni de déréférencement d'une erreur), UBO de test dimensionnés par l'alignement réel, recherche d'include dans les racines testée, dossiers temporaires exclusifs par processus (`tests/support/temp_dir.hpp`), minuteries GPU sans horloge (`waitIdle`), `repo_paths` ancré sur l'exécutable, `saveColorPng` testé sans dépendre des droits, `ENGINE_TEST_REQUIRE_GPU` (un test graphique sans pilote échoue en CI au lieu d'être ignoré), `make demo-test-unit` / `demo-test-gpu`, `Gpu` qui adopte un périphérique fenêtré, test MRT à 8 cibles ; `docs/RHI.md` (exemple corrigé), `docs/05` (preuves B1, B2), `docs/02` (preuve de chaque « Fait », RHI-03 Absent), `make check` bloquant et décrit comme tel (`CLAUDE.md`, `README.md`, `Makefile`). **Non fait, avec raison** : comparer `ui.png` en CI (l'image montre les FPS en direct ; la parité de l'interface est prouvée de façon déterministe par `tests/renderer/parity_tests.cpp`). | fait |
| G101 | Fichiers hors de `platform` | Le compilateur de shaders (résolveur d'includes, cache disque, rechargement) et `savePng`/`loadPng` ouvrent les fichiers par la bibliothèque standard (`std::ifstream`, `std::filesystem`, stb) et non par un service de fichiers de `platform`/VFS (docs/03 §6, revue CodeRabbit). Ce ne sont pas des appels système directs, mais le passage par le VFS (CORE-08) est à faire avec les assets (M3+) : l'`IncludeResolver` est déjà le point d'injection. | M3 (assets) |
| G102 | Revue CodeRabbit PR #7, 2e passe : reporté ou refusé, avec raison | (a) **Rechargement à chaud** : seul le fichier finalement choisi par le résolveur d'includes est surveillé (ou, s'il n'y en a aucun, le chemin relatif au fichier qui inclut) ; créer un include plus prioritaire dans une autre racine ne relance pas la compilation. Corriger demande que `IncludeResolver` expose les chemins examinés : changement de contrat, à décider avec les assets (M3). (b) **Entrées retirées du rechargeur** gardées dans son tableau (un booléen testé par entrée et par image) : coût négligeable devant la complexité d'identifiants découplés de l'indice ; refusé. (c) **Test de la clôture GPU** qui échouerait si l'attente disparaissait : il faudrait garder une clôture non signalée de façon déterministe, ce qu'OpenGL ne permet pas ; la libération reste prouvée par le seuil en images et par le code (`flushReleases`). | M3 / refusé |
