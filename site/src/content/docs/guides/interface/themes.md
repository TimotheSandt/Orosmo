---
title: Thèmes JSON
description: Jetons, styles par widget et par état, héritage, images en neuf parties, polices et rechargement à chaud.
sidebar:
  order: 5
---

L'apparence de l'interface vient de fichiers JSON (`res/themes/`) ; un widget ne contient pas de couleur ni de taille en
dur. *(Source : `docs/UI_LANGUAGE.md` §7, `src/ui/README.md`.)*

## Structure

Un thème déclare des **jetons** et, par type de widget, des propriétés qui renvoient à ces jetons, avec des variantes par
**état**. Un thème peut en étendre un autre :

```json
{
  "name": "dark",
  "extends": "base.json",
  "tokens": { "color.primary": "#4c8dff", "radius.small": 3, "color.brand": "color.primary" },
  "widgets": {
    "default": { "foreground": "color.text", "fontSize": "font.size.body", "states": { "disabled": { "foreground": "color.textDisabled" } } },
    "button":  { "background": "color.control", "radius": "radius.small", "states": { "hover": { "background": "color.controlHover" } } }
  }
}
```

- **Jeton** : couleur `"#rrggbb"` ou `"#rrggbbaa"`, nombre, ou nom d'un autre jeton (alias).
- **Propriétés** : couleurs (`background`, `foreground`, `border`, `accent`, `muted`, `shadowColor`, `imageTint`), tailles
  en pixels (`borderWidth`, `radius`, `padding`, `spacing`, `fontSize`, `shadowSize`, `shadowOffset`, `width`), `image`,
  `font`.
- **États** : `normal`, `hover`, `pressed`, `disabled`, `focus`. Ordre d'application : `default`, le widget, l'état de
  `default`, l'état du widget.
- **Variantes** : `.variant("title")` dans une vue choisit le style `widgets.title`.

## Thèmes livrés

| Fichier | Contenu |
| --- | --- |
| `base.json` | la structure : chaque propriété de chaque widget renvoie à un jeton |
| `dark.json`, `light.json` | jetons seulement (étendent `base.json`) |
| `framed.json` | hérite de `dark.json` et ajoute des cadres en neuf parties (`res/themes/images/`) |
| `hud.json` | hérite de `dark.json` : panneau de performances de la démo |

## Images en neuf parties et polices

```json
"images": { "frame.button": { "file": "images/button.png", "slice": 8, "edges": "repeat", "center": "stretch", "scale": 1 } },
"widgets": { "button": { "background": "#00000000", "image": "frame.button",
                         "states": { "hover": { "image": "frame.buttonHover" }, "disabled": { "imageTint": "#ffffff80" } } } }
```

L'image est découpée en quatre coins fixes, quatre bords et un centre (`"stretch"`, `"repeat"` ou `"none"`). Les polices
se déclarent dans `"fonts"` (`"title": "fonts/MaPolice-Bold.ttf"`) puis s'utilisent par `"font": "title"`.

## Chargement et rechargement à chaud

```cpp
orosmo::ui::ThemeHost themes;
if (auto r = themes.load("res/themes/dark.json"); !r) { /* r.error().message : "res/themes/dark.json: widgets.button.radius: ..." */ }
themes.watch(watcher);                       // watcher.poll() puis themes.reloadIfChanged() à chaque image
const orosmo::ui::Style &s = themes.current().style("button", orosmo::ui::WidgetState::kHover);
```

Un fichier invalide laisse le thème précédent en place et écrit l'erreur, avec le chemin JSON fautif, dans le journal.
Dans la démo : `--theme dark|light|framed`, ou l'onglet « Themes » de l'interface.

**Limite** : quelques tailles sont encore en dur dans des widgets (UI-07 est « Partiel », `docs/02`).
