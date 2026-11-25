---
title: "Foules instanciées"
description: "Afficher des centaines ou des milliers d'agents skinnés qui partagent un modèle glTF, avec un draw call par primitive."
sidebar:
  order: 3
---

Les noms des variables (`device`, `renderer`…) et les chemins de ressources des extraits sont expliqués dans les [conventions des exemples](/demarrer/premiere-application/#conventions-des-exemples).

`anim::CrowdRenderer` dessine beaucoup de copies d'un même modèle skinné, chacune avec sa propre pose. Le skinning se fait dans le vertex shader, avec une palette d'os par agent dans un tampon de stockage. Chaque primitive skinnée du modèle donne un seul draw call instancié pour tous les agents. Pour un seul personnage, voir plutôt [Animer un personnage](/guides/animation/squelettes/).

## Exemple minimal

Adapté de `apps/demo/src/Scene/anim_scene.cpp` (`buildModels`, `updateCrowd`, `render`). Il suppose un `model` (glTF avec une peau et des clips, voir [Importer un modèle glTF](/guides/contenus/gltf/)) et les objets décrits dans les commentaires de l'extrait, avec les noms de [Lire la boucle d'une application](/demarrer/premiere-application/) (`device`, `shaders`, `pipelines`, `renderer`, `shaderDir`).

```cpp
#include <orosmo/anim/clip.hpp>
#include <orosmo/anim/crowd.hpp>
#include <orosmo/anim/pose.hpp>
#include <orosmo/assets/render_model.hpp>
#include <orosmo/assets/texture_cache.hpp>
#include <orosmo/renderer/material.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <memory>
#include <vector>

namespace an = orosmo::anim;
namespace as = orosmo::assets;
namespace rd = orosmo::renderer;

// Variables supposées (voir « Animer un personnage » pour les créer) :
//   as::Model model;                       as::TextureCache textures(device);
//   rd::PbrShaderCache pbr(shaders, shaderDir);   std::shared_ptr<rd::PbrDefaults> defaults;
//   std::vector<an::Clip> clips;           float time;   std::vector<float> speed, phase;   // un par agent

// Mise en place (une fois). Un RenderModel DÉDIÉ à la foule : sans `skinnable`.
auto render = as::RenderModel::create(device, model, textures, pbr, defaults, {});
if (!render) { /* ... */ }
auto crowdModel = std::make_unique<as::RenderModel>(std::move(*render));

if (!an::CrowdRenderer::supportedBy(device.capabilities())) { /* pas de foule sur ce périphérique */ }
auto crowdResult = an::CrowdRenderer::create(device, shaders, pipelines, shaderDir, model, *crowdModel);
if (!crowdResult) { /* crowdResult.error().message */ }
an::CrowdRenderer &crowd = **crowdResult;

constexpr std::size_t kAgents = 1000;
std::vector<orosmo::core::Mat4> worlds(kAgents);       // où est chaque agent
for (std::size_t i = 0; i < kAgents; ++i)              // une matrice monde par agent : ici une grille de 40 par 25, espacée de 1,5 m
    worlds[i] = glm::translate(glm::mat4(1.0f), glm::vec3(1.5f * float(i % 40), 0.0f, 1.5f * float(i / 40)));
std::vector<an::Pose> poses(kAgents, an::restPose(model));

// Chaque image.
for (std::size_t i = 0; i < kAgents; ++i) {
    float t = std::fmod(time * speed[i] + phase[i], clips[0].duration());   // un temps par agent
    (void)clips[0].sample(t, poses[i]);
}
(void)crowd.setAgents(worlds, poses);              // calcule les palettes, envoie les tampons
// ... renderer.beginFrame, renderer.setCamera ...
auto stats = crowd.draw(renderer);                    // un draw call par lot
// ... renderer.endFrame
```

Les `worlds` sont des matrices monde (position, rotation, échelle de chaque agent). Chaque agent a sa propre `Pose` : on peut échantillonner le même clip à des temps différents, ou des clips différents.

Pièges :

