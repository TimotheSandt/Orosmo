---
title: "Écrire une vue en .cui"
description: "Décrire une interface dans un fichier .cui, la compiler en C++, lier l'état du jeu et nommer les événements."
sidebar:
  order: 3
---

Un fichier `.cui` décrit un arbre de widgets, des liaisons et des événements. L'outil `cui` le compile en C++ avant le
compilateur C++ : il n'y a pas d'interprète à l'exécution, et le code généré est celui de [la page précédente](/guides/interface/premiere-interface/).

Le langage décrit, le C++ décide. Un événement nomme une fonction C++, jamais une expression. L'apparence vient du
[thème JSON](/guides/interface/themes/) ; il n'y a dans le langage ni boucle, ni condition, ni état local.

## Exemple minimal

C'est la vue du projet de démarrage (`orosmo init`, `tools/engine-sdk/template/src/HudView.cui`).

```text title="HudView.cui"
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

L'état est une structure C++ ordinaire :

```cpp title="hud_state.hpp"
struct HudState
{
    std::string title = "mygame";
    bool rotate = true;
    std::uint64_t frame = 0;

    void resetFrame() { frame = 0; }
};
```

Le compilateur produit `HudView.gen.h` et `HudView.gen.cpp`. Le programme installe la vue :

```cpp
#include "HudView.gen.h"

HudState state;              // déclaré avant gui : il doit lui survivre
ui::Ui gui(font);
gui.setRoot(cui::buildHudView(gui, state));
```

Chaque vue `V` donne une structure `cui::V` et une fonction `cui::buildV(ui, ...)` dont les paramètres sont les champs de la
vue, dans l'ordre. Les objets liés doivent vivre plus longtemps que l'interface.

## Les champs de la vue

| Déclaration | C++ généré | Usage |
| --- | --- | --- |
| `@Mutable var state: State` | `State *state` | lu et écrit : liaisons `$state.x`, événements `state.f` |
| `@Observed var stats: Stats` | `const Stats *stats` | lu seulement ; `$stats.x` est une erreur |
| `@Snapshot var title: string = "x"` | `std::string title` (copie) | valeur figée à la construction ; paramètre de `buildV` |
| `let width = 320` | `static constexpr auto width = 320.0;` | constante |
| `func pct(v: double) -> double = v * 100.0` | `static double pct(double v)` | mise en forme d'une valeur affichée |

`@State` et `@Binding` sont refusés : l'état vit en C++, la vue le lie par `@Mutable`. Les noms qui commencent par `cui_` sont
réservés au code généré.

## Un exemple plus complet

```text title="SettingsView.cui"
import "orosmo/ui"
import "settings_state.hpp"

