# AUDIT_REPORT : état réel du code par rapport à `docs/02`

Audit du 2026-11-17, branche `docs/audit` depuis `dev` (après M00). **Aucun fichier de code n'a été modifié.** Seuls des rapports ont été écrits.

## Méthode (ce qui a été fait, pas supposé)

- **Build et exécution** (Linux, g++ 13.3, Intel Arc/Mesa 26.1.6) : `make all-debug`, `make demo-test` (102/102 verts), démo lancée en `--api opengl` (8 s) et `--api vulkan` (12 s) : voir `docs/BASELINE/`. Vulkan : couche de validation disponible, aucun message d'erreur dans le journal, 640 à 1 075 FPS.
- **Lecture** des en-têtes publics de `include/` et des fichiers listés dans la colonne « Où ».
- **Greps d'absence** (mots-clés cités dans la colonne « Comment ») sur `include src apps shaders mk Makefile vcpkg.json`.
- **Compilateur d'interface** exécuté sur la vue réelle et sur 5 fichiers invalides (`docs/UI_LANGUAGE_AUDIT.md`).
- **Non vérifié** : Windows, macOS, rendu visuel (aucune capture : l'écran n'est pas capturable dans cette session), parité OpenGL/Vulkan, la branche `feat/vulkan` et les 17 stashes (non lus), le travail Benchmark (repris tel quel).
- Règle appliquée : `Fait` seulement si tout le critère est rempli **et** exercé par un test ou une démo ; sinon `Partiel` ; `Incompatible` = existe mais contredit l'architecture ou les règles de `CLAUDE.md`.

## Synthèse chiffrée

| Module | Fait | Partiel | Absent | Incompatible | Total |
| --- | ---: | ---: | ---: | ---: | ---: |
| A. Noyau | 0 | 6 | 7 | 0 | 13 |
| B. Plateforme et entrées | 0 | 2 | 3 | 0 | 5 |
| C. Couche graphique | 0 | 10 | 7 | 2 | 19 |
| D. Rendu 3D | 0 | 8 | 33 | 0 | 41 |
| E. Rendu 2D et mélange 2D/3D | 0 | 4 | 3 | 0 | 7 |
| F. Contenu et animation | 0 | 1 | 11 | 0 | 12 |
| G. Terrain et monde | 0 | 2 | 12 | 0 | 14 |
| H. Scène et simulation | 0 | 0 | 6 | 0 | 6 |
| I. Interface | 0 | 5 | 7 | 0 | 12 |
| J. Logique de jeu rechargeable | 0 | 0 | 7 | 0 | 7 |
| K. Voxels et ray tracing | 0 | 0 | 4 | 1 | 5 |
| L. Outils, qualité et construction | 1 | 8 | 2 | 0 | 11 |
| **Total** | **1** | **46** | **102** | **3** | **152** |

Par priorité :

| Prio | Fait | Partiel | Absent | Incompatible |
| --- | ---: | ---: | ---: | ---: |
| P0 | 1 | 36 | 24 | 1 |
| P1 | 0 | 10 | 53 | 1 |
| P2 | 0 | 0 | 19 | 1 |
| P3 | 0 | 0 | 6 | 0 |

**Lecture** : le moteur actuel est une base 3D/2D OpenGL + Vulkan avec un texte MSDF, une interface en langage maison et un bruit CPU. Aucun identifiant n'est `Fait` sauf `REPO-01` (M00). L'écart principal est l'architecture : le RHI à handles, passes et groupes de liaison de `docs/03` n'existe pas, et tout ce qui en dépend (compute, MRT complet, plugins de rendu, éclairage clusterisé, terrain GPU) est absent.

## Lecture par module (étape 3 du protocole)

