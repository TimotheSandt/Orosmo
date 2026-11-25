---
title: "Beaucoup d'objets : instancing et culling"
description: "Choisir entre Mesh, InstanceRenderer, CrowdRenderer et GpuDrivenFeature selon le nombre d'objets, puis dessiner des milliers d'objets avec un draw call par lot et rejeter ce qui est hors champ."
sidebar:
  order: 9
  label: Beaucoup d'objets
---

Les noms des variables (`device`, `renderer`…) et les chemins de ressources des extraits sont expliqués dans les [conventions des exemples](/demarrer/premiere-application/#conventions-des-exemples).

Un draw call coûte du temps au processeur, quel que soit le nombre de triangles qu'il dessine. Pour dessiner beaucoup
d'objets, on regroupe donc ceux qui partagent un maillage et un matériau en un seul appel (instancing), et on ne soumet pas
ce qui est hors de l'image (culling). Cette page dit quel outil du moteur choisir, puis détaille le chemin CPU :
`InstanceRenderer`, `ObjectSet` et le culling sur le processeur.

## Choisir comment dessiner un ensemble d'objets

Le moteur ne choisit pas à votre place : chaque ensemble d'objets passe par l'un de ces chemins, et l'application décide
lequel. Le tableau dit combien de draw calls chaque chemin émet et ce qu'il fait pour vous.

| Chemin | Convient à | Draw calls émis | Culling | LOD |
|---|---|---|---|---|
| `Mesh` + `SceneRenderer::drawMesh` | Peu d'objets différents : décor unique, personnage, objets de jeu. | Un par `drawMesh`, donc un par objet (un par nœud pour un `RenderModel`). | Aucun : tout ce qu'on dessine est soumis. | Aucun : l'application choisit le maillage. |
| `Mesh` avec `MeshData::instances` | Un même maillage répété avec ses propres données d'instance, shader écrit par vous. | Un par `drawMesh`, pour toutes les instances du maillage. | Aucun. | Aucun. |
| `InstanceRenderer` + `ObjectSet` | Des centaines à des dizaines de milliers d'objets, avec peu de maillages et de matériaux différents. | Un par couple (maillage, matériau) présent dans la vue : 4 pour 10 000 objets dans la démo. | CPU : pyramide de vue, BVH, taille à l'écran, distance. | Par distance, avec fondu tramé. |
| `CrowdRenderer` (module `anim`) | Une foule d'agents animés qui partagent un modèle skinné. | Un par primitive skinnée du modèle, pour tous les agents. | Aucun : tout le lot est dessiné. | Aucun (voir [LOD](/guides/rendu-3d/lod/)). |
| `GpuDrivenFeature` | Beaucoup d'objets quand le culling CPU ou la soumission des lots coûte trop. | Un appel indirect (multi-draw) par couple (matériau, pool de géométrie) ; le GPU écrit le nombre d'instances. | GPU : pyramide, distance, taille, puis occlusion Hi-Z. | GPU, 8 niveaux au plus. |

Pour décider :

- Un objet sans jumeau (même maillage, même matériau) n'a rien à gagner à l'instancing : `drawMesh`.
- Dès que le même maillage apparaît des dizaines de fois, `InstanceRenderer` réduit les draw calls au nombre de couples
  (maillage, matériau) et `ObjectSet` ne soumet que ce qui est visible. C'est le chemin à essayer en premier.
- Des personnages animés en nombre : `CrowdRenderer`, qui ne passe pas par `ObjectSet` (ni culling ni LOD). Voir
  [Foules instanciées](/guides/animation/foules/).
- `GpuDrivenFeature` reprend le même `ObjectSet` et le même `InstanceRenderer` que le chemin CPU et déplace le culling et le
  LOD sur le GPU. Il demande des capacités optionnelles (compute, dessin indirect) et refuse proprement sans elles : on garde
  alors le chemin CPU. Il n'est pas forcément plus rapide : sur un GPU intégré, le culling compute a coûté de 3 à 5,5 ms pour
  100 000 objets. Mesurez avant de le choisir. Voir [Culling sur le GPU](/guides/rendu-3d/rendu-gpu/).

Les lignes « Mesh » du tableau ne passent pas par cette page : voir [Dessiner une scène](/guides/rendu-3d/scene/) pour `drawMesh`, et la section suivante pour `MeshData::instances`. Le nombre de draw calls se lit dans `InstanceDrawStats::drawCalls` (chemin CPU) et `GpuDrawStats::indirectCalls` (chemin GPU).

## Un maillage avec ses propres instances

`MeshData::instances` est le chemin le plus simple : le maillage porte lui-même un tampon de données par instance, lues par
**votre** vertex shader. Un seul `drawMesh` dessine toutes les instances. Le moteur ne fait ni culling, ni LOD, ni éclairage
pour vous : c'est adapté à quelques centaines de copies d'un petit objet (particules, herbe, pièces).

Les attributs de sommet sont décrits par `attributeSizes` (emplacements 0, 1, 2...), les attributs d'instance par
`instanceAttributeSizes` : ils prennent les emplacements **à la suite** de ceux des sommets. Avec `attributeSizes = {3, 3, 3}`
(position, normale, couleur), les attributs d'instance sont aux emplacements 3, 4...

```cpp
rd::MeshData data = cubeData;                      // attributeSizes = {3, 3, 3}
data.instanceAttributeSizes = {3, 1};              // emplacement 3 : décalage (vec3), 4 : échelle (float)
data.instances = {0.0f, 0.0f, 0.0f, 1.0f,          // 4 flottants par instance, instance après instance
                  2.0f, 0.0f, 0.0f, 0.5f,
                  4.0f, 0.0f, 0.0f, 2.0f};
auto mesh = rd::Mesh::create(device, data);
mesh->setProgram(*program);                        // votre programme (ci-dessous)
(void)mesh->updateInstances(newValues);            // met à jour les instances sans recréer le maillage
```

Le vertex shader lit ces emplacements comme n'importe quel attribut, un par instance :

```glsl
layout(location = 3) in vec3 iOffset;
layout(location = 4) in float iScale;
// ... gl_Position = camera.matrix * vec4(aPos * iScale + iOffset, 1.0);
```

Les emplacements 8 à 15 sont ceux des instances de `InstanceRenderer` : un maillage destiné à `InstanceRenderer` n'a donc pas
d'instances propres et au plus 8 attributs de sommet. Pour écrire le programme, voir
[Écrire et recharger des shaders](/guides/avance/shaders/).

## Exemple minimal avec InstanceRenderer

Dans les extraits, `device`, `shaders`, `pipelines`, `renderer` et `shaderDir` sont ceux de [Lire la boucle d'une application](/demarrer/premiere-application/) (section « Conventions des exemples ») : `device` est un `rhi::Device &`, et `shaderDir` vaut `GET_RESOURCE_PATH("shader/rhi")`.

On enregistre maillages et matériaux dans un `InstanceRenderer`, on ajoute les objets à un `ObjectSet`, puis à chaque image on
calcule les lots visibles et on les dessine. Code adapté de
[`apps/demo/src/Scene/demo_scene.cpp`](https://github.com/TimotheSandt/Orosmo/blob/dev/apps/demo/src/Scene/demo_scene.cpp)
(`initInstances`, `renderInstances`).

```cpp
#include <orosmo/renderer/instance_renderer.hpp>
#include <orosmo/renderer/object_set.hpp>

namespace rd = orosmo::renderer;

// Une fois : maillages, matériau, objets.
auto instancesResult = rd::InstanceRenderer::create(device, shaders, pipelines, shaderDir);
if (!instancesResult) { /* instancesResult.error().message */ }
rd::InstanceRenderer &instances = **instancesResult;

rd::Mesh cube = *rd::Mesh::create(device, cubeData);    // MeshData : attributeSizes = {3, 3, 3}
auto meshId = instances.addMesh(cube);               // non possédé : cube doit vivre plus longtemps
const std::uint32_t materialId = instances.addMaterial({}); // programme par défaut

rd::ObjectSet objects;
rd::ObjectDesc desc;
desc.lodMeshes = {*meshId};
desc.material = materialId;
desc.localBounds = {glm::vec3(-0.5f), glm::vec3(0.5f)}; // boîte dans le repère du maillage
for (int i = 0; i < 10000; ++i)
{
    desc.transform = glm::translate(glm::mat4(1.0f), glm::vec3(float(i % 100), 0.0f, float(i / 100)));
    (void)objects.add(desc);
}

// À chaque image, entre setCamera et endFrame.
rd::InstanceBatchBuilder batches;
rd::CullView view = rd::CullView::fromMatrices(camera.viewMatrix(), camera.projectionMatrix(), float(renderer.frameHeight()));
view.minScreenRadius = 1.0f;                              // moins d'un pixel de rayon : rejeté
const rd::VisibilityStats visible = objects.prepare(view, batches);
auto drawn = instances.draw(renderer, batches);       // un draw call par lot
```

La couleur de l'instance (`ObjectDesc::color`) est multipliée à celle des sommets (blancs dans la démo). Le programme par
défaut est `mesh_instanced.vert` avec `mesh.frag` compilé avec `OROSMO_INSTANCED` ; il lit les lumières du `SceneRenderer`
(`setLights`). Dans la démo : `apps/demo/bin/debug/main --instances 10000` (après `make all-debug`).

## Matériaux et maillages

`InstanceRenderer` garde un registre : maillages et matériaux y sont enregistrés une fois, puis désignés par leur identifiant
(consécutifs à partir de 0). Un `Mesh` enregistré a au plus 8 attributs de sommet et aucune donnée d'instance propre : les
attributs d'instance du moteur occupent les emplacements 8 à 15.

| Champ de `InstanceMaterial` | Type | Défaut | Effet |
|---|---|---|---|
| `program` | `std::shared_ptr<ShaderProgram>` | nul | Programme du lot. Nul : `mesh_instanced.vert` + `mesh.frag` avec `OROSMO_INSTANCED`. |
| `textures` | `rhi::BindGroupHandle` | nul | Groupe 1 (textures du matériau), non possédé. |
| `pushConstants` | `std::vector<std::byte>` | vide | Octets de push constants ; le premier entier est le drapeau fil de fer de `mesh.frag`. |
| `cull` | `rhi::CullMode` | `kNone` | Faces éliminées. |

Avec des ombres, la démo prend `shadows->instancedReceiverProgram()` et `shadows->receiverGroup()` (voir
[Ombres](/guides/rendu-3d/ombres/)).

## Objets et visibilité

| Champ de `ObjectDesc` | Type | Défaut | Effet |
|---|---|---|---|
| `lodMeshes` | `std::vector<std::uint32_t>` | vide | Identifiants de maillage, le 0 le plus détaillé. Au moins un, 255 au plus. |
| `material` | `std::uint32_t` | 0 | Identifiant de matériau de l'`InstanceRenderer`. |
| `lodSettings` | `std::uint32_t` | 0 | Identifiant rendu par `ObjectSet::addLodSettings` ; 0 : un seul niveau, jamais rejeté au loin. |
| `localBounds` | `Aabb` | vide | Boîte dans le repère du maillage ; le moteur en déduit la boîte monde. |
| `transform` | `glm::mat4` | identité | Matrice du modèle. |
| `color` | `glm::vec4` | `(1, 1, 1, 1)` | Multipliée à la couleur des sommets. |

`add` rend un `ObjectId` (emplacement et génération). `remove`, `setTransform` et `setColor` le prennent ; `contains`, `size`
et `clear` complètent. Les identifiants de maillage et de matériau ne sont pas vérifiés par `ObjectSet`.

`prepare(view, batches, options)` vide `batches`, y range les instances visibles et appelle `build()`.

| Option de `VisibilityOptions` | Type | Défaut | Effet |
|---|---|---|---|
| `useBvh` | `bool` | `true` | Faux : test linéaire objet par objet (référence, mesures). |
| `frustumCulling` | `bool` | `true` | Faux : rien n'est rejeté par la pyramide. |
| `lod` | `bool` | `true` | Faux : toujours le niveau 0. |
| `lodColors` | `bool` | `false` | Une couleur par niveau de détail (voir [Debug visuel](/guides/rendu-3d/debug-visuel/)). |

La vue de culling est une `CullView`, construite par `CullView::fromMatrices(view, projection, viewportHeight, orthographic)`.

| Champ de `CullView` | Type | Défaut | Effet |
|---|---|---|---|
| `minScreenRadius` | `float` | 0 | Rayon projeté (pixels) sous lequel un objet est rejeté ; 0 : pas de rejet. |
| `maxDistance` | `float` | 0 | Distance (mètres) au-delà de laquelle tout est rejeté ; 0 : pas de borne. |

`prepare` rend un `VisibilityStats` : `cull` (un `CullStats` : `nodesVisited`, `objectsTested`, `visible`, `frustumCulled`,
`smallCulled`, `distanceCulled`), `objects`, `instances` (un objet en fondu de LOD en produit deux), `crossFades`, `lodCulled`.
Mesure du README du renderer (1280x720, vue par défaut) : 10 000 objets, 3 717 visibles, 125 en fondu, 3 842 instances en 4
draw calls.

## Lots sans ObjectSet

Si votre code trie déjà ses objets, remplissez l'`InstanceBatchBuilder` à la main : un lot par clé (maillage, matériau).

```cpp
rd::InstanceBatchBuilder batches;
batches.add({*meshId, materialId}, modelMatrix, glm::vec4(1.0f, 0.5f, 0.2f, 1.0f));
batches.build();                          // obligatoire avant draw ; un nouvel add l'invalide
auto drawn = instances.draw(renderer, batches);
```

## Culling seul

`culling.hpp` s'utilise sans `ObjectSet` ni GPU : `Aabb`, `Frustum::fromMatrix(viewProjection)`, `projectedRadiusPixels`, `Bvh`
et `cullLinear`.

```cpp
rd::Bvh bvh;
bvh.build(boxes);                          // std::span<const rd::Aabb>, repère monde
std::vector<std::uint32_t> visibleIndices; // query AJOUTE à ce vecteur : le vider avant
rd::CullStats stats = bvh.query(view, boxes, visibleIndices);
```

`Bvh::refit(boxes)` recalcule les boîtes sans changer la topologie (objets mobiles) ; une construction neuve vaut mieux quand
les objets se sont beaucoup déplacés. `ObjectSet` fait ce choix seul. La coupure est déterministe.

## Pièges

- `InstanceRenderer::draw` ne s'appelle qu'une fois par image : le tampon d'instances est réécrit à chaque appel.
- Les maillages enregistrés ne sont pas possédés : ils vivent jusqu'à `endFrame` et plus longtemps que l'`InstanceRenderer`.
- Une `localBounds` trop petite fait disparaître des objets encore visibles.
- Les instances n'écrivent que la couleur : pas d'AOV, donc pas de vecteurs de mouvement. La démo désactive `--aa taa` avec
  `--instances`.
- Le tramage du LOD désactive l'early-Z pour les fragments concernés.

## Limites

- Les objets instanciés n'écrivent aucun AOV (sorties auxiliaires) ; les vues de debug `albedo`, `overdraw` et `normals` les
  ignorent.
- `CrowdRenderer` n'a ni culling par agent ni LOD.
- Le détail de ce qui est fait et de ce qui manque est dans [État du moteur](/projet/etat/).

## Voir aussi

- [Niveaux de détail (LOD)](/guides/rendu-3d/lod/)
- [Culling sur le GPU](/guides/rendu-3d/rendu-gpu/)
- [Foules instanciées](/guides/animation/foules/)
- [État du moteur](/projet/etat/)