view SettingsView {
    @Mutable var state: SettingsState
    @Observed var stats: Stats
    @Snapshot var title: string = "Settings"
    let panelWidth = 280
    func percent(v: double) -> double = v * 100.0

    Panel {
        Label("\(title)").variant("title")
        Label("FPS \(stats.fps)").variant("caption")
        HBox {
            Label("Speed")
            Slider($state.speed, min: 0, max: 5, step: 0.1).width(.fill).tooltip("Rotation speed")
        }
        Label("\(percent(state.speed)) % of the maximum")
        Dropdown(options: state.qualityNames(), selected: $state.quality).onChange(refreshQuality)
        Checkbox("Fullscreen", checked: $state.fullscreen)
        TextField($state.name).id("name")
        Plot(values: stats.history).range(0, 120).height(40)
        Button("Apply").enabled(state.dirty()).onClick(state.apply).shortcut("Ctrl+R")
    }
    .width(panelWidth)
    .height(.fit)
    .margin(12)
    .align(.start)
}
```

Côté C++, `SettingsState` a les champs `speed` (`float`), `quality` (`int`), `fullscreen`, `name`, les méthodes
`std::vector<std::string> qualityNames() const`, `bool dirty() const` et `void apply()`. `Stats` a `double fps` et
`std::vector<float> history`. Installation :

```cpp
gui.actions().add("refreshQuality", ui::handler([&] { applyQuality(state.quality); }));
gui.setRoot(cui::buildSettingsView(gui, state, stats, "Settings"));
```

## Composants, modificateurs, événements

Les arguments sont positionnels (dans l'ordre de la [référence](/reference/langage-cui/)) ou nommés (`min: 0`). Un
argument qui a une valeur par défaut peut être omis. Les modificateurs s'enchaînent après le composant, un point devant :
`.width(240)`, `.margin(12)`, `.id("reset")`.

- Mise en page : `VBox`, `HBox`, `Panel`, `Grid(columns)`, `Overlay`, `ScrollView`, `Tabs` avec des `Page`, `Section`.
- Widgets : tous ceux du [catalogue](/guides/interface/widgets/), plus `ParamPanel($state.params)` pour une structure réfléchie.
- Texte : une chaîne `"Graine"`, une chaîne interpolée `"FPS \(stats.fps)"` (relue à chaque image) ou une expression C++ (`state.title`).
- Valeur liée : `$state.x` lit et écrit (écriture différée à la fin de `Ui::update`) ; une expression sans `$` est en lecture seule.

### Les trois formes d'événement

| Écriture | Appel généré |
| --- | --- |
| `.onClick(state.apply)` | `state->apply()` ou `state->apply(event)`, selon la signature |
| `.onClick(Tools::save)` | la fonction libre `Tools::save()` ou `Tools::save(event)` |
| `.onClick(refreshQuality)` | une action nommée, cherchée dans `gui.actions()` au moment de l'événement |

Un événement qui est un appel (`.onClick(state.go(1))`) est refusé. Une `func` de la vue ne peut pas non plus servir
d'événement. Le piège classique : `$state.x` est une liaison (la valeur change), `\(state.x)` est une interpolation
(le texte s'affiche) ; ne pas les confondre.

## Exemple : un menu qui s'ouvre avec Échap

Un menu avec trois boutons : Reprendre, changer de thème, Quitter. Échap l'affiche ou le masque, et le jeu se met en pause
tant qu'il est ouvert. Le langage ne décide de rien : la vue montre `state.open`, et c'est la boucle C++ qui lit les drapeaux
`open`, `quit` et `themeChanged`.

```cpp title="menu_state.hpp"
#pragma once

#include <array>
#include <string>

struct MenuState
{
    bool open = false;        // menu visible ; le jeu se met en pause tant qu'il l'est
    bool quit = false;        // mis à vrai par le bouton Quitter ; la boucle principale le lit
    int theme = 0;            // index dans kThemes
    bool themeChanged = true; // la boucle recharge le thème quand c'est vrai

    static constexpr std::array<const char *, 3> kThemes{"dark", "light", "framed"};

    void toggle() { open = !open; }
    void resume() { open = false; }
    void requestQuit() { quit = true; }
    void nextTheme()
    {
        theme = (theme + 1) % static_cast<int>(kThemes.size());
        themeChanged = true;
    }
    std::string themeName() const { return kThemes[static_cast<std::size_t>(theme)]; }
};
```

```text title="MenuView.cui"
import "orosmo/ui"
import "menu_state.hpp"

view MenuView {
    @Mutable var state: MenuState

    Overlay {
        Panel {
            Label("Menu").variant("title")
            Button("Reprendre").onClick(state.resume).id("resume")
            Button("Thème : \(state.themeName())").onClick(state.nextTheme).id("theme")
            Button("Quitter").onClick(state.requestQuit).id("quit")
        }
        .width(260)
        .height(.fit)
        .align(.center)
        .visible(state.open)
    }
}
```

`.visible(state.open)` est relue à chaque image : un panneau masqué ne dessine rien et ne reçoit aucun clic. Un bouton ne
peut pas appeler `window.requestClose()` : la vue ne connaît pas la fenêtre. Le bouton Quitter met donc `quit` à vrai, et la
boucle ferme la fenêtre.

```cpp title="main.cpp (extrait)"
#include <orosmo/core/paths.hpp>
#include <orosmo/platform/input_manager.hpp>
#include <orosmo/platform/window.hpp>
#include <orosmo/ui/ui.hpp>
#include <orosmo/ui/window_input.hpp>
#include "MenuView.gen.h"