| Module | Rôle et API | Dépendances réelles | À conserver | Défauts |
| --- | --- | --- | --- | --- |
| Logger (`include/Utilities/Logger.h`) | `LOG_*`, niveaux, fichier, dédoublonnage, pile en DEBUG | `<stacktrace>`, mutex | Dédoublonnage, macros, format | Aucun test ; mémoire non bornée ; ANSI codé en dur |
| InputManager (`include/Utilities/InputManager.h`) | États par image, actions, 4 dispositions | GLFW, `windows.h` | Modèle d'actions et de dispositions | Détection de disposition seulement Windows ; molette « TODO » ; singleton par fenêtre ; 0 test |
| UI (`include/UI/`, `src/UI/`) | Composants, conteneurs, VBox/HBox, `DeferredValue`, liaisons `Bind*`, texte | `Graphics` (cycle), FreeType, msdfgen | Suivi de saleté (`dirty`) à double tampon ; liaisons typées ; 14 tests | Pas de widgets interactifs ; thème en C++ ; exceptions pour les % sans parent ; pas de liste de dessin |
| TextRenderer (`src/UI/Rendering/TextRenderer.cpp`) | Atlas MSDF, ancrage, retour à la ligne | FreeType, msdfgen, `Texture`, `Mesh` | Qualité du rendu MSDF | 1 224 lignes, 0 test, octets (pas d'UTF-8) |
| Éclairage (`include/Graphics/Light.h`, `src/Graphics/Light.cpp`) | 4 types de lumières en SSBO std430 | `Buffer` | Disposition commentée après correction d'un bug | 0 test, pas de `static_assert`, exceptions |
| Graphics / backends (`include/Graphics/Core`, `src/Graphics/Backends/`) | Fabrique de ressources `IGraphicsDevice`, 2 backends réels (OpenGL 4.3, Vulkan), Metal en stub | GLFW, glad, Vulkan, shaderc | Séparation backend/façade (`Mesh`, `Texture`, `ShaderProgram`) ; cache binaire GL ; Vulkan fonctionnel | Voir RHI-01, RHI-18 ; état global caché ; singletons (`GraphicsRuntime`, `ShaderLibrary`) |
| Renderer (`include/Graphics/Renderer*.h`) | Passe 3D, passe 2D, upscaling, jitter, historique | `RenderTarget` | Infrastructure temporelle déjà posée | Pipeline fixe, pas de graphe |
| Noise / terrain (`include/Noise`, `apps/demo/src/ProceduralGeneration`) | `Noise`, `PCGRandom`, `Grid`, `TerrainGenerator` | glm ; `Grid` dépend de `Mesh` | PCG et hachage 64 bits déterministes | Graine par horloge en défaut ; terrain dans l'app ; CPU seul |
| Profiler (`include/Profiler`) | Minuteries CPU/GPU, FPS | `Graphics/Core/GraphicsRuntime.h` | API `ProfileGPU` pratique | Vulkan : GPU factice ; cycle avec `Graphics` |
| Compilateur `.cui` (`tools/cui-rs`) | Lexer, parseur, validateur, générateur, registre | Rust std | Messages d'erreur avec ligne:colonne ; mode `check` JSON pour l'éditeur | Pas d'événements ; 0 test ; voir `docs/UI_LANGUAGE_AUDIT.md` |
| Build (`Makefile`, `mk/engine.mk`, `tools/engine-sdk/engine.mk`) | Archives statiques, SDK, `engine init` | vcpkg, cargo | Mode SDK/modèle de projet | Fichiers de 550 lignes pleins de branches PowerShell ; flags Linux incomplets avant M00 |

## Tableau complet par identifiant

Colonnes : **Où** (fichier : symbole) · **Comment** (ce qui a été lu, lancé, mesuré, ou le mot-clé du grep d'absence) · **Écart** (par rapport au critère) · **Portabilité** (API graphique/système présente dans ce code) · **Jalon** (voir `docs/GAPS.md`).


### A. Noyau

| ID | Fonctionnalité | Prio | État | Où | Comment | Écart | Portabilité | Jalon |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| CORE-01 | Logger (pile d'appels, dédoublonnage, couleurs, `<stacktrace>`) | P0 | **Partiel** | include/Utilities/Logger.h : `Logger`, `LOG_*` ; src/Utilities/Logger.cpp : `Logger::AddLog`, `CaptureStackTrace` | Lu ; la démo l'utilise et son journal est dans docs/BASELINE ; build Linux avec `-lstdc++_libbacktrace` | Niveaux, fichier, mutex, dédoublonnage, couleurs, pile (`<stacktrace>`, DEBUG seulement) : présents. **Aucun test** (le mot `Logger` n'apparaît que dans tests/test_main.cpp). `logs` (vecteur statique) n'est jamais purgé hors `Clear()` : croissance non bornée. Le dédoublonnage réécrit la console par séquences ANSI (`\033[nF`) : faux si la sortie est redirigée. | `<windows.h>` (`GetConsoleScreenBufferInfo`) sous `_WIN32` ; ANSI ailleurs → core | M1 |
| CORE-02 | Gestion d'erreurs | P0 | **Absent** | — | grep `std::expected` : 0 fichier. Exceptions dans le flux : `UI/Core/Bounds.cpp:22,33`, `Graphics/Light.cpp:172,188`, `Graphics/ShaderProgram.cpp:27` (`throw(errno)`), `apps/demo/src/Game/Game.cpp:16` | Aucune erreur attendue ne passe par `std::expected` ; plusieurs `throw` dans le flux normal (violation de la règle 10). Pas d'assertion avec message. | — | M1 |
| CORE-03 | Allocateurs | P1 | **Absent** | — | grep `Arena\|PoolAllocator\|pmr::` : 0 | Aucun allocateur. | — | M5 (proposé : première utilisation, données par image) |
| CORE-04 | Système de tâches | P0 | **Absent** | — | grep `ThreadPool\|std::thread\|std::jthread` : 0 hors backend Vulkan ; seul `std::async` dans `VulkanGraphicsResources.cpp:865` (compilation de shaders) | Pas de pool, de dépendances ni d'attente de groupe. | — | M1 |
| CORE-05 | Profilage | P1 | **Partiel** | include/Profiler/Profiler.h : `Profiler`, `ProfileGPU` ; include/Profiler/{FPSCounter,RingBuffer}.h ; include/Graphics/GPUTimestampQuery.h | Lu ; 8 tests (tests/suites/profiler_tests.cpp) verts ; la démo affiche les moyennes | Minuteries CPU nommées et moyennées : oui. Minuteries GPU : réelles en OpenGL (`glQueryCounter`), **factices en Vulkan** (`VulkanTimestampQueryResource` mesure l'horloge CPU `steady_clock`). Pas d'export (grep `tracy\|traceEvents` : 0). Dépend de `Graphics/Core/GraphicsRuntime.h` (cycle). | `<windows.h>` dans FPSCounter (sommeil précis) → platform | M1 (CPU, export) ; M2/M4 (GPU) ; (proposé : placement de CORE-05) |
| CORE-06 | Configuration | P1 | **Partiel** | include/Graphics/Backend/GraphicsAPI.h : `ParseGraphicsLaunchOptions` | Lu + 14 tests `GraphicsAPI` | Seul `--api`/`--choose-api`/`--list-apis`/`--help` existe. Pas de lecture/écriture JSON (grep `json` : seulement Benchmark), pas de valeurs par défaut documentées. | — | M1 |
| CORE-07 | Temps | P0 | **Partiel** | include/Profiler/FPSCounter.h : `FPSCounter` (limiteur, delta) ; src/Graphics/Window.cpp | Lu | Horloge et delta variable : oui. Pas de pas fixe, pas de test déterministe du temps. | Sommeil `Sleep`/`timeBeginPeriod` (Windows), `nanosleep` Linux dans FPSCounter.cpp → platform | M1 |
| CORE-08 | Événements | P1 | **Absent** | — | grep `EventBus\|Signal\|Dispatcher\|subscribe` : 0 (seul `InputEvent` dans InputManager) | Pas de bus d'événements typé. | — | M3 (proposé : premier usage, événements de l'interface) |
| CORE-09 | Mathématiques | P0 | **Partiel** | glm partout (`glm::vec3/mat4`, `glm::quat` non utilisé) ; include/Graphics/Camera.h | grep `double` hors Profiler/UI/Logger : aucun type de position en double | Vecteurs et matrices en `float` ; **aucune position monde en `double`**, pas de conversion explicite. Repère : main droite, Y haut (`glm::lookAt`), à confirmer. | — | M1 |
| CORE-10 | Gestionnaire de ressources | P0 | **Absent** | src/Graphics/ShaderLibrary.cpp (cache de programmes) — hors critère | grep `Handle<\|ResourceHandle` : 0 | Aucun handle avec génération, aucun chargement asynchrone ni comptage ni rechargement à chaud (grep `reload\|hot` : 0 hors commentaire). | — | M1 |
| CORE-11 | Système de fichiers virtuel | P1 | **Partiel** | include/Utilities/utilities.h : `GET_RESOURCE_PATH`, `GetExecutablePath`, `GetUserDataPath` ; src/Utilities/Utilities.cpp | Lu ; 10 tests `Utilities` verts | Chemins relatifs `res/...` par macro ; pas de schémas logiques `res://` / `user://`. Les chemins utilisateur sont déjà par OS (APPDATA, XDG_DATA_HOME, HOME). | `windows.h`, `readlink(/proc/self/exe)`, `_NSGetExecutablePath` → platform | M1 |
| CORE-12 | Sérialisation et réflexion | P1 | **Absent** | — | grep `reflect\|Reflect\|REFLECT` : 0 (seul `reflect()` GLSL) ; pas de bibliothèque JSON dans `vcpkg.json` | Ni sérialisation, ni réflexion, ni versionnage. | — | M1 |
| CORE-13 | Observateur de fichiers | P1 | **Absent** | — | grep `inotify\|FileWatcher\|ReadDirectoryChanges\|filewatch` : 0 | Aucun observateur de fichiers. | — | M1 |

### B. Plateforme et entrées

| ID | Fonctionnalité | Prio | État | Où | Comment | Écart | Portabilité | Jalon |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| PLAT-01 | Fenêtre | P0 | **Partiel** | include/Graphics/Window.h : `Window` ; src/Graphics/Window.cpp | Lu ; démo lancée (fenêtre créée en OpenGL et Vulkan) | Création, redimensionnement, plein écran, bordure, vsync (`parameters.vsync`, faux par défaut) : oui. **GLFW est exposé** (`#include <GLFW/glfw3.h>` dans Window.h, `GLFWwindow*` dans WindowContext.h, InputManager.h, Camera.h). Pas de DPI (grep `ContentScale\|dpi` : 0). Cache/affiche la barre des tâches Windows (`FindWindowA("Shell_TrayWnd")`). Pas d'API `createSurface(Backend)`. | GLFW, `windows.h` → platform | M2 |
| PLAT-02 | InputManager | P0 | **Partiel** | include/Utilities/InputManager.h : `InputManager`, `KeyLayout`, `InputAction`, `BindActionToInput` ; src/Utilities/InputManager.cpp | Lu ; grep des tests : aucun test d'InputManager | Dispositions QWERTY/QWERTZ/AZERTY/DVORAK, remappage par actions, états `Release/PressBegin/Pressed/PressEnd` : présents. Détection de la disposition **seulement sous Windows** (`GetKeyboardLayout`), QWERTY ailleurs ; molette « TODO » (`InputManager.cpp:98`, `InputManager.h:301`). Singleton par fenêtre (`GetInstance(GLFWwindow*)`). Aucun test. | GLFW + `windows.h` → platform | M2 |
| PLAT-03 | Manettes | P2 | **Absent** | — | grep `glfwGetGamepad\|glfwJoystick` : 0 | Aucune manette. | — | Après M10 (P2) ; UI-09 la demande en M8 |
| PLAT-04 | Mode sans fenêtre | P0 | **Absent** | — | grep `GLFW_VISIBLE\|headless` : 0 ; `Window::Init` crée toujours une fenêtre | Aucun mode sans fenêtre. | — | M2 |
| PLAT-05 | Saisie de texte | P1 | **Absent** | — | grep `glfwSetCharCallback\|glfwGetClipboardString` : 0 ; TextRenderer charge un `char` à la fois (`FT_Load_Char`, TextRenderer.cpp:347) | Pas de saisie de texte, d'IME, de presse-papiers ; pas de décodage UTF-8 vers points de code. | — | M3 (proposé : champ de texte, UI-03) |

### C. Couche graphique

| ID | Fonctionnalité | Prio | État | Où | Comment | Écart | Portabilité | Jalon |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| RHI-01 | Interface RHI | P0 | **Incompatible** | include/Graphics/Core/{GraphicsDevice,GraphicsResources,GraphicsTypes,RenderState,WindowContext}.h | Lu en entier | Existe une abstraction (`IGraphicsDevice`, `IBufferResource`…), mais elle ne suit pas `docs/03` §3 : objets `unique_ptr` avec `Bind()/Unbind()`, uniformes par emplacement entier, `AttachToFramebuffer(std::uint32_t framebufferHandle)`, **état global caché** (`GraphicsRenderState::SetDepthTest…`, singleton `GraphicsRuntime`), pas de handles (indice+génération), pas de `CommandEncoder`, de passes déclaratives ni de groupes de liaison. Les en-têtes publics contiennent `GLFWwindow*` et, sous `include/Graphics/Backends/OpenGL/`, des types GL (`GLenum`, `GLuint`). | GL/GLFW dans les en-têtes → rhi | M2 |
| RHI-02 | Backend OpenGL 4.3+ | P0 | **Partiel** | src/Graphics/Backends/OpenGL/ (`OpenGLGraphicsBackend`, `OpenGLGraphicsResources`, `OpenGLRenderState`) ; contexte 4.3 core (`OpenGLGraphicsBackend.h:20-21`) | Démo lancée en OpenGL : terrain 498 002 triangles, interface, texte ; 102 tests verts | Le rendu actuel **est** OpenGL 4.3 core, mais n'est pas derrière le RHI cible et **aucune image de référence** n'existe (RHI-14). Fuites GL hors backend : `src/Graphics/Core/RenderState.cpp` (11 occurrences `GL_*`), `include/Graphics/Texture.h` (commentaire). | `gl*` : 260 occurrences dans OpenGLGraphicsResources.cpp, 32 dans OpenGLRenderState.cpp → rhi/opengl | M2 |
| RHI-03 | Backend Vulkan | P0 | **Partiel** | src/Graphics/Backends/Vulkan/ (`VulkanGraphicsBackend`, `VulkanWindowContext`, `VulkanGraphicsResources` 2 535 lignes, `VulkanPipelineCache`) | Lancé : `./main --api vulkan` 12 s sur Intel Arc (Mesa 26.1.6) : 640 à 1 075 FPS, couche de validation disponible (`Validation layer available=yes`), aucun message `[ERROR]`/`VUID` dans le journal | Fonctionne sur ce GPU. Écarts : **pas de VMA** (sous-allocateur maison, `VulkanGraphicsResources.cpp:32`) ; GLSL « patché » par chaînes puis compilé à l'exécution par shaderc (`VulkanPipelineCache.cpp:104-191`) ; parité avec OpenGL **non mesurée** (pas d'images de référence) ; fonctionnalités de compute absentes ; capacités annoncées fausses (voir RHI-18) ; minuteries GPU factices (RHI-11). | `vk*` : tout sous Backends/Vulkan (conforme à la règle 4) | M4 |
| RHI-04 | Choix du backend à l'exécution | P0 | **Partiel** | include/Graphics/Backend/GraphicsAPI.h : `ParseGraphicsLaunchOptions`, `GraphicsCapabilities` (include/Graphics/Core/GraphicsTypes.h) | `--help` lancé ; `--api vulkan` et `--api opengl` lancés ; tests graphics_api (14) verts | `--api opengl\|vulkan\|metal` et `--choose-api` existent ; `GraphicsCapabilities` est interrogeable. Pas de repli documenté (`Game.cpp:16` lance une exception si l'API n'est pas OpenGL/Vulkan) ; Metal annoncé « reconnu mais non implémenté ». | — | M4 |
| RHI-05 | Buffers, textures (2D, tableau, cube, 3D, mips), échantillonneurs | P0 | **Partiel** | include/Graphics/Core/GraphicsTypes.h : `BufferUsage`, `TextureFormat`, `TextureDesc` ; OpenGLGraphicsResources.cpp:735-761 | Lu ; grep `GL_TEXTURE_3D\|GL_TEXTURE_CUBE\|sampler3D\|sampler2DArray` : 0 | Buffers (vertex/index/uniform/storage/staging) et textures **2D** avec mips (OpenGL `glGenerateMipmap`) ; formats RGBA8, BGRA8, D24S8, D32F, R8, R32UI, RG16F, RGB8. Pas de tableau, cube, 3D ; l'échantillonneur n'est pas un objet (`nearestFiltering` booléen) ; pas de copie explicite. | — | M2 |
| RHI-06 | Pipelines graphiques et de calcul | P0 | **Partiel** | src/Graphics/ShaderLibrary.cpp (cache disque de binaires GL, magie `SHBC`) ; src/Graphics/Backends/Vulkan/VulkanPipelineCache.cpp (`CachedKey`) | Lu | Cache **mémoire** des pipelines Vulkan (clé : programme, attributs, passe, état) et cache **disque** de programmes OpenGL. Pas de cache disque Vulkan, pas de pipelines de calcul, `GraphicsPipelineDesc` défini mais jamais utilisé (grep : 0 usage). | — | M2 (OpenGL) ; M4 (disque Vulkan) |
| RHI-07 | Groupes de liaison | P0 | **Absent** | — | Liaison par numéros plats : `CAMERA_BINDING_POINT`, `MESH_MODEL_BINDING_POINT`, `layout(binding = N)` dans shaders/*.vert\|frag ; grep `BindGroup` : 0 | Pas de groupes 0/1/2 ; le Vulkan « patche » `layout(binding)` en `set = 0` (une seule table). | — | M2 |
| RHI-08 | Passes de rendu et swapchain | P0 | **Partiel** | src/Graphics/Backends/Vulkan/VulkanWindowContext.cpp (swapchain) ; OpenGL : framebuffer par défaut ; include/Graphics/RenderTarget.h | Démo lancée avec redimensionnement ; lu | Swapchain Vulkan et cibles de rendu avec `Resize` existent. Pas de passes décrites par attachements load/store ; `vsync` faux par défaut. | — | M2 |
| RHI-09 | Compute | P0 | **Absent** | — (`ShaderStage::Compute` et `supportsComputeShaders` existent comme étiquettes) | grep `glDispatchCompute\|vkCmdDispatch\|dispatch` : 0 | Aucun appel de calcul, aucun tampon de stockage utilisé hors SSBO des lumières. | — | M2 |
| RHI-10 | Dessin instancié et indirect | P0 | **Partiel** | src/Graphics/Mesh.cpp:212 `DrawIndexedInstanced` ; OpenGLGraphicsResources.cpp:699 | Lu | Dessin instancié (attributs par instance) oui ; `drawIndirect` / multi-draw : grep `DrawIndirect\|MultiDraw` : 0. | — | M2 (instancié) ; M5 (indirect) |
| RHI-11 | Minuteries GPU et noms de debug | P1 | **Partiel** | include/Graphics/GPUTimestampQuery.h ; OpenGL : `glQueryCounter` (OpenGLGraphicsResources.cpp:1078) ; Vulkan : `VulkanTimestampQueryResource` | Lu | Les `debugName` sont conservés mais **jamais transmis** à l'API (grep `glObjectLabel\|glPushDebugGroup\|SetDebugUtilsObjectName` : 0). Minuterie Vulkan = horloge CPU (stub qui prétend marcher, règle 3). | — | M2 (OpenGL) ; M4 (Vulkan) |
| RHI-12 | Pipeline de shaders | P0 | **Partiel** | src/Graphics/Backends/Vulkan/VulkanPipelineCache.cpp (shaderc à l'exécution) ; shaders/*.vert\|frag (`#version 430 core`) | Lu | Source GLSL 430 compilée directement par OpenGL ; pour Vulkan : réécriture par chaînes (`#version 450`, `set = 0`) puis shaderc **à l'exécution**. Absents : compilation hors ligne, SPIRV-Cross (grep : 0), réflexion des liaisons, `#include`, rechargement à chaud. | — | M2 |
| RHI-13 | Backend Metal | P2 | **Absent** | src/Graphics/Backends/Metal/MetalGraphicsBackend.cpp | Lu ; test « metal backend remains a stub » (graphics_core_tests.cpp:572) | Stub honnête : `IsAvailable()` renvoie faux, message clair. | — | M9 |
| RHI-14 | Tests de rendu | P0 | **Absent** | — | grep `golden\|reference image` dans tests et src : 0 ; `ITextureResource::Readback` existe mais n'est pas utilisé en test | Aucun test d'image de référence. | — | M2 |
| RHI-15 | Rendu vers plusieurs cibles (MRT) | P0 | **Partiel** | include/Graphics/Core/GraphicsTypes.h : `RenderTargetDesc::colorAttachments`, `R32UI`, `RG16F` ; `IRenderTargetResource::SetDrawBuffers`, `ReadPixelUInt` ; src/Graphics/Renderer.cpp:151-154 | Lu | Plusieurs cibles couleur (jusqu'à `maxColorAttachments`) et profondeur lisible : oui (vecteurs de mouvement RG16F ajoutés à la demande). Pas d'opérations load/store par cible ; limite 8 codée en dur côté Vulkan (`VulkanGraphicsBackend.cpp:106`) puis lue du matériel ; pas de test d'image. | — | M2 |
| RHI-16 | Textures 3D et images de stockage | P0 | **Absent** | — | grep `RGBA16F\|R32F\|GL_TEXTURE_3D\|imageStore` : 0 | Pas de textures 3D, d'images de stockage ; RGBA16F/R32F absents des formats (seuls R32UI et RG16F). | — | M2 |
| RHI-17 | Dispatch indirect et groupes dynamiques | P1 | **Absent** | — | grep `dispatchIndirect\|DrawIndirect` : 0 | Absent. | — | M5 (proposé : rendu piloté par GPU) |
| RHI-18 | Capacités optionnelles réservées | P1 | **Incompatible** | include/Graphics/Core/GraphicsTypes.h : `GraphicsCapabilities` ; src/Graphics/Backends/Vulkan/VulkanGraphicsBackend.cpp:91-106 `MakeAdvertisedCapabilities` ; src/Graphics/AccelerationStructure.cpp | Lu ; `VulkanAccelerationStructureResource` ne contient que `desc` et `debugName` | Champs `supportsRayQueries`, `supportsAccelerationStructures`, `supportsRayTracingPipelines`, `supportsTemporalUpscaling`, `supportsFrameGeneration` **mis à `true` sans condition** côté Vulkan, avec une ressource sans implémentation : contraire aux règles 3 et 11. Pas de champs `bindless`, `meshShader`, `bufferDeviceAddress`, `sparseResources`. | — | M2 |
| RHI-19 | Transferts asynchrones | P2 | **Absent** | — | grep `VK_QUEUE_TRANSFER` : 0 | Absent. | — | Après M10 (P2) |

### D. Rendu 3D

| ID | Fonctionnalité | Prio | État | Où | Comment | Écart | Portabilité | Jalon |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| REN-01 | Graphe de passes et plugins de rendu | P1 | **Absent** | include/Graphics/Renderer.h : `Renderer`, `IPostProcessPass` (chaîne fixe BeginPass/EndPass, upscaling) | Lu | Pas de graphe de passes, de ressources transitoires ni de plugins : le pipeline est une classe `Renderer` avec `Renderer2D`/`Renderer3D` dérivés. Points d'extension existants : passes de post-traitement et modes d'upscaling enregistrables. | — | M5 |
| REN-02 | Caméra | P0 | **Partiel** | include/Graphics/Camera.h, src/Graphics/Camera.cpp:119 (`glm::perspective`) ; `SetJitter` | Lu ; 5 tests de caméra verts | Perspective seule ; jitter présent. Pas d'orthographique, pas de reverse-Z/plan lointain infini, pas de rendu relatif à la caméra (flottants). | — | M3 |
| REN-03 | Données par image (groupe 0) | P0 | **Partiel** | src/Graphics/Camera.cpp:20 `struct CameraUBO { vec3 position; float padding; mat4 matrix; }` | Lu | Un UBO par caméra avec position et viewProj seulement ; pas de matrices inverses, image précédente, temps, résolution, graine, soleil, atmosphère. Pas de `static_assert` sur tailles/offsets. | — | M5 |
| REN-04 | Éclairage forward+ clusterisé | P0 | **Absent** | — (toutes les lumières dans un SSBO, boucle dans shaders/default.frag) | grep `cluster\|Cluster\|forward\+` : 0 ; pas de pré-passe de profondeur | Éclairage forward simple, sans clusters ni budget. | — | M5 |
| REN-05 | Types de lumières | P0 | **Partiel** | include/Graphics/Light.h : `lght::Light`, `LightBlock` (std430) ; src/Graphics/Light.cpp (`BufferUsage::Storage`) ; shaders/default.frag (`buffer LightBlock`) | Lu ; **aucun test** (grep `Light` dans tests : 0) | Ambiante, directionnelle, ponctuelle, spot via SSBO : oui, sur l'abstraction actuelle (pas le RHI cible). Pas de `static_assert` de disposition (un commentaire documente un bug de décalage corrigé). `LightManager::SetLight` lève `std::out_of_range` (Light.cpp:172). | — | M2 (migration) ; M5 |
| REN-06 | Mobilité et mode d'ombre des lumières | P1 | **Absent** | — | grep `mobility\|Stationary\|shadowMode` : 0 | Absent. | — | M5 |
| REN-07 | Ombres en cascade | P0 | **Absent** | — | grep `cascade\|Cascade\|shadow` : 0 | Absent. | — | M5 |
| REN-08 | Atlas d'ombres et cache | P1 | **Absent** | — | même grep | Absent. | — | M5 |
| REN-09 | Ombres avancées | P2 | **Absent** | — | même grep | Absent. | — | Après M10 (P2) |
| REN-10 | Matériaux PBR | P0 | **Absent** | shaders/default.frag (Blinn-Phong simple : couleur sommet, normale, `specAmount`) | Lu | Pas de PBR (grep `metallic\|roughness\|albedo` : 0) ; textures sur `Mesh::AddTexture` mais pas de modèle de matériau. | — | M6 |
| REN-11 | Variantes et instances de matériaux | P1 | **Absent** | — | pas de notion de matériau | Absent. | — | M6 |
| REN-12 | Extensions de matériaux | P2 | **Absent** | — | — | Absent. | — | Après M10 (P2) |
| REN-13 | IBL et sondes de lumière | P1 | **Absent** | — | grep `IBL\|irradiance\|probe` : 0 | Absent. | — | M6 |
| REN-14 | Occlusion ambiante | P1 | **Absent** | — | grep `SSAO\|GTAO\|ssao` : 0 | Absent. | — | M7 |
| REN-15 | Réflexions et GI | P2 | **Absent** | — | — | Absent. | — | Après M10 (P2) |
| REN-16 | Éclairage précalculé | P2 | **Absent** | — | — | Absent (le mode `Baked` n'existe pas). | — | Après M10 (P2) |
| REN-17 | HDR et tonemapping | P0 | **Absent** | — | grep `tonemap\|HDR` : 0 ; cibles `RGBA8` | Pas de cible HDR ni de tonemapping. | — | M5 |
| REN-18 | Post-traitement courant | P1 | **Absent** | include/Graphics/Upscaling/IPostProcessPass.h (interface seule) | grep `bloom\|Bloom` : 0 | Interface de passe de post-traitement, aucun effet fourni. | — | M5 |
| REN-19 | Anticrénélage | P1 | **Partiel** | src/Graphics/Renderer.cpp:113 (séquence de Halton pour le jitter), `historyTarget`, `SetMotionVectorsEnabled` ; include/Graphics/Upscaling/ | Lu ; `renderers_tests` (5) verts | Infrastructure temporelle (jitter, historique, vecteurs de mouvement RG16F, mode d'upscaling bilinéaire `BilinearBlitUpscaleMode`) ; aucun SMAA, TAA, MSAA ni résolution dynamique par effet. | — | M5 |
| REN-20 | Effets coûteux optionnels | P2 | **Absent** | — | — | Absent. | — | Après M10 (P2) |
| REN-21 | Sorties auxiliaires (AOV) | P1 | **Partiel** | src/Graphics/Renderer.cpp:151-154 ; `SetMotionVectorsEnabled`, `SetDepthAsTextureEnabled` ; `IRenderTargetResource::ReadPixelUInt` | Lu | Vecteurs de mouvement et profondeur lisible **activés à la demande** ; identifiants R32UI lisibles. Pas de normales octaédriques, rugosité/métal, profondeur linéaire, émission ; pas de règle MSAA. | — | M5 |
| REN-22 | Boîte à outils de shaders | P1 | **Absent** | — | shaders/ : 8 fichiers, aucun `#include` | Pas de boîte à outils GLSL partagée. | — | M5 |
| REN-23 | Textures de bruit en compute | P1 | **Absent** | — | — | Absent (pas de compute). | — | M7 |
| REN-24 | Ciel et atmosphère | P1 | **Absent** | — | grep `atmosphere\|rayleigh\|fog` : 0 | Absent. | — | M7 |
| REN-25 | Brouillard | P1 | **Absent** | — | même grep | Absent. | — | M7 |
| REN-26 | Nuages volumétriques | P2 | **Absent** | — | grep `cloud` : 0 | Absent. | — | M7 |
| REN-27 | Eau | P2 | **Absent** | — | — | Absent. | — | M7 |
| REN-28 | Instancing | P0 | **Partiel** | src/Graphics/Mesh.cpp:124,212 (`instances`, `DrawIndexedInstanced`) | Lu | Données par instance possibles dans un maillage ; un draw call par `Mesh`, pas de regroupement par (maillage, matériau). | — | M5 |
| REN-29 | Rendu piloté par GPU | P1 | **Absent** | — | grep `DrawIndirect\|MultiDraw\|Hi-Z` : 0 | Absent. | — | M5 |
| REN-30 | Culling CPU | P0 | **Absent** | — | grep `Frustum\|BVH\|cull` : 0 (hors `CullMode`) | Absent. | — | M5 |
| REN-31 | LOD et imposteurs | P1 | **Absent** | — | grep `\blod\b` hors backends : 0 dans le code suivi (un stash « add LOD » existe sur feat/vulkan, non intégré, non lu) | Absent de `dev`. | — | M5 |
| REN-32 | Tri et ordre des transparents | P1 | **Absent** | — | — | Absent. | — | M5 |
| REN-37 | Modes de fusion des matériaux | P0 | **Absent** | include/Graphics/Core/RenderState.h : `SetAlphaBlend`, `SetPremultipliedAlphaBlend` (états globaux) | Lu | Pas de `BlendMode` par matériau ; seulement deux interrupteurs d'état global. | — | M5 |
| REN-38 | Réfraction et transmission | P2 | **Absent** | — | — | Absent. | — | M7 |
| REN-39 | Ombres des transparents | P1 | **Absent** | — | — | Absent. | — | M5 |
| REN-40 | Transparence indépendante de l'ordre (OIT) | P2 | **Absent** | — | — | Absent. | — | Après M10 (P2) |
| REN-41 | Transparents et sorties auxiliaires | P1 | **Absent** | — | — | Absent. | — | M5 |
| REN-33 | Particules et décalques | P1 | **Absent** | — | grep `particle\|decal` : 0 | Absent. | — | M7 |
| REN-34 | Dessin de debug | P0 | **Absent** | — | grep `DebugDraw\|DrawLine\|DrawBox` : 0 | Absent. | — | M3 |
| REN-35 | Modes d'affichage de debug | P1 | **Partiel** | include/Graphics/Camera.h : `SetWireframe` ; `GraphicsRenderState::SetWireframe` | Lu | Un seul mode (filaire) sur huit. | — | M5 |
| REN-36 | Rendu du texte | P0 | **Partiel** | src/UI/Rendering/TextRenderer.cpp (1 224 lignes) ; include/UI/Rendering/TextRenderer.h : `TextAnchor`, `TextLayoutParams`, `TextAlign`, `TextOverflow` | Lu ; texte visible dans la démo ; **aucun test** (grep `TextRenderer` dans tests : 0) | FreeType + atlas MSDF (msdfgen), ancrage, retour à la ligne, alignements, dépassement : oui. Passe par `Texture`/`Mesh` de l'abstraction actuelle, pas le RHI cible. Un octet à la fois (`unsigned char c`, `FT_Load_Char`, TextRenderer.cpp:343-347) : pas de décodage UTF-8. Dépend de `UI` depuis `Graphics/Renderer2D.cpp` (cycle). | — | M2 |

### E. Rendu 2D et mélange 2D/3D

| ID | Fonctionnalité | Prio | État | Où | Comment | Écart | Portabilité | Jalon |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| R2D-01 | Batch de sprites | P0 | **Partiel** | include/Graphics/Sprite.h, src/Graphics/Renderer2D.cpp : `DrawSprite`, `PushClipRect` | Lu ; 3 tests de sprite (move/expand/shrink) | Un sprite = un dessin ; pas de batch instancié, d'atlas, de tri par couche et matériau. | — | M5 |
| R2D-02 | Caméra 2D | P0 | **Partiel** | src/Graphics/Renderer2D.cpp : `BeginCanvasPass` ; `TextureFilterMode::Nearest` | Lu | Passe en coordonnées pixel et filtrage « plus proche » disponible ; pas de classe de caméra 2D ni d'échelle entière. | — | M5 |
| R2D-03 | Formes, lignes, texte | P1 | **Partiel** | src/Graphics/Renderer2D.cpp : `RenderText`, `DrawSprite` | Lu | Texte et rectangles (sprites) ; pas de cercles, lignes, polygones. | — | M5 |
| R2D-04 | Cartes de tuiles | P1 | **Absent** | — | grep `Tilemap\|Tile` : 0 | Absent. | — | M5 (proposé : non cité dans docs/05) |
| R2D-05 | Mélange 2D/3D | P1 | **Partiel** | src/Graphics/Renderer.cpp (interface dessinée après l'upscaling : profil `UIUpscale`) | Lu | Seul le mode « superposition » existe (UI après le rendu 3D) ; ni 2D monde avec profondeur, ni 2D vers texture. | — | M5 |
| R2D-06 | Plusieurs vues | P1 | **Absent** | — | — | Absent (un `Renderer3D` et un `Renderer2D` par fenêtre). | — | M5 |
| R2D-07 | Éclairage 2D | P3 | **Absent** | — | — | Absent (P3). | — | Après M10 (P3) |

### F. Contenu et animation

| ID | Fonctionnalité | Prio | État | Où | Comment | Écart | Portabilité | Jalon |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| ASSET-01 | Import glTF 2.0 | P0 | **Absent** | — | grep `gltf\|glTF\|KTX\|tinyobj` : 0 | Aucun import de modèle. | — | M6 |
| ASSET-02 | Textures | P0 | **Partiel** | src/Graphics/Texture.cpp:69 `stbi_load` ; `TextureCreateInfo::generateMipmaps` | Lu | PNG/JPG via stb, mips : oui. Pas d'espace colorimétrique (aucun format sRGB). | — | M6 |
| ASSET-03 | Textures compressées | P1 | **Absent** | — | — | Absent. | — | M6 |
| ASSET-04 | Outil de cuisson | P1 | **Absent** | — | grep `meshopt` : 0 | Absent. | — | M6 |
| ASSET-05 | Rechargement à chaud des assets | P1 | **Absent** | — | — | Absent. | — | M6 |
| ASSET-06 | OBJ | P2 | **Absent** | — | — | Absent. | — | Après M10 (P2) |
| ANIM-01 | Squelettes et skinning GPU | P1 | **Absent** | — | grep `skeleton\|skinning` : 0 | Absent. | — | M6 |
| ANIM-02 | Clips, mélange, machine à états | P1 | **Absent** | — | — | Absent. | — | M6 |
| ANIM-03 | Animation de propriétés | P1 | **Absent** | — | — | Absent. | — | M6 |
| ANIM-04 | Foules instanciées | P1 | **Absent** | — | — | Absent. | — | M6 |
| ANIM-05 | IK, morph targets, root motion | P2 | **Absent** | — | — | Absent. | — | Après M10 (P2) |
| ANIM-06 | Retargeting | P3 | **Absent** | — | — | Absent. | — | Après M10 (P3) |

### G. Terrain et monde

| ID | Fonctionnalité | Prio | État | Où | Comment | Écart | Portabilité | Jalon |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| TER-01 | Bruit | P0 | **Partiel** | include/Noise/Noise.h, src/Noise/Noise.cpp : `Noise::WhiteNoise/SmoothNoise/FractalNoise/PerlinNoise` ; include/Noise/Random.h : `PCGRandom` | Lu ; 10 tests `Noise` verts (même graine → mêmes valeurs ; graines différentes → valeurs différentes) | Bruit blanc, bruit lissé (interpolation de valeurs hachées), fBm : oui (CPU, `float`). `PerlinNoise` n'est pas un bruit de Perlin à gradients. Pas de Simplex, Worley, distorsion de domaine ; **pas de version GPU** ni de comparaison CPU/GPU. `Noise()` et `PCGRandom::init()` s'initialisent avec l'**horloge** (`high_resolution_clock`) : contraire à la règle 9 si utilisés sans graine. Conversion `float`→`int` du hachage : reproductibilité inter-plateformes non testée. | — | M3 |
| TER-02 | Génération par tuiles | P0 | **Partiel** | apps/demo/src/ProceduralGeneration/TerrainGeneration.cpp : `GeneratePerlinTerrain`, `GenerateFractalTerrain` ; Grid.cpp | Lu ; 8 tests `Procedural` verts | Une grille unique déterministe pour une graine. Pas de tuiles, pas d'asynchronisme, pas de dépendance (graine, coordonnées, paramètres). Le code est dans l'**application de démo**, et `TerrainGenerator` possède un `Mesh` (dépendance au rendu, contraire à `docs/03` §1 : `world` sans `renderer`). | — | M3 |
| TER-03 | Maillage à niveaux de détail | P0 | **Absent** | — | grep `quadtree\|clipmap` : 0 | Absent (grille régulière de 498 002 triangles). | — | M3 |
| TER-04 | Streaming | P0 | **Absent** | — | — | Absent. | — | M3 |
| TER-05 | Érosion | P1 | **Absent** | — | grep `erosion` : 0 | Absent (seul `GenerateCrater`). | — | M7 |
| TER-06 | Biomes | P1 | **Absent** | — | grep `biome` : 0 | Absent. | — | M7 |
| TER-07 | Texturage du terrain | P0 | **Absent** | apps/demo/src/ProceduralGeneration/Grid.cpp (couleur par sommet) | Lu | Pas de mélange pente/altitude ni texturage triplanaire. | — | M3 |
| TER-08 | Végétation et dispersion | P1 | **Absent** | — | — | Absent. | — | M7 |
| TER-09 | Planètes sphériques | P1 | **Absent** | — | — | Absent. | — | M7 |
| TER-10 | Eau, rivières, lacs | P2 | **Absent** | — | — | Absent. | — | M7 |
| TER-11 | Export | P0 | **Absent** | — | grep `stbi_write` et écriture OBJ/STL : 0 | Aucun export. | — | M3 |
| TER-12 | Interface de réglage | P0 | **Absent** | — | pas de widget curseur (UI-03) ni de réflexion | Aucune interface de réglage. | — | M3 |
| TER-13 | Lancer de rayon sur le terrain | P1 | **Absent** | — | — | Absent. | — | M3 |
| TER-14 | Grottes et voxels | P3 | **Absent** | — | — | Absent (P3). | — | Après M10 (P3) |

### H. Scène et simulation

| ID | Fonctionnalité | Prio | État | Où | Comment | Écart | Portabilité | Jalon |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| ECS-01 | Entités et composants | P1 | **Absent** | — | grep `ECS\|entt\|Entity` : 0 | Absent ; choix ECS maison/EnTT à trancher. | — | M8 |
| ECS-02 | Transformations hiérarchiques | P1 | **Absent** | — | — | Absent (transformations dans `Mesh`). | — | M8 |
| ECS-03 | Chargement et sauvegarde de scène | P1 | **Absent** | — | — | Absent. | — | M8 |
| SIM-01 | Boucle de simulation à pas fixe | P1 | **Absent** | — | — | Absent. | — | M8 |
| SIM-02 | Accès aux données du monde | P2 | **Absent** | — | — | Absent. | — | M8 |
| SIM-03 | Agents et écosystème | P3 | **Absent** | — | — | Hors moteur (P3). | — | Après M10 (P3) |

### I. Interface

| ID | Fonctionnalité | Prio | État | Où | Comment | Écart | Portabilité | Jalon |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| UI-01 | UIManager | P0 | **Partiel** | include/UI/Core/{Component,Container}.h : `ComponentBase`, `ContainerBase`, `ChainableComponent` (CRTP), `Component`, `Container` ; include/UI/Layout/{VBox,HBox}.h ; include/UI/Core/DeferredValue.h ; src/UI/Manager.cpp | Lu ; 14 tests `UI` verts ; `PerformanceView` visible dans la démo | Composant, conteneur, VBox/HBox, `DeferredValue<T>` à double tampon, mixin CRTP : présents et testés. « Migré sur le RHI » : non (dépend de `Graphics` actuel, cycle `UI`↔`Graphics`). `Manager` est un singleton (`Manager::Instance`). | — | M2 |
| UI-02 | Rendu indépendant du backend | P0 | **Absent** | src/UI/Core/Component.cpp, Container.cpp (créent des `Sprite` et appellent le rendu) | Lu | Les composants dessinent directement via sprites ; aucune liste de dessin intermédiaire, pas d'adaptateur vers un batch 2D. | — | M2 |
| UI-03 | Widgets de base | P0 | **Absent** | include/UI/Widgets/Text.h, Label.h (seuls widgets) | Lu ; grep `onClick\|hover\|click\|focus` : seulement `Bounds::isHover` | Aucun bouton, curseur, case à cocher, liste, champ de texte, etc. Seuls `Text` et `Label` (alias) existent. | — | M3 |
| UI-04 | Outils de debug | P1 | **Absent** | — | grep `imgui` : 0 | Absent. | — | M8 |
| UI-05 | Langage d'interface : compilateur vers C++ | P0 | **Partiel** | tools/cui-rs/src/{lexer,parser,validator,generator,scanner,registry,main}.rs ; apps/demo/src/UI/Views/PerformanceView.cui ; tools/engine-sdk/engine.mk:194-220 | Compilé et exécuté : `PerformanceView.cui` → `.gen.h/.gen.cpp` → démo ; 4 fichiers invalides essayés (docs/UI_LANGUAGE_AUDIT.md) | Compilation vers C++ sans coût d'exécution supplémentaire, dépendances de build, `@cui-cpp` : oui. Erreurs **ligne:colonne** du source : oui (nom du fichier présent à l'exécution `run`, absent du JSON `check`). **Manque** : événements (`onClick`…, 0 occurrence dans le compilateur ; `Text(...).onClick(go)` → « has no modifier »), liaison bidirectionnelle (`@Binding` prévu v0.3), erreurs C++ sans `#line` (les erreurs de compilation pointent le `.gen.cpp`), défaut confirmé : nom de l'en-tête généré selon le fichier mais `#include` selon le nom de vue (casse si différents). Aucun test du compilateur. | — | M3 |
| UI-06 | Mise en page complète | P0 | **Partiel** | include/UI/Layout/VBox.h, HBox.h ; include/UI/Core/{Bounds,Container}.h : `Value` (px, %), `Anchor`, `JustifyContent`, `OverflowMode`, padding, spacing | Lu ; 5 tests Bounds verts | VBox, HBox, marges, alignement, tailles px/% : oui. Pas de grille, de superposition (ZStack), de tailles flexibles/minimales. | — | M3 |
| UI-07 | Thèmes en JSON | P0 | **Partiel** | include/UI/Rendering/Theme.h, src/UI/Rendering/Theme.cpp : `Theme`, `Colors`, `IdentifierKind`, `PresetTheme::{LIGHT,DARK}` | Lu | Thème en C++ (11 couleurs nommées, préréglages clair/sombre, états `HOVER/PRESSED/DISABLED`). Pas de JSON (grep `json` dans src/UI : 0), pas de jetons de rayon/police/ombre, pas de changement à chaud. Des couleurs sont aussi codées dans les vues (`.color(0.05, 0.05, 0.08, 0.65)` dans PerformanceView.cui). | — | M3 |
| UI-08 | Rechargement à chaud de l'interface | P1 | **Absent** | — | grep `reload\|hot` : 0 | Absent. | — | M8 |
| UI-09 | Navigation et accessibilité de base | P1 | **Absent** | — | grep `focus\|tab` dans UI : 0 | Absent. | — | M8 |
| UI-10 | Inspecteur d'interface | P1 | **Absent** | — | — | Absent. | — | M8 |
| UI-11 | Spécification et tests du langage | P0 | **Partiel** | docs/UI_DSL_Spec.md (v0.2 « In Progress », en anglais) | Lu ; grep `#[test]` dans tools : 0 test Rust | Une spécification existe (exigences, grammaire, plan) mais pas `docs/UI_LANGUAGE.md`, et **aucun test de compilation** (valides/invalides). | — | M3 |
| UI-12 | Paramètres réfléchis → curseurs | P1 | **Absent** | — | dépend de CORE-12 (absent) | Absent. | — | M3 |

### J. Logique de jeu rechargeable

| ID | Fonctionnalité | Prio | État | Où | Comment | Écart | Portabilité | Jalon |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| GAME-01 | Module de jeu rechargeable | P1 | **Absent** | — | grep `dlopen\|LoadLibrary\|GameApi` : 0 ; `apps/demo/src/Game/Game.cpp` est lié statiquement | Absent. | — | M8 |
| GAME-02 | État côté moteur | P1 | **Absent** | — | grep `dlopen\|LoadLibrary\|GameApi` : 0 ; `apps/demo/src/Game/Game.cpp` est lié statiquement | Absent. | — | M8 |
| GAME-03 | Migration d'état | P1 | **Absent** | — | grep `dlopen\|LoadLibrary\|GameApi` : 0 ; `apps/demo/src/Game/Game.cpp` est lié statiquement | Absent. | — | M8 |
| GAME-04 | Registre des gestionnaires | P1 | **Absent** | — | grep `dlopen\|LoadLibrary\|GameApi` : 0 ; `apps/demo/src/Game/Game.cpp` est lié statiquement | Absent. | — | M8 |
| GAME-05 | Paramètres de gameplay en JSON | P1 | **Absent** | — | grep `dlopen\|LoadLibrary\|GameApi` : 0 ; `apps/demo/src/Game/Game.cpp` est lié statiquement | Absent. | — | M8 |
| GAME-06 | Construction incrémentale | P1 | **Absent** | — | grep `dlopen\|LoadLibrary\|GameApi` : 0 ; `apps/demo/src/Game/Game.cpp` est lié statiquement | Absent. | — | M8 |
| GAME-07 | Version finale | P1 | **Absent** | — | grep `dlopen\|LoadLibrary\|GameApi` : 0 ; `apps/demo/src/Game/Game.cpp` est lié statiquement | Absent. | — | M8 |

### K. Voxels et ray tracing

| ID | Fonctionnalité | Prio | État | Où | Comment | Écart | Portabilité | Jalon |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| EXT-01 | Plugin de rendu voxel | P2 | **Absent** | — | — | Absent (pas de graphe de passes, voir REN-01). | — | M10 |
| EXT-02 | Tracé de rayons logiciel sur voxels | P2 | **Absent** | — | — | Absent. | — | Après M10 (P2) |
| EXT-03 | Interface d'accélération de rayons | P2 | **Incompatible** | include/Graphics/AccelerationStructure.h, src/Graphics/AccelerationStructure.cpp ; `IAccelerationStructureResource` | Lu | Une façade existe, **sans construction BLAS/TLAS ni requête de rayon**, et Vulkan annonce `rayQuery = true` sans implémentation (voir RHI-18) : stub qui prétend marcher. | — | M2 (corriger les capacités) ; M10 |
| EXT-04 | Ray tracing matériel | P3 | **Absent** | — | — | Absent (P3). | — | Après M10 (P3) |
| EXT-05 | Traceur de chemin de référence | P3 | **Absent** | — | — | Absent (P3). | — | Après M10 (P3) |

### L. Outils, qualité et construction

| ID | Fonctionnalité | Prio | État | Où | Comment | Écart | Portabilité | Jalon |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| TOOL-01 | Visualiseur de terrain | P0 | **Partiel** | apps/demo/src/main.cpp, Game/Game.cpp, Game/World.cpp ; Camera::Inputs | Démo lancée | Caméra libre et terrain affichés ; pas de réglages (pas de widgets) ni d'export. Ce n'est pas un `terrain-viewer`. | — | M3 |
| TOOL-02 | Générateur en ligne de commande | P0 | **Absent** | — | — | Absent (nécessite PLAT-04). | — | M3 |
| TOOL-03 | Capture d'écran | P2 | **Absent** | include/Graphics/RenderTarget.h : `ReadPixelUInt`, `ReadPixelRGBA8` | grep `stbi_write` : 0 | Lecture de pixels possible, pas d'enregistrement d'image. **Désaccord** : P2 mais requis par RHI-14 (P0). | — | M2 (proposé : requis par RHI-14) |
| BLD-01 | Construction multiplateforme | P0 | **Partiel** | Makefile, mk/engine.mk, apps/demo/Makefile, vcpkg.json, README.md | Build Linux reproduit (docs/BASELINE, docs/releases/M00.md) | Makefile + vcpkg, types `debug/dev/release`, Linux vérifié (apt + vcpkg). Windows et macOS non vérifiés ici. Dépendances non épinglées (pas de `builtin-baseline`). Pas de CMake (proposition à faire, voir QUESTIONS). | — | M0 |
| REPO-01 | Dépôt propre et structuré | P0 | **Fait** | .gitignore, .gitattributes, .editorconfig, .clang-format, README.md, tools/check-repo.sh | `sh tools/check-repo.sh` : OK ; 102/102 tests et journal de démarrage identiques à la référence (docs/releases/M00.md) | Tous les points sont remplis ; `third_party/` n'existe pas car aucune bibliothèque n'est copiée (README de `src/ThirdParty/` l'explique) — écart assumé, à valider. | — | M00 (fait) |
| GIT-01 | Flux de branches | P0 | **Partiel** | docs/06 ; historique de la branche `dev` | `git log` lu | Flux appliqué depuis M00 (branches `chore/`, `fix/`, `docs/`, fusions `--no-ff`). Protections côté hébergeur non activées par l'auteur ; avant M00, 92 commits sur 178 hors format `type(ID): résumé`. | — | Continu |
| BLD-02 | Intégration continue | P0 | **Partiel** | .github/workflows/build-engine.yml | Lu | Matrice Windows/Linux/macOS (x64 et arm64) existe mais ne se lance que sur tag `v*` ou à la main, ne lance **aucun test** ni vérification des couches. | — | M0 |
| TEST-01 | Tests unitaires | P0 | **Partiel** | tests/framework/TestFramework.{h,cpp}, tests/suites/*.cpp ; `make demo-test` | Lancé : **102/102** verts | Framework **maison** (spec : Catch2 ou doctest), non exécuté en CI ; `core` partiellement couvert (Logger, InputManager, TextRenderer, Light, UI compiler : 0 test). | — | M0 |
| TEST-02 | Assainisseurs et analyse | P1 | **Partiel** | .clang-tidy, .clang-format, tools/format_sources.sh | Lu ; grep `sanitize` : 0 | Configurations présentes mais non exécutées ; pas d'ASan/UBSan ; 6 avertissements `-Wall -Wextra` (`-Wmissing-field-initializers` dans `BindFunc.h:137`, `BindMethods.h:319`). | — | M0 |
| TEST-03 | Tests de déterminisme | P0 | **Partiel** | tests/suites/noise_tests.cpp | Lancé | Déterminisme « même graine, mêmes valeurs » dans un processus ; pas de somme de contrôle de référence ni de comparaison inter-plateformes. | — | M3 |
| DOC-01 | Documentation | P0 | **Partiel** | src/*/README.md, docs/STATUS.md, docs/GRAPHICS_BACKENDS.md | Lu | README par module (M00) et STATUS existent. Manquent : schéma des modules, `docs/ADD_A_BACKEND.md`, mise à jour par jalon. | — | M0 puis continu ; ADD_A_BACKEND en M4 |

## Écarts avec la spécification (étape 6)

Voir `docs/QUESTIONS.md` § « Audit » : chaque point y est numéroté avec une proposition. Résumé : (1) le plan suppose un seul backend OpenGL existant alors que deux backends existent déjà sur une autre abstraction ; (2) arborescence et nommage de `docs/03` ≠ code existant ; (3) `-Werror` et `std::expected` vs code actuel ; (4) TEST-01 demande Catch2/doctest, 102 tests utilisent un framework maison ; (5) dépendances manquantes (JSON, SPIRV-Cross, VMA) ; (6) `TOOL-03` P2 requis par `RHI-14` P0 ; (7) `R2D-04`, `RHI-17`, `CORE-03/05/08`, `PLAT-05` sans jalon dans `docs/05` ; (8) règle « `world` sans `renderer` » violée par le terrain de la démo ; (9) cycles `Graphics`/`UI`/`Profiler` ; (10) singletons contre `docs/03` §7.

## Risques (étape 7), par gravité

| Gravité | Risque | Preuve |
| --- | --- | --- |
| Haute | Capacités Vulkan annoncées vraies sans implémentation (rayQuery, accélération, upscaling temporel) : un futur code qui « lit `Capabilities` » croira à tort | `VulkanGraphicsBackend.cpp:91-106` |
| Haute | Minuterie GPU Vulkan factice : les chiffres du profileur Vulkan (affichés à l'écran) sont des durées CPU | `VulkanGraphicsResources.cpp:2511-2535` |
| Haute | Dépendance circulaire `Graphics` ↔ `UI` ↔ `Profiler` : bloque la couche `rhi` propre | `src/Graphics/Renderer2D.cpp:5`, `include/Profiler/Profiler.h:11`, `src/Graphics/Window.cpp:8` |
| Haute | Génération procédurale seedée par l'horloge quand on omet la graine | `src/Noise/Noise.cpp:19`, `Random.cpp:6` |
| Moyenne | `Logger::logs` non borné (chaque entrée garde une pile en DEBUG) : fuite lente sur longue session | `Logger.cpp:396` |
| Moyenne | Exceptions dans le flux normal (`Bounds`, `LightManager::SetLight`, `ShaderProgram`, `Game`) | voir CORE-02 |
| Moyenne | Compilateur `.cui` : `#include` généré selon le nom de la vue, fichier écrit selon le nom du fichier | `UI_LANGUAGE_AUDIT.md` §Défauts |
| Moyenne | Windows : `Window` masque la barre des tâches (`FindWindowA`) ; un plantage la laisse masquée | `Window.cpp:118,277,333,460` |
| Moyenne | Singletons et états globaux (`GraphicsRuntime`, `ShaderLibrary`, `Manager`, `InputManager`, `GraphicsRenderState`) | `include/Graphics/Core/GraphicsRuntime.h` etc. |
| Basse | 6 avertissements `-Wmissing-field-initializers` ; `Libraries/…` supprimé mais `obj/…/Libraries/` nommé ainsi dans `mk/engine.mk` | build |
| Basse | Dépendances non épinglées (vcpkg sans baseline, apt selon la distribution) | `vcpkg.json` |
| Info | Code mort possible : `UI/UI.h`, `Graphics/Presentation/PresentationController.h` (conservés) ; module Benchmark non validé | M00 |

## Proposition d'ajustement du plan (étape 8)

Non appliquée, à valider (détail dans `docs/QUESTIONS.md`) :

1. **M2** devient plus gros que prévu : il faut réécrire **deux** backends (OpenGL et Vulkan, ce dernier existe déjà et marche) derrière le nouveau RHI. Proposition : M2 = RHI + OpenGL + migration de `UI`/`Renderer`/`TextRenderer`, avec **Vulkan migré à minima** (mêmes tests) ; M4 = parité, VMA, cache disque, validation. Alternative : garder l'API actuelle jusqu'à M3 et ne créer le RHI qu'après le premier terrain (risque : refaire deux fois).
2. **Casser les cycles avant M2** dans un jalon court M1 bis (ou dans M1) : sortir `Profiler` de `Graphics/Core`, sortir `TextRenderer` de `UI` vers `renderer`.
3. **TOOL-03 (capture d'écran) passe en M2**, avec RHI-14.
4. **Fixer la graine par défaut** (supprimer l'horloge) dès M1, avec CORE-12.
5. **Ajouter une CI Linux de tests dès M0** (workflow `pull_request`) avant d'activer les protections de branche.

