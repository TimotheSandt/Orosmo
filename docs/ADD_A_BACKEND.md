# Ajouter un backend au RHI

Guide pour brancher un nouveau backend graphique (Metal natif en M9, WebGPU, ...) sur le RHI d'Orosmo. Les deux backends existants
servent d'exemples : **OpenGL 4.3** (`src/rhi/opengl/`, **référence du comportement observable**) et **Vulkan 1.3**
(`src/rhi/vulkan/`, écrit en M4 selon ce même contrat). Principes généraux : `docs/03-ARCHITECTURE_ET_PORTABILITE.md` §3, §4, §9 ;
usage du RHI : `docs/RHI.md` ; détails de chaque backend : `src/rhi/README.md` (OpenGL) et `src/rhi/vulkan/README.md`.

> [!NOTE]
> Les points marqués « à vérifier » n'ont pas pu être confirmés dans le dépôt au moment de la rédaction. Ne les prends pas pour
> acquis : vérifie-les dans le code avant de t'y appuyer.

## 1. Le contrat

### 1.1 Ce qu'il faut fournir

Le RHI public est dans `include/orosmo/rhi/` (aucun type d'API graphique). Un backend fournit :

| Élément | Où c'est déclaré | Remarque |
| --- | --- | --- |
| Une classe dérivée de `rhi::Device` | `include/orosmo/rhi/device.hpp` | toutes les fonctions virtuelles pures (ci-dessous) |
| Une classe dérivée de `rhi::CommandEncoder` | idem | renvoyée par `Device::createCommandEncoder()`, rendue à `Device::submit` |
| `createXxxDevice(const DeviceDesc &)` | modèle : `gl::createGlDevice` (`src/rhi/opengl/gl_device.hpp`), `vk::createVulkanDevice` (`src/rhi/vulkan/vk_device.hpp`) | renvoie `Result<std::unique_ptr<Device>>` |
| `applyWindowHints(const DeviceDesc &)` | modèle : `gl::applyWindowHints`, `vk::applyWindowHints` | appelée par `rhi::prepareWindowHints` avant la création de la fenêtre (OpenGL : 4.3 core, contexte de debug ; Vulkan : `GLFW_NO_API`) |
| Une valeur de `Backend` | `include/orosmo/rhi/types.hpp` : `Backend::{kVulkan, kOpenGL, kMetal}` | `kMetal` existe déjà ; WebGPU demande un nouvel enregistrement (et `toString`, à vérifier) |
| La table de formats | modèles : `src/rhi/opengl/gl_translate.hpp`, `src/rhi/vulkan/vk_translate.hpp` | `Format` du RHI -> format natif ; les informations indépendantes de l'API sont dans `src/rhi/format.cpp` (`formatInfo`) |

**Fonctions de `Device`** (liste de `device.hpp`) : `capabilities`, `createBuffer` / `createTexture` / `createSampler` /
`createShader` / `createGraphicsPipeline` / `createComputePipeline` / `createBindGroup` / `createQueryPool`, les sept `destroy(...)`,
`textureDesc`, `updateBuffer`, `updateTexture`, `readBuffer`, `readTexture`, `timestampsReady`, `readTimestamps`,
`createCommandEncoder`, `submit`, `beginFrame`, `present`, `resize`, `setVSync`, `waitIdle`, `validationErrorCount`, `stats`.

**Fonctions de `CommandEncoder`** : `beginRenderPass` / `endRenderPass`, `setPipeline`, `setBindGroup`, `setVertexBuffer`,
`setIndexBuffer`, `setPushConstants`, `setViewport`, `setScissor`, `draw`, `drawIndexed`, `drawIndirect`, `drawIndexedIndirect`,
`bindComputePipeline`, `dispatch`, `dispatchIndirect`, `copyBuffer`, `copyBufferToTexture`, `copyTextureToBuffer`, `generateMips`,
`beginDebugGroup` / `endDebugGroup`, `writeTimestamp`, `error`.

### 1.2 Contrat observable (celui du backend OpenGL)

Le critère de réussite est le **même comportement observable** que `src/rhi/opengl/` (`gl_device.cpp`, `gl_encoder.cpp`) : mêmes
résultats, mêmes codes d'erreur, mêmes validations. Ce qui s'en déduit des en-têtes et des deux README :

- **Handles opaques** (`include/orosmo/rhi/handle.hpp` : `Handle<Tag>`, `HandlePool<Tag, T>`) : indice + génération. `destroy` rend le
  handle périmé **tout de suite** ; un handle périmé est refusé, jamais déréférencé. La libération réelle est **différée** (OpenGL :
  2 images + clôture `glFenceSync` ; Vulkan : série du sémaphore à chronologie). `DeviceStats::pendingDestruction` compte les
  libérations en attente.
- **Erreurs** : `Result<T>` (`std::expected`), jamais d'exception. Codes utilisés : `ErrorCode::kUnsupported` (`createDevice` si le
  pilote ou le GPU ne convient pas, avec la raison), `kInvalidArgument`, `kBusy` (`beginFrame` sur une fenêtre de taille nulle :
  l'appelant saute l'image).
