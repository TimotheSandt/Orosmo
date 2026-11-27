---
title: "Construire une interface en C++"
description: "Créer un Ui, poser des widgets dans des conteneurs, lier des valeurs, réagir aux événements et dessiner l'interface chaque image."
sidebar:
  order: 1
---

`orosmo::ui` fournit un arbre de widgets, une mise en page et des thèmes. Il produit une liste de dessin
(`renderer::DrawList`) que le `CanvasRenderer` envoie au GPU. Cette page construit une interface directement en C++. Pour
la décrire dans un fichier, voir [le langage .cui](/guides/interface/langage-cui/) : il génère exactement ce code.

## Exemple minimal

L'interface a besoin d'une police (`TextRenderer`), d'un thème et d'un objet `Ui`. C'est ce que `app::Application` crée
pour le projet de démarrage d'`orosmo init` (`src/app/application.cpp`), écrit ici à la main, sans la partie fenêtre et
rendu 3D (voir [Première application](/demarrer/premiere-application/)).

```cpp
#include <orosmo/core/paths.hpp>
#include <orosmo/ui/font.hpp>
#include <orosmo/ui/layout.hpp>
#include <orosmo/ui/text_renderer.hpp>
#include <orosmo/ui/ui.hpp>
#include <orosmo/ui/widgets.hpp>

namespace ui = orosmo::ui;

struct Settings
{
    std::string name = "mygame";
    bool rotate = true;
    double speed = 1.0;
    std::uint64_t frame = 0;
    void reset() { frame = 0; }
};

ui::TextRenderer text;
if (!text.init(1280, 720) || !text.loadFont(GET_RESOURCE_PATH("fonts/Roboto-Regular.ttf"), "default", 48))
    return 1;
text.setActiveFont("default");
ui::TextRendererFont font(text);

Settings settings;          // déclaré AVANT gui : détruit après l'arbre, qui garde des liaisons vers lui
ui::Ui gui(font);
if (auto theme = gui.themes().load(GET_RESOURCE_PATH("themes/dark.json")); !theme)
{
    std::cerr << theme.error().message << '\n';
    return 1;
}

auto panelOwner = std::make_unique<ui::Panel>();
ui::Panel &panel = *panelOwner;    // reste valide après le transfert à gui (l'objet est sur le tas)
panel.setWidth(ui::SizeSpec::fixed(260)).setHeight(ui::SizeSpec::fit());
panel.setMargin(ui::Insets::all(12)).setAlign(ui::Align::kStart, ui::Align::kStart);
panel.emplace<ui::Label>("Settings", "title");
panel.emplace<ui::Label>().bindText([&] { return ui::text("Frame ", settings.frame); });
panel.emplace<ui::Checkbox>("Rotate", ui::Binding<bool>::ref(settings.rotate));
panel.emplace<ui::Button>("Reset").onClick([&](const ui::Event &) { settings.reset(); });
gui.setRoot(std::move(panelOwner));
```

Le deuxième argument de `Label` est le nom de style du thème (`widgets.title`). N'appelez pas l'objet `interface` sous
Windows : c'est une macro de `<objbase.h>`.

## Dessiner une image

Chaque image : lire les entrées, `update`, puis `draw` après la scène 3D, sur la même cible.

```cpp
ui::InputFrame input = ui::readWindowInput(window, dt);        // souris, texte, touches, raccourcis
gui.update(input, {1280.0f, 720.0f});                          // taille de l'interface en pixels logiques

// ... scène 3D : scene->beginFrame ... scene->endFrame(...) ...

orosmo::renderer::DrawList list;
gui.draw(list);
auto ok = canvas->render(*encoder, list, {target, orosmo::rhi::Format::kRGBA8Unorm, size}, true);
```

`canvas` est le `std::unique_ptr<CanvasRenderer>` de `CanvasRenderer::create`. Le dernier argument (`loadExisting = true`)
garde la scène déjà dessinée : l'interface se pose dessus. `update` recharge le thème si besoin, applique les entrées à la mise
en page affichée, écrit les liaisons, appelle les événements, puis refait la mise en page.

## Conteneurs et tailles

| Classe | Rôle |
| --- | --- |
| `VBox`, `HBox` | enfants l'un sous l'autre, ou côte à côte ; espacement `spacing` du thème |
| `Panel` | `VBox` avec le style `panel` (fond, bordure, ombre) |
| `Grid(int colonnes)` | grille ; `columnSpan` d'un enfant couvre plusieurs colonnes |
| `Overlay` | enfants superposés, chacun aligné |
| `ScrollView` | un seul enfant plus grand que la vue : molette et barre |

`emplace<T>(args...)` construit un enfant et renvoie sa référence ; `add(std::unique_ptr<T>)` ajoute un widget déjà créé.
La taille de chaque axe est un `SizeSpec` :

