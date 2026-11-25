---
title: "Lumières et forward+"
description: "Ajouter des lumières ambiante, directionnelle, ponctuelles et projecteurs, puis passer au forward+ clusterisé quand il y en a beaucoup."
sidebar:
  order: 3
  label: Lumières
---

Les noms des variables (`device`, `renderer`…) et les chemins de ressources des extraits sont expliqués dans les [conventions des exemples](/demarrer/premiere-application/#conventions-des-exemples).

Les lumières vivent dans un `rd::LightManager`, qui les range dans un tampon de stockage lu par les shaders. Le
`SceneRenderer` le lit par un pointeur donné à `setLights`. Par défaut chaque fragment parcourt toutes les lumières
(forward) ; avec beaucoup de lumières ponctuelles, le forward+ ne lui donne que celles de son cluster.

Les exemples suivent les noms de [Dessiner une scène](/guides/rendu-3d/scene/#noms-utilisés-dans-les-exemples) (`device`, `renderer`, `shaders`, `pipelines`, `shaderDir`, `lights`).

## Exemple minimal

```cpp
auto lightsResult = rd::LightManager::create(device);    // capacité initiale 16, le tampon grandit au besoin
if (!lightsResult) { /* erreur */ }
rd::LightManager &lights = *lightsResult;
lights.setAmbientLight({1.0f, 1.0f, 1.0f}, 0.35f);
lights.addLight(rd::directionalLight({0.6f, 0.8f, 0.5f} /* VERS la lumière */, {1.0f, 0.96f, 0.9f}, 0.85f));
lights.addLight(rd::pointLight({2.0f, 1.0f, 0.0f}, {1.0f, 0.5f, 0.2f}, 0.8f, 2.0f, 0.5f));
lights.addLight(rd::spotLight({0.0f, 4.0f, 0.0f}, {0.0f, -1.0f, 0.0f} /* sens du faisceau */, {0.2f, 1.0f, 0.3f}, 1.0f,
                               std::cos(glm::radians(25.0f)), std::cos(glm::radians(18.0f)), 0.1f, 0.1f));
if (auto r = lights.upload(); !r) { /* erreur */ }       // sans upload, le GPU garde l'ancien contenu
renderer.setLights(&lights);                              // pointeur gardé : `lights` doit vivre plus longtemps
```

La lumière directionnelle est celle du gabarit `orosmo init`
([`tools/engine-sdk/template/src/main.cpp`](https://github.com/TimotheSandt/Orosmo/blob/dev/tools/engine-sdk/template/src/main.cpp)) ;
les deux autres sont adaptées de [`tests/renderer/forward_plus_tests.cpp`](https://github.com/TimotheSandt/Orosmo/blob/dev/tests/renderer/forward_plus_tests.cpp).

## Sens des champs, type par type

Le champ `direction` n'a pas le même sens selon le type : c'est le piège le plus courant.

| Type (`LightType`) | Fabrique | `direction` | Autres champs |
|---|---|---|---|
| `kAmbient` | `ambientLight(couleur, force)` ou `setAmbientLight` | inutilisé | couleur × force partout. |
| `kDirectional` | `directionalLight(direction, couleur, force)` | de la surface **vers** la lumière : `{0, 1, 0}` = soleil au zénith | aucun. |
| `kPoint` | `pointLight(position, couleur, force, a, b)` | inutilisé | `position` en coordonnées monde ; atténuation `a`, `b`. |
| `kSpot` | `spotLight(position, direction, couleur, force, outerCone, innerCone, a, b)` | sens du **faisceau**, de la lumière vers la scène : `{0, -1, 0}` éclaire vers le bas | cônes en **cosinus**, atténuation `a`, `b`. |

Unités et formules :

- l'atténuation vaut `1 / (a d² + b d + 1)` ; ce sont des coefficients, pas une portée en mètres. La portée est calculée :
  là où `couleur × force × 1,5 × atténuation` tombe sous 1/256 (`rd::lightRange(light)` donne cette distance) ; au-delà, la
  lumière n'apporte rien ;
- `outerCone` et `innerCone` sont des cosinus d'angles (`std::cos(glm::radians(deg))`), donc `outerCone < innerCone` :
  pleine lumière dans le cône intérieur, rien hors du cône extérieur, transition linéaire entre les deux ;
- `a = b = 0` : aucune atténuation, portée maximale (`rd::kLightMaxRange`).

Les champs `mobility` et `shadowMode` de `Light` servent aux [ombres](/guides/rendu-3d/ombres/) ; par défaut une lumière
ne projette pas d'ombre.

## Modifier une lumière

On ne modifie une lumière existante que par `setLight(index, light)`, puis `upload()`. `light(i)` et `lights()` sont en
lecture seule : changer une copie ne change rien au GPU.

```cpp
rd::Light sun = rd::directionalLight(sunDirection, sunColor, sunIntensity);
if (auto r = lights.setLight(0, sun); !r) { /* kOutOfRange : index périmé */ }
if (auto r = lights.upload(); !r) { /* erreur */ }
```

Adapté de `DemoScene::syncLights` ([`apps/demo/src/Scene/demo_scene.cpp`](https://github.com/TimotheSandt/Orosmo/blob/dev/apps/demo/src/Scene/demo_scene.cpp)).
Autres appels : `addLights(vector)`, `removeLight(index)`, `removeLight(light)` (premier élément égal), `resetAmbientLight()`.

## Passer au forward+

Le forward+ ajoute une pré-passe de profondeur et une passe compute qui range les lumières par cluster (cases de l'écran ×
tranches de profondeur). Les maillages doivent être compilés avec `rd::forwardPlusDefines()`.

```cpp
bool forwardPlus = false;
auto enabled = rd::enableForwardPlus(renderer, device, shaders, pipelines, shaderDir);
if (enabled) forwardPlus = true;                 // plugin enregistré, pré-passe activée
else { /* kUnsupported : pas de compute ; on reste en forward */ }

// Ensuite seulement, compiler le programme de maillage :
std::vector<rd::Define> defines = forwardPlus ? rd::forwardPlusDefines() : std::vector<rd::Define>{};
auto program = shaders.loadGraphics(shaderDir / "mesh.vert", shaderDir / "mesh.frag", defines);
```

Adapté de `Game::init` ([`apps/demo/src/Game/Game.cpp`](https://github.com/TimotheSandt/Orosmo/blob/dev/apps/demo/src/Game/Game.cpp)).
Pour les matériaux PBR, passez `rd::forwardPlusDefines()` en macros en plus de `PbrShaderCache::program`.
`rd::disableForwardPlus(renderer)` retire le plugin et la pré-passe ; les maillages doivent alors reprendre le programme sans
ces macros. Le forward et le forward+ donnent la même image tant que les budgets ne sont pas dépassés.

| Option (`ForwardPlusSettings`) | Type | Défaut | Effet |
|---|---|---|---|
| `gridX`, `gridY` | `uint32_t` | `16`, `9` | Cases de l'écran (en NDC, indépendantes de la résolution). |
| `gridZ` | `uint32_t` | `24` | Tranches de profondeur, exponentielles. |
| `maxLightsPerCluster` | `uint32_t` | `128` | Lumières listées par cluster ; les suivantes sont ignorées. |
| `maxLights` | `uint32_t` | `65536` | Lumières examinées, dans l'ordre du tampon. |
| `nearDepth` | `float` | `0.1` | Début de la tranche 1 (m). |
| `farDepth` | `float` | `500` | Fin de l'avant-dernière tranche ; la dernière va à l'infini. |

Les réglages se passent à `enableForwardPlus(..., settings)` ou plus tard à `ForwardPlusLighting::setSettings` (refusés si
invalides). Debug : `rd::forwardPlusDefines(true)` peint chaque fragment selon le nombre de lumières de son cluster
(magenta : débordement) ; `readClusterStats()` donne les mêmes chiffres côté CPU (lecture lente).

## Combien de lumières

Mesure de M5 avec 1000 lumières ponctuelles (debug -O1, GPU Intel), passe `scene` : forward 93,6 ms, forward+ 5,7 ms sous
OpenGL ; 37,5 ms puis 2,9 ms sous Vulkan (`docs/releases/M5.md`). Pour le reproduire :

```bash
apps/demo/bin/debug/main --lighting forwardplus --lights 1000
```

`--lights N` place N lumières ponctuelles déterministes ; sans `--lighting forwardplus`, la démo reste en forward.

## Limites

- Pas de lumières de surface ; dans les clusters, un projecteur est traité comme une sphère (plus large que son cône).
- Pas d'éclairage précalculé (lightmaps).
- En forward+ : un seul `LightManager` par image, et les dessins externes (`drawCustom`, objets instanciés) ne passent pas
  par la pré-passe et lisent toutes les lumières.
- Forward et forward+ peuvent différer d'un pixel sur la démo (effet de la pré-passe de profondeur).

Détail et suivi : [État du projet](/projet/etat/).

## Voir aussi

- [Ombres](/guides/rendu-3d/ombres/)
- [Éclairage d'environnement et sondes](/guides/rendu-3d/environnement-ibl/)
- [Options de la démo](/reference/options-demo/)
