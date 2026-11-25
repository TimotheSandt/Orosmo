---
title: Interface (orosmo::ui)
description: Arbre de widgets, mise en page, liaisons, événements nommés, thèmes et listes de dessin.
sidebar:
  order: 4
---

`orosmo::ui` construit l'interface : arbre de widgets, mise en page, thèmes JSON, événements nommés. Il **produit des
listes de dessin** (`renderer::DrawList`) et ne connaît ni le RHI ni le backend. Aucun singleton : chaque application
possède son `Ui`. *(Sources : `src/ui/README.md`, `docs/03` §12.)*

## Une image

```cpp
ui.update(input, viewport);   // rechargement du thème, entrées, écritures des liaisons puis événements, mise en page
ui.draw(list);                // liste de dessin de l'image
canvas->render(*encoder, list, target);   // CanvasRenderer (renderer) la dessine
```

Les entrées viennent d'une fenêtre `platform` par `readWindowInput(window, dt)` ; le presse-papiers du système se branche
par `useSystemClipboard(ui, window)`.

## Ce qu'il y a dedans

| En-tête | Contenu |
| --- | --- |
| `widget.hpp` | `Widget` (arbre, identifiant, style, tailles `fit` / `fixed` / `fill`, marges, alignement, visibilité et activation liées, infobulle, événements), `Binding<T>` (liaison dans les deux sens, écriture différée) |
| `layout.hpp` | `VBox`, `HBox`, `Panel`, `Grid`, `Overlay`, `ScrollView` |
| `widgets.hpp`, `controls.hpp`, `color.hpp`, `menus.hpp` | boutons, curseurs, champs, listes, onglets, sections, éditeurs de couleur, de dégradé et de courbe, menus, fenêtres modales, notifications, images produites par le jeu |
| `theme.hpp` | `Theme`, `Style`, `WidgetState`, `ThemeHost` (rechargement à chaud, dernier thème valide gardé) |
| `ui.hpp` | `Ui` : racine, thème, police, `ActionRegistry` (actions par nom), survol, focus, tabulation, fenêtres surgissantes, raccourcis |
| `text_renderer.hpp`, `font.hpp` | texte FreeType en atlas **MSDF** construits à la demande (ASCII puis Latin étendu) |
| `reflect.hpp` | `reflectedPanel(objet)` : une structure `core::Reflect<T>` devient un panneau de widgets adaptés au type et à la plage |

## Séparation des rôles

- **Structure et liaisons** : décrites en [`.cui`](/utiliser/langage-cui/) (ou construites en C++).
- **Logique** : en C++. Un événement nomme une fonction ; en développement, une action nommée est retrouvée par nom dans
  le registre au moment de l'appel.
- **Apparence** : dans un [thème JSON](/utiliser/themes/), jamais en dur dans un widget sauf surcharge locale explicite.
- **Liaison de données** : typée, dans les deux sens, écriture visible à l'image suivante.

## Rendu économe

Un widget caché ne dessine rien ; un sous-arbre hors de sa zone de découpage est éliminé (une liste de 1000 lignes dans
un `ScrollView` ne produit que les lignes visibles). Le cache de rendu par couche n'est pas encore livré.

## Limites

Pas de navigation à la manette, pas de rechargement à chaud de la vue elle-même, composition IME non affichée
(`docs/GAPS.md`).
