---
title: Cadence d'images, mode sans fenêtre, captures
description: Régler la vsync et la limite d'images par seconde, rendre sans fenêtre, enregistrer et comparer des images.
sidebar:
  order: 3
---

Trois outils se suivent : `FramePacer` choisit la cadence, un périphérique sans fenêtre permet de rendre sans écran, et
`savePng` / `compareImages` enregistrent et comparent le résultat. Les en-têtes sont `orosmo/renderer/frame_pacing.hpp` et
`orosmo/renderer/image.hpp`.

## Exemple minimal : la cadence

```cpp
#include <orosmo/renderer/frame_pacing.hpp>
namespace rd = orosmo::renderer;

rd::FramePacer pacer;
pacer.bind([&](bool enabled) { device->setVSync(enabled); },
           [&](int maxFps) { window.setMaxFPS(maxFps); });   // 0 : pas de limite

pacer.setMode(rd::FramePacingMode::kVSync);   // synchronisée sur l'écran
pacer.setFpsLimit(120);                       // limitée à 120 images par seconde, vsync coupée
pacer.setMode(rd::FramePacingMode::kUnlimited);
```

`FramePacer` ne connaît ni la fenêtre ni le périphérique : vous lui donnez deux fonctions et il les appelle quand une
valeur change (pas à chaque image). Source : `Game::createGraphics` dans `apps/demo/src/Game/Game.cpp`.

| Mode | Vsync | Limiteur | Usage |
|---|---|---|---|
| `kVSync` (défaut) | oui | non | cadence de l'écran |
| `kLimited` | non | `fpsLimit` images par seconde | cadence fixe plus basse |
| `kUnlimited` | non | non | mesures |

`FramePacing { mode, fpsLimit }` : `fpsLimit` vaut 60 par défaut, n'est lu que par `kLimited` et est borné à
`[kMinFpsLimit, kMaxFpsLimit]` = [10, 1000] (`clampFpsLimit`). `describeFramePacing` donne le texte du panneau
(« vsync », « limit 120 FPS », « unlimited »). Pour la démo : `--vsync [on|off]` et `--fps-limit N`, qui sont
incompatibles quand la vsync est demandée explicitement.

:::caution
La limite passe par `Window::setMaxFPS`, appelée dans `Window::newFrame` : elle suppose une boucle qui appelle `newFrame`
à chaque image. Sans fenêtre, il n'y a ni vsync ni limite.
:::

## Rendre sans fenêtre

Une `DeviceDesc` dont `surface.nativeWindow` reste nul crée un périphérique hors écran. `Window::init` n'est pas appelé.

```cpp
rhi::DeviceDesc desc;
desc.backend = rhi::Backend::kOpenGL;
desc.surface.size = {1280, 720};            // taille du backbuffer hors écran
auto created = rhi::createDevice(desc);
rhi::Device &device = **created;       // à tester avant : voir « Créer le périphérique »

auto backbuffer = device.beginFrame();  // une texture RGBA8 de cette taille
// ... mêmes appels que dans la boucle d'une fenêtre ...
(void)device.present();                 // hors écran : libère seulement les ressources détruites
```

Ce qui change : le backbuffer a la taille de `surface.size` (modifiable par `Device::resize`), `setVSync` est sans effet, et
il n'y a ni entrées ni fenêtre réduite. Avec OpenGL, un affichage reste nécessaire sous Linux : sans serveur X,
lancez sous `xvfb-run -a`. Le projet de démarrage et la démo utilisent ce mode avec `--headless`.

## Enregistrer une image

```cpp
if (auto saved = rd::savePng(device, texture, "sortie/scene.png"); !saved)
    std::cerr << saved.error().message << '\n';
```

`savePng(device, texture, chemin)` lit la texture (RGBA8 ou BGRA8, RGBA8 sRGB, ou RGBA16F ramené à [0, 1] et arrondi
au 1/255), puis écrit le PNG. `readImage` donne une `Image` en mémoire (`width`, `height`, `rgba`, première ligne = haut) et
`loadPng` relit un fichier. Le dossier doit exister. La couleur de la scène avant mise à l'échelle s'enregistre aussi par
`SceneRenderer::saveColorPng(chemin)`, valable après `endFrame`.

La démo enregistre ainsi `scene.png` et `ui.png` :

```bash
apps/demo/bin/debug/main --headless --capture /tmp/out                       # 10 images, puis les deux PNG
apps/demo/bin/debug/main --headless --capture /tmp/out --capture-frames 30   # 30 images
```

Sans fenêtre, le pas de temps est fixe (1/60 s) : le résultat est reproductible d'une exécution à l'autre.

## Comparer deux images

```cpp
auto a = rd::loadPng("ref.png");
auto b = rd::loadPng("sortie/scene.png");
if (a && b)
{
    auto diff = rd::compareImages(*a, *b, /*tolerance=*/ 8);   // même taille obligatoire, sinon kInvalidArgument
    if (diff && diff->outOfTolerance > 0) { /* diff->maxDelta, diff->meanDelta */ }
}
```

`ImageDiff` donne `pixels`, `outOfTolerance` (pixels dont un canal dépasse la tolérance), `maxDelta` et `meanDelta`. En ligne
de commande :

```bash
python3 tools/compare_png.py REF.png CANDIDAT.png --tolerance 8 --max-diff-percent 2 --diff-out diff.png
```

| Option | Effet |
|---|---|
| `--tolerance T` | écart maximal par canal (0 à 255, défaut 0) |
| `--max-diff-pixels N` | nombre de pixels hors tolérance accepté (défaut 0) |
| `--max-diff-percent P` | idem en pourcentage ; remplace `--max-diff-pixels` |
| `--diff-out FICHIER` | écrit une image de différence (noir : identique, rouge : écart amplifié) |

Le script sort avec 0 si les images concordent, 1 sinon (taille différente comprise).

## Limites

- Le mode sans fenêtre n'est vérifié que sous Linux.
- Les captures n'ont pas d'exécutable dédié : elles passent par la démo ou par votre propre code.
- Les images de référence viennent d'un GPU précis ; d'autres pilotes demandent une tolérance (8 sur 255 dans `make demo-capture`).

Voir [État du moteur](/projet/etat/).
