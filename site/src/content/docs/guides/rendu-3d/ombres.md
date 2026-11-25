---
title: "Ajouter des ombres"
description: "Activer les ombres en cascade du soleil et des projecteurs, déclarer les objets qui projettent, régler l'atlas et le cache."
sidebar:
  order: 5
  label: Ombres
---

`rd::ShadowFeature` est un plugin qui tient à jour un atlas d'ombres : des cascades pour la première lumière
directionnelle ombrée et une vue par projecteur ombré. En mode `kCached`, la géométrie fixe est dessinée une fois dans un
cache ; quand rien ne bouge, rien n'est redessiné. Il faut trois choses : le plugin, des objets qui projettent (casters)
et des matériaux qui reçoivent.

Les exemples suivent les noms de [Dessiner une scène](/guides/rendu-3d/scene/#noms-utilisés-dans-les-exemples)
(`device`, `renderer`, `shaders`, `pipelines`, `shaderDir`, `lights`), plus `namespace as = orosmo::assets;`.

## Exemple minimal : un modèle glTF qui reçoit et projette

Le cas le plus courant : des modèles glTF (matériaux PBR) éclairés par un soleil. L'ordre compte : le plugin d'ombres doit
exister avant la création des modèles, car leurs matériaux reçoivent les liaisons de l'atlas à la création.

```cpp
// 1. Le plugin, avant les modèles et les programmes de maillage
auto created = rd::ShadowFeature::create(device, shaders, pipelines, shaderDir);
if (!created) { /* created.error().message */ }
auto added = renderer.addFeature(std::move(*created));
if (!added) { /* erreur */ }
auto *shadows = static_cast<rd::ShadowFeature *>(*added);   // non possédé : le renderer le garde

// 2. Un soleil qui projette (défaut : kNone, aucune ombre)
rd::Light sun = rd::directionalLight({0.4f, 0.8f, 0.3f} /* vers la lumière */, {1.0f, 0.96f, 0.9f}, 1.0f);
sun.shadowMode = rd::ShadowMode::kCached;
lights.addLight(sun);
if (auto r = lights.upload(); !r) { /* erreur */ }
shadows->setLights(&lights);                                 // non possédé, comme pour SceneRenderer::setLights

// 3. Les récepteurs : RenderModelOptions::shadows (aucune macro en plus n'est nécessaire)
as::TextureCache textures(device);
rd::PbrShaderCache pbr(shaders, shaderDir);
auto defaults = rd::PbrDefaults::create(device);
auto level = as::loadGltf(GET_RESOURCE_PATH("models/niveau.glb"));
if (!defaults || !level) { /* erreur */ }
as::RenderModelOptions options;
options.shadows = shadows;                                    // matériaux récepteurs (bit kPbrShadows)
auto levelRender = as::RenderModel::create(device, *level, textures, pbr, *defaults, options);
if (!levelRender) { /* erreur */ }

// 4. Les casters : chaque maillage du modèle, fixe ici (gardé dans le cache)
for (std::size_t i = 0; i < levelRender->meshCount(); ++i) {
    auto id = shadows->addMeshCaster(levelRender->mesh(i), rd::Aabb::empty(), rd::CasterMobility::kStatic);
    if (!id) { /* kUnsupported : maillage à données d'instance */ }
}
```

À chaque image, rien de plus : `levelRender->draw(renderer, camera)` entre `setCamera` et `endFrame`, comme sans ombres.
`rd::Aabb::empty()` est une boîte invalide : le maillage est dessiné dans toutes les vues d'ombre, sans culling. Pour un
grand niveau, passez la boîte locale du maillage (le culling par vue d'ombre en profite).

Recevoir et projeter sont indépendants : un sol peut recevoir sans être caster, un objet peut projeter sans recevoir. Les
casters ne sont pas possédés : appelez `shadows->removeCaster(id)` avant de détruire le modèle.

Dans la démo (maillages écrits à la main, même plugin) :

```bash
apps/demo/bin/debug/main --shadows on
```

![Scène de la démo : portique sur un dallage, ombres en cascade du soleil](../../../../assets/captures/scene-ombres.webp)

Capture de la démo avec `--shadows on`.

## Un modèle animé (skinning)

Un modèle créé avec `RenderModelOptions::skinnable = true` et déformé par `anim::SkinnedModel` (voir
[Animer un personnage](/guides/animation/squelettes/)) se déclare de la même façon : `options.shadows` pour recevoir,
`addMeshCaster(render->mesh(i), …, rd::CasterMobility::kMovable)` pour projeter. Le caster lit le tampon de sommets du
maillage, celui que le compute de skinning réécrit au début de l'image : l'ombre suit la pose.

:::caution
En mode `kCached`, une ombre mobile n'est redessinée que si la matrice d'un caster change. Un personnage qui s'anime sur
place (bras qui bougent, même position) garde alors l'ombre d'une ancienne pose. Pour un personnage animé, donnez au soleil
`sun.shadowMode = rd::ShadowMode::kDynamic` : chaque vue due est redessinée à chaque image. Ce comportement est déduit du
code du plugin ; ni les tests ni la démo (`--scene anim` n'active pas les ombres) ne le couvrent encore.
:::

## Un maillage écrit à la main

Un `rd::Mesh` au format de `mesh.vert` (position, normale, couleur) reçoit par la variante `OROSMO_SHADOWS` de `mesh.frag`,
que le plugin compile, et par le groupe 1 des ombres :

```cpp
// Macros des autres plugins (forward+, sorties auxiliaires), ou vide
std::vector<rd::Define> meshDefines = renderer.aovShaderDefines();
auto receiver = shadows->receiverProgramWith(meshDefines);   // mesh.vert + mesh.frag avec OROSMO_SHADOWS
if (!receiver) { /* erreur */ }
ground.setProgram(*receiver);                                  // ground, structure : rd::Mesh &, créés depuis
ground.setMaterialGroup(shadows->receiverGroup());              // groundData et structureData (rd::MeshData)
structure.setProgram(*receiver);
structure.setMaterialGroup(shadows->receiverGroup());

rd::Aabb bounds = rd::Aabb::empty();                           // boîte locale : positions des sommets
for (std::size_t v = 0; v + 2 < structureData.vertices.size(); v += 9)   // 9 flottants par sommet
    bounds.merge({{structureData.vertices[v], structureData.vertices[v + 1], structureData.vertices[v + 2]},
                  {structureData.vertices[v], structureData.vertices[v + 1], structureData.vertices[v + 2]}});
auto id = shadows->addMeshCaster(structure, bounds, rd::CasterMobility::kStatic);
```

Adapté de `DemoScene::init` et `registerCaster`
([`apps/demo/src/Scene/demo_scene.cpp`](https://github.com/TimotheSandt/Orosmo/blob/dev/apps/demo/src/Scene/demo_scene.cpp)).
Un maillage qui avait déjà un programme le remplace simplement par `setProgram(*receiver)`.

Pour un `PbrMaterial` créé à la main, passez le plugin à la création :
`rd::PbrMaterial::create(device, desc, *defaults, shadows)`, puis le programme de `material->features()`. Un matériau
qui a ses propres textures (hors PBR) ajoute les liaisons 6 et 7 à son groupe par `shadows->appendReceiverBindings(desc)`.
Un modèle glTF déjà créé sans ombres se met à jour par `levelRender->rebuildMaterials(shadows, pbr)`.

## Mobilité et mode d'ombre

La lumière porte `mobility` (`LightMobility`) et `shadowMode` (`ShadowMode`). Le caster porte `CasterMobility` :
`kStatic` (gardé dans le cache) ou `kMovable` (redessiné par-dessus le cache quand il bouge).

| Mobilité de la lumière \ mode | `kNone` | `kBaked` | `kDynamic` | `kCached` |
|---|---|---|---|---|
| `kStatic` (rien ne change) | pas d'ombre | repli `kCached` (pas de cuisson) | tout redessiné à chaque mise à jour | fixe en cache, mobiles par-dessus |
| `kStationary` (couleur seule) | pas d'ombre | repli `kCached` | idem | idem |
| `kDynamic` (tout, défaut) | pas d'ombre | invalide : repli `kDynamic` | idem | cache invalidé à chaque changement |

Une lumière `kStatic` qui bouge ou change de couleur, ou `kStationary` qui bouge, est comptée dans
`ShadowStats::mobilityViolations` et avertie dans le journal ; l'ombre reste juste. `lightStatus(index)` donne le résultat
pour une lumière (`ShadowStatus` : `kRealtime`, `kBakedFallback`, `kUnsupportedType` pour une ponctuelle, `kAtlasFull`…).

## Réglages

| Option (`ShadowSettings`) | Type | Défaut | Effet |
|---|---|---|---|
| `atlasSize` | `uint32_t` | `4096` | Côté de l'atlas (puissance de deux) ; le cache a la même taille. |
| `minTileSize` | `uint32_t` | `64` | Taille minimale d'une tuile avant abandon de la vue. |
| `cascadeCount` | `uint32_t` | `4` | Cascades du soleil, 1 à 4. |
| `cascadeResolution` | `uint32_t` | `1024` | Résolution d'une cascade. |
| `maxDistance` | `float` | `80` | Portée des ombres du soleil depuis la caméra (m). |
| `splitLambda` | `float` | `0.75` | Découpage des cascades : 0 uniforme, 1 logarithmique. |
| `fadeFraction` | `float` | `0.1` | Fondu sur la dernière fraction de `maxDistance`. |
| `depthExtension` | `float` | `60` | Casters pris en compte vers la lumière hors de la cascade (m). |
| `cascadeUpdateInterval` | `array<uint32_t, 4>` | `{1, 1, 2, 4}` | Une cascade n'est mise à jour qu'une image sur N. |
| `maxViewUpdatesPerFrame` | `uint32_t` | `8` | Budget de vues redessinées par image. |
| `pcfRadius` | `int` | `1` | Filtrage PCF sur (2r + 1)² texels. |
| `constantBias`, `slopeBias` | `float` | `1`, `1.5` | Biais en texels (le second × tan de l'angle). |
| `normalOffset` | `float` | `1` | Décalage du récepteur le long de la normale (texels). |
| `spotResolution` | `uint32_t` | `1024` | Résolution d'un projecteur proche. |
| `maxSpotShadows` | `uint32_t` | `8` | Projecteurs ombrés au plus. |

Les réglages se passent à `ShadowFeature::create(..., settings)` ou à `setSettings`. Un changement de `atlasSize` recrée
l'atlas : `receiverGroup()` change, et chaque maillage ou matériau doit être remis à jour (`PbrMaterial::rebuild`,
`RenderModel::rebuildMaterials`).

Par lumière, `setLightParams(index, params)` règle `LightShadowParams` : `priority` (`0`, plus grand servi d'abord),
`maxResolution` (`0` : celle par défaut), et pour un projecteur `spotNear` (`0.1`), `spotRange` (`30`), `fadeStart`
(`40`) et `fadeEnd` (`50`, au-delà plus d'ombre).

## Statistiques

`shadows->stats()` rend un `ShadowStats` de la dernière image : `views`, `viewsUpdated`, `staticRedraws`,
`movableRedraws`, `cacheCopies`, `cacheHits`, `deferred`, `dropped`, `renderPasses`, `drawCalls`… Sur une image où rien ne
bouge en mode `kCached`, `viewsUpdated`, `renderPasses` et `drawCalls` valent 0. La démo les journalise.

## Ombres des objets transparents

Un caster maillage projette par défaut une ombre pleine. `setCasterTransparency(id, {mode, alpha, cutoff})` change ce
comportement par caster (`TransparentShadowMode`) :

| Mode | Ombre | Usage |
|---|---|---|
| `kNone` | aucune | lueurs additives, particules |
| `kFull` (défaut) | pleine, `alpha` ignoré | verre très opaque |
| `kMasked` | pleine si `alpha >= cutoff`, sinon aucune | feuillage, grillages |
| `kDithered` | part d'ombre ≈ `alpha` (trame de Bayer 4×4) | verre, fumée |

`rd::defaultTransparentShadowMode(blendMode)` donne le mode conseillé pour un `BlendMode`, mais n'est jamais appliqué tout
seul. Un caster dont l'opacité s'anime doit être `kMovable`, sinon tout le cache de la vue est redessiné. Démo :
`--scene mixed --shadows on --transparent-shadow dithered`.

## Limites

- Pas d'ombres de contact ni de fondu entre cascades ; le cache d'une cascade est redessiné dès que la caméra bouge d'un
  texel.
- Une seule lumière directionnelle ombrée ; pas d'ombres des lumières ponctuelles ni de pénombre variable.
- Le mode `kBaked` n'a pas de cuisson : il retombe sur une ombre temps réel.
- Ombre des transparents : opacité par objet (pas par texel), pas d'ombre colorée ; les objets instanciés projettent une
  ombre pleine.
- Les foules (`CrowdRenderer`) ne projettent pas d'ombre.

Détail et suivi : [État du projet](/projet/etat/).

## Voir aussi

- [Lumières](/guides/rendu-3d/lumieres/)
- [Objets transparents](/guides/rendu-3d/transparence/)
- [Instancing et culling](/guides/rendu-3d/instancing-culling/) (`addObjectCasters`)
