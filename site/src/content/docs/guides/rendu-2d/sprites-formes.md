---
title: "Sprites, formes et caméra 2D"
description: "Dessiner des sprites instanciés, des lignes, rectangles, cercles et polygones avec Renderer2D et une Camera2D, en mode pixel parfait si besoin."
sidebar:
  order: 1
---

`Renderer2D` dessine du 2D dans une texture : des sprites (un quad texturé par instance) et des formes pleines ou en
contour. Les dessins sont triés par couche `z`, puis par type, puis par matériau (texture) ; les dessins consécutifs du même
matériau forment un lot, donc un seul draw call. La vue vient d'une `Camera2D`. Pour afficher ce 2D dans une scène 3D ou
par-dessus, voir [Mélanger 2D et 3D](/guides/rendu-2d/melange-2d-3d/).

## Exemple minimal

Code adapté de `tests/renderer/renderer2d_tests.cpp`. `target` est une texture que vous possédez (format `kRGBA8Unorm` ici),
`encoder` un `rhi::CommandEncoder` sans passe ouverte.

```cpp
#include <orosmo/renderer/renderer2d.hpp>

namespace rd = orosmo::renderer;
namespace rhi = orosmo::rhi;

auto r2d = rd::Renderer2D::create(*device, shaders, pipelines, shaderDirectory);   // shaders/rhi
if (!r2d) { /* r2d.error().message */ }

auto material = (*r2d)->material(spriteTexture);       // rd::Texture : doit vivre plus longtemps que son usage
rd::Camera2D camera;
camera.setViewport(640, 360);                           // pixels de la cible
camera.setCenter({320.0f, 180.0f});                     // zoom 1 : une unité de monde = un pixel

(*r2d)->begin(camera);
(void)(*r2d)->draw({.material = *material, .position = {100.0f, 50.0f}, .size = {64.0f, 64.0f}});
(*r2d)->rect({10.0f, 10.0f}, {200.0f, 4.0f}, {1.0f, 1.0f, 1.0f, 1.0f});
auto rendered = (*r2d)->render(*encoder, target, rhi::Format::kRGBA8Unorm, {640, 360});
```

`render` trie, regroupe, dessine tout ce qui a été déposé, puis vide les dépôts (succès ou échec). Il garde le contenu de la
cible par défaut (`loadExisting = true`) ; passez `false` et une `clearColor` pour l'effacer d'abord.

## Sprites

`Renderer2D::material(texture, nearest = false)` enregistre une texture et rend un `SpriteMaterial` (id 0 : invalide). Un même
handle donne le même matériau. Un sprite est un `Sprite` :

| Champ de `Sprite` | Type | Défaut | Effet |
|---|---|---|---|
| `material` | `SpriteMaterial` | invalide | Texture du sprite ; un matériau inconnu est refusé (`kInvalidArgument`). |
| `position` | `glm::vec2` | `(0, 0)` | Position du pivot, en unités de monde (Y vers le haut). |
| `size` | `glm::vec2` | `(1, 1)` | Taille en unités de monde. |
| `pivot` | `glm::vec2` | `(0.5, 0.5)` | (0, 0) bas-gauche, (1, 1) haut-droit. |
| `rotation` | `float` | 0 | Radians, sens trigonométrique, autour du pivot. |
| `uvMin`, `uvMax` | `glm::vec2` | `(0, 0)`, `(1, 1)` | Rectangle de la texture : coin haut-gauche et coin bas-droit (atlas). |
| `color` | `glm::vec4` | blanc | Teinte multipliée au texel, non prémultipliée. |
| `z` | `float` | 0 | Couche : petit `z` derrière. |

Un matériau est une seule texture : pas de tableau de textures, donc utilisez un atlas avec `uvMin` et `uvMax`. Après avoir
détruit ou recréé une texture, appelez `clearMaterials()`.

## Formes

Toutes prennent une couleur non prémultipliée (mélange alpha) et un `z` facultatif.

| Appel | Effet |
|---|---|
| `line(a, b, thickness, color, z)` | Segment épais. |
| `polyline(points, thickness, color, closed, z)` | Ligne brisée ; `closed` relie le dernier point au premier. |
| `rect(position, size, color, rotation, z)` | Rectangle plein, `position` = coin bas-gauche, rotation autour du centre. |
| `rectOutline(position, size, thickness, color, z)` | Contour de rectangle. |
| `circle(center, radius, color, segments, z)` | Disque ; `segments` 0 : choisi selon la taille à l'écran. |
| `circleOutline(center, radius, thickness, color, segments, z)` | Anneau, `thickness` au plus `2 * radius`. |
| `polygon(points, color, z)` | Polygone simple (convexe ou non, sens quelconque). Moins de 3 points ou dégénéré : ignoré. |

## Caméra 2D

| Appel de `Camera2D` | Défaut | Effet |
|---|---|---|
| `setCenter(vec2)`, `move(delta)` | `(0, 0)` | Point du monde au centre de la vue. |
| `setZoom(float)` | 1 | Pixels d'écran par unité de monde. |
| `setRotation(float)` | 0 | Radians ; ignorée en pixel parfait. |
| `setViewport(w, h)` | 1 x 1 | Taille de la vue en pixels. |
| `setPixelPerfect(bool)` | `false` | Voir ci-dessous. |

Le monde a X vers la droite et Y vers le haut ; l'écran a son origine en haut à gauche. `worldToScreen`, `screenToWorld` et
`visibleBounds()` convertissent et délimitent. En pixel parfait, l'échelle est la partie entière du zoom (au moins 1), le
centre est aligné sur la grille des pixels, la rotation est ignorée, les sprites non tournés sont alignés sur les pixels et
l'échantillonnage est au plus proche. `effectiveZoom()` et `effectiveCenter()` donnent les valeurs réellement utilisées.

## Texte et interface

`Renderer2D` ne trace pas de glyphes. Le texte et l'interface passent par une `DrawList` dessinée par `CanvasRenderer::render`
sur la même cible, après `Renderer2D::render` et avec `loadExisting = true` : elle se pose sur le 2D. Voir
[Texte et polices](/guides/rendu-2d/texte/). `Renderer2DStats` (`stats()`) donne `sprites`, `shapes`, `vertices`, `drawCalls`
et `bufferBytes` du dernier `render`.

## Limites

- R2D-01, R2D-02, R2D-03 (Fait) : `tests/renderer/renderer2d_tests.cpp`.
- G160 : un matériau est une seule texture ; à `z` égal, des matériaux différents peuvent s'intercaler (donnez des `z`
  différents pour un ordre précis) ; formes sans anticrénelage ; polygone découpé en O(n²).
- R2D-04 (cartes de tuiles) et R2D-07 (éclairage 2D) : absents.

## Voir aussi

- [Texte et polices](/guides/rendu-2d/texte/)
- [Mélanger 2D et 3D](/guides/rendu-2d/melange-2d-3d/)
- [État du moteur](/projet/etat/)
