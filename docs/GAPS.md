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
| G12 | Casse `utilities.h` / `Utilities.cpp` | Incohérence interne. | M1 |
| G13 | `Graphics/Backend/` (singulier) vs `Backends/` | Non renommé : changerait des `#include`. | M2 |
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
| G26 | `TODO` dans le code | `InputManager.cpp:98`, `InputManager.h:301` (molette), `Window.h:43` (`taskbarVisible`). | M2 (platform) |
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
| G59 | `core` appelle le système directement | Revue CodeRabbit PR #5 : `Vfs::defaultUserDirectory` lit `APPDATA`/`HOME`/`XDG_DATA_HOME` (`std::getenv`), `FileWatcher` et `TraceRecorder` utilisent `std::filesystem` sans passer par `platform` (`docs/03` §6). `src/platform` n'existe pas encore : l'interface (chemins, dossiers utilisateur, fichiers) est créée avec la couche plateforme. | M1 bis (interface `platform`) puis M2 |
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
| M3 | CORE-08, PLAT-05, REN-02, REN-34, TER-01, TER-02, TER-03, TER-04, TER-07, TER-11, TER-12, TER-13, TEST-03, TOOL-01, TOOL-02, UI-03, UI-05, UI-06, UI-07, UI-11, UI-12 |
| M4 | RHI-03, RHI-04 |
| M5 | CORE-03, R2D-01, R2D-02, R2D-03, R2D-04, R2D-05, R2D-06, REN-01, REN-03, REN-04, REN-06, REN-07, REN-08, REN-17, REN-18, REN-19, REN-21, REN-22, REN-28, REN-29, REN-30, REN-31, REN-32, REN-35, REN-37, REN-39, REN-41, RHI-17 |
| M6 | ANIM-01, ANIM-02, ANIM-03, ANIM-04, ASSET-01, ASSET-02, ASSET-03, ASSET-04, ASSET-05, REN-10, REN-11, REN-13 |
| M7 | REN-14, REN-23, REN-24, REN-25, REN-26, REN-27, REN-33, REN-38, TER-05, TER-06, TER-08, TER-09, TER-10 |
| M8 | ECS-01, ECS-02, ECS-03, GAME-01, GAME-02, GAME-03, GAME-04, GAME-05, GAME-06, GAME-07, SIM-01, SIM-02, UI-04, UI-08, UI-09, UI-10 |
| M9 | RHI-13 |
| M10 | EXT-01 |
| Après M10 | ANIM-05, ANIM-06, ASSET-06, EXT-02, EXT-04, EXT-05, PLAT-03, R2D-07, REN-09, REN-12, REN-15, REN-16, REN-20, REN-40, RHI-19, SIM-03, TER-14 |
| Continu | GIT-01 |
