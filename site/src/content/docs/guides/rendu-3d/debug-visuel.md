---
title: "Dessin et vues de debug"
description: "Dessiner des lignes, boîtes et sphères de debug, afficher normales, albédo, surdessin, LOD, fil de fer et mouvement, geler le frustum de culling."
sidebar:
  order: 12
  label: Debug visuel
---

Les noms des variables (`device`, `renderer`…) et les chemins de ressources des extraits sont expliqués dans les [conventions des exemples](/demarrer/premiere-application/#conventions-des-exemples).

Trois outils aident à voir ce que fait le rendu : `DebugDraw` trace des lignes et des formes en coordonnées monde,
`SceneRenderer::setDebugView` change ce que l'image affiche, et `FrustumFreeze` fige la vue du culling pour la regarder de
l'extérieur. Aucun n'a de coût tant qu'il n'est pas utilisé.

## Lignes, boîtes, sphères

```cpp
#include <orosmo/renderer/debug_draw.hpp>

namespace rd = orosmo::renderer;
rd::DebugDraw debug;                       // actif par défaut hors NDEBUG, désactivé en release

// À chaque image, entre setCamera et endFrame.
debug.clear();
debug.axes(glm::mat4(1.0f), 3.5f);                                    // X rouge, Y vert, Z bleu
debug.box(minCorner, maxCorner, {0.35f, 0.90f, 1.0f, 1.0f});
debug.line(origin, origin + dir * 4.5f, {1.0f, 0.9f, 0.15f, 1.0f});
debug.sphere(center, 0.22f, {1.0f, 0.9f, 0.15f, 1.0f}, 12);
auto drawn = renderer.drawDebug(debug);    // copie les segments : dessinés dans la passe de scène
```

Code tiré de `apps/demo/src/Scene/demo_scene.cpp`. `drawDebug` copie les segments à l'appel : vider la liste est à vous (`clear`).

| Réglage de `DebugDraw` | Type | Défaut | Effet |
|---|---|---|---|
| `setEnabled(bool)` | `bool` | `true` hors `NDEBUG`, `false` en release | Désactivé, chaque appel revient aussitôt sans rien allouer et `drawDebug` ne fait rien. |
| `setDepthTest(bool)` | `bool` | `true` | Vrai : les segments cachés par la scène ne sont pas dessinés. |

`sphere(center, radius, color, segments = 24)` trace trois grands cercles. Dans la démo, `debugDraw` est un réglage du panneau
de la scène.

## Vues de debug

`setDebugView(DebugView)` remplace l'image par une vue de diagnostic. `kNone` (défaut) : image et cibles inchangées.

| Valeur | Nom | Affiche |
|---|---|---|
| `kNone` | `none` | Rien de changé. |
| `kNormals` | `normals` | Normales monde. Demande l'AOV `kNormals` : le programme de maillage doit être compilé avec `aovShaderDefines()`. |
| `kAlbedo` | `albedo` | Couleur de base sans éclairage (programme de remplacement). |
| `kOverdraw` | `overdraw` | Nombre de fragments par pixel, en gris (1/8 par couche, 8 au plus), sans test de profondeur. |
| `kLod` | `lod` | Une couleur par niveau de détail (objets instanciés, avec `VisibilityOptions::lodColors`). |
| `kWireframe` | `wireframe` | Arêtes blanches sur fond sombre. |
| `kMotion` | `motion` | Vecteurs de mouvement (AOV `kMotion`) : gris = immobile, teinte = direction. |

`debugViewName` et `debugViewFromName` convertissent depuis et vers ces noms ; `lodDebugColor(level)` donne la couleur d'un niveau
(rouge, orange, jaune, vert, cyan, bleu, violet, puis cycle).

L'ordre compte : `normals` et `motion` demandent leur AOV, donc l'appel doit précéder le chargement du programme de maillage.

```cpp
if (auto set = renderer.setDebugView(rd::DebugView::kNormals); !set) { /* set.error().message */ }
// Après setDebugView seulement : compiler le programme avec les macros des AOV actifs.
auto program = shaders.loadGraphics(shaderDir / "mesh.vert", shaderDir / "mesh.frag", renderer.aovShaderDefines());
```

Pour la vue `lod`, mettez `VisibilityOptions::lodColors = true` dans `ObjectSet::prepare` (seul le culling CPU les produit).
Dans la démo : `--debug-view none|normals|albedo|overdraw|lod|wireframe|motion` (voir
[Options de la démo](/reference/options-demo/)).

## Sorties auxiliaires (AOV)

Un AOV est une cible couleur de plus dans la passe de scène. Rien n'est alloué tant que personne n'en demande :
`SceneRenderer::declareAov(Aov)`, `RenderFeature::requiredAovs()`, `setMotionVectorsEnabled` ou une vue de debug.

| Aov | Contenu |
|---|---|
| `kMotion` | Mouvement NDC par image, sans le jitter. |
| `kNormals` | Normale monde, encodage octaédrique. |
| `kMaterial` | Rugosité et métal. |
| `kObjectId` | Identifiant d'objet (`Mesh::setObjectId`) ; 0 : aucun. |
| `kLinearDepth` | Distance le long de l'axe de la vue ; 0 : pas de géométrie. |
| `kEmission` | Lumière émise. |
| `kAlbedo` | Couleur de base avant éclairage. |

`releaseAov` retire la demande, `activeAovs()` donne l'ensemble effectif. Le programme de maillage s'écrit pour les AOV actifs
avec `aovShaderDefines()`, à recharger si l'ensemble change (voir [Shaders](/guides/avance/shaders/)). Un matériau
transparent n'écrit aucun AOV par défaut ; `Mesh::setAovMaterial` en demande.

## Geler le frustum

`FrustumFreeze` garde la `CullView` d'une image. Une fois gelé, `apply` rend la vue gelée quelle que soit la caméra : on se
déplace et on voit ce que la pyramide gelée a retenu.

```cpp
rd::FrustumFreeze freeze;
if (!freeze.frozen()) freeze.freeze(view, camera.projectionMatrix() * camera.viewMatrix());
view = freeze.apply(view);                 // à donner à ObjectSet::prepare
freeze.drawFrozen(debug);                  // dessine la pyramide gelée (12 arêtes) dans un DebugDraw
```

`unfreeze()` rend la vue réelle. `drawFrozen(draw, color, farDepth = 0.01f)` : le plan lointain est choisi pour l'affichage,
car la projection a un plan lointain infini. Dans la démo : `--freeze-frustum` avec `--instances N`.

## Panneau de performances

La démo affiche un panneau de performances avec `--hud off|fps|full|graph`, et Ctrl+F passe au niveau suivant.

## Limites

- Pas de vue des cascades d'ombres ni des clusters forward+.
- Les objets instanciés ignorent `albedo`, `overdraw` et `normals` ; seule `lod` les concerne.
- Les objets instanciés et GPU n'écrivent aucun AOV.
- Pas d'outils de debug dans l'interface (profileur, console).
- Détail : [État du moteur](/projet/etat/).

## Voir aussi

- [Instancing et culling](/guides/rendu-3d/instancing-culling/)
- [LOD](/guides/rendu-3d/lod/)
- [Profilage](/guides/noyau/profilage/)
