---
title: "Outils de debug : console et inspecteurs"
description: "Ouvrir les outils de debug du moteur (F12) : console avec journal et commandes par nom, inspecteur des widgets, inspecteur des entités et de leurs composants, profileur."
sidebar:
  order: 7
---

`ui::debug::DebugTools` est un panneau à droite de l'écran, construit avec `orosmo::ui` (sans Dear ImGui), avec quatre
onglets. Fermé, il ne coûte rien : ni mise en page, ni lecture des entrées, ni commande de dessin.

En-têtes : `orosmo/ui/debug_tools.hpp`, `orosmo/ui/debug_console.hpp`, `orosmo/ui/frame_history.hpp`.

| Onglet (`DebugTab`) | Contenu |
| --- | --- |
| Console | journal du moteur (avertissements et erreurs marqués) et commandes enregistrées par nom |
| Widgets | arbre des widgets de l'interface inspectée : type, identifiant, style, rectangle calculé ; survoler une ligne surligne le widget, un clic garde le surlignage |
| Entities | entités d'un `scene::World` ; l'entité choisie montre ses composants champ par champ, modifiables, d'après leur description |
| Profiler | historique des temps d'image et mesures nommées de `core::Profiler` |

## Dans la démo

```bash
apps/demo/bin/debug/main --debug-tools --debug-tab entities   # ouverts au démarrage, sur un onglet
```

F12 ouvre et ferme les outils. La console a toujours la commande `help` ; celle de la démo y ajoute `hud`, `ui`, `camera` et `quit`.

## Dans une application

```cpp
#include <orosmo/ui/debug_tools.hpp>
namespace debug = orosmo::ui::debug;

debug::DebugTools tools(font);                         // la police de l'application
if (auto loaded = tools.ui().themes().load("res/themes/dark.json"); !loaded)
    return std::unexpected(loaded.error());
tools.inspect(&appUi);                                 // interface inspectée (onglet Widgets)
tools.inspectWorld(&world);                            // monde inspecté (onglet Entities)
auto added = tools.console().add("spawn", "spawn <n> : add n enemies",
    [&](std::span<const std::string> args) -> orosmo::core::Result<std::string> {
        return "spawned " + (args.empty() ? std::string("1") : args[0]);
    });

// à chaque image
tools.handleToggle(input);                             // F12
tools.frames().push(cpuMs, gpuMs, intervalMs);         // onglet Profiler
tools.update(input, viewport);
if (tools.wantsKeyboard()) {                          // la saisie dans la console ne va pas à l'application
    input.text.clear();
    input.keys.clear();
    input.events.clear();
    input.shortcuts.clear();
}
if (tools.wantsMouse()) {                             // la souris va aux outils quand ils la veulent
    const orosmo::ui::InputFrame none;
    input.mouse = none.mouse;
    input.mouseDown = input.secondaryDown = false;
    input.wheel = none.wheel;
}
appUi.update(input, viewport);                         // toujours : le clavier reste à l'application
appUi.draw(list);
tools.draw(list);                                      // par-dessus l'interface
```

Une commande rend un texte (affiché) ou une erreur, jamais d'exception ; une ligne `nom arg1 "arg 2"` l'appelle
(`Console::execute`). L'apparence vient des styles `debugPanel`, `debugRow`, `debugRowSelected`, `debugText`,
`debugInput`, `debugInputLine`, `debugWarning`, `debugError` et `debugHighlight` du thème ([Thèmes JSON](/reference/themes-json/)).

Dans les builds debug de la démo, F11 bascule la fenêtre sans bordure et F10 le plein écran (F12 est réservé aux outils).

## Limites

- Console : 18 lignes affichées sans défilement, ni historique ni complétion (G295).
- Inspecteur de widgets : le contenu des fenêtres surgissantes, menus et dialogues n'y figure pas (G296).
- Inspecteur d'entités : 200 entités au plus, sans nom ni hiérarchie, sans ajout ni retrait de composant (G297).
- Profileur : liste plate et graphe du temps CPU seulement (G298).
- La façade [`app::Application`](/guides/application/facade/) ne crée pas les outils : créez-les vous-même (G380).

## Voir aussi

- [Mesurer : profiler, traces, benchmarks](/guides/noyau/profilage/)
- [Dessin et vues de debug](/guides/rendu-3d/debug-visuel/)
- [Options de la démo](/reference/options-demo/#debug)
