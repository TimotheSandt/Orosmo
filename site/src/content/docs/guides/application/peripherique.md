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

if (window.init([&] { (void)rhi::prepareWindowHints(desc); }) != 0)   // avant la création de la fenêtre
    return 1;
desc.surface.nativeWindow = window.getWindow();                      // après

auto device = rhi::createDevice(desc);
if (!device)
{
    std::cerr << device.error().message << '\n';                     // raison : « Vulkan: no usable physical device… »
    return 1;
}
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
d'`orosmo init` ne crée qu'un périphérique OpenGL (G209).

## Vulkan d'abord, OpenGL en repli

La démo essaie Vulkan et retombe sur OpenGL si le périphérique ne se crée pas (pas de chargeur Vulkan, pas de GPU 1.3). Comme
les attributs de fenêtre diffèrent, une nouvelle fenêtre est créée pour la deuxième tentative :

```cpp
auto tryBackend = [](rhi::Backend backend, platform::Window &window) -> core::Result<std::unique_ptr<rhi::Device>>
{
    rhi::DeviceDesc desc;
    desc.backend = backend;
    if (window.init([&] { (void)rhi::prepareWindowHints(desc); }) != 0)
        return core::makeError(core::ErrorCode::kUnsupported, "fenêtre impossible");
    desc.surface.nativeWindow = window.getWindow();
    return rhi::createDevice(desc);
};

auto window = std::make_unique<platform::Window>("monjeu", 1280, 720, platform::WindowState::kWindowed);
auto device = tryBackend(rhi::Backend::kVulkan, *window);
if (!device && device.error().code == core::ErrorCode::kUnsupported)
{
    std::cerr << device.error().message << " ; repli sur OpenGL\n";
    window = std::make_unique<platform::Window>("monjeu", 1280, 720, platform::WindowState::kWindowed);
    device = tryBackend(rhi::Backend::kOpenGL, *window);
}
```

Adapté de `apps/demo/src/main.cpp` et `Game::createGraphics`. La démo prend la décision dans `demo::resolveBackend`
(`apps/demo/src/launch_options.cpp`) : seul Vulkan retombe sur OpenGL, et `--strict-backend` supprime le repli. En ligne
de commande : `--api vulkan|opengl`, `--list-apis`, `--choose-api`
([Options de la démo](/reference/options-demo/)).

## Lire les capacités

`device->capabilities()` renvoie une `Capabilities` : nom du GPU et du pilote, limites (`maxTextureSize`,
`maxColorAttachments`, tailles de groupes compute…) et fonctions. Ne supposez jamais une fonction optionnelle : lisez-la.

| Champ | Sens |
|---|---|
| `compute`, `indirectDraw`, `multiDrawIndirect`, `drawIndirectFirstInstance` | compute et dessin indirect |
| `timestampQueries` | minuteries GPU |
| `anisotropy`, `wireframe`, `shaderFloat64`, `spirvShaders` | fonctions de rendu et de shaders |
| `bc7Textures`, `astcTextures` | formats compressés échantillonnables |
| `rayQuery`, `accelerationStructure`, `bindless`, `meshShader`, `bufferDeviceAddress`, `sparseResources` | réservés : toujours `false` |

```cpp
const rhi::Capabilities &caps = (*device)->capabilities();
LOG_INFO("GPU : ", caps.deviceName, " (", caps.driverVersion, ")");
if (!caps.compute) { /* le moteur retombe sur forward ; le skinning GPU n'est pas disponible */ }
```

## Validation

Avec `validation = true`, les messages d'erreur et d'avertissement de la couche de validation sont comptés :
`device->validationErrorCount()` doit rester à 0 (c'est ce que les tests exigent). Le starter et la démo l'activent en debug
seulement (`#ifdef DEBUG`). Sous Vulkan, la couche est `VK_LAYER_KHRONOS_validation` (paquet `vulkan-validationlayers`) ; si elle est
absente, un avertissement est écrit et le rendu continue sans validation.

## Choisir le GPU

Variable `ENGINE_VULKAN_DEVICE` : un indice (`0`, `1`…) ou un fragment du nom du GPU (`Intel`, `RTX`). Le GPU correspondant
passe en premier ; sans variable, le backend préfère un GPU discret, puis intégré. Voir
[Variables d'environnement](/reference/variables-environnement/).

## Limites

- RHI-04 (choix à l'exécution) : Partiel ; Metal est reconnu mais refusé.
- RHI-13 (backend Metal) : Absent.
- Vulkan n'est vérifié que sous Linux (G142).
- RHI-19 (transferts asynchrones) : Absent.
