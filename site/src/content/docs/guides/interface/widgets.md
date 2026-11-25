---
title: "Catalogue des widgets"
description: "Chaque widget de orosmo::ui avec son constructeur C++, son équivalent .cui et son usage."
sidebar:
  order: 2
---

Tous les widgets sont des classes de `namespace orosmo::ui`. Les constructeurs ci-dessous sont ceux des en-têtes
`widgets.hpp`, `controls.hpp`, `color.hpp` et `menus.hpp`. Le même widget s'écrit dans un fichier `.cui`
(voir [la référence](/reference/langage-cui/)). Pour voir chacun à l'écran : `apps/demo/bin/debug/main --tab widgets`
(autres onglets : `controls`, `color`, `windows`, `layout`) ; la source de la démo est
`apps/demo/src/UI/Views/DemoView.cui`.

## Exemple minimal

Tous les exemples de la page tiennent dans une même fonction qui remplit un conteneur :

```cpp
#include <orosmo/ui/color.hpp>
#include <orosmo/ui/controls.hpp>
#include <orosmo/ui/layout.hpp>
#include <orosmo/ui/menus.hpp>
#include <orosmo/ui/ui.hpp>
#include <orosmo/ui/widgets.hpp>

namespace ui = orosmo::ui;

auto root = std::make_unique<ui::Overlay>();
auto &col = root->emplace<ui::VBox>();
col.emplace<ui::Button>("Save").onClick([&](const ui::Event &) { save(); });
gui.setRoot(std::move(root));
```

## Base

| Widget | Constructeur | Usage |
| --- | --- | --- |
| `Label` | `Label(text = {}, styleName = "label")` | texte ; `setText`, `bindText(f)`, `setTextAlign(Align)` |
| `Button` | `Button(text = {}, styleName = "button")` | clic souris, ou Entrée / Espace avec le focus ; `setShortcut("Ctrl+R")` |
| `Checkbox` | `Checkbox(text, Binding<bool>)` | booléen |
| `Slider` | `Slider(Binding<double>, min, max, step = 0)` | nombre borné ; `step` 0 = continu ; `setDefault(v)` |
| `Dropdown` | `Dropdown(std::vector<std::string>, Binding<int>)` ou `Dropdown(std::function<std::vector<std::string>()>, Binding<int>)` | choix dans une liste (fenêtre surgissante) |
| `TextField` | `TextField(Binding<std::string>)` | texte validé par Entrée ou perte du focus, Échap annule |
| `NumberField` | `NumberField(Binding<double>, integer, min, max)` | nombre tapé ; une saisie invalide est refusée |
| `ColorPicker` | `ColorPicker(Binding<Color>, alpha = false)` | carré d'aperçu ; un clic ouvre la roue `ColorEditor` en fenêtre surgissante |
| `Plot` | `Plot(std::function<std::vector<float>()>)` | barres d'une suite de nombres relue chaque image ; `setRange(min, max)` |
| `Tabs` | `Tabs()` ou `Tabs(Binding<int> selected)` | `addPage(titre, std::unique_ptr<Widget>)` |
| `Section` | `Section(title, expanded = true)` | bloc repliable ; le contenu va dans `body()` |
| `Separator` | `Separator()` | ligne fine |

```cpp
col.emplace<ui::Label>("Seed");
col.emplace<ui::Checkbox>("Snow", ui::Binding<bool>::ref(s.snow));
col.emplace<ui::Slider>(ui::numberBinding(s.scale), 0.0, 500.0, 1.0);
col.emplace<ui::Dropdown>(std::vector<std::string>{"Low", "Medium", "High"}, ui::intBinding(s.quality));
col.emplace<ui::TextField>(ui::Binding<std::string>::ref(s.name));
col.emplace<ui::NumberField>(ui::numberBinding(s.seed), true, 0.0, 4294967295.0);
col.emplace<ui::Plot>([&] { return s.history; }).setRange(0.0f, 120.0f);

auto &tabs = col.emplace<ui::Tabs>(ui::intBinding(s.tab));
tabs.addPage("Params", std::make_unique<ui::VBox>());

auto &more = col.emplace<ui::Section>("More", false);
more.body().emplace<ui::Button>("Export");
```

Un clic sur le curseur place la valeur sous le pointeur ; un glissement part de la valeur courante (Maj : dix fois plus fin) ;
un clic droit remet la valeur par défaut. `Tabs`, `Section` et les éditeurs gardent leur structure quand on appelle
`clearChildren()`.

## Contrôles

| Widget | Constructeur | Usage |
| --- | --- | --- |
| `RadioButton` | `RadioButton(text, int value, Binding<int> selected)` | les boutons d'un groupe lient le même entier |
| `RangeSlider` | `RangeSlider(Binding<double> low, Binding<double> high, min, max, step = 0)` | deux poignées qui ne se croisent pas |
| `SpinBox` | `SpinBox(Binding<double>, min, max, step = 1, integer = true)` | champ numérique avec boutons − et +, et la molette |
| `TextArea` | `TextArea(Binding<std::string>)` | éditeur multiligne ; `setLineNumbers`, `setReadOnly`, `setWrap` (retour à la ligne actif par défaut) |

