---
title: RHI, la couche graphique abstraite
description: Ce que fait le RHI et ce qu'il laisse au renderer, le cycle d'une image, les handles et leur destruction différée, les durées de vie et l'ordre de destruction, les capacités optionnelles, les liaisons et les backends.
sidebar:
  order: 2
---

Le RHI (`orosmo::rhi`, couche 2) est la seule porte vers le GPU. Son API publique (`include/orosmo/rhi/`) ne contient aucun
type d'API graphique ; les backends vivent dans `src/rhi/opengl/` et `src/rhi/vulkan/`, seuls endroits où `gl*` et `Vk*`
existent. Sources : `docs/RHI.md`, `src/rhi/README.md`, `src/rhi/vulkan/README.md`, `docs/03` §3 à §6.

## RHI et renderer

Le RHI donne des objets GPU bruts et des commandes : tampons, textures, échantillonneurs, shaders, pipelines, groupes de
liaison, passes de rendu, dessins, calcul, copies. Il ne connaît ni caméra, ni maillage, ni lumière, ni matériau. Ses
objets sont désignés par des handles que l'appelant détruit lui-même.

Le renderer (`orosmo::renderer`, couche 3) est construit dessus et sert à l'application : `SceneRenderer`, `Mesh`,
`PbrMaterial`, `LightManager`, plugins. Ses objets possèdent leurs ressources GPU et les libèrent à leur destruction.

