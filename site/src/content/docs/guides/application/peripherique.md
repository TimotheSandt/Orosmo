---
title: Choisir le backend graphique
description: Créer le rhi::Device sur Vulkan ou OpenGL, gérer le repli, lire les capacités et les erreurs de validation.
sidebar:
  order: 2
---

Le `rhi::Device` est l'objet qui parle au GPU. `rhi::createDevice(desc)` le crée sur le backend demandé dans
`DeviceDesc`. Le moteur a deux backends, Vulkan 1.3 et OpenGL 4.3+ ; Metal est reconnu mais refusé. Les en-têtes sont
`orosmo/rhi/device.hpp` et `orosmo/rhi/capabilities.hpp`.

## Exemple minimal

```cpp
#include <orosmo/platform/window.hpp>
#include <orosmo/rhi/device.hpp>

namespace rhi = orosmo::rhi;

orosmo::platform::Window window("monjeu", 1280, 720, orosmo::platform::WindowState::kWindowed);

rhi::DeviceDesc desc;
desc.backend = rhi::Backend::kOpenGL;
desc.validation = true;

if (!window.init([&] { (void)rhi::prepareWindowHints(desc); }))   // avant la création de la fenêtre
    return 1;
desc.surface.nativeWindow = window.getWindow();                      // après

auto created = rhi::createDevice(desc);
if (!created)
{
    std::cerr << created.error().message << '\n';                    // raison : « Vulkan: no usable physical device… »
    return 1;
}
rhi::Device &device = **created;
```

`prepareWindowHints` doit précéder la création de la fenêtre, car les attributs de contexte dépendent du backend
(OpenGL 4.3 core et contexte de debug ; pas de contexte pour Vulkan). Un backend non compilé ou indisponible sur la
machine renvoie `ErrorCode::kUnsupported` avec la raison.

## Options de DeviceDesc

| Option | Type | Défaut | Effet |
|---|---|---|---|
| `backend` | `Backend` | `kOpenGL` | `kVulkan`, `kOpenGL` ou `kMetal` (refusé : Absent) |
| `surface.nativeWindow` | `void *` | `nullptr` | fenêtre à adopter (`Window::getWindow()`) ; nul : périphérique hors écran |
| `surface.size` | `Extent2D` | `{1, 1}` | taille du backbuffer hors écran ; ignoré avec une fenêtre |
| `validation` | `bool` | `true` | `GL_KHR_debug` ou couches de validation Vulkan ; les erreurs sont comptées |
| `vsync` | `bool` | `false` | synchronisation verticale au départ ; réglable ensuite par `setVSync` |
| `directPresentation` | `bool` | `true` | Vulkan : dessin direct dans l'image de la fenêtre au lieu d'une copie ; sans effet sur OpenGL et hors écran |
| `appName` | `std::string` | `"engine"` | nom de l'application transmis au backend |

Le défaut de `backend` est OpenGL dans la structure ; la démo, elle, demande Vulkan d'abord. Le projet de démarrage
d'`orosmo init` passe par `app::Application`, qui fait le repli de la section suivante (Vulkan d'abord, puis OpenGL ;
`--api opengl` le force, `--strict-backend` supprime le repli).

## Vulkan d'abord, OpenGL en repli

La démo essaie Vulkan et retombe sur OpenGL si le périphérique ne se crée pas (pas de chargeur Vulkan, pas de GPU 1.3). Comme
les attributs de fenêtre diffèrent, une nouvelle fenêtre est créée pour la deuxième tentative :