```cpp
col.emplace<ui::RadioButton>("Low", 0, ui::intBinding(s.quality));
col.emplace<ui::RadioButton>("High", 1, ui::intBinding(s.quality));
col.emplace<ui::RangeSlider>(ui::numberBinding(s.lo), ui::numberBinding(s.hi), 0.0, 100.0, 1.0);
col.emplace<ui::SpinBox>(ui::numberBinding(s.count), 0.0, 10.0);
col.emplace<ui::TextArea>(ui::Binding<std::string>::ref(s.notes)).setLineNumbers(true);
```

Les champs de texte gèrent sélection, presse-papiers, annuler / rétablir (Ctrl+Z, Ctrl+Y). Le presse-papiers du système se
branche par `ui::useSystemClipboard(gui, window)` ; sans lui, un presse-papiers interne est utilisé.

## Couleur

| Widget | Constructeur | Usage |
| --- | --- | --- |
| `ColorWheel` | `ColorWheel(Binding<Color>)` | anneau des teintes et carré saturation × valeur |
| `ColorEditor` | `ColorEditor(Binding<Color>, alpha = false)` | roue, formats RGB / HSV / HSL, alpha, champ hexadécimal |
| `GradientEditor` | `GradientEditor(Binding<Gradient>)` | clic ajoute un arrêt, glisser le déplace, clic droit le retire |
| `CurveEditor` | `CurveEditor(Binding<Curve>)` | courbe dans [0, 1] : mêmes gestes |

```cpp
col.emplace<ui::ColorEditor>(ui::colorBinding(s.color), true);
col.emplace<ui::GradientEditor>(ui::Binding<ui::Gradient>::ref(s.gradient));
col.emplace<ui::CurveEditor>(ui::Binding<ui::Curve>::ref(s.curve));
float t = s.gradient.evaluate(0.5f).r;        // Gradient::evaluate(t) et Curve::evaluate(x) servent au jeu
```

`Color` est un `glm::vec4`. `colorBinding` accepte aussi un `glm::vec3` (alpha à 1). `Gradient` et `Curve` sont des données :
le jeu les évalue lui-même.

## Menus et fenêtres

| Widget | Constructeur | Usage |
| --- | --- | --- |
| `MenuBar` | `MenuBar()` | rangée de `Menu` |
| `Menu` | `Menu(title)` | ses éléments vont dans `items()` : `MenuItem`, `Menu` (sous-menu), `Separator` |
| `MenuItem` | `MenuItem(text, shortcut = {}, Binding<bool> checked = {})` | le raccourci est affiché ET installé (marche menu fermé) |
| `ContextMenu` | `ContextMenu()` | enfant invisible d'un widget : un clic droit sur lui ouvre `items()` |
| `Dialog` | `Dialog(title, Binding<bool> open, dismissOnOutsideClick = true)` | fenêtre modale ouverte tant que la liaison vaut vrai ; contenu dans `body()` |
| `Notifications` | `Notifications(maxCount = 5, maxHeight = 0, width = 0)` | pile des messages de `Ui::notify` |

```cpp
auto &file = col.emplace<ui::MenuBar>().emplace<ui::Menu>("File");
file.items().emplace<ui::MenuItem>("Save", "Ctrl+S").onClick([&](const ui::Event &) { save(); });
file.items().emplace<ui::MenuItem>("Grid", std::string(), ui::Binding<bool>::ref(s.grid));

auto &dialog = root->emplace<ui::Dialog>("Settings", ui::Binding<bool>::ref(s.dialogOpen));
dialog.body().emplace<ui::Label>("Inside");

root->emplace<ui::Notifications>(3).setAlign(ui::Align::kEnd, ui::Align::kEnd);
gui.notify("Saved", ui::NotificationKind::kSuccess, 4.0f);   // kInfo, kSuccess, kWarning, kError
```

Échap ferme le menu ouvert, sinon la fenêtre modale. Au clavier : Tab jusqu'au titre d'un menu, Bas ou Entrée l'ouvrent,
les flèches parcourent, Entrée active. Une fenêtre modale ouverte ne laisse passer que ses propres raccourcis.

## Images produites par le jeu

`DynamicImage` est une image RGBA que le jeu remplit ; `ImageView` l'affiche. Après avoir écrit les pixels, appelez
`commit()` : la texture est mise à jour en place à l'image suivante (même taille).

```cpp
ui::DynamicImage minimap(128, 128);
col.emplace<ui::ImageView>([&] { return minimap.image(); }, ui::ImageFit::kCover);
// chaque image :
minimap.setPixel(10, 20, {1.0f, 0.0f, 0.0f, 1.0f});
minimap.commit();
```

`ImageFit` : `kContain` (défaut, image entière), `kCover` (remplit en coupant), `kStretch` (déforme).

## Raccourcis clavier

`readWindowInput` remplit `InputFrame::shortcuts` avec des combinaisons comme `"Ctrl+S"` (forme de `normalizeShortcut`). L'interface
les propose aux widgets visibles et actifs : les `MenuItem` et les boutons (`Button::setShortcut`). Avec le focus dans un champ
de texte, Ctrl+A, C, X, V, Z, Y sont des commandes d'édition à la place.

## Limites

- `Plot` ne dessine que des barres.
- La composition IME (saisie chinoise, japonaise...) n'est pas affichée dans les champs : seul le texte validé arrive.
- Pas de navigation à la manette. Quelques tailles de comportement restent en dur (voir [l'état du projet](/projet/etat/)).

## Voir aussi

[Construire une interface en C++](/guides/interface/premiere-interface/), [référence du langage .cui](/reference/langage-cui/),
[thèmes JSON](/guides/interface/themes/).
