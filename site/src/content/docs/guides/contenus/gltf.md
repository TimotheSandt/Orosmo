---
title: "Importer un modèle glTF"
description: "Charger un fichier .gltf ou .glb, le valider, le dessiner avec ses matériaux PBR et l'enregistrer."
sidebar:
  order: 1
---

`loadGltf` lit un fichier glTF 2.0 et rend un `assets::Model` : des données sur le CPU, sans GPU. Pour le dessiner, on construit ensuite un `assets::RenderModel`, qui crée un maillage et un matériau PBR par primitive. Les deux étapes sont séparées pour pouvoir lire ou modifier un modèle sans fenêtre (outils, tests, chargement dans une tâche).

## Exemple minimal

Ce code charge un modèle statique et le dessine. Les noms (`device`, `shaders`, `pipelines`, `renderer`, `camera`, `shaderDir`) sont ceux des [conventions des exemples](/demarrer/premiere-application/#conventions-des-exemples) : `device` est un `rhi::Device &` et `renderer` un `rd::SceneRenderer &`.

```cpp
#include <orosmo/assets/model.hpp>
#include <orosmo/assets/render_model.hpp>
#include <orosmo/assets/texture_cache.hpp>

namespace as = orosmo::assets;
namespace rd = orosmo::renderer;

// Une fois, au chargement.
auto model = as::loadGltf(GET_RESOURCE_PATH("models/casque.glb"));
if (!model) { /* model.error().message */ }
if (auto ok = model->validate(); !ok) { /* kParse : indice hors bornes */ }

as::TextureCache textures(device);                     // partage les textures entre les modèles
rd::PbrShaderCache pbr(shaders, shaderDir);               // programmes PBR ; shaderDir = GET_RESOURCE_PATH("shader/rhi")
auto defaults = rd::PbrDefaults::create(device);       // textures neutres (blanc, normale plate...)
if (!defaults) { /* ... */ }

auto render = as::RenderModel::create(device, *model, textures, pbr, *defaults);
if (!render) { /* render.error().message */ }
render->setRootTransform(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -3.0f)));

// Chaque image, entre renderer.beginFrame/setCamera et renderer.endFrame.
render->draw(renderer, camera);
```

`GET_RESOURCE_PATH` (`orosmo/core/paths.hpp`) donne le chemin d'une ressource : `res/...` en debug, à côté de l'exécutable en release. Le modèle va dans `res/models/` de votre projet, les shaders dans `res/shader/rhi/` (voir [Make, engine.mk et CMake](/reference/construction/)). Le `device` doit survivre à tous ces objets, et le `RenderModel` doit rester vivant jusqu'à la fin des images qui le dessinent.

Source : `apps/demo/src/Scene/anim_scene.cpp` (`AnimScene::buildModels`) et `tests/assets/render_model_tests.cpp`.

## Lire le contenu d'un Model

`Model` est un ensemble de tableaux dont les éléments se renvoient entre eux par indice (`-1` : absent). Le repère est celui de glTF : Y vers le haut, mètres, quaternions `(x, y, z, w)`.

| Champ | Contenu |
|---|---|
| `meshes` | `MeshData` : un nom et des `MeshPrimitive` (positions, normales, tangentes, `uv0`, `uv1`, couleurs, `joints`, `weights`, indices, `material`, bornes). |
| `materials` | `MaterialData` : métal/rugosité, cartes (`TextureRef`), émission, `alphaMode`, `doubleSided`, `unlit`. |
| `textures`, `images`, `samplers` | Une texture lie une image et un échantillonneur. Une image est un chemin externe ou des octets inclus (`encoded`). |
| `nodes` | Hiérarchie TRS : `parent`, `children`, `translation`, `rotation`, `scale`, et les indices `mesh`, `skin`, `camera`, `light` du nœud. |
| `skins` | Articulations (indices de nœuds) et matrices de liaison inverses. |
| `animations` | `AnimationData` : pistes par nœud, durée. Voir [Animer un personnage](/guides/animation/squelettes/). |
| `cameras`, `lights` | Caméras et lumières `KHR_lights_punctual` du fichier. |
| `scenes`, `defaultScene` | Racines de chaque scène. |

Les fonctions utiles :

| Fonction | Effet |
|---|---|
| `Model::validate()` | Vérifie que tous les indices sont dans les bornes et que les attributs ont la bonne longueur. Erreur `kParse` sinon. À appeler après avoir modifié un modèle à la main. |
| `Model::stats()` | `ModelStats` : `vertices`, `triangles`, `primitives`. |
| `Model::worldMatrices()` | Matrice monde de chaque nœud, calculée depuis les TRS. |
| `parseGltf(bytes, baseDir, name)` | Comme `loadGltf`, mais depuis des octets en mémoire ; `baseDir` résout les URI relatives. |

Les erreurs de `loadGltf` : `kIo` (fichier illisible), `kParse` (contenu invalide), `kUnsupported` (extension requise inconnue, Draco...). Les images externes ne sont pas lues à l'import : `ImageData::path` est rendu absolu, et c'est le `TextureCache` qui les lit au moment de `RenderModel::create`.

## Caméras et lumières du fichier

`loadGltf` les importe dans `Model::cameras` et `Model::lights`, et chaque nœud porte l'indice (`NodeData::camera`, `NodeData::light`). `RenderModel` ne les utilise pas : il ne dessine que les maillages. Pour s'en servir, on lit la matrice monde du nœud (`model->worldMatrices()[nodeIndex]`) et on remplit soi-même la `Camera` ou le `LightManager` du renderer. Une lumière glTF éclaire vers -Z de son nœud ; l'intensité est en candelas (ponctuelle, spot) ou en lux (directionnelle).

## Régler le dessin

`RenderModel::create` prend un `RenderModelOptions` en dernier argument.

| Option | Type | Défaut | Effet |
|---|---|---|---|
| `shadows` | `const renderer::ShadowFeature *` | `nullptr` | Si non nul, le modèle reçoit les ombres de cette `ShadowFeature`. |
| `extraDefines` | `std::vector<renderer::Define>` | vide | Définitions de shader à ajouter, par exemple `forwardPlusDefines()` quand l'éclairage forward+ est actif. |
| `sourceKey` | `std::string` | `"model"` | Nom du modèle dans le cache de textures et dans les noms de debug. Deux modèles différents doivent avoir deux clés. |
| `scene` | `int` | `-1` | Scène à dessiner. `-1` : la scène par défaut, ou toutes les racines si le fichier n'en déclare pas. |
| `skinnable` | `bool` | `false` | Les primitives qui ont des articulations reçoivent un tampon de sommets inscriptible par le compute de skinning. Nécessaire pour `anim::SkinnedModel`. |

```cpp
as::RenderModelOptions options;
options.sourceKey = "hero";
options.extraDefines = rd::forwardPlusDefines();   // si la scène est en forward+
auto render = as::RenderModel::create(device, *model, textures, pbr, *defaults, options);
```

Pour recevoir et projeter des ombres, voir la section « Ombres » de [Animer un personnage](/guides/animation/squelettes/) : `shadows` se règle ici, et les casters se déclarent sur la `ShadowFeature`. Le guide [Ajouter des ombres](/guides/rendu-3d/ombres/#exemple-minimal--un-modèle-gltf-qui-reçoit-et-projette) décrit la lumière ombrée, et le [tutoriel du jeu 3D](/demarrer/tutoriel-jeu-3d/) montre un programme complet.

Autres appels de `RenderModel` :

| Appel | Effet |
|---|---|
| `setRootTransform(mat4)` | Matrice qui place tout le modèle dans le monde. |
| `setNodeMatrices(std::vector<glm::mat4>)` | Remplace les matrices monde des nœuds (pose animée). La taille doit être le nombre de nœuds. |
| `draw(scene, camera)` | Enregistre un dessin par (nœud, primitive). À appeler une fois par image, entre `setCamera` et `endFrame`. |
| `rebuildMaterials(shadows, pbr)` | Recrée les groupes de matériaux après un changement d'ombres. À appeler entre deux images. |
| `drawCount()`, `meshCount()`, `materials()`, `mesh(i)` | Inspection. |

Un matériau glTF devient un `renderer::PbrMaterial` : la couleur de base et l'émission sont lues en sRGB, les autres cartes en linéaire. `AlphaMode::kBlend` devient `BlendMode::kBlend` (voir [Transparence](/guides/rendu-3d/transparence/)).

## Enregistrer

`saveGltf(model, path)` écrit un `.glb` (un seul fichier, images incluses) ou un `.gltf` accompagné de `<nom>.bin`, selon l'extension de `path`. La démo l'utilise pour écrire un personnage généré par le code, puis le relire avec `loadGltf`.

```cpp
if (auto saved = as::saveGltf(*model, "build/copie.glb"); !saved) { /* saved.error() */ }
```

Les pistes `weights` (cibles de forme) sont supprimées à l'export.

## Limites

- L'import OBJ n'existe pas. Le tableau complet des extensions est dans [Formats de fichiers](/reference/formats/).
- Pas de cibles de forme (morph targets) : ignorées (pas encore prévues). Draco et `EXT_meshopt_compression` sont refusés (`kUnsupported`) quand le fichier les exige. `KHR_texture_transform` est ignoré.
- Rendu : un seul jeu d'UV (une carte qui demande `TEXCOORD_1` est lue avec `TEXCOORD_0`, avec un avertissement), un seul mode d'adressage par texture (celui de `wrapS`), tangentes calculées par triangle (pas MikkTSpace), transparents `BLEND` dessinés dans l'ordre du fichier sans tri.
- Un nœud à échelle négative inverse le sens des faces.
- Un modèle glTF n'est pas rechargé à chaud : seules les textures le sont (rechargement de textures seulement).
- Aucune façade « charger et afficher en un appel » n'existe : il faut créer `TextureCache`, `PbrShaderCache` et `PbrDefaults` soi-même.

## Voir aussi

- [Textures : PNG, KTX2, cache et rechargement](/guides/contenus/textures/)
- [Animer un personnage](/guides/animation/squelettes/)
- [Formats de fichiers](/reference/formats/)
- Code : [`include/orosmo/assets/model.hpp`](https://github.com/TimotheSandt/Orosmo/blob/dev/include/orosmo/assets/model.hpp), [`render_model.hpp`](https://github.com/TimotheSandt/Orosmo/blob/dev/include/orosmo/assets/render_model.hpp)
