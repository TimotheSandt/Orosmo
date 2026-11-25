---
title: "Niveaux de détail (LOD)"
description: "Choisir un maillage selon la distance avec fondu tramé. Les niveaux sont des maillages fournis par le développeur ; la cuisson ne fait que proposer des LOD automatiques."
sidebar:
  order: 10
---

Un niveau de détail (LOD) est un maillage plus simple utilisé quand l'objet est loin. Dans Orosmo, les niveaux sont des
maillages que vous fournissez : le moteur n'en invente pas. Il choisit le niveau selon la distance à la caméra et fond les
deux niveaux voisins près d'une bascule. La cuisson d'assets peut simplement vous proposer des LOD automatiques, que vous
devez ensuite transformer vous-même en maillages.

## Exemple minimal

Un objet reçoit la liste de ses maillages (le 0 est le plus détaillé) et un identifiant de réglages de LOD. Code adapté de
[`apps/demo/src/Scene/demo_scene.cpp`](https://github.com/TimotheSandt/Orosmo/blob/dev/apps/demo/src/Scene/demo_scene.cpp) :

```cpp
namespace rd = orosmo::renderer;

rd::LodSettings lod;
lod.switchDistances = {28.0f};   // niveau 0 jusqu'à 28 m, puis niveau 1
lod.transitionWidth = 6.0f;      // fondu sur 6 m autour de la bascule
const std::uint32_t lodSettings = objects.addLodSettings(lod);

rd::ObjectDesc desc;
desc.lodMeshes = {detailedMeshId, coarseMeshId};   // identifiants d'InstanceRenderer::addMesh
desc.lodSettings = lodSettings;
desc.material = materialId;
desc.localBounds = {glm::vec3(-0.45f), glm::vec3(0.45f)};
auto id = objects.add(desc);
```

Le choix se fait ensuite dans `ObjectSet::prepare` (voir [Instancing et culling](/guides/rendu-3d/instancing-culling/)). Dans la
démo : `apps/demo/bin/debug/main --instances 10000 --debug-view lod` colore chaque niveau.

## Réglages

| Champ de `LodSettings` | Type | Défaut | Effet |
|---|---|---|---|
| `switchDistances` | `std::vector<float>` | vide | Distances de bascule croissantes : le niveau i sert jusqu'à `switchDistances[i]`. N distances pour N + 1 niveaux ; vide : un seul niveau. |
| `transitionWidth` | `float` | 0 | Largeur (mètres) de la bande de fondu centrée sur chaque bascule ; 0 : bascule nette. |
| `cullDistance` | `float` | 0 | Distance au-delà de laquelle l'objet n'est plus dessiné ; 0 : jamais. Un fondu de `transitionWidth` le fait disparaître. |
| `bias` | `float` | 1 | Multiplie les distances mesurées ; au-dessus de 1, les niveaux grossiers arrivent plus tôt. |

Les bascules doivent être espacées de plus de `transitionWidth`, sinon la première bande l'emporte.

## Fondu tramé

Dans une bande de transition, les deux niveaux sont dessinés avec des masques de tramage complémentaires (motif de Bayer
4x4) : chaque pixel où les deux niveaux produisent un fragment est couvert par exactement un des deux, sans trou ni double
couverture. Là où un seul niveau a de la géométrie (silhouettes différentes), il n'y a pas de garantie. La fonction pure
`selectLod(settings, levelCount, distance)` rend un `LodSelection` : `count` vaut 0 (rejeté), 1 ou 2 (fondu), avec pour chaque
dessin un `level` et un seuil `dither`. `ditherKeeps` et `ditherCoverage` reproduisent la règle du shader pour les tests.

## Produire les niveaux

Vous faites les maillages : à la main, avec votre outil de modélisation. Rien dans le
moteur ne relie la cuisson aux `lodMeshes` d'un objet. La cuisson (`assets::bakePrimitive`, `BakeOptions::lodRatios`, par
défaut `{0.5f, 0.25f}`, et `lodError`, par défaut 0,01) produit des listes d'indices simplifiées dans
`BakedPrimitive::lods` ; `unbake(baked, lod)` redonne une primitive avec les indices du niveau demandé. À vous de créer un
`Mesh` par niveau. Ces LOD automatiques sont guidés par les positions seules et partagent les sommets du LOD 0. Voir
[Cuire les maillages](/guides/contenus/cuisson/).

Le dernier niveau peut être un imposteur : n'importe quel maillage (un quad) convient, mais le moteur ne génère pas
l'imposteur (G167).

## LOD sur le GPU

Avec [`GpuDrivenFeature`](/guides/rendu-3d/rendu-gpu/), le même `ObjectSet` est copié en scène GPU et `selectLod` est
reproduit dans le shader de culling. Limite : 8 niveaux par objet (`kGpuSceneMaxLevels`) ; un objet qui en a plus n'est pas
dessiné par le GPU.

## Modèles animés

Le LOD des modèles animés n'existe pas. `CrowdRenderer` et `SkinnedModel` dessinent toujours le maillage complet avec son
squelette complet, sans choix de niveau ni culling par agent (G199). Le besoin est noté dans A73 de `docs/QUESTIONS.md` : un
maillage simplifié par niveau, avec son propre squelette réduit et sa propre animation, choisis par le développeur et jamais
générés. Il est prévu pour le jalon M8 ; en attendant, le jeu peut seulement changer lui-même la pose qu'il passe à
`setAgents`.

## Limites

- REN-31 (Partiel) : LOD par distance et fondu tramé faits ; imposteurs automatiques absents (G167).
- Un fondu croisé qui chevauche la bande de `cullDistance` n'est pas combiné : seul le dernier niveau s'efface.
- Pas de LOD pour les modèles animés (A73, M8).

## Voir aussi

- [Instancing et culling](/guides/rendu-3d/instancing-culling/)
- [Culling sur le GPU](/guides/rendu-3d/rendu-gpu/)
- [Debug visuel](/guides/rendu-3d/debug-visuel/)
