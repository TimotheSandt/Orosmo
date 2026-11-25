---
title: "Mélanger 2D et 3D, plusieurs vues"
description: "Mettre du 2D dans le monde 3D, par-dessus l'image ou dans une texture avec Layer2DFeature, et afficher plusieurs vues avec SceneView et ViewsFeature."
sidebar:
  order: 3
---

`Layer2DFeature` enveloppe un `Renderer2D` dans un plugin du graphe de rendu, en trois modes. `SceneView` rend une caméra 3D
dans une texture à part, et `ViewsFeature` place ces textures dans des zones de la sortie.

## Exemple minimal : 2D dans le monde

Code adapté de
[`apps/demo/src/Scene/mixed_scene.cpp`](https://github.com/TimotheSandt/Orosmo/blob/dev/apps/demo/src/Scene/mixed_scene.cpp).

```cpp
#include <orosmo/renderer/layer2d_feature.hpp>

namespace rd = orosmo::renderer;

// Une fois : créer le plugin et l'enregistrer.
auto world = rd::Layer2DFeature::create("game.world2d", rd::Layer2DMode::kWorld, *device, shaders, pipelines, shaderDirectory);
auto added = renderer.addFeature(std::move(*world));          // possédé par le SceneRenderer
auto *layer = static_cast<rd::Layer2DFeature *>(*added);       // pointeur non possédé

// À chaque image, avant endFrame.
layer->begin({});                                              // Camera2D inutilisée en kWorld
layer->setPlaneTransform(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f)));
auto material = layer->renderer().material(checker, true);
layer->renderer().draw({.material = *material, .position = {0.0f, 2.0f}, .size = {1.4f, 1.4f}});
layer->renderer().line({-5.0f, 0.2f}, {5.0f, 0.2f}, 0.12f, {1, 0.9f, 0.2f, 1});
```

Le plugin possède son `Renderer2D` (`renderer()`) : les sprites et formes se déposent comme avec un `Renderer2D` seul
(voir [Sprites, formes et caméra 2D](/guides/rendu-2d/sprites-formes/)). Dans la démo :
`apps/demo/bin/debug/main --scene mixed`.

## Les trois modes

| `Layer2DMode` | Où le 2D est dessiné | Caméra | Profondeur |
|---|---|---|---|
| `kWorld` | Dans la scène 3D : le plan 2D (x, y) est placé par `setPlaneTransform` et vu par la caméra 3D. | La caméra 3D ; `Camera2D` inutilisée, pas de pixel parfait. | Testée contre la profondeur de la scène (un objet 3D devant cache le sprite), jamais écrite. |
| `kOverlay` | Sur la sortie, après le post-traitement et la mise à l'échelle, en pixels de la sortie. | `Camera2D` sur la taille de la sortie. | Aucun test. |
| `kTexture` | Dans une texture propre, avant la passe de scène, donnée à un maillage 3D. | `Camera2D` de la texture. | Aucun test. |

Pour `kTexture`, réglez la texture puis liez-la à un maillage :

```cpp
auto panel = rd::Layer2DFeature::create("game.panel", rd::Layer2DMode::kTexture, *device, shaders, pipelines, shaderDirectory);
auto *layer = static_cast<rd::Layer2DFeature *>(*renderer.addFeature(std::move(*panel)));
(void)layer->setTextureSize(256, 256);                // crée ou recrée la texture : son contenu est perdu
layer->setClearColor({0.0f, 0.0f, 0.0f, 1.0f});       // effacement avant chaque image
const rd::TextureBinding binding[] = {layer->binding()};
(void)screenMesh.setTextures(binding);                // maillage avec uv, programme texturé

// À chaque image :
rd::Camera2D cam;
cam.setViewport(256, 256);
cam.setCenter({128.0f, 128.0f});
layer->begin(cam);
layer->renderer().rect({0.0f, 0.0f}, {256.0f, 32.0f}, {0.0f, 0.0f, 1.0f, 1.0f});
```

Code adapté de `tests/renderer/mixed_2d3d_tests.cpp`. Le programme du maillage est celui de `mesh_textured.vert` + `mesh.frag`
avec `OROSMO_TEXTURED` (voir [conventions GLSL](/reference/conventions-glsl/)).

## Ordre d'enregistrement

L'ordre des plugins est l'ordre des `addFeature` (D9). L'ordre voulu d'une image est : 3D opaque, 3D transparent, 2D du monde,
post-traitement, 2D en superposition, interface.

```cpp
renderer.addFeature(std::move(transparents));   // après la passe « scene »
renderer.addFeature(std::move(world2d));        // Layer2DFeature kWorld
// ... post-traitements ...
renderer.addFeature(std::move(overlay2d));      // Layer2DFeature kOverlay : écrit la sortie
// ... puis l'interface
```

Un `Layer2DFeature` en `kTexture` peut être enregistré n'importe où : sa passe s'exécute avant la scène. Si vous ajoutez ces
plugins après une interface déjà enregistrée, retirez l'interface (`removeFeature`) et réenregistrez-la après eux, comme le
fait la démo. Un `Layer2DFeature` ne reçoit qu'un seul dépôt par image (G160).

## Plusieurs vues

Une `SceneView` est une caméra 3D avec son propre `SceneRenderer` qui rend dans une texture à elle.

```cpp
auto view = rd::SceneView::create(*device, shaders, pipelines, shaderDirectory, 320, 180);
(*view)->renderer().setLights(&lights);              // lumières et maillages peuvent être partagés
(*view)->camera().setPosition({0.0f, 5.0f, 5.0f});

auto views = rd::ViewsFeature::create(*device, shaders, pipelines, shaderDirectory);
auto *placer = static_cast<rd::ViewsFeature *>(*renderer.addFeature(std::move(*views)));

// À chaque image, AVANT l'endFrame de l'image principale.
(void)(*view)->render(*encoder, {.clearColor = {0, 0, 0, 1}},
                      [&](rd::SceneRenderer &r, rd::Camera &c) { return r.drawMesh(mesh, c); });
placer->place(**view, {20, 20, 320, 180});          // ViewRect : x, y, largeur, hauteur, origine en haut à gauche
```

Code adapté de `tests/renderer/mixed_2d3d_tests.cpp`. `SceneView::render` enchaîne `beginFrame`, `setCamera`, votre fonction de
dépôt et `endFrame` de la vue ; elle referme toujours l'image, même en cas d'erreur. Les placements de `ViewsFeature` ne valent
que pour une image : ils sont retirés à la fin de l'image même si elle échoue. `resize(w, h)` change la taille d'une vue (son
contenu est perdu). Une zone qui dépasse la sortie est découpée ; une zone vide est ignorée.

Une vue peut aussi s'afficher dans l'interface : `CanvasRenderer::registerExternalImage(id, texture, sampler)` fait d'une
texture une image que l'interface dessine comme une autre (`ImageView`).

## Limites

- R2D-05 (Fait) : `tests/renderer/mixed_2d3d_tests.cpp`, démo `--scene mixed`.
- R2D-06 (Fait) : exercé dans les tests ; la vue incrustée dans une interface n'est pas dans la démo.
- G160 : un seul dépôt par `Layer2DFeature` et par image ; un matériau = une texture.

## Voir aussi

- [Sprites, formes et caméra 2D](/guides/rendu-2d/sprites-formes/)
- [Transparence](/guides/rendu-3d/transparence/)
- [Écrire un plugin de rendu](/guides/avance/plugin-de-rendu/)