- **Encodeur** : une commande invalide est ignorée et journalisée ; la **première** erreur est conservée (`CommandEncoder::error()`)
  et renvoyée par `Device::submit`. Une passe ou un groupe de debug laissé ouvert est une erreur (refermé quand même).
- **Barrières** : le RHI les calcule (docs/03 §3, règle 1). Aucune barrière manuelle côté moteur. Une implémentation conservatrice est
  acceptée (Vulkan : `memoryBarrierAll` ; OpenGL : `glMemoryBarrier(GL_ALL_BARRIER_BITS)`).
- **Un seul fil** : le périphérique et ses encodeurs s'utilisent depuis le fil créateur. Une seule file.
- **Surface** (`SurfaceDesc`) : `nativeWindow` nul = périphérique **hors écran** (backbuffer = texture RGBA8 de `surface.size`, utilisé
  par les tests) ; sinon la fenêtre est **adoptée** (le RHI ne la détruit pas) et créée après `prepareWindowHints`. `beginFrame`
  renvoie la texture de fond ; `present()` l'affiche. Le backbuffer est une texture RGBA8 du périphérique (la chaîne d'échange
  Vulkan reçoit une copie).
- **Validation** (`DeviceDesc::validation`) : l'API de debug du backend est activée et **chaque message d'erreur est compté** dans
  `validationErrorCount()`.
- **Noms de debug** : `debugName` des descriptions -> étiquettes natives ; `beginDebugGroup` / passes nommées -> groupes de debug natifs.

### 1.3 Repère A26 (`docs/QUESTIONS.md`, appliqué en M3)

- **Origine en haut à gauche** : la ligne 0 d'une texture (envoi, lecture, rendu) est son **haut** ; viewport et ciseau partent du haut ;
  `readTexture` rend d'abord la première ligne = haut, sans retournement.
