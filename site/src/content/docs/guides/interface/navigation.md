---
title: "Naviguer au clavier et à la manette"
description: "Ordre de tabulation, déplacement du focus aux flèches, navigation à la manette avec GamepadNavigator, et anneau de focus dessiné par le thème."
sidebar:
  order: 6
---

Toute l'interface se parcourt sans souris : Tab et Maj+Tab suivent l'ordre de tabulation, les flèches passent au voisin dans
leur direction, Entrée active et Échap annule. Une manette fait la même chose. Après une navigation au clavier ou à la
manette, un anneau de focus entoure le widget, avec l'apparence du thème.

En-têtes : `orosmo/ui/ui.hpp`, `orosmo/ui/widget.hpp`, `orosmo/ui/gamepad_navigation.hpp`, `orosmo/ui/window_input.hpp`, `orosmo/platform/gamepad.hpp`.

## Ordre de tabulation

| Réglage | C++ | `.cui` | Effet |
| --- | --- | --- | --- |
| indice | `Widget::setTabIndex(n)` | `.tabIndex(n)` | positif : en premier, par indice croissant ; 0 (défaut) : dans l'ordre de l'arbre ; négatif : hors de l'ordre de Tab |
| focus interdit | `Widget::setFocusable(false)` | `.focusable(false)` | le widget ne prend jamais le focus (Tab, flèches, clic) |

```text
Button("Play").id("play").tabIndex(1)
Button("Options").id("options").tabIndex(2)
Label("Version 0.7.0").focusable(false)
```

En C++ : `ui.focus(widget)`, `ui.focusNext()` (et `focusNext(true)` pour revenir en arrière), `ui.focused()`.

## Flèches

Une flèche que le widget focalisé n'utilise pas lui-même passe le focus au widget le plus proche dans sa direction
(`Ui::moveFocus`), sans boucler. Au clavier, un curseur garde les quatre flèches, un champ de texte Gauche et Droite, une
liste déroulante Haut et Bas, une zone de texte et les menus toutes ; à la manette, un curseur laisse Haut et Bas à la
navigation, comme une liste déroulante fermée (ouverte, Haut et Bas changent l'option). Sans focus, les flèches ne font rien : l'application peut s'en servir pour autre chose. Les menus se parcourent
aux flèches, Entrée et Échap.

## Manette

La plateforme lit les manettes à disposition standard (`platform::Gamepads`, branchement à chaud) ; `ui::GamepadNavigator`
traduit leur état en commandes de navigation de l'image :

| Manette | Commande |
| --- | --- |
| croix, stick gauche | flèches (répétées tant qu'elles sont tenues) |
| A | Entrée (activer) |
| B | Échap (annuler) |
| RB, LB | Tab, Maj+Tab |

```cpp
orosmo::platform::Gamepads gamepads;
orosmo::ui::GamepadNavigator navigator;      // réglages : zone morte 0,5, répétition après 0,4 s puis toutes les 0,12 s

// à chaque image, après Window::newFrame :
gamepads.poll();
orosmo::ui::InputFrame input = orosmo::ui::readWindowInput(window, dt);
navigator.apply(gamepads.first(), input);    // ajoute les commandes à input.navigation
ui.update(input, viewport);
```

Sans focus, une direction le donne au premier widget. `Gamepads::simulate(slot, état)` remplace une manette réelle (tests).

## Anneau de focus

Après une navigation au clavier ou à la manette (`Ui::focusVisible()`), le style `focusRing` du thème est dessiné autour du
widget focalisé ; un clic l'éteint. Couleur, épaisseur, rayon et écart viennent du thème (`color.focus`, `border.focus`,
`focus.ringGap`) : voir [Thèmes JSON](/reference/themes-json/). Un thème sans `focusRing` n'en dessine pas. L'état `focus`
d'un style de widget s'applique aussi.

## Limites

- Une `ScrollView` ne défile pas vers le widget focalisé (G222).
- Manettes : ni vibration, ni remappage en jeu (G223). Une manette sans disposition connue est ignorée et son GUID est
  écrit dans le journal : donnez sa correspondance (format SDL_GameControllerDB) dans la variable d'environnement
  `SDL_GAMECONTROLLERCONFIG` avant de lancer le programme.
- La façade [`app::Application`](/guides/application/facade/) ne branche pas la manette : appelez `GamepadNavigator`
  vous-même (G380).

## Voir aussi

- [Catalogue des widgets](/guides/interface/widgets/)
- [Fenêtre, clavier et souris](/guides/application/fenetre-entrees/)