Une application touche le RHI pour créer le `rhi::Device` et faire tourner l'image (`beginFrame`, `createCommandEncoder`,
`submit`, `present`) ; le reste passe par le renderer. On descend au RHI pour écrire un plugin de rendu ou un dessin
externe (`SceneRenderer::drawCustom`). Voir [Quel niveau une application utilise](/interne/couches/#quel-niveau-une-application-utilise).

## Principes

- État explicite, pas d'état global caché. Chaque passe déclare ses attachements, chaque pipeline porte tout son état fixe.
  Vulkan et Metal y sont naturels ; OpenGL est une traduction.
- Handles opaques (`Handle<Tag>` : indice + génération) ; destruction différée tant que le GPU utilise l'objet.
- Erreurs par `Result<T>`, jamais d'exception. Une commande invalide est refusée avant tout appel au pilote.
- Barrières calculées par le RHI, pas par l'appelant.
- Un seul thread : le périphérique et ses encodeurs s'utilisent depuis le thread qui les a créés.

## Une image, de bout en bout

```cpp
#include <orosmo/rhi/device.hpp>

rhi::DeviceDesc desc;                                  // backend = kOpenGL par défaut, validation = true
desc.backend = rhi::Backend::kVulkan;
if (window.init([&] { (void)rhi::prepareWindowHints(desc); }) != 0) { /* pas d'affichage */ }
desc.surface.nativeWindow = window.getWindow();        // nul : périphérique hors écran de taille surface.size

auto device = rhi::createDevice(desc);                 // kUnsupported, avec la raison, si le backend manque
if (!device) { std::cerr << device.error().message << '\n'; return EXIT_FAILURE; }

while (!window.shouldClose())
{
    if (!window.newFrame()) continue;
    auto frame = (*device)->beginFrame();              // texture de l'image
    if (!frame)
    {
        if (frame.error().code == core::ErrorCode::kBusy) continue;   // fenêtre réduite : sauter l'image
        core::logError(frame.error());
        break;
    }
    auto encoder = (*device)->createCommandEncoder();
    encoder->beginRenderPass({.colors = {{.texture = *frame, .clearColor = {0, 0, 0, 1}}}});
    encoder->setPipeline(pipeline);
    encoder->draw(3);
    encoder->endRenderPass();
    if (auto done = (*device)->submit(std::move(encoder)); !done) { core::logError(done.error()); }
    (void)(*device)->present();                        // présente et libère les ressources détruites
}
(*device)->waitIdle();
```

`prepareWindowHints` s'appelle dans le rappel de `Window::init`, juste avant la création de la fenêtre : il pose les
attributs de contexte du backend (OpenGL 4.3 core et contexte de debug, ou aucune API pour Vulkan). `device` est le
`Result` rendu par `createDevice`, d'où `(*device)->` ([conventions des exemples](/demarrer/premiere-application/#conventions-des-exemples)). Exemple complet :
[`apps/demo/src/Game/Game.cpp`](https://github.com/TimotheSandt/Orosmo/blob/dev/apps/demo/src/Game/Game.cpp).

| Champ de `DeviceDesc` | Défaut | Effet |
| --- | --- | --- |
| `backend` | `Backend::kOpenGL` | `kOpenGL`, `kVulkan` ; `kMetal` est refusé (`kUnsupported`). |
| `surface.nativeWindow` | `nullptr` | Fenêtre adoptée (le RHI ne la détruit pas) ; nul = hors écran. |
| `surface.size` | `{1, 1}` | Taille de l'image hors écran ; ignorée avec une fenêtre. |
| `validation` | `true` | `GL_KHR_debug` ou couches de validation Vulkan ; les messages sont comptés dans `validationErrorCount()`. |
| `vsync` | `false` | Synchronisation verticale ; changeable par `setVSync`. |
| `directPresentation` | `true` | Vulkan : dessin direct dans l'image présentée (une copie de moins). Sans effet en OpenGL et hors écran. |
| `appName` | `"engine"` | Nom donné au pilote. |

## Handles et destruction différée

Un handle (`BufferHandle`, `TextureHandle`, `SamplerHandle`, `ShaderHandle`, `PipelineHandle`, `BindGroupHandle`,
`QueryPoolHandle`) est un couple indice + génération. Il ne possède rien : pas de destructeur, pas de comptage. On le libère
par `Device::destroy(handle)`.

- `destroy` rend le handle périmé tout de suite. La ressource GPU n'est libérée qu'une fois les images en vol terminées
  (OpenGL : deux images présentées et une clôture `glFenceSync` franchie ; Vulkan : sémaphore à chronologie).
  `DeviceStats::pendingDestruction` compte les libérations en attente.
- Détruire un handle nul ou déjà périmé ne fait rien.
- Un emplacement réutilisé change de génération : un ancien handle ne désigne jamais la nouvelle ressource.

Un handle périmé n'est pas un plantage. Utilisé dans un encodeur, la commande est ignorée et journalisée, l'encodeur garde
la première erreur (`CommandEncoder::error()`), et `Device::submit` la renvoie. L'erreur arrive donc au `submit`, loin de
l'appel fautif : en cas d'image incomplète, lire l'erreur de `submit` et le journal, qui nomment la commande refusée.

## Durées de vie et ordre de destruction

Trois régimes coexistent dans le moteur et les noms ne les distinguent pas :

| Régime | Types | Règle |
| --- | --- | --- |
| RAII (l'objet possède ses ressources GPU) | `renderer::GpuBuffer`, `Texture`, `RenderTarget`, `Mesh`, `SceneRenderer`, `CanvasRenderer` | Libérées à la destruction. Le `rhi::Device` doit leur survivre. |
| `std::shared_ptr` (partagé) | `ShaderProgram` (`ShaderLibrary::loadGraphics`), `PbrMaterial`, `PbrDefaults`, textures de `assets::TextureCache` | Vivant tant qu'un détenteur le garde. |
| Référence, pointeur ou handle non possédé | handles RHI, `TextureBinding`, `Mesh::setMaterialGroup`, `SceneRenderer::setLights(LightManager *)`, pointeur rendu par `addFeature` | Le propriétaire garde l'objet vivant tant qu'il sert. |

Les cas à surveiller :

| Objet | Doit vivre au moins jusqu'à |
| --- | --- |
| `rhi::Device` | La destruction de tout objet du renderer, d'`assets` et d'`anim`. |
| `ShaderLibrary`, `PipelineCache` | La destruction des renderers et plugins créés avec eux (`SceneRenderer`, `CanvasRenderer`, `ForwardPlusLighting`, `SkinnedModel`, `CrowdRenderer`...). |
| `LightManager` passé à `setLights` | La dernière image qui l'utilise. |
| `CommandEncoder` passé à `SceneRenderer::beginFrame` | `endFrame` (les commandes y sont enregistrées). |
| Tampons d'un `Mesh` dessiné | `endFrame` de l'image (les dessins sont joués là). |
| Textures d'un `TextureBinding`, groupe de `setMaterialGroup` | Le dernier dessin du maillage. |
| `assets::RenderModel` | Le `SkinnedModel` et le `CrowdRenderer` créés depuis lui. |
| État lié à une `ui::Ui` (structure de la vue) | La `ui::Ui` (l'arbre garde des liaisons vers lui). |

`PbrMaterial::rebuild` détruit l'ancien groupe 1 en cas de succès : chaque maillage qui l'a reçu par `group()` doit recevoir
le nouveau avant son prochain dessin (`assets::RenderModel::rebuildMaterials` le fait pour un modèle importé).

En C++, les membres d'une classe sont détruits dans l'ordre inverse de leur déclaration. On déclare donc d'abord ce dont les
autres dépendent (ici les membres gardent les `unique_ptr` déjà sortis de leur `Result`) :

```cpp
namespace rd = orosmo::renderer;

struct App
{
    orosmo::platform::Window window;                       // détruite en dernier
    std::unique_ptr<orosmo::rhi::Device> device;
    std::unique_ptr<rd::ShaderLibrary> shaders;            // avant tout renderer qui la référence
    std::unique_ptr<rd::PipelineCache> pipelines;
    std::unique_ptr<rd::SceneRenderer> scene;
    rd::LightManager lights;                               // = std::move(*rd::LightManager::create(*device))
    HudState hud;                                          // avant `ui` : l'arbre garde des liaisons vers lui
    std::unique_ptr<orosmo::ui::Ui> ui;
};
```

En fin de programme : `(*device)->waitIdle()`, puis destruction des objets (ordre inverse de déclaration), puis
`platform::Window::terminate()` après la destruction de toutes les fenêtres et du périphérique. Si le `Profiler` mesure sur
le GPU, retirer son fournisseur avant de détruire le périphérique ([Mesurer](/guides/noyau/profilage/#minuteries-gpu)).

## Capacités optionnelles

`Device::capabilities()` rend une `Capabilities` remplie par le backend : nom du GPU et du pilote, limites
(`maxTextureSize`, `maxColorAttachments`, `maxComputeWorkGroupSize`, `maxStorageBufferBindings`, `minUniformAlignment`...)
et fonctions qui peuvent manquer : `compute`, `indirectDraw`, `multiDrawIndirect`, `drawIndirectFirstInstance`,
`timestampQueries`, `anisotropy`, `bc7Textures`, `astcTextures`, `wireframe`, `spirvShaders`, `shaderFloat64`. Les capacités
réservées (`rayQuery`, `accelerationStructure`, `bindless`, `meshShader`, `bufferDeviceAddress`, `sparseResources`) valent
toutes `false` : rien ne les implémente encore.

Le moteur lit ces valeurs, il ne les suppose jamais. Sur un pilote logiciel (Mesa llvmpipe) ou sur macOS (OpenGL 4.1 au
plus), le calcul ou les tampons de stockage peuvent manquer. Les objets qui en ont besoin le disent de deux façons :

- une fonction `supportedBy(const rhi::Capabilities &)` à tester avant de créer : `ForwardPlusLighting`, `GpuDrivenFeature`,
  `AutoExposureFeature`, `anim::SkinnedModel`, `anim::CrowdRenderer` ;
- `create` rend `kUnsupported`, et `SceneRenderer::addFeature` refuse un plugin dont `supports(capabilities)` est faux.

C'est à l'application de prévoir le repli :

```cpp
const auto &caps = (*device)->capabilities();
if (anim::SkinnedModel::supportedBy(caps))
{
    // skinning sur le GPU
}
else
{
    // repli : pose de repos (la démo fait ainsi)
}

if (auto fp = rd::enableForwardPlus(**scene, **device, shaders, pipelines, shaderDir); !fp)
    LOG_WARNING("forward+ unavailable, using forward: ", fp.error().message);   // le renderer reste en forward
```

Adapté de [`apps/demo/src/Game/Game.cpp`](https://github.com/TimotheSandt/Orosmo/blob/dev/apps/demo/src/Game/Game.cpp).

## Modèle de liaison

Trois groupes fixes pour tout le projet, de 8 liaisons chacun (`kGroupCount = 3`, `kBindingsPerGroup = 8`) :

| Groupe | Contenu | Fréquence |
| --- | --- | --- |
| 0 | données par image : caméra (0), lumières (1), `FrameData` (2), liaisons 3 à 7 des plugins | une fois par image |
| 1 | matériau ou passe : textures, échantillonneurs, constantes | par matériau ou par passe |
| 2 | données par objet | par objet |

Dans le GLSL 450, groupe = `set` et liaison = `binding`. Vulkan utilise un ensemble de descripteurs par groupe ; OpenGL
aplatit en emplacement = groupe × 8 + liaison. Les push constants (128 octets au plus) sont émulés par un tampon uniforme
sous OpenGL. Les décalages dynamiques de `setBindGroup` se consomment dans l'ordre des entrées du groupe.

## Les backends

| Backend | État | Points clés |
| --- | --- | --- |
| Vulkan 1.3 (`src/rhi/vulkan/`) | Fait sous Linux (Windows et macOS non vérifiés) | chargé à l'exécution par volk, mémoire par VMA, rendu dynamique, `synchronization2`, sémaphore à chronologie, cache de pipelines sur disque, validation de synchronisation |
| OpenGL 4.3+ (`src/rhi/opengl/`) | Fait | référence du comportement observable ; contexte de debug `GL_KHR_debug`, `glClipControl`, barrières conservatrices |
| Metal | Absent (prévu en M9) | reconnu et refusé avec un message |

La démo choisit au lancement (`--api vulkan|opengl`) ; si Vulkan ne démarre pas, elle affiche la raison et retombe sur
OpenGL, sauf avec `--strict-backend` ([Choisir le backend](/guides/application/peripherique/)). Les mêmes tests
(`tests/rhi/rhi_device_tests.cpp`) tournent sur les deux backends et exigent `validationErrorCount() == 0`.

## Repère commun

Origine en haut à gauche pour toutes les cibles (la ligne 0 d'une texture est son haut) ; NDC avec y = +1 en haut et
profondeur dans [0, 1] ; caméra en reverse-Z (profondeur effacée à 0) ; un triangle anti-horaire est la face avant. OpenGL
l'obtient par `glClipControl`, Vulkan par un viewport de hauteur négative.

## Limites

- Pas de backend Metal (prévu en M9). Sur macOS, OpenGL s'arrête à 4.1, en dessous de ce que le backend OpenGL demande.
- Une seule file : pas de transferts asynchrones ni de calcul en parallèle du rendu.
- Dessin indirect sans nombre de commandes lu sur le GPU.
- Textures cube et tableau créables, mais pas encore vérifiées par un test d'échantillonnage dans un shader.

Détail : [État du projet](/projet/etat/).

## Voir aussi

- [Ajouter un backend](/interne/ajouter-un-backend/)
- [Renderer, graphe de passes et plugins](/interne/renderer/)
- [Chaîne des shaders](/interne/shaders/)
