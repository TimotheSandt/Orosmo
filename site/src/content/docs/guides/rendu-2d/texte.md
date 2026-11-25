---
title: "Texte et polices"
description: "Afficher du texte avec une police TrueType : TextRenderer, atlas MSDF, mesure, mise en page, dessin par CanvasRenderer."
sidebar:
  order: 2
---

`ui::TextRenderer` charge une police TrueType (FreeType), construit un atlas MSDF à la demande et produit des séries de glyphes
dans une `renderer::DrawList`. Il n'appelle aucune API graphique : c'est `CanvasRenderer` qui dessine la liste. C'est le même
chemin pour l'interface et pour du texte posé à la main sur l'image.

## Exemple minimal

Code adapté du gabarit `tools/engine-sdk/template/src/main.cpp` et de `tests/ui/text_tests.cpp`.

```cpp
#include <orosmo/ui/text_renderer.hpp>
#include <orosmo/renderer/canvas_renderer.hpp>

namespace rd = orosmo::renderer;

orosmo::ui::TextRenderer text;
if (!text.init(1280, 720) || !text.loadFont(GET_RESOURCE_PATH("fonts/Roboto-Regular.ttf"), "default", 48))
{
    /* échec : FreeType ou fichier de police */
}

auto canvas = rd::CanvasRenderer::create(*device, shaders, pipelines, shaderDirectory);

// À chaque image, après la scène.
rd::DrawList list;
list.logicalSize = {1280.0f, 720.0f};
text.setDrawListSink(&list);
text.renderText("Bonjour", 20.0f, 20.0f, 0.5f, glm::vec3(1.0f));   // taille = 48 * 0,5 = 24 pixels
text.setDrawListSink(nullptr);
(void)(*canvas)->render(*encoder, list, {outputTexture, orosmo::rhi::Format::kRGBA8Unorm, {1280, 720}}, true);
```

`loadFont(chemin, nom, tailleDeBase)` enregistre la police et la rend active si c'est la première. La taille de base est celle
de l'atlas : l'échelle de `renderText` la multiplie. Sans `setDrawListSink`, `renderText` ne produit rien. Le dernier argument
`true` de `render` garde le contenu de la cible : le texte se pose sur la scène.

## Dessiner

| Appel | Effet |
|---|---|
| `renderText(texte, x, y, scale, color, anchor)` | Une ligne ; `color` en `vec3` (opaque) ou `vec4` (l'alpha multiplie la couverture) ; `anchor` vaut `TextAnchor::TopLeft` par défaut (9 ancrages). |
| `renderTextAdvanced(texte, x, y, params, color, scale)` | Texte mis en page selon un `TextLayoutParams`. |
| `setActiveFont(nom)` | Police des appels suivants. |
| `updateScreenSize(w, h)` | Taille de l'écran après un redimensionnement. |

Le texte est en UTF-8 : ASCII, Latin-1, Latin étendu A et la ponctuation typographique courante. Les glyphes non ASCII sont
chargés au premier texte qui en contient. Une séquence UTF-8 invalide n'est pas dessinée.

## Mise en page

| Champ de `TextLayoutParams` | Type | Défaut | Effet |
|---|---|---|---|
| `maxWidth`, `maxHeight` | `float` | 0 | Limites ; 0 : illimité. |
| `wordWrap` | `bool` | `false` | Retour à la ligne automatique. |
| `overflow` | `TextOverflow` | `Visible` | `Visible`, `Hidden`, `Ellipsis` ou `Scroll`. |
| `horizontalAlign` | `TextAlign` | `Left` | `Left`, `Center`, `Right`. |
| `verticalAlign` | `VerticalAlign` | `Top` | `Top`, `Middle`, `Bottom`. |
| `lineSpacing` | `float` | 1,2 | Espacement des lignes. |
| `padding` | `glm::vec2` | `(0, 0)` | Marge interne. |

## Mesurer

`measureTextWidth(texte, scale)` et `measureText(texte, scale)` (largeur et hauteur) servent à centrer ou à caler.
`calculateLayout(texte, scale, params)` rend un `TextLayout` (lignes, taille totale, `hasOverflow`, défilement).
`calculateMinSize`, `getCenteredPosition` et `findOptimalScale` aident à faire tenir un texte dans un rectangle.

## Dans l'interface

L'interface n'appelle pas `renderText` elle-même : elle prend une `ui::Font`. `TextRendererFont(text, baseSize = 48, "default")`
adapte un `TextRenderer` à cette interface. Le `TextRenderer` doit vivre plus longtemps que cet objet. Les polices que
nomment les thèmes sont chargées au premier usage ; une police illisible est signalée une fois et remplacée par celle par
défaut. Voir [Thèmes JSON](/guides/interface/themes/).

## Limites

- REN-36 (Partiel) : ancrage, retour à la ligne, alignement, débordement fonctionnent ; la ligne reste Partiel dans
  `docs/02-CHECKLIST_FONCTIONNALITES.md`.
- Jeux de caractères : ASCII, Latin-1, Latin étendu A, ponctuation courante ; pas d'autres alphabets.
- Le texte n'est pas dessiné par `Renderer2D` : seul `CanvasRenderer` le trace, en 2D écran.

## Voir aussi

- [Sprites, formes et caméra 2D](/guides/rendu-2d/sprites-formes/)
- [Construire une interface en C++](/guides/interface/premiere-interface/)