namespace ui = orosmo::ui;
namespace platform = orosmo::platform;

// Une fois, après avoir créé gui (et MenuState menu avant lui) :
gui.setRoot(cui::buildMenuView(gui, menu));

// Chaque image :
auto &input = platform::InputManager::getInstance(window.getWindow());
if (!gui.wantsKeyboard() && input.isKeyJustPressed(platform::KeyButton::kEscape))
    menu.toggle();                       // pas d'Échap volé à un champ de texte
if (menu.themeChanged)
{
    menu.themeChanged = false;
    auto path = std::filesystem::path(GET_RESOURCE_PATH("themes")) / (menu.themeName() + ".json");
    if (auto r = gui.themes().load(path); !r)
        std::cerr << r.error().message << '\n';
}
gui.update(ui::readWindowInput(window, dt), {1280.0f, 720.0f});
if (menu.quit)
    window.requestClose();               // la boucle sort au prochain shouldClose()
const bool paused = menu.open;           // si paused : ne pas avancer la simulation ni lire la caméra
```

Le premier chargement du thème passe par la même branche, car `themeChanged` vaut vrai au départ. Pour passer d'un écran à
l'autre (menu principal, jeu), gardez un entier dans l'état et mettez `.visible(state.screen == 0)` sur chaque écran ; une
comparaison est une expression C++ ordinaire.

## Compiler

```bash
cui run --output Generated/ src/HudView.cui     # Generated/HudView.gen.h et HudView.gen.cpp
cui check src/HudView.cui                       # diagnostics en JSON (utilisés par l'extension VS Code)
```

Dans un projet créé par `orosmo init`, rien à faire : `engine.mk` compile chaque `.cui` du dossier `GAME_SRC_DIR` en
`$(ENGINE_GEN_DIR)/<nom>.gen.cpp` (liste dans `GEN_CPP_SOURCES`) et régénère le fichier quand le `.cui` change. Avec CMake :
`orosmo_add_cui_views(mygame src/HudView.cui)`. Détails dans [Construction](/reference/construction/).

## Erreurs du compilateur

Les erreurs ont la forme des compilateurs, `fichier:ligne:colonne: error: message`, et toutes sont signalées d'un coup. Un
fichier invalide ne produit aucun fichier généré, et l'ancien est supprimé : le build échoue au lieu de compiler une version
périmée.

```text
tests/ui/cui_errors/e03_unknown_modifier.cui:5:19: error: 'Label' has no modifier 'colour' (modifiers: width, height, ...)
```

Une erreur du compilateur C++ dans le code généré (méthode inexistante, type faux) désigne la ligne du `.cui`, grâce aux
directives `#line`. La liste des messages est dans la [référence](/reference/langage-cui/#erreurs).

## Extension VS Code

`tools/vscode-cui/` ajoute la coloration, les diagnostics pendant la frappe, la complétion et le survol. Installation :
`sh tools/vscode-cui/install.sh` (Linux, macOS) ou `.\tools\vscode-cui\install.ps1` (Windows), puis « Developer: Reload Window ».

## Limites

- Pas de rechargement à chaud de la vue elle-même : le thème se recharge, la vue se recompile.
- Les positions d'erreur sont celles de l'élément ou du modificateur, pas de l'argument fautif.
- `once` et `always` devant une expression sont acceptés mais sans effet : le texte est relu à chaque image.

## Voir aussi

[Référence du langage](/reference/langage-cui/), [catalogue des widgets](/guides/interface/widgets/),
[le compilateur .cui](/interne/compilateur-cui/).
