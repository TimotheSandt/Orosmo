---
title: "Changer l'apparence avec un thème JSON"
description: "Écrire un thème, hériter d'un thème livré, charger et recharger à chaud, images en neuf parties et polices."
sidebar:
  order: 4
---

L'apparence de l'interface vient de fichiers JSON. Un widget ne contient ni couleur ni taille en dur : il lit son style dans le
thème courant, par type de widget et par état (normal, survol, appuyé, désactivé, focus). Toutes les clés sont listées
dans la [référence des thèmes](/reference/themes-json/).

## Exemple minimal

Un thème qui hérite de `dark.json` et ne change que quelques jetons, `res/themes/mine.json` :

```json title="res/themes/mine.json"
{
  "name": "mine",
  "extends": "dark.json",
  "tokens": {
    "color.primary": "#e0803a",
    "color.primaryHover": "#f0a060",
    "radius.small": 6
  },
  "widgets": {
    "button": { "padding": 10 }
  }
}
```

```cpp
if (auto r = gui.themes().load(GET_RESOURCE_PATH("themes/mine.json")); !r)
    std::cerr << r.error().message << '\n';     // "fichier: chemin.json: message"
```

Le chemin de `extends` est relatif au fichier qui l'écrit. Les jetons et les propriétés du fichier enfant remplacent ceux du
parent.

## Structure

- `tokens` : des valeurs nommées. Une couleur `"#rrggbb"` ou `"#rrggbbaa"`, un nombre, ou le nom d'un autre jeton (alias).
- `widgets` : par type de widget (`button`, `panel`, `slider`...), des propriétés qui valent un jeton, une couleur ou un nombre,
  et un objet `states` avec les variantes `normal`, `hover`, `pressed`, `disabled`, `focus`.
- `widgets.default` s'applique à tous les widgets ; un type sans entrée prend le style de `default`.
- L'état d'un widget est choisi dans cet ordre : désactivé, appuyé (pointeur dessus), focus, survol, normal.

Les propriétés sont appliquées dans cet ordre : `default`, le widget, puis, hors état normal, l'état `normal` de `default` et du
widget, puis l'état courant de `default` et du widget. Le dernier gagne.

```json
"button": {
  "background": "color.control",
  "radius": "radius.small",
  "states": { "hover": { "background": "color.controlHover" } }
}
```

Dans une vue `.cui`, `.variant("title")` choisit le style `widgets.title`. Pour créer un style à vous, ajoutez-le au thème
(`"warning": { "foreground": "color.warning" }`) puis écrivez `.variant("warning")`.

## Thèmes livrés

| Fichier | Contenu |
| --- | --- |
| `base.json` | la structure : chaque propriété de chaque widget renvoie à un jeton ; contient aussi les images `frame.*` |
| `dark.json`, `light.json` | les jetons de couleur seulement (ils étendent `base.json`) |
| `framed.json` | étend `dark.json` et donne des cadres en neuf parties au panneau et aux boutons |
| `hud.json` | étend `dark.json` : panneau de performances de la démo (styles `hud`, `hudTitle`, `hudMetric`, `hudDetail`, `hudPlotCpu`, `hudPlotGpu`) |

Pour voir les trois premiers dans la démo : `apps/demo/bin/debug/main --theme light --tab themes` (valeurs de `--theme` :
`dark`, `light`, `framed`).

## Images en neuf parties

Un fond qui n'est pas une couleur unie, valable pour toutes les tailles sans déformer les coins. Déclarez l'image dans
`images`, puis nommez-la dans la propriété `image` d'un widget (par état si besoin).

```json
"images": {
  "frame.button": { "file": "images/button.png", "slice": 8, "edges": "repeat", "center": "stretch", "scale": 1 }
},
"widgets": {
  "button": {
    "background": "#00000000",
    "image": "frame.button",
    "states": { "disabled": { "imageTint": "#ffffff80" } }
  }
}
```

| Clé | Valeur | Effet |
| --- | --- | --- |
| `file` | chemin PNG relatif au thème | l'image |
| `slice` | nombre, ou `[gauche, haut, droite, bas]` | taille des coins en texels |
| `edges`, `center` | `"stretch"` (étiré), `"repeat"` (répété), `"none"` (supprimé) | remplissage des bords et du centre |
| `scale` | 0,01 à 100 | pixels par texel |

L'image se dessine par-dessus `background` ; `"image": "none"` retire une image héritée ; `imageTint` la teinte.
Exemple complet : `res/themes/framed.json` et `res/themes/images/`.

## Polices

```json
"fonts": { "title": "fonts/MaPolice-Bold.ttf" },
"widgets": { "title": { "font": "title" } }
```

Le fichier TrueType ou OpenType est relatif au thème et doit exister au chargement. `"font": "default"` revient à la police de
l'application. Dans une vue : `.style(font: "title")`. Le texte est en UTF-8 : ASCII, Latin-1 et Latin étendu A, guillemets,
tirets, points de suspension, euro.

## Charger et recharger à chaud

`ThemeHost` (`gui.themes()`) garde le thème courant. `Ui::update` appelle `reloadIfChanged()` ; c'est à l'application
d'appeler `watcher.poll()` à chaque image.

```cpp
orosmo::core::FileWatcher watcher;
gui.themes().watch(watcher);          // watcher doit vivre plus longtemps que gui
// à chaque image, avant gui.update(...) :
watcher.poll();
```

Le thème et ses parents (et les images et polices qu'il nomme) sont observés. Si le fichier modifié est invalide, le thème
précédent reste en place, l'erreur est écrite dans le journal et disponible par `gui.themes().lastError()`. Un `load` qui
échoue garde aussi le thème courant, mais observe le fichier demandé : le corriger l'installe. `themes().set(theme)` installe
un thème construit en code, sans fichier à recharger.

## Styles locaux d'un widget

Une surcharge locale nomme un jeton ou donne une valeur, sans toucher au thème :

```text
Button("Delete").style(background: "color.error", radius: 4)
```

En C++ : `button.overrideStyle(ui::StyleProperty::kBackground, "color.error")`. Une valeur qui ne se résout pas (jeton absent)
est ignorée et signalée une fois dans le journal. Préférez un `.variant(...)` quand le style sert plusieurs fois.

## Limites

Quelques constantes de comportement restent dans le code : décalage de l'infobulle, pas de défilement de 3 lignes, largeur
minimale d'un champ de 10 caractères ; le détail est dans [l'état du projet](/projet/etat/).

## Voir aussi

[Référence des thèmes](/reference/themes-json/), [langage .cui](/guides/interface/langage-cui/),
[fonctionnement interne](/interne/ui/).
