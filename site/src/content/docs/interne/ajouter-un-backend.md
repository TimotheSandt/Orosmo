---
title: "Ajouter un backend"
description: "Brancher un nouveau backend graphique (Metal, WebGPU...) derrière le RHI : ce qu'il faut implémenter, où l'enregistrer, la règle d'isolation des API, les tests et la liste de contrôle."
sidebar:
  order: 7
---

Un backend implémente l'interface du RHI (`rhi::Device` et `rhi::CommandEncoder`) pour une API graphique. Le reste du moteur
ne change pas. Les deux backends existants servent de modèles : OpenGL 4.3 (`src/rhi/opengl/`), qui est la référence du
comportement observable, et Vulkan 1.3 (`src/rhi/vulkan/`), écrit selon le même contrat. Le guide complet est
[`docs/ADD_A_BACKEND.md`](https://github.com/TimotheSandt/Orosmo/blob/dev/docs/ADD_A_BACKEND.md) ; cette page en donne
l'essentiel.

## La règle d'isolation

Les appels et types d'une API graphique (`gl*`, `Vk*`, `MTL*`...) n'existent que dans `src/rhi/<backend>/`. Aucun type natif
dans `include/orosmo/rhi/` ni dans une couche supérieure. `make check-api` (`tools/check_api_isolation.py --strict`) le
vérifie et bloque la CI. Un dossier `src/rhi/<backend>/` est déjà autorisé ; les motifs `MTL*` sont déjà surveillés. Un
préfixe nouveau (WebGPU : `wgpu*`, `WGPU*`) doit être ajouté à `PATTERNS` dans le script.

`src/rhi/*` est au niveau 2 de `tools/layers.json` : un backend n'inclut que `core`, `platform` et `rhi`, jamais `renderer`
(`make check-layers`).

## Ce qu'il faut fournir

| Élément | Modèle |
| --- | --- |
| Une classe dérivée de `rhi::Device`, toutes les fonctions virtuelles pures | `src/rhi/vulkan/vk_device.hpp` |
| Une classe dérivée de `rhi::CommandEncoder`, rendue par `createCommandEncoder` et reprise par `submit` | `src/rhi/vulkan/vk_encoder.hpp` |
| `create<X>Device(const DeviceDesc &)` rendant `Result<std::unique_ptr<Device>>` | `gl::createGlDevice`, `vk::createVulkanDevice` |
| `applyWindowHints(const DeviceDesc &)`, appelée avant la création de la fenêtre | `gl::applyWindowHints`, `vk::applyWindowHints` |
| Une valeur de `rhi::Backend` | `kMetal` existe déjà ; une autre API demande une nouvelle valeur |
| La table des formats `rhi::Format` vers le format natif | `src/rhi/opengl/gl_translate.hpp`, `src/rhi/vulkan/vk_translate.hpp` |

Découpage conseillé, celui de Vulkan : `<x>_device.{hpp,cpp}`, `<x>_resources.cpp`, `<x>_pipelines.cpp`,
`<x>_encoder.{hpp,cpp}`, `<x>_swapchain.{hpp,cpp}`, `<x>_translate.hpp`, et un `README.md`. Ces en-têtes sont privés : seul
`src/rhi/device.cpp` inclut celui du périphérique.

## Le contrat à respecter

Le backend doit donner le même comportement observable qu'OpenGL : mêmes résultats, mêmes codes d'erreur, mêmes refus.

- Handles : `destroy` rend le handle périmé tout de suite, la libération est différée ; un handle périmé est refusé, jamais
  déréférencé ; `DeviceStats` revient à 0 à l'arrêt.
- Erreurs : `Result<T>`, jamais d'exception. `createDevice` rend `kUnsupported` avec la raison si le pilote ou le GPU ne
  convient pas ; `beginFrame` rend `kBusy` pour une fenêtre de taille nulle.
- Encodeur : une commande invalide est ignorée et journalisée, la première erreur est gardée (`error()`) et rendue par
  `submit`. Une passe ou un groupe de debug resté ouvert est une erreur.
- Barrières calculées par le backend ; une version conservatrice est acceptée.
- Un seul thread et une seule file.
- Surface : `nativeWindow` nul = hors écran (image RGBA8 de `surface.size`) ; sinon la fenêtre est adoptée, pas détruite.
- Repère commun : ligne 0 d'une texture en haut, viewport et ciseau depuis le haut, NDC y = +1 en haut, profondeur dans
  [0, 1], triangle anti-horaire = face avant.
- Liaisons : 3 groupes de 8 liaisons, décalages dynamiques dans l'ordre des entrées, push constants jusqu'à 128 octets.
- Shaders : la source est du GLSL 450 compilé en SPIR-V. Un backend qui consomme du SPIR-V met
  `Capabilities::spirvShaders = true`. Un autre langage (MSL, WGSL) demande une nouvelle valeur de `ShaderLanguage` et une
  traduction dans `src/rhi/shader_compiler.cpp`, à proposer d'abord dans `docs/QUESTIONS.md`.
- Validation : avec `DeviceDesc::validation`, chaque message d'erreur ou d'avertissement de la couche de debug native est
  compté dans `validationErrorCount()`.

### Capacités

Le constructeur du périphérique remplit `Capabilities` honnêtement : limites, fonctions courantes (`compute`,
`indirectDraw`, `timestampQueries`...) et capacités réservées (`rayQuery`, `bindless`, `meshShader`...), qui restent `false`
tant que rien ne les implémente. Un drapeau à `true` doit être couvert par un test. Le moteur se fie à ces valeurs pour
choisir ses replis ([Capacités optionnelles](/interne/rhi/#capacités-optionnelles)) : un drapeau à `true` sans la fonction
derrière donne une image fausse ou une erreur de validation, au lieu du repli prévu.

## Enregistrer le backend

Dans [`src/rhi/device.cpp`](https://github.com/TimotheSandt/Orosmo/blob/dev/src/rhi/device.cpp) :

1. inclure `<backend>/<x>_device.hpp` ;
2. ajouter le backend à `isBackendCompiled` (aujourd'hui `kOpenGL || kVulkan`) ;
3. appeler `<ns>::applyWindowHints(desc)` dans `prepareWindowHints` ;
4. ajouter un `case Backend::<k>:` dans `createDevice`. Le cas par défaut rend `kUnsupported` (« RHI backend '...' is not
   compiled in this build ») ; `kMetal` y tombe aujourd'hui.

Les sources : `mk/engine.mk` compile tout `.cpp` et `.c` sous `src/rhi/` dans `librhi.a`, sans rien ajouter au makefile.
Les bibliothèques à lier vont dans `tools/engine-sdk/engine.mk` (bloc de la plateforme) et les dépendances dans
`vcpkg.json`, avec la commande d'installation documentée. Vulkan est chargé à l'exécution par volk, ce qui permet de
démarrer sans pilote et de rendre `kUnsupported` ; un nouveau backend fait de même si possible.

## Tester

Les tests de `tests/rhi/` sont la spécification exécutable.

- `tests/rhi/rhi_device_tests.cpp` est commun : chaque cas boucle sur `FOR_EACH_BACKEND(backend)`
  (`tests/rhi/backends.hpp`). Ajouter le backend aux candidats de `backends.hpp` suffit pour que cette suite et les suites
  `renderer/*` le testent.
- Les tests propres au backend vont dans un fichier à part, sur le modèle de `vulkan_device_tests.cpp` : le test se saute
  si le backend n'est pas compilé ou si `createDevice` échoue.
- Ajouter le préfixe de la suite (`rhi/<backend>*`) à `GPU_TEST_SUITES` du `Makefile`, pour qu'elle soit exclue de
  `make demo-test-unit` et lancée par `make demo-test-gpu`.
- Chaque test finit par `CHECK(device->validationErrorCount() == 0)`.
- Comparer une capture de la démo à celle d'OpenGL avec `tools/compare_png.py`
  ([Images de référence](/interne/tests-controles/#images-de-référence)).

```bash
ENGINE_TEST_BACKENDS=opengl,vulkan ENGINE_TEST_REQUIRE_GPU=1 make demo-test
```

`ENGINE_TEST_BACKENDS` choisit les backends testés ; avec `ENGINE_TEST_REQUIRE_GPU=1`, un backend demandé mais indisponible
fait échouer au lieu de sauter le test.

## Liste de contrôle

- `Device` et `CommandEncoder` complets ; une fonction non prise en charge rend `kUnsupported` et figure dans
  `docs/GAPS.md`, jamais un stub qui prétend marcher.
- Enregistrement dans `src/rhi/device.cpp` (les quatre endroits).
- `Capabilities` exacte ; table des formats complète ou refus explicite.
- Repère commun, liaisons, handles périmés, libération différée, surfaces hors écran et fenêtre, `kBusy`, `resize`,
  `setVSync`.
- Validation native à 0 message sur toute la suite et sur la démo.
- `make demo-test` et `make check` verts ; capture comparée.
- `src/rhi/<backend>/README.md` écrit ; `docs/RHI.md`, `docs/STATUS.md`, `docs/GAPS.md` à jour.
- Compilation des autres plateformes intacte, ou écart noté dans `docs/GAPS.md`.

## Limites

RHI-13 (Metal) est Absent. Les points marqués « à vérifier » dans `docs/ADD_A_BACKEND.md` (repère de Metal et WebGPU,
fichiers `.mm`, liaison des frameworks) n'ont pas été confirmés.

## Voir aussi

- [RHI, la couche graphique abstraite](/interne/rhi/)
- [Couches et modules](/interne/couches/)
- [Tests, contrôles et images de référence](/interne/tests-controles/)
