# PORTABILITY_REPORT : portabilité et organisation du rendu

Audit du 2026-11-17 (branche `docs/audit`, depuis `dev` après M00). **Rien n'a été corrigé pendant l'audit.** Chaque correction est une proposition avec un jalon et une estimation d'effort (en jours de travail d'une personne, ordre de grandeur, à confirmer).

## 1. Environnement mesuré

| Élément | Valeur |
| --- | --- |
| Système | Linux 7.2.15, Pop!_OS 24.04, x86_64 ; dépôt sur NTFS (`fuseblk`, sans bit exécutable) |
| Compilateur | g++ 13.3.0 (C++23) ; clang++ et clang-format absents |
| GPU / pilote | Intel Arc Graphics (Meteor Lake), Mesa 26.1.6 ; Vulkan 1.4.354 (instance 1.3.280), OpenGL core accéléré |
| Dépendances | apt : glfw 3.3.10, glm 0.9.9.8, stb, shaderc 2023.8, freetype, vulkan 1.3.280 ; vcpkg : glad (v1), msdfgen |
| Build | `make all-debug` : OK (6 avertissements) ; `make demo-test` : 102/102 |

## 2. Étape 1 : problèmes de portage Windows → Linux (liste de `CLAUDE.md` § Environnement)

| Sujet | Constat | État | Correction |
| --- | --- | --- | --- |
| Casse des `#include` | 1 seule faute sur 112 inclusions : `"Utilities.h"` vs `utilities.h` (`include/UI/Core/Container.h`) | **Corrigé en M00** | — |
| Casse des fichiers | Aucune collision, ni dans `HEAD` ni dans l'historique ; `core.ignorecase=true` hérité de Windows (réglé en local) | Corrigé (réglage local, non versionné) | Documenter dans le README (0,1 j) |
| Fins de ligne | Index LF, arbre de travail CRLF sur 186 fichiers ; aucun `.gitattributes` | **Corrigé en M00** (`.gitattributes`, `.editorconfig`) | — |
| Chemins en dur | Aucune lettre de lecteur ni chemin absolu dans le code suivi. Ressources par `GET_RESOURCE_PATH("shader/…")` = `"res/" "…"` relatif au dossier de l'exécutable (`include/Utilities/utilities.h:19-21`). Makefiles : `.exe`, `powershell`, `mklink` (28 occurrences dans `Makefile`, `mk/engine.mk`, `apps/demo/Makefile`) sous des tests de plateforme | OK sous Linux | Remplacer à terme par CMake/scripts (M0, 3 j) ; sinon conserver |
| Dépendances | vcpkg sous Windows ; sous Linux, apt + vcpkg (glad v1 et msdfgen n'existent pas dans apt). `vcpkg.json` sans `builtin-baseline` | **Contourné en M00** par `tools/setup-linux-deps.sh` | Épingler la baseline vcpkg ; envisager un manifeste vcpkg Linux complet (M0, 1 j) |
| `<stacktrace>` | GCC 13 : lien avec `-lstdc++_libbacktrace` ; GCC 14+ : `-lstdc++exp` ; utilisé par le `Logger` (DEBUG seulement) | **Corrigé en M00** (`tools/engine-sdk/engine.mk`) | Vérifier clang/libc++ et MSVC (`/std:c++latest`) en CI (M0, 1 j) |
| Compilateur | GCC 13.3 OK ; clang 17+ et MSVC non essayés ici | Non vérifié | CI (M0) |
| Liens manquants | `-lmsdfgen-core` absent de la liste Linux avant M00 | **Corrigé en M00** | — |
| `stb_image.h` | apt le place dans `/usr/include/stb/`, vcpkg à la racine des includes | Contourné (`setup-linux-deps.sh`) | — |
| Pilotes | OpenGL 4.3 core (`OpenGLGraphicsBackend.h:20-21`) et Vulkan 1.3 : tournent avec Mesa ; couche de validation Vulkan disponible | OK | Journaliser la version du contexte GL (0,2 j) |

## 3. Étape 4 : cartographie des appels dépendants d'une API ou d'un système

### 3.1 Destination : `rhi` (backends `src/rhi/<backend>/`)

Cible de la règle 4 de `CLAUDE.md` (« aucun `gl*`/`Vk*`/`MTL*` hors des backends »). État : **les appels sont tous sous `src/Graphics/Backends/<API>/`** (aucun `gl*(`, `vk*(` hors backends), mais des *types* et *constantes* d'API fuient dans le code commun.

| Où | Quoi | Pourquoi | Remplaçant | Effort |
| --- | --- | --- | --- | --- |
| `src/Graphics/Backends/OpenGL/` | 198 appels `gl*` (83 fonctions distinctes) dont 260 occurrences `GL_*`/`GLuint` dans `OpenGLGraphicsResources.cpp` | Implémentation OpenGL | Reste dans `src/rhi/opengl/` derrière la nouvelle interface | 8 j (réécriture sur `Device`/`CommandEncoder`) |
| `src/Graphics/Backends/Vulkan/` | 177 appels `vk*` (91 fonctions distinctes), 2 535 lignes dans `VulkanGraphicsResources.cpp`, sous-allocateur maison | Implémentation Vulkan | Reste dans `src/rhi/vulkan/` ; VMA ; barrières déduites | 12 j |
| `src/Graphics/Core/RenderState.cpp` (11 `GL_*`) | `OpenGLRenderState::BindFramebuffer(GL_FRAMEBUFFER, …)`, `GL_COLOR_BUFFER_BIT`, `GL_TRUE` dans du code « commun » | Le dispatch par API est fait par `if` dans le code commun | Supprimer ; l'état devient explicite dans les passes/pipelines | 2 j |
| `include/Graphics/Backends/OpenGL/*.h` (17 + 4 occurrences `GL*`) | En-têtes publics qui incluent des types GL | Les backends sont des en-têtes publics | Déplacer sous `src/rhi/opengl/` (privés) | 0,5 j |
| `include/Graphics/Core/GraphicsResources.h:139-141`, `Texture.h:54` | Commentaires `GL_COLOR_ATTACHMENT0` ; paramètres `std::uint32_t framebufferHandle` | Interface calquée sur OpenGL | Handles opaques, plus de `framebufferHandle` | Inclus dans RHI-01 |
| `shaders/*.vert|frag` (8 fichiers) | `#version 430 core` ; `layout(binding = N, std140)` sans `set` ; uniformes libres (`uniform vec4 color;`, `uniform sampler2D screenTexture;`, `uniform int wireframe;`) | GLSL OpenGL 4.3 | GLSL 450 « style Vulkan » avec `set`/`binding` ; constantes dans des UBO/push constants (`docs/03` §4-5) | 3 j |
| `src/Graphics/Backends/Vulkan/VulkanPipelineCache.cpp:104-191,566` | Réécriture **par chaînes** de `layout(binding)` → `layout(set = 0, …)` et `#version` → 450 avant shaderc | Contourner le GLSL non portable | Source unique GLSL 450 + compilation hors ligne + SPIRV-Cross (réflexion) | 4 j |
| `src/Graphics/ShaderLibrary.cpp:16` | Cache disque de **binaires de programmes GL** (`GL_PROGRAM_BINARY_FORMAT`) | Spécifique à OpenGL | Cache de SPIR-V / de pipelines par backend | 1,5 j |
| `src/Graphics/Backends/Vulkan/VulkanGraphicsBackend.cpp:91-106` | Capacités annoncées vraies à tort | Voir RHI-18 | `Capabilities` lues du périphérique | 0,5 j |

### 3.2 Destination : `platform` (fenêtre, entrées, horloge, fichiers)

| Où | Quoi | Remplaçant | Effort |
| --- | --- | --- | --- |
| `include/Graphics/Window.h`, `src/Graphics/Window.cpp` (40 appels `glfw*`) | Fenêtre et plein écran sur GLFW, exposés dans l'en-tête public | `platform::Window` + `createSurface(Backend)` (`docs/03` §6) | 3 j |
| `src/Graphics/Window.cpp:118,277,333,460` | `FindWindowA("Shell_TrayWnd")`/`ShowWindow` : masque la barre des tâches Windows | Supprimer (le plein écran sans bordure suffit) ou isoler dans `platform/windows` | 0,5 j |
| `include/Graphics/Core/WindowContext.h`, `src/Graphics/Core/WindowContext.cpp` | `GLFWwindow*` dans une interface « graphique » | Surface opaque fournie par `platform` | Inclus ci-dessus |
| `src/Utilities/InputManager.cpp` (4 appels `glfw*` + table de touches `GLFW_KEY_*`), `include/Utilities/InputManager.h` | Entrées GLFW, disposition par `GetKeyboardLayout` (Windows seulement) | `platform::Input`, détection de disposition portable (`glfwGetKeyName`) ; molette | 2 j |
| `src/Graphics/Camera.cpp` (5 appels `glfw*`) | `Camera::Inputs(GLFWwindow*)` lit les touches directement | Passer par `InputManager`/actions | 0,5 j |
| `apps/demo/src/Game/Game.cpp:1` | `glfwSetWindowShouldClose` | `platform::Window::requestClose` | 0,1 j |
| `src/Profiler/FPSCounter.cpp:5-8,245-320` | `windows.h` (`Sleep`, `timeBeginPeriod`) et branche `__linux__` pour le sommeil précis | `platform::Clock` | 1 j |
| `src/Utilities/Utilities.cpp:17-180` | `GetModuleFileNameA`, `SHGetFolderPathA`, `readlink(/proc/self/exe)`, `_NSGetExecutablePath`, `XDG_DATA_HOME`/`HOME` | `platform::Paths` + schémas `res://`/`user://` (CORE-11) | 1 j |
| Absents | DPI/échelle de contenu, mode sans fenêtre, manettes, IME, presse-papiers | `platform` | 4 j |

### 3.3 Destination : `core` (sans dépendance système)

| Où | Quoi | Remplaçant | Effort |
| --- | --- | --- | --- |
| `src/Utilities/Logger.cpp:11,24,218` | `windows.h` pour largeur de console ; séquences ANSI `\033[nF` pour réécrire une ligne | Sortie console séparée du formatage ; ANSI seulement si TTY | 1 j |
| `src/Noise/Noise.cpp:19`, `Random.cpp:6` | `high_resolution_clock` comme graine par défaut | Graine explicite obligatoire (règle 9) | 0,2 j |
| `src/Noise/Noise.cpp` | Hachage `int32_t(x * 113.0f)` et `std::cos(0.5f)` : résultats flottants dépendant du compilateur/FMA | Arithmétique entière ou bibliothèque de fonctions déterministes ; somme de contrôle de référence (TEST-03) | 2 j |

### 3.4 Système de build

| Où | Quoi | Remplaçant | Effort |
| --- | --- | --- | --- |
| `Makefile`, `mk/engine.mk`, `apps/demo/Makefile`, `tools/engine-sdk/engine.mk` | 28 occurrences de `powershell`/`mklink`/`.exe` ; liens de bibliothèques écrits en dur par plateforme (`_ENG_VCPKG_LIBS`) | Proposition (non appliquée) : CMake + presets en gardant le Makefile en enveloppe (BLD-01) | 3-5 j |
| `.github/workflows/build-engine.yml` | Se déclenche seulement sur tag/manuel ; aucun test | Workflow `pull_request` : build + `make demo-test` (Linux), puis Windows/macOS | 1-2 j |
| `apps/demo/installers/` | NSIS (Windows) et `.deb` : chemins adaptés à M00 sans test | Tester ou remplacer par CPack | 1 j |
| macOS | Aucune branche `__APPLE__` pour les fenêtres/contextes ; OpenGL macOS plafonne à 4.1 (spec : OpenGL hors macOS) ; `MetalGraphicsBackend` est un stub | Vulkan via MoltenVK (M9) | 3 j (M9) |

## 4. Étape 3 ter : organisation du rendu actuel

| Question | Réponse (preuve) |
| --- | --- |
| Qui décide l'ordre des passes ? | `Game::render()` (`apps/demo/src/Game/Game.cpp:150-165`) : `renderer3D.BeginPass → Clear → SetCamera → World::Render → EndPass (upscale)`, puis `rendererUI.BeginPass → UI::Manager::Render → EndPass`. L'ordre est dans le code de l'application ; `Renderer` n'impose que BeginPass/EndPass et la chaîne post-traitement/upscale/frame generation. |
| Qui décide des cibles de rendu ? | `Renderer::SetOutputResolution`, `SetRenderScale`, `SetMotionVectorsEnabled`, `SetDepthAsTextureEnabled` ; création dans `src/Graphics/Renderer.cpp:151-197` (RGBA8 [+ RG16F], profondeur optionnelle lisible). `Renderer3D` et `Renderer2D` possèdent chacun un `RenderTarget`. |
| Les shaders ? | Chargés par chemin (`GET_RESOURCE_PATH("shader/…")`) dans `Mesh::SetShader`, `Sprite::SetShader` ; compilés à l'exécution ; cache disque de binaires GL (`ShaderLibrary::Instance()`, singleton). Shaders en `#version 430`. |
| Les lumières ? | `LightManager` (`src/Graphics/Light.cpp`) : un SSBO (`BufferUsage::Storage`) lié au point 1 ; caméra en UBO au point 0 ; modèle au point 3 (`CAMERA_BINDING_POINT`, `MESH_MODEL_BINDING_POINT`). Constantes de liaison réparties dans les `.cpp`. |
| Les ressources GPU ? | `unique_ptr<I…Resource>` créées par `IGraphicsDevice` (obtenu par `TryGetActiveGraphicsDevice()`, global) et possédées par les façades `Mesh`, `Texture`, `ShaderProgram`, `Buffer`, `RenderTarget`. Pas de gestionnaire central. |
| Qui crée les contextes ? | `CreateGraphicsBackend` (`GraphicsBackend.cpp`), puis `OpenGLGraphicsBackend::Initialize` (`glfwInit`, hints du contexte 4.3 core, contexte de debug) ou `VulkanGraphicsBackend::Initialize` ; la fenêtre appelle `GraphicsWindowContext::Initialize(GLFWwindow*, …)` (`Window::Init`). Le runtime actif est un **global** (`BindGraphicsRuntime`). |
| États OpenGL modifiés globalement ? | Oui, par `GraphicsRenderState::*` (`BindFramebuffer`, `SetViewport`, `SetDepthTest`, `SetBlend`, `SetAlphaBlend`, `SetPremultipliedAlphaBlend`, `SetScissorTest`, `SetWireframe`, `ClearColor`) avec `CaptureFramebufferState`/`RestoreFramebufferState` pour sauvegarder/restaurer manuellement. Pas d'état de pipeline explicite. |

## 5. Synthèse d'effort et ordre proposé

| Lot | Jalon | Effort |
| --- | --- | --- |
| Hygiène de build et CI (CI sur PR, épinglage, vérifications des couches) | M0 | 6-9 j |
| Core : graines explicites, déterminisme, journal, erreurs | M1 | 5 j |
| Plateforme (fenêtre, entrées, chemins, horloge, DPI, sans fenêtre) | M2 | 12 j |
| RHI + backends (OpenGL, Vulkan migré) + shaders | M2/M4 | 30-35 j |
| macOS (MoltenVK) | M9 | 3 j |

Total hors rendu avancé : environ 55-65 jours-personne pour que **toutes** les dépendances d'API passent par `rhi` et `platform`. Ces chiffres sont des ordres de grandeur, pas des engagements.

## 6. Vérifications automatiques de `docs/03` §8 : état

| Contrôle | État |
| --- | --- |
| Couches | Absent (le script n'existe pas ; il échouerait à cause des cycles `Graphics`/`UI`/`Profiler`) |
| Isolation des API | Absent ; un `grep` manuel donne : aucun `gl*(`/`vk*(` hors backends ; références à GLFW ou à des symboles GL (types, constantes ou commentaires) dans 13 fichiers hors `Backends/` (Camera, Texture, Window, WindowContext, GraphicsResources, InputManager, ShaderLibrary, RenderState, Game…) |
| En-têtes publics autonomes | Non vérifié |
| Déterminisme | Absent |
| Images de référence | Absent |