- **NDC** : x vers la droite, **y = +1 en haut**, profondeur dans **[0, 1]** (caméra en reverse-Z : effacement de profondeur à 0).
- **Faces** : triangle anti-horaire en NDC = face avant (`FrontFace::kCounterClockwise`).
- OpenGL l'obtient par `glClipControl(GL_UPPER_LEFT, GL_ZERO_TO_ONE)` (refus clair de `createDevice` sans 4.5 ni
  `GL_ARB_clip_control`) ; Vulkan par un viewport à hauteur négative (`{x, y + h, w, -h}`). **Metal / WebGPU : à vérifier** (origine du
  viewport, du ciseau et des textures ; profondeur [0, 1] native ; y inversé du clip-space possible selon l'API).

### 1.4 Groupes de liaison

`include/orosmo/rhi/types.hpp` : `kGroupCount = 3` (0 image, 1 matériau / passe, 2 objet), `kBindingsPerGroup = 8`,
`kPushConstantBinding = kGroupCount * kBindingsPerGroup` (= 24). `BindGroupDesc` = liste de `BindGroupEntry` (`binding`, `type`
parmi `BindingType`, tampon + `offset` + `size`, `dynamicOffset`, texture + `sampler`, `mipLevel` pour les images de stockage).

- groupe = `set`, liaison = `binding` du GLSL 450 ; `setBindGroup(slot, group, dynamicOffsets)` consomme une valeur de
  `dynamicOffsets` par entrée `dynamicOffset`, **dans l'ordre des entrées**.
- Push constants : jusqu'à `Capabilities::maxPushConstantSize` (128 octets) ; émulés par un UBO sur OpenGL (binding 24).
- Traduction OpenGL : emplacement plat = `groupe * 8 + binding` (UBO, SSBO, unités de texture, unités d'image). Vulkan : un
  `VkDescriptorSet` par groupe, disposition **tirée de la réflexion SPIR-V du shader** (`ShaderInterface`), tampons dynamiques.
  Metal : tables d'arguments ou liaisons directes générées par SPIRV-Cross (docs/03 §4) ; **à vérifier**.
- Les liaisons se prennent en compte au dessin ou au dispatch : l'ordre `setPipeline` / `setBindGroup` est libre (Vulkan l'impose par
  construction ; OpenGL le respecte aussi).

### 1.5 Shaders

Source unique en **GLSL 450 « style Vulkan »** (`layout(set = N, binding = M)`) dans `shaders/rhi/`, compilée par shaderc vers
SPIR-V, réflexion et traduction par SPIRV-Cross (`include/orosmo/rhi/shader_compiler.hpp`, `src/rhi/shader_compiler.cpp` :
`compileGlsl`, `translateToGlsl430`, `ShaderCache`, `createShaderFromGlsl`, `ShaderHotReloader`).

- `ShaderDesc::language` : `ShaderLanguage::kGlsl` (GLSL 430, OpenGL) ou `kSpirv`.
- Un backend qui consomme du SPIR-V (Vulkan, MoltenVK) annonce **`Capabilities::spirvShaders = true`** : `createShaderFromGlsl` lui
  livre alors du SPIR-V directement. Sinon (OpenGL aujourd'hui : `spirvShaders = false`), le pipeline traduit en GLSL 430.
- Un backend qui veut un autre langage (MSL pour Metal natif, WGSL pour WebGPU) : **à vérifier / à concevoir**. Il n'existe pas de
  valeur de `ShaderLanguage` pour MSL ni WGSL ; propose-la dans `docs/QUESTIONS.md` (règle 7 de `CLAUDE.md`) avant de changer l'enum.
  SPIRV-Cross sait émettre du MSL (docs/03 §5) ; il faudra ajouter la traduction à `src/rhi/shader_compiler.cpp`.
- Un même shader ne doit **jamais** se comporter différemment selon le backend.

### 1.6 Capacités (`include/orosmo/rhi/capabilities.hpp`)

Remplis `Capabilities` honnêtement dans le constructeur du périphérique : `backend`, `deviceName`, `driverVersion`, limites
(`maxTextureSize`, `max3DTextureSize`, `maxColorAttachments`, `maxComputeWorkGroupSize/Count`, `maxComputeInvocations`,
`minUniformAlignment`, `minStorageAlignment`, `maxUniformBufferSize`, `maxPushConstantSize`, `maxStorageBufferBindings`), fonctions
courantes (`compute`, `indirectDraw`, `multiDrawIndirect`, `timestampQueries`, `anisotropy`, `bc7Textures`, `wireframe`,
`spirvShaders`, `shaderFloat64`) et capacités optionnelles réservées (`rayQuery`, `accelerationStructure`, `bindless`, `meshShader`,
`bufferDeviceAddress`, `sparseResources`). Ces dernières sont **toutes `false`** aujourd'hui. Règle 11 de `CLAUDE.md` : le moteur les
lit, ne les suppose jamais ; un drapeau à `true` doit être couvert par un test. Ne fais jamais mentir un drapeau (l'ancien backend
Vulkan annonçait de fausses capacités, `docs/AUDIT_REPORT.md`, RHI-18).

## 2. Étapes

### 2.1 Arborescence `src/rhi/<backend>/`

Reprends le découpage de Vulkan, qui est le plus lisible :

```
src/rhi/<backend>/
  README.md              rôle, exigences, limites connues (règle 8 de CLAUDE.md)
  <x>_device.hpp         CONTRAT INTERNE : structures des ressources, classe du périphérique, qui possède quoi
  <x>_device.cpp         création, GPU, capacités, libération différée
  <x>_resources.cpp      tampons, textures, échantillonneurs, requêtes de temps, update* / read*
  <x>_pipelines.cpp      shaders, pipelines, groupes de liaison
  <x>_encoder.{hpp,cpp}  CommandEncoder + Device::submit
  <x>_swapchain.{hpp,cpp} surface, image de fond, beginFrame / present / resize / setVSync
  <x>_translate.hpp      tables RHI -> natif
```

Les en-têtes de `src/rhi/<backend>/` sont **privés** : seul `src/rhi/device.cpp` inclut `<backend>/<x>_device.hpp`. Les fichiers
OpenGL sont `gl_device.{hpp,cpp}`, `gl_encoder.cpp`, `gl_translate.hpp`. Un fichier Objective-C++ (`.mm`) pour Metal : **à vérifier**,
la règle de sources de `mk/engine.mk` ne cherche que `*.cpp` et `*.c`.

### 2.2 Enregistrement dans `src/rhi/device.cpp`

Trois endroits, dans l'ordre du fichier (la compilation n'est pas conditionnelle : un backend est toujours dans `librhi.a`) :

1. `#include "<backend>/<x>_device.hpp"` en tête.
2. `isBackendCompiled(Backend)` : ajouter le backend à la condition (aujourd'hui `kOpenGL || kVulkan`). Les tests s'en servent pour
   se sauter sur une build sans le backend.
3. `prepareWindowHints(const DeviceDesc &)` : appeler `<ns>::applyWindowHints(desc)` pour ce backend.
4. `createDevice(const DeviceDesc &)` : un `case Backend::<k>:` du `switch` qui appelle `<ns>::create<X>Device(desc)`. Sortie par
   défaut : `ErrorCode::kUnsupported` « RHI backend '...' is not compiled in this build » (à garder pour les backends absents ;
   `kMetal` y tombe aujourd'hui).

Le choix à l'exécution côté démo passe par `--api <nom>` (`apps/demo/src/launch_options.cpp`) ; ajoute le nom si besoin (à vérifier
jusqu'où va l'énumération des API).

### 2.3 Build et dépendances

- **Sources** : `mk/engine.mk` définit `Rhi_SRCS := $(shell find $(LIBRARIES_SRC_DIR)/rhi -type f \( -name "*.cpp" -o -name "*.c" \))`.
  Tout `.cpp` placé sous `src/rhi/` (donc `src/rhi/<backend>/`) est compilé et archivé dans `librhi.a` (`$(LIB_RHI): $(Rhi_OBJS)`) **sans
  rien modifier** ; ne l'écris pas à la main dans le makefile.
- **Liaison** : les bibliothèques de l'application sont listées dans `tools/engine-sdk/engine.mk` (`_ENG_VCPKG_LIBS` par plateforme :
  `-lvolk -lglfw3 -lglad -lGL ...` sous Linux ; blocs Windows et Darwin séparés, ce dernier avec `-framework OpenGL ...`). Ajoute ici les
  bibliothèques ou frameworks du nouveau backend (pour Metal : `-framework Metal -framework QuartzCore`, **à vérifier**). Pour garder
  la liaison optionnelle sur une plateforme, protège avec le bloc de plateforme existant.
- **vcpkg** : dépendances déclarées dans `vcpkg.json` (`glad`, `spirv-cross`, `volk`, `vulkan-memory-allocator` ; `shaderc`, `glfw3`,
  `doctest`... sous `"platform": "!linux"` car Linux les prend en paquets système, `tools/setup-linux-deps.sh`). Ajoute la bibliothèque
  du backend dans `vcpkg.json`, puis `vcpkg install` à la racine ; documente la commande exacte (règle d'environnement de `CLAUDE.md`).
- Vulkan se charge à l'exécution par **volk** : le moteur ne se lie pas au chargeur. Préfère ce modèle (chargement dynamique et
  `kUnsupported` clair en cas d'absence) à une liaison dure, pour qu'un binaire démarre sur une machine sans le pilote.
- Aucune `#define` de sélection de backend n'a été trouvée : à vérifier avant d'en introduire une.

### 2.4 Isolation des API (`make check`)

`make check` = `check-repo check-layers check-api check-headers`. `check-api` lance `tools/check_api_isolation.py --strict` : les motifs
`gl*`, `GL_*`, `Vk*`, `vk*`, `VK_*`, `MTL*`, `glfw*`, `GLFW*`, `<vulkan/`, `<glad/`, `<GL/`, `<d3d`, `<windows.h>` sont interdits hors
des dossiers `api_isolation.allowed` de `tools/layers.json` : `src/rhi/`, `src/platform/`, `include/orosmo/platform/`. Un nouveau
backend sous `src/rhi/<backend>/` est donc **déjà autorisé**. Ne laisse **aucun** type natif dans `include/orosmo/rhi/` ni dans une
couche supérieure. Les motifs `MTL*` sont déjà prévus pour Metal. Un autre préfixe (WebGPU : `wgpu*`, `WGPU*`) n'est pas surveillé : à
ajouter à `PATTERNS` de `tools/check_api_isolation.py` (à vérifier).

### 2.5 Couches (`tools/layers.json`)

`src/rhi/*` est au niveau `rhi` (2) : un fichier n'inclut que `core` (0), `platform` (1) ou `rhi`. Un backend ne doit **pas** inclure
`renderer` ni plus haut. La seule rencontre `platform` / `rhi` est la création de la surface : le backend consomme le descripteur
opaque fourni par `platform` (`include/orosmo/platform/window.hpp`), il ne déduit rien d'une couche supérieure. `make check-layers`
(`tools/check_layers.py --strict`) le vérifie.

## 3. Tests

### 3.1 Tests communs du RHI

Les tests de `tests/rhi/` sont la spécification exécutable :

- `handle_tests.cpp` (CPU) ; `window_surface_tests.cpp` ; `shader_compiler_tests.cpp` ;
- `opengl_device_tests.cpp`, `opengl_state_tests.cpp`, `opengl_backend_tests.cpp` : **propres à OpenGL** ;
- `vulkan_device_tests.cpp` : propre à Vulkan (suite `rhi/vulkan/device`), construit son périphérique par `isBackendCompiled(...)` puis
  `createDevice` avec `desc.backend = Backend::kVulkan`, et se **saute** (`SKIP_TEST_IF_NULL`) si le pilote manque.
- `gpu_fixture.hpp` : aides partagées (struct `Gpu(backend)`, ressources de test, lecture de pixels).

- `rhi_device_tests.cpp` : la suite **commune aux backends**. Chaque `TEST_CASE` boucle sur `FOR_EACH_BACKEND(backend)` (défini dans
  `tests/rhi/backends.hpp`) et crée son périphérique par `Gpu gpu(backend)` ; elle vérifie le contrat du RHI (tailles, pixels lus,
  `ErrorCode`, couches de validation) avec les mêmes assertions pour tous. Un backend non compilé ou sans pilote est sauté
  (`MESSAGE("Skipped: ...")`) ; les capacités optionnelles sont lues dans `Capabilities` (`timestampQueries`, calcul...) et le test se saute si elles manquent.
- `backends_tests.cpp` : teste la liste des backends elle-même. `ENGINE_TEST_BACKENDS=opengl,vulkan` restreint (et désigne) les
  backends testés, sans variable ce sont tous ceux compilés ; avec `ENGINE_TEST_REQUIRE_GPU=1`, un backend désigné mais indisponible fait échouer.

Ajouter un backend : l'ajouter aux candidats de `tests/rhi/backends.hpp` (si `Backend` ne le nomme pas encore, l'enum du RHI d'abord) ; **`rhi_device_tests.cpp` et les suites `renderer/*` le
testent alors sans autre écriture**. Les tests propres au backend (état natif, extensions) vont dans un fichier à part :

1. Écris `tests/<...>/<backend>_device_tests.cpp` sur le modèle de `vulkan_device_tests.cpp` : une fonction `make<Backend>()` qui
   renvoie `nullptr` (avec `MESSAGE`) si le backend n'est pas compilé ou si `createDevice` échoue, et `SKIP_TEST_IF_NULL`.
2. Rends tes assertions identiques à celles d'OpenGL : mêmes tailles, mêmes pixels lus, mêmes `ErrorCode`.
3. Les suites GPU doivent se déclarer dans `GPU_TEST_SUITES` du `Makefile` (aujourd'hui `rhi/opengl*,rhi/shader/opengl,renderer/*,...`)
   pour être exclues de `make demo-test-unit` et lancées par `make demo-test-gpu`. Ajoute ton préfixe de suite (`rhi/<backend>*`).
   `rhi/vulkan*` n'y figure pas : **à vérifier** (omission ou choix).
4. `ENGINE_TEST_REQUIRE_GPU=1` fait **échouer** au lieu de sauter un test GPU sans pilote : à utiliser en CI.

Commandes : `make demo-test-unit` (sans GPU), `make demo-test-gpu`, `make demo-test` ; sous Linux sans affichage,
`xvfb-run -a make demo-test` (llvmpipe fournit GL 4.5 ; lavapipe pour Vulkan).

### 3.2 Couches de validation : 0 erreur

Chaque test finit par `CHECK(device->validationErrorCount() == 0)`. Active la validation native du backend par
`DeviceDesc::validation` (Vulkan : `VK_LAYER_KHRONOS_validation` + validation de synchronisation, coupée par
`ENGINE_VULKAN_SYNC_VALIDATION=0` ; OpenGL : `GL_KHR_debug`). Metal : couche de validation de l'API Metal (**à vérifier**). Compte
les messages d'**erreur** et d'**avertissement**. La cible est **0 message**, sur la suite complète et sur la démo.

### 3.3 Images de référence

- La démo capture `scene.png` et `ui.png` : `apps/demo/bin/debug/main --api <nom> --capture <dossier> [--capture-frames N] [--headless]`
  (cf. `CLAUDE.md`). Sous Mesa : `LIBGL_ALWAYS_SOFTWARE=1`.
- `python3 tools/compare_png.py REFERENCE CANDIDAT [--tolerance T] [--max-diff-pixels N] [--max-diff-percent P] [--diff-out FICHIER]`
  compare deux PNG ; sortie non nulle au-delà du seuil, image de différence avec `--diff-out`.
- Compare la capture de ton backend à celle d'OpenGL, avec une tolérance adaptée au pilote (llvmpipe, GPU réel, MoltenVK).
- Le but de RHI-14 (aucune différence selon le backend) n'est que **Partiel** (`docs/05`, `docs/GAPS.md`) : les images de référence
  par backend et le rapport de différence publié restent à produire. Si tu les ajoutes, enregistre aussi la tolérance et le GPU dans
  `docs/BASELINE/`.
- Preuve du repère A26 : le test « A26: a counter-clockwise triangle... » (`tests/rhi/opengl_state_tests.cpp`) est à rejouer sur le
  nouveau backend.

## 4. Liste de contrôle finale

- [ ] `Device` et `CommandEncoder` entièrement implémentés (aucune fonction virtuelle laissée en stub qui prétend marcher ; sinon `kUnsupported` + ligne dans `docs/GAPS.md`).
- [ ] `create<X>Device`, `applyWindowHints`, `isBackendCompiled`, `prepareWindowHints`, `createDevice` branchés dans `src/rhi/device.cpp`.
- [ ] `Capabilities` remplie honnêtement ; `spirvShaders` correct ; capacités optionnelles lues, pas supposées.
- [ ] Table de formats (`<x>_translate.hpp`) couvrant tous les `Format` du RHI, ou refus explicite avec `kUnsupported`.
- [ ] Repère A26 : lignes 0 en haut, viewport / ciseau depuis le haut, y = +1 en haut, profondeur [0, 1], sens des faces.
- [ ] 3 groupes x 8 liaisons ; décalages dynamiques dans l'ordre des entrées ; push constants jusqu'à 128 octets.
- [ ] Handles périmés refusés ; libération différée correcte (aucune fuite : `DeviceStats` revient à 0 à l'arrêt).
- [ ] Surface hors écran ET fenêtre ; `beginFrame` renvoie `kBusy` pour une taille nulle ; `resize` et `setVSync`.
- [ ] Validation native à 0 message sur toute la suite ; `validationErrorCount()` fonctionne.
- [ ] Tests de backend écrits, sautés proprement sans pilote, ajoutés à `GPU_TEST_SUITES` ; `make demo-test` vert.
- [ ] Capture `scene.png` comparée à la référence par `tools/compare_png.py`.
- [ ] `make check` vert (`check-repo`, `check-layers`, `check-api`, `check-headers`).
- [ ] Dépendances ajoutées à `vcpkg.json` et à `tools/engine-sdk/engine.mk`, commande d'installation documentée.
- [ ] `src/rhi/<backend>/README.md` écrit ; `src/rhi/README.md`, `docs/RHI.md`, `docs/STATUS.md`, `docs/GAPS.md` mis à jour.
- [ ] Linux ET Windows : la compilation de l'autre plateforme n'est pas cassée, ou l'écart est noté dans `docs/GAPS.md`.

## 5. Pièges connus (README OpenGL et Vulkan)

**OpenGL**
- **État global implicite** : lier une texture pour `updateTexture` ou `generateMips` écrase une liaison utilisateur. Le backend réserve
  une unité (24, `kScratchTextureUnit`), retire les tampons de sommets / d'indices du VAO au changement de pipeline et en fin de passe,
  remet la plage de profondeur à [0, 1] à chaque passe, et rend la liaison de framebuffer trouvée après une lecture synchrone.
- **Exécution immédiate** : l'encodeur OpenGL exécute chaque commande à l'appel ; un `draw` valide a déjà agi quand `submit` est appelé.
  Une commande invalide est refusée **avant** tout appel natif. Une passe dont une valeur d'effacement est refusée ne touche à
  aucune cible (valide tout avant le premier effacement).
- **Débordements** : plages de copies et de commandes indirectes vérifiées sans débordement arithmétique ; décalages et pas indirects
  multiples de 4 ; copie tampon <-> texture sans remplissage après la dernière ligne ; `updateTexture` lit des lignes serrées
  (`bufferRowLength` ne vaut que pour tampon <-> texture).
- **Fenêtre** : framebuffer 0 affiché ligne 0 en bas, donc passes en origine basse et rectangles / lectures retournés par le backend ;
  sRGB du visuel coupé (`GL_FRAMEBUFFER_SRGB`) ; backbuffer unique attachement de sa passe, sans profondeur ni échantillonnage.
- **Limites** : `GL_MAX_IMAGE_UNITS` peut valoir 8 (le groupe 0 est le choix portable pour les images) ; 8 SSBO garantis seulement
  (`maxStorageBufferBindings` ; le périphérique se crée même sous 24).
- **Contexte** : fenêtre GLFW invisible 1x1 hors écran ; GLFW jamais initialisé ni terminé par une fenêtre adoptée.

**Vulkan**
- **Volk** : fonctions globales, **un seul pilote par processus** ; sans chargeur, `kUnsupported` (les démos retombent sur OpenGL).
- **Vulkan 1.3 exigé** (rendu dynamique, `synchronization2`, sémaphore à chronologie, `hostQueryReset`) : un GPU incomplet est écarté
  avec la liste dans le message. `ENGINE_VULKAN_DEVICE` force un GPU ; discret, puis intégré, puis processeur (lavapipe).
- **Viewport à hauteur négative** pour garder y = +1 en haut et `kCounterClockwise` = `COUNTER_CLOCKWISE`.
- **Dispositions « de repos »** : après chaque commande, chaque texture revient à `TextureRes::restLayout` (`UNDEFINED` -> repos par
  `flushPending`, appelé avant toute soumission) ; aucun état de layout partagé entre encodeurs.
- **Groupes de liaison sans disposition propre** : la disposition vient du shader ; tout tampon est `*_BUFFER_DYNAMIC` ; une entrée non
  dynamique reçoit le décalage 0. Les entrées en trop sont ignorées ; une liaison absente ou d'un autre type -> `kInvalidArgument`.
- **Liaison au dessin / dispatch** : les jeux de descripteurs sont liés au dessin, pas à `setBindGroup`.
- **Pipelines** : rendu dynamique ; la passe doit avoir les mêmes formats que le pipeline (`kInvalidArgument` à `setPipeline`).
- **Transferts** : `update*` est ordonné avec les soumissions via un lot, pas à la position d'un encodeur déjà enregistré ; `read*` bloque.
- **Présentation** : `beginFrame` attend la série N - 2 ; fenêtre créée avec `GLFW_NO_API` ; le format de la chaîne (BGRA / RGBA) ne
  regarde pas le moteur grâce à la copie (blit).
- **Limites connues** : barrières conservatrices, une file, dispositions par texture entière, `D24S8` remplacé par `D32S8` si absent,
  écritures de stockage dans une passe de rendu non prises en charge, couleurs de bord d'échantillonneur limitées à trois.

**Pour tous** : n'écris jamais « terminé » de mémoire (règle 1 de `CLAUDE.md`) : donne le fichier, le test et son résultat.