- Le `RenderModel` de la foule ne doit pas être celui d'un `SkinnedModel` : le skinning de la foule lit les sommets au repos. Pour un personnage principal et une foule du même fichier, créer deux `RenderModel` (c'est ce que fait la démo : `hero_` et `crowdModel_`).
- `crowdModel`, `shaders` et `pipelines` doivent survivre au `CrowdRenderer`.
- `setAgents` se rappelle à chaque image avant `draw` ; `draw` ne se rappelle qu'une fois par image, car les tampons sont réécrits au `setAgents` suivant.
- `draw` s'appelle entre `beginFrame`/`setCamera` et `endFrame`, comme les autres dessins.

## Options

`CrowdRenderer::create` prend un `CrowdOptions` en dernier argument.

| Option | Type | Défaut | Effet |
|---|---|---|---|
| `extraDefines` | `std::vector<renderer::Define>` | vide | Définitions de shader du matériau, à faire correspondre à celles du reste de la scène : `forwardPlusDefines()` en forward+, `iblDefines()` avec l'IBL. Pas d'AOV. |

`create` rend `kUnsupported` sans les tampons de stockage lus par le vertex shader, et `kInvalidArgument` si le modèle n'a aucune primitive skinnée dessinée.

## Choisir setAgents ou setAgentPalettes

| Appel | Quand |
|---|---|
| `setAgents(worlds, poses)` | Cas général : une matrice et une pose par agent. Les palettes sont calculées ici, sur le CPU. |
| `setAgentPalettes(worlds, palettes)` | Palettes déjà calculées : `worlds.size() × jointCount()` matrices, agent par agent. Utile pour des agents à la même pose (on calcule une fois et on copie) ou pour calculer sur plusieurs fils. |
| `agentPalette(pose, out)` | Calcule la palette d'un agent (`jointCount()` matrices). Peut s'appeler depuis plusieurs fils. |

```cpp
std::vector<orosmo::core::Mat4> palettes(kAgents * crowd.jointCount());
for (std::size_t i = 0; i < kAgents; ++i) {            // boucle parallélisable
    std::span<orosmo::core::Mat4> out(palettes.data() + i * crowd.jointCount(), crowd.jointCount());
    (void)crowd.agentPalette(poses[i], out);
}
(void)crowd.setAgentPalettes(worlds, palettes);
```

Tailles refusées avec `kInvalidArgument`. Les calculs sont déterministes.

## Lire le résultat

| Appel | Effet |
|---|---|
| `agentCount()` | Nombre d'agents du dernier `setAgents`. |
| `jointCount()` | Os d'une palette d'agent (toutes les peaux du modèle à la suite). |
| `batchCount()` | Nombre de lots, donc de draw calls. |
| `draw(scene, wireframe = false)` | Rend un `CrowdDrawStats` : `drawCalls`, `instances` (agents × lots), `paletteBytes`. |

La démo `--scene anim` dessine 1000 agents avec 2 primitives skinnées : 2 draw calls, 2000 instances. Pour la voir : `make all-debug` puis `apps/demo/bin/debug/main --scene anim`.

## Coût

Mesuré sur un GPU intégré Intel Arc, pour un personnage de 10 147 sommets et 1000 agents (10,1 millions de sommets skinnés) : environ 15 ms par image sur OpenGL et sur Vulkan (`docs/PERF_OPENGL_VS_VULKAN.md`, section « M6 : skinning et foules »). Le coût est dominé par les sommets : pour 60 images par seconde sur ce GPU, viser environ un million de sommets skinnés par image. Le coût CPU des palettes n'a pas été mesuré.

## Limites

- Limites :
  - pas de culling par agent : tout le lot est dessiné ;
  - pas d'AOV ni de vecteurs de mouvement, donc pas de TAA ni de flou de mouvement sur la foule ;
  - la foule ne projette pas d'ombres ;
  - les palettes sont calculées sur le CPU à chaque `setAgents` (pas d'évaluation des clips sur le GPU).
- Une normale est déformée par `mat3(modèle × palette)` : exact sans échelle non uniforme.
- Le pipeline instancié ne suit pas la face avant d'une transformation miroir.
- Les lots de la foule gardent l'ancien groupe de matériau après `RenderModel::rebuildMaterials` : recréer le `CrowdRenderer` après un changement de texture ou d'ombres.

## Voir aussi

- [Animer un personnage](/guides/animation/squelettes/)
- [Beaucoup d'objets : instancing et culling](/guides/rendu-3d/instancing-culling/)
- [Importer un modèle glTF](/guides/contenus/gltf/)
