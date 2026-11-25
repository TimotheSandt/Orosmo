---
title: Le langage d'interface .cui
description: Décrire une interface en .cui, la compiler en C++, lier l'état et nommer les événements.
sidebar:
  order: 3
---

Une interface s'écrit dans le langage maison `.cui`. Le fichier est compilé en C++ par l'outil `cui`, puis par le
compilateur C++ avec le reste du programme : **aucun interprète à l'exécution**.
*(Source : `docs/UI_LANGUAGE.md`, la spécification complète, vérifiée par `make cui-test` et `make demo-test`.)*

## Le principe : décrire, pas décider

Le langage **décrit** l'arbre de widgets, les liaisons et les événements ; il ne **décide** rien :

- la **logique** est en C++ : un événement **nomme** une fonction C++, jamais une expression ;
- l'**apparence** est dans un [thème JSON](/guides/interface/themes/) : une surcharge locale nomme un jeton du thème ;
- l'**état** vit en C++ et la vue le lie (`@Mutable`) ; il n'y a ni boucle, ni condition, ni état local dans le langage.

## Un exemple : le panneau du projet de démarrage

C'est la vue générée par `orosmo init` (`tools/engine-sdk/template/src/HudView.cui`) :

```text title="HudView.cui"
// Starter HUD view compiled by cui into HudView.gen.h / HudView.gen.cpp.
import "orosmo/ui"
import "hud_state.hpp"

view HudView {
    @Mutable var state: HudState

    Panel {
        Label("\(state.title)").variant("title").id("title")
        Label("Frame: \(state.frame)").variant("caption").id("frame")
        Checkbox("Rotate cube", checked: $state.rotate).id("rotate")
        Button("Reset counter").onClick(state.resetFrame).id("reset")
    }
    .width(240)
    .height(.fit)
    .margin(12)
    .align(.start)
}
```

L'état est une structure C++ ordinaire (`hud_state.hpp`) :

```cpp
struct HudState
{
    std::string title = "mygame";
    bool rotate = true;
    std::uint64_t frame = 0;

    void resetFrame() { frame = 0; }
};
```

et le programme installe la vue : `interface.setRoot(cui::buildHudView(interface, state));`.

## Champs de la vue

| Déclaration | C++ généré | Usage |
| --- | --- | --- |
| `@Mutable var state: State` | `State *state` | lu et **écrit** (liaisons `$state.x`, événements `state.f`) |
| `@Observed var stats: Stats` | `const Stats *stats` | lu seulement ; `$stats.x` est une erreur |
| `@Snapshot var title: string = "x"` | `std::string title` (copie) | valeur figée à la construction |
| `let width = 320` | `static constexpr auto width = 320.0;` | constante |
| `func pct(v: double) -> double = v * 100.0` | `static double pct(double v)` | mise en forme d'une valeur affichée |

## Composants, modificateurs, événements

- **Mise en page** : `VBox`, `HBox`, `Panel`, `Grid`, `Overlay`, `ScrollView`, `Tabs` / `Page`, `Section`.
- **Widgets** : `Label`, `Button`, `Checkbox`, `Slider`, `RangeSlider`, `SpinBox`, `Dropdown`, `TextField`, `TextArea`,
  `NumberField`, `RadioButton`, `ColorPicker`, `ColorWheel`, `ColorEditor`, `GradientEditor`, `CurveEditor`, `Plot`,
  `Separator`, `ImageView`, `Notifications`, `Dialog`, `MenuBar` / `Menu` / `MenuItem`, `ContextMenu`, et `ParamPanel`
  (panneau automatique d'une structure réfléchie `core::Reflect<T>`).
- **Modificateurs** : `.width` / `.height` (`.fit`, `.fill`, taille fixe), `.margin`, `.align`, `.span`, `.id`,
  `.tooltip`, `.visible`, `.enabled`, `.variant` (style du thème), `.overflow`, `.style(...)` (surcharge locale par jeton),
  `.shortcut`.
- **Événements** : `.onClick`, `.onChange`, `.onHover`, `.onFocus`. `.onClick(state.save)` appelle une méthode,
  `.onClick(Tools::save)` une fonction libre, `.onClick(save)` une **action nommée** retrouvée dans `ui.actions()` au moment
  de l'événement.
- **Texte** : chaîne, chaîne interpolée (`"FPS \(stats.fps)"`, relue à chaque image) ou expression C++.

La liste complète des arguments de chaque composant est dans `docs/UI_LANGUAGE.md` §4, et `cui spec` l'écrit en JSON.

## Compiler

```bash
cui run --output Generated/ src/Panel.cui   # Generated/Panel.gen.h, Panel.gen.cpp
cui check src/Panel.cui                     # diagnostics en JSON (utilisés par l'extension VS Code)
```

Avec `engine.mk` ou `orosmo_add_cui_views` (CMake), le build régénère le C++ quand le `.cui` change. Les erreurs ont la
forme des compilateurs (`fichier:ligne:colonne: error: message`), toutes signalées d'un coup ; un fichier invalide ne
produit aucun fichier généré. Des directives `#line` font pointer les erreurs du compilateur C++ vers la ligne du `.cui`.

```text
tests/ui/cui_errors/e03_unknown_modifier.cui:5:19: error: 'Label' has no modifier 'colour' (modifiers: width, height, …)
```

L'extension VS Code (`tools/vscode-cui/`) affiche ces diagnostics pendant la frappe, avec complétion et survol.

## Limites

Pas de rechargement à chaud de la vue elle-même (le thème, lui, se recharge) ; pas de navigation à la manette ; les
positions d'erreur sont celles de l'élément ou du modificateur, pas de l'argument fautif.
