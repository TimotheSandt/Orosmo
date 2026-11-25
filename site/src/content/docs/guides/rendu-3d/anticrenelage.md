---
title: "Anticrénelage"
description: "Choisir FXAA, SMAA ou TAA, les enregistrer dans le bon ordre et connaître leurs contraintes ; échelle de rendu."
sidebar:
  order: 8
  label: Anticrénelage
---

Les noms des variables (`device`, `renderer`…) et les chemins de ressources des extraits sont expliqués dans les [conventions des exemples](/demarrer/premiere-application/#conventions-des-exemples).

Trois techniques sont des plugins du graphe, comme les post-traitements : FXAA et SMAA lissent les contours de l'image
affichée, le TAA accumule les images successives grâce à un léger décalage de la caméra (jitter) et aux vecteurs de
mouvement. Une seule est active à la fois, choisie par `rd::AntiAliasing::select`.

Les exemples suivent les noms de [Dessiner une scène](/guides/rendu-3d/scene/#noms-utilisés-dans-les-exemples) (`device`, `renderer`, `shaders`, `pipelines`, `shaderDir`, `lights`).

## Exemple minimal

FXAA ou SMAA seuls :

```cpp
auto context = rd::PostContext::create(device, shaders, pipelines, shaderDir);
if (!context) { /* erreur */ }
auto edge = rd::addEdgeAntiAliasing(renderer, *context);   // FXAA et SMAA enregistrés, désactivés
if (!edge) { /* erreur */ }
rd::AntiAliasing aa = *edge;
if (auto r = aa.select(rd::AntiAliasingMode::kSmaa); !r) { /* erreur */ }   // ou kFxaa, kNone
```

Dans la démo :

```bash
apps/demo/bin/debug/main --aa smaa          # none (défaut), fxaa, smaa ou taa ; combinable avec --post
```

## Ordre d'enregistrement

Le TAA travaille sur la scène HDR, avant l'exposition et le tonemapping ; FXAA et SMAA travaillent sur les valeurs
affichées, après. L'ordre des passes étant l'ordre d'enregistrement :

1. plugins de la scène (ombres, transparents, 2D dans le monde) ;
2. `addTemporalAntiAliasing` ;
3. `addPostEffects` ;
4. `addEdgeAntiAliasing` ;
5. superpositions (interface).

```cpp
auto context = rd::PostContext::create(device, shaders, pipelines, shaderDir);
if (!context) return std::unexpected(context.error());
auto taa = rd::addTemporalAntiAliasing(renderer, *context);        // après les plugins de la scène
if (!taa) return std::unexpected(taa.error());
auto fx = rd::addPostEffects(renderer, device, shaders, pipelines, shaderDir);
if (!fx) return std::unexpected(fx.error());
auto edge = rd::addEdgeAntiAliasing(renderer, *context);
if (!edge) return std::unexpected(edge.error());
rd::AntiAliasing aa = *edge;
aa.taa = *taa;
if (auto r = aa.select(rd::AntiAliasingMode::kTaa); !r) return r;
```

Source : `src/renderer/README.md` (§ Anticrénelage) et `Game::createPostEffects`
([`apps/demo/src/Game/Game.cpp`](https://github.com/TimotheSandt/Orosmo/blob/dev/apps/demo/src/Game/Game.cpp)).
`select` renvoie `kUnsupported` si la technique demandée n'est pas enregistrée.

## TAA : sorties auxiliaires et programmes

Le TAA lit deux sorties auxiliaires (AOV) de la scène : le mouvement (`Aov::kMotion`) et la profondeur linéaire
(`Aov::kLinearDepth`). Les programmes de maillage doivent les écrire, donc être compilés avec `aovShaderDefines()` une fois
ces AOV connus du renderer. Deux façons :

- enregistrer le TAA d'abord, puis compiler les programmes (le plugin les exige par `requiredAovs`, même désactivé) ;
- ou, comme la démo, déclarer les AOV avant de compiler, puis enregistrer le TAA plus tard :

```cpp
for (const rd::Aov aov : rd::AovSet{rd::Aov::kMotion, rd::Aov::kLinearDepth}.list())
    if (auto r = renderer.declareAov(aov); !r) { /* kUnsupported : pas assez de cibles */ }
auto program = shaders.loadGraphics(shaderDir / "mesh.vert", shaderDir / "mesh.frag", renderer.aovShaderDefines());
```

:::caution
Un objet qui n'écrit pas l'AOV de mouvement (objets instanciés de `InstanceRenderer`, transparents, rendu GPU) prend le
mouvement de ce qui est derrière lui et peut laisser une traînée. La démo refuse `--aa taa` avec `--instances` et passe
alors en `none` ; le moteur, lui, ne refuse rien.
:::

Le jitter est la suite de Halton(2, 3) du `SceneRenderer`, appliquée seulement quand le TAA est actif (16 positions,
`setJitterSequenceLength`). Après une coupure de scène ou une téléportation, appelez `taa->resetHistory()` ; c'est fait
tout seul à la première image, après un changement de taille et après une image où le plugin était désactivé.

## Réglages

| Option (`FxaaSettings`) | Type | Défaut | Effet |
|---|---|---|---|
| `edgeThreshold` | `float` | `0.125` | Contraste local minimal (relatif à la luminance maximale du voisinage). |
| `edgeThresholdMin` | `float` | `0.0312` | Contraste minimal absolu (zones sombres). |
| `subpixel` | `float` | `0.75` | Force du lissage sous-pixel ; 0 : aucun. |

| Option (`SmaaSettings`) | Type | Défaut | Effet |
|---|---|---|---|
| `threshold` | `float` | `0.1` | Contraste de luminance minimal d'un contour. |
| `localContrastFactor` | `float` | `2` | Ignore un contour plus faible que (contraste voisin maximal / facteur). |
| `maxSearchSteps` | `uint32_t` | `16` | Texels cherchés de chaque côté le long d'un contour, 1 à 64. |

| Option (`TaaSettings`) | Type | Défaut | Effet |
|---|---|---|---|
| `maxAccumulatedFrames` | `uint32_t` | `12` | Images accumulées au plus (poids minimal 1 / N), 2 à 64. |
| `clipGamma` | `float` | `1` | Écrêtage de l'historique : moyenne ± gamma × écart type du voisinage. |
| `clampHistory` | `bool` | `true` | Faux : aucun écrêtage (debug). |
| `depthTolerance` | `float` | `0.1` | Écart relatif de profondeur toléré avant de rejeter l'historique. |
| `motionTolerancePixels` | `float` | `2` | Écart de mouvement (pixels) au-delà duquel l'historique est rejeté progressivement. |

Chaque plugin expose `settings()` en référence : `aa.smaa->settings().threshold = 0.05f;`.

## Échelle de rendu

`renderer.setRenderScale(0.5f)` rend la scène à la moitié de la taille de sortie, puis l'agrandit par le mode de mise à
l'échelle actif. Le seul mode fourni est `rd::BilinearBlitUpscaleMode` (`"bilinear-blit"`, actif par défaut), une copie
bilinéaire. Les effets et l'anticrénelage s'exécutent à la résolution de rendu, avant l'agrandissement.

`IUpscaleMode` et `IAdvancedUpscaleMode` permettent d'écrire son propre mode (`registerUpscaleMode`,
`setActiveUpscaleMode(nom)`) ; aucun mode du type FSR n'est livré.

## Limites

- Le SMAA est une version simplifiée : pas de détection des diagonales, pas d'arrondi des coins, pas de seuil guidé par la
  profondeur.
- Pas de MSAA : le RHI n'a pas encore de cibles multi-échantillons.
- Le TAA n'utilise pas de filtre Catmull-Rom et ne retire pas le jitter de l'image courante ; les objets sans vecteurs de
  mouvement peuvent laisser une traînée.
- Pas de résolution dynamique automatique (seulement `setRenderScale`) ni de mise à l'échelle temporelle du type FSR.

Détail et suivi : [État du projet](/projet/etat/).

## Voir aussi

- [HDR et post-traitement](/guides/rendu-3d/post-traitement/)
- [Dessin et vues de debug](/guides/rendu-3d/debug-visuel/) (AOV)
- [Instancing et culling](/guides/rendu-3d/instancing-culling/)