| Valeur | Effet |
| --- | --- |
| `SizeSpec::fit()` (défaut) | la taille du contenu |
| `SizeSpec::fixed(px)` | `px` pixels |
| `SizeSpec::fill(poids = 1)` | la place restante du parent, partagée selon le poids ; champs publics `min` et `max` |

Autres réglages d'un widget : `setMargin(Insets)`, `setAlign(x, y)` (`Align::kStart`, `kCenter`, `kEnd`, `kStretch` ; défaut
`kStretch`), `setEnabled`, `setVisible`, `bindVisible(f)`, `bindEnabled(f)`, `setTooltip`, `setId`, `columnSpan`, `overflow`.

## Lier des valeurs

Un widget lit sa valeur par `get` à chaque image et l'écrit par `set`. L'écriture est différée : elle est appliquée à la fin de
`Ui::update`, donc visible à l'image suivante.

| Fonction | Pour |
| --- | --- |
| `Binding<T>::ref(variable)` | `bool`, `std::string`, `Gradient`, `Curve`, `glm::vec4` |
| `numberBinding(variable)` | tout entier ou flottant, vers un `Binding<double>` (arrondi pour un entier) |
| `intBinding(variable)` | entier ou énumération, vers `Binding<int>` (liste déroulante, onglets, boutons radio) |
| `colorBinding(variable)` | `glm::vec3` ou `glm::vec4`, vers `Binding<Color>` |
| `Binding<T>::constant(valeur)` | valeur en lecture seule |

Un `Binding` sans `set` affiche la valeur sans permettre de la changer. Pour un texte calculé : `label.bindText(f)`.
`ui::text(a, b, ...)` met bout à bout chaînes, entiers, booléens et flottants (au plus deux décimales).

## Réagir aux événements

`onClick`, `onChange`, `onHover` et `onFocus` prennent un `Handler` (`std::function<void(const Event &)>`). Ils sont appelés à la
fin de `update`, après les écritures de liaisons : un `onChange` lit déjà la nouvelle valeur.

```cpp
panel.emplace<ui::Slider>(ui::numberBinding(settings.speed), 0.0, 5.0, 0.1).onChange([&](const ui::Event &) { /* ... */ });

gui.actions().add("reset", ui::handler([&] { settings.reset(); }));   // action nommée
```

`ui::handler(f)` accepte une fonction avec ou sans le paramètre `Event`. Les actions nommées sont utiles au code généré par
`.cui` : `.onClick(reset)` les retrouve dans `gui.actions()` au moment de l'événement. `gui.actions().missing()` liste celles
qui ont été demandées mais jamais ajoutées.

## Panneau automatique

Une structure décrite par `core::Reflect<T>` devient un panneau de widgets, sans code d'interface :

```cpp
panel.add(ui::reflectedPanel(params));          // un widget par champ, adapté au type et à la plage
```

Booléen : `Checkbox` ; énumération avec `core::EnumNames` : `Dropdown` ; nombre borné : `Slider` ; non borné : `NumberField` ;
`std::string` : `TextField` ; `Vec3` ou `Vec4` dont le nom contient « color » : `ColorPicker` ; structure imbriquée : `Section`.
Un `std::vector` s'affiche seulement comme « [n] » (pas d'édition de liste). Les identifiants sont les chemins des champs
(`"relief.octaves"`), `onChange` du panneau est appelé à chaque modification.

## Laisser passer la souris au jeu

```cpp
gui.update(input, viewport);
if (!gui.wantsMouse())    { /* caméra, sélection : la souris n'est pas sur l'interface */ }
if (!gui.wantsKeyboard()) { /* touches du jeu : aucun champ de texte n'a le focus */ }
```

`wantsMouse()` est vrai si le pointeur est sur un widget interactif ou à fond visible (un conteneur transparent laisse
passer), pendant un glissement commencé sur l'interface, ou si une fenêtre modale est ouverte. `wantsKeyboard()` est vrai
quand un champ de texte a le focus.

Un exemple complet de menu (Échap, Quitter, changement de thème) est dans [Écrire une vue en .cui](/guides/interface/langage-cui/#exemple--un-menu-qui-souvre-avec-échap).

## Limites

- La couche interface est marquée « Partiel » dans [l'état du projet](/projet/etat/). Navigation au clavier et à la manette :
  [Naviguer au clavier et à la manette](/guides/interface/navigation/).
- Un arbre construit en C++ ne se recharge pas à chaud (le thème, si) ; une vue `.cui` se recharge en développement :
  [Vues rechargées à chaud](/guides/interface/rechargement-vues/).

## Voir aussi

[Catalogue des widgets](/guides/interface/widgets/), [le langage .cui](/guides/interface/langage-cui/),
[thèmes JSON](/guides/interface/themes/), [fonctionnement interne de l'interface](/interne/ui/).