Les noms suivent les [conventions des exemples](/demarrer/premiere-application/#conventions-des-exemples).

```cpp
auto tryBackend = [](rhi::Backend backend, platform::Window &window) -> core::Result<std::unique_ptr<rhi::Device>>
{
    rhi::DeviceDesc desc;
    desc.backend = backend;
    if (!window.init([&] { (void)rhi::prepareWindowHints(desc); }))
        return core::makeError(core::ErrorCode::kUnsupported, "fenêtre impossible");
    desc.surface.nativeWindow = window.getWindow();
    return rhi::createDevice(desc);
};

auto window = std::make_unique<platform::Window>("monjeu", 1280, 720, platform::WindowState::kWindowed);
auto created = tryBackend(rhi::Backend::kVulkan, *window);
if (!created && created.error().code == core::ErrorCode::kUnsupported && rhi::fallbackBackend(rhi::Backend::kVulkan))
{
    std::cerr << created.error().message << " ; repli sur OpenGL\n";
    window = std::make_unique<platform::Window>("monjeu", 1280, 720, platform::WindowState::kWindowed);
    created = tryBackend(rhi::Backend::kOpenGL, *window);
}
// puis, si created est valide : rhi::Device &device = **created;
```

`rhi::fallbackBackend(demandé)` dit quel backend essayer : OpenGL après Vulkan, rien pour les autres, et rien sous macOS,
où OpenGL s'arrête à la version 4.1 (l'erreur de Vulkan est alors rendue telle quelle ; elle dit quoi installer, voir
[Installer](/demarrer/installation/#macos--vulkan-par-moltenvk)).

Adapté de `apps/demo/src/main.cpp` et `Game::createGraphics`. La démo prend la décision dans `demo::resolveBackend`
(`apps/demo/src/launch_options.cpp`) : seul Vulkan retombe sur OpenGL, et `--strict-backend` supprime le repli. En ligne
de commande : `--api vulkan|opengl`, `--list-apis`, `--choose-api`
([Options de la démo](/reference/options-demo/)).

## Lire les capacités

`device.capabilities()` renvoie une `Capabilities` : nom du GPU et du pilote, limites (`maxTextureSize`,
`maxColorAttachments`, tailles de groupes compute…) et fonctions. Ne supposez jamais une fonction optionnelle : lisez-la.

| Champ | Sens |
|---|---|
| `compute`, `indirectDraw`, `multiDrawIndirect`, `drawIndirectFirstInstance` | compute et dessin indirect |
| `timestampQueries` | minuteries GPU |
| `anisotropy`, `wireframe`, `shaderFloat64`, `spirvShaders` | fonctions de rendu et de shaders |
| `bc7Textures`, `astcTextures` | formats compressés échantillonnables |
| `srgbStorageImages` | `Format::kRGBA8Srgb` utilisable en image de stockage |
| `validation` | couche de validation réellement active (Vulkan : `VK_LAYER_KHRONOS_validation` ; OpenGL : contexte de debug) |
| `rayQuery`, `accelerationStructure`, `bindless`, `meshShader`, `bufferDeviceAddress`, `sparseResources` | réservés : toujours `false` |

```cpp
const rhi::Capabilities &caps = device.capabilities();
LOG_INFO("GPU : ", caps.deviceName, " (", caps.driverVersion, ")");
if (!caps.compute) { /* forward+ et skinning GPU ne sont pas disponibles */ }
```

### Valeurs par backend

Les deux backends donnent les mêmes valeurs sur un GPU récent, avec ces différences :

| Capacité | OpenGL 4.3+ | Vulkan 1.3 |
|---|---|---|
| `compute`, `indirectDraw` | toujours vrai | toujours vrai |
| `multiDrawIndirect`, `drawIndirectFirstInstance` | toujours vrai | selon le GPU |
| `timestampQueries` | toujours vrai | selon la file et le GPU ; faux sous MoltenVK quand les horodatages ne mesurent pas le travail du GPU (constaté à la création) |
| `srgbStorageImages` | toujours faux | selon le format (MoltenVK sur GPU Apple : vrai) |
| `validation` | si `desc.validation` et contexte de debug | si `desc.validation` et couche installée |
| `anisotropy` | si l'extension de filtrage anisotrope est présente | selon le GPU |
| `bc7Textures` | toujours vrai | selon le GPU |
| `astcTextures` | si `GL_KHR_texture_compression_astc_ldr` est présente | selon le GPU |
| `wireframe`, `shaderFloat64` | toujours vrai | selon le GPU |
| `spirvShaders` | faux | vrai |

### Quelle fonction demande quoi

Chaque fonction ci-dessous a une méthode statique `supportedBy(caps)`, et sa fabrique renvoie `kUnsupported` sans les capacités :
testez avant de créer.

| Fonction | Capacités requises | Si elles manquent |
|---|---|---|
| `ForwardPlusLighting` (éclairage forward+) | `compute`, assez d'emplacements de tampons de stockage, groupes de 256 invocations | utiliser l'éclairage forward (la démo le fait) |
| `GpuDrivenFeature` (culling GPU) | `compute`, `indirectDraw`, `drawIndirectFirstInstance`, tampons de stockage, groupes de 64 | culling sur le processeur (la démo le fait) |
| `AutoExposureFeature` | `compute`, groupes de 256 invocations | `post_effects` ne l'ajoute pas : exposition fixe |
| `SkinnedModel` (skinning) | `compute`, 4 emplacements de tampons de stockage | aucun repli automatique : `skinVertices` calcule la pose sur le processeur, ou affichez la pose de repos |
| `CrowdRenderer` (foules) | assez d'emplacements de tampons de stockage | pas de foule |
| `IblFeature` (environnement) | `compute` | pas d'éclairage par environnement |
| textures KTX2 compressées | `bc7Textures` ou `astcTextures` | décodage en RGBA8 |

Les capacités optionnelles du tableau précédent (ray tracing, bindless, shaders de maillage) sont toujours fausses : le
moteur ne les utilise pas.

## Validation

Avec `validation = true`, les messages d'erreur et d'avertissement de la couche de validation sont comptés :
`device.validationErrorCount()` doit rester à 0 (c'est ce que les tests exigent). Le projet de démarrage (`app::defaultSettings`) et la démo
l'activent en debug seulement (`#ifdef DEBUG`). Sous Vulkan, la couche est `VK_LAYER_KHRONOS_validation` (paquet `vulkan-validationlayers`) ; si elle est
absente, un avertissement est écrit et le rendu continue sans validation.

## Choisir le GPU

Variable `ENGINE_VULKAN_DEVICE` : un indice (`0`, `1`…) ou un fragment du nom du GPU (`Intel`, `RTX`). Le GPU correspondant
passe en premier ; sans variable, le backend préfère un GPU discret, puis intégré. Voir
[Variables d'environnement](/reference/variables-environnement/).

## Limites

- Metal est reconnu par `Backend::kMetal` mais refusé : il n'existe pas encore.
- Vulkan n'est vérifié que sous Linux. Sous Windows et macOS (MoltenVK), il ne l'est pas.
- Pas de transferts asynchrones (copies sur une file dédiée) : toutes les copies passent par la file principale.

Voir [État du moteur](/projet/etat/).
