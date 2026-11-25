---
title: "Culling sur le GPU"
description: "Faire le culling, le LOD et l'émission des dessins sur le GPU : scène GPU persistante, multi-draw indirect, occlusion Hi-Z, repli sur le CPU."
sidebar:
  order: 11
---

`GpuDrivenFeature` est un plugin de rendu qui déplace sur le GPU ce que `ObjectSet::prepare` fait sur le CPU : un shader
compute teste chaque objet (pyramide de vue, distance, taille à l'écran, occlusion Hi-Z), choisit le LOD et remplit des
commandes de dessin indirectes. Le CPU n'a plus de liste de visibles à construire. Il réutilise le même `ObjectSet` et le
même `InstanceRenderer` que le chemin CPU, avec les mêmes matériaux et le même format d'instance : l'image est la même.
Pour choisir entre les deux, voir le tableau de [Instancing et culling](/guides/rendu-3d/instancing-culling/).

## Exemple minimal

Code adapté de
[`apps/demo/src/Scene/demo_scene.cpp`](https://github.com/TimotheSandt/Orosmo/blob/dev/apps/demo/src/Scene/demo_scene.cpp)
(`enableGpuDriven`, `renderInstances`). `instances` est l'`InstanceRenderer` et `objects` l'`ObjectSet` du guide précédent.

```cpp
namespace rd = orosmo::renderer;

// Une fois, après le chargement des maillages dans l'InstanceRenderer.
auto gpu = rd::enableGpuDriven(renderer, *device, shaders, pipelines, shaderDirectory, *instances);
rd::GpuDrivenFeature *gpuDriven = gpu ? *gpu : nullptr;   // nul : repli sur le chemin CPU
// (gpu.error().code == kUnsupported sans compute ou dessin indirect)

// À chaque image, entre setCamera et endFrame.
if (gpuDriven)
{
    rd::GpuDrivenView view;
    view.cull = rd::CullView::fromMatrices(camera.viewMatrix(), camera.projectionMatrix(), float(renderer.frameHeight()));
    view.cull.minScreenRadius = 1.0f;
    view.viewProjection = camera.projectionMatrix() * camera.viewMatrix();
    auto drawn = gpuDriven->draw(renderer, objects, view);
}
else
{
    rd::CullView view = rd::CullView::fromMatrices(camera.viewMatrix(), camera.projectionMatrix(), float(renderer.frameHeight()));
    objects.prepare(view, batches);
    (void)(*instances)->draw(renderer, batches);
}

// À l'arrêt, avant de détruire l'InstanceRenderer et ses maillages.
rd::disableGpuDriven(renderer);
```

Dans la démo : `apps/demo/bin/debug/main --instances 100000 --gpu-driven on`. Le journal compare « GPU-driven » et « CPU
culling ».

`enableGpuDriven` installe le plugin sur le `SceneRenderer` (qui le possède) et rend un pointeur non possédé. Le plugin
garde des références à l'`InstanceRenderer`, à `ShaderLibrary` et à `PipelineCache` : ils doivent lui survivre, d'où
`disableGpuDriven` d'abord.

## Options

`draw(scene, objects, view, options)` prend un `GpuDrivenOptions` :

| Option | Type | Défaut | Effet |
|---|---|---|---|
| `frustumCulling` | `bool` | `true` | Faux : tout est visible (ni pyramide, ni distance, ni taille). |
| `lod` | `bool` | `true` | Faux : toujours le niveau 0. |
| `wireframe` | `bool` | `false` | Second passage en fil de fer. |

Occlusion :

| Appel | Défaut | Effet |
|---|---|---|
| `setOcclusion(bool)` | actif | Désactivé : ni test Hi-Z ni construction de la Hi-Z. |
| `resetOcclusion()` | | Oublie la Hi-Z (coupure de caméra, téléportation) : pas d'occlusion à l'image suivante. |
| `hizValid()`, `hizLevels()` | | Lisent l'état de la Hi-Z. |

## Scène GPU persistante

`GpuScene` (`gpuScene()`) est un miroir de l'`ObjectSet` dans un tampon du GPU, un enregistrement par emplacement d'objet.
À chaque `draw`, seuls les objets modifiés depuis l'image précédente (`add`, `remove`, `setTransform`, `setColor`) sont
renvoyés, par plages contiguës. Un objet immobile ne coûte donc aucun transfert. Premier envoi, changement d'`ObjectSet` ou
agrandissement du tampon : tout est renvoyé.

## Occlusion Hi-Z

Après la passe de scène, le plugin construit une pyramide de profondeur (Hi-Z) depuis la profondeur finale. Le culling de
l'image suivante la lit : un objet entièrement caché derrière un mur est rejeté. Conséquence : un objet qui se découvre
(caméra qui contourne un mur, occulteur qui bouge) manque une image (G173). Il n'y a pas d'occlusion à la première image,
après un redimensionnement ou après `resetOcclusion()`.

## Capacités et repli

`GpuDrivenFeature::supportedBy(device->capabilities())` dit si le périphérique convient : compute, dessin indirect,
`drawIndirectFirstInstance`, 7 emplacements de tampons de stockage et des groupes de travail d'au moins 64. Sinon
`enableGpuDriven` et `create` renvoient `kUnsupported` et rien n'est changé : gardez le chemin CPU. Sans
`multiDrawIndirect`, le RHI rejoue les commandes une par une, avec le même résultat.

## Statistiques

| Source | Contenu |
|---|---|
| `GpuDrawStats` (rendu par `draw`) | `commands`, `indirectCalls` (un par couple matériau, pool non vide ; le double en fil de fer), `pools`, `sync` (`GpuSceneSyncStats` : objets relus et envoyés, plages, octets, `fullUpload`). |
| `readStats()` | `GpuCullStats` de la dernière image exécutée : `frustumCulled`, `distanceCulled`, `smallCulled`, `frustumVisible`, `occlusionCulled`, `visible`, `lodCulled`, `crossFades`, `instances`, `drawsWithInstances`. |
| `readVisibility()`, `readCommandCounts()`, `readHiz(level)` | Détail par emplacement, par commande, et la Hi-Z. |

Les lectures `read*` sont synchrones : un chemin lent pour les outils, les tests et le journal, pas pour chaque image.

## Pièges

- `draw` s'appelle une fois par image.
- Les pools de géométrie sont copiés à l'enregistrement des maillages : un `Mesh::updateVertices` ultérieur n'est pas vu.
- Les ombres gardent le culling CPU (`ShadowFeature::addObjectCasters`).
- `--debug-view lod` repasse la démo sur le culling CPU, seul à produire les couleurs de LOD.
- Comme `InstanceRenderer`, les dessins GPU n'écrivent pas l'AOV de mouvement : le TAA suppose des objets fixes.

## Limites

- REN-29 (Fait) : preuves dans `tests/renderer/gpu_driven_tests.cpp` (même ensemble visible et même image que le CPU).
- G154 : pas de `drawIndexedIndirectCount` (toutes les commandes, même vides, sont jouées) ; 8 niveaux de LOD au plus ; un
  seul dessin GPU par image.
- G173 : occlusion en une passe, avec une image de retard. G172 : copies de géométrie perdues si une soumission échoue.
- G169 : pas d'AOV pour les objets GPU.

## Voir aussi

- [Instancing et culling](/guides/rendu-3d/instancing-culling/)
- [LOD](/guides/rendu-3d/lod/)
- [Profilage](/guides/noyau/profilage/)
