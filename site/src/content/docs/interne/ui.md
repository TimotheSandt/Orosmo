---
title: "Interface (orosmo::ui)"
description: Arbre de widgets, passes de mise en page, écritures différées, événements et rendu économe du module ui.
sidebar:
  order: 5
---

`orosmo::ui` (`include/orosmo/ui/`, `src/ui/`) construit l'interface : arbre de widgets, mise en page, thèmes JSON, événements
nommés. Il produit des listes de dessin (`renderer::DrawList`) et ne connaît ni le RHI ni le backend. Il n'a aucun singleton :
chaque application possède son `Ui`. Couche : `ui`, au-dessus de `core` et `renderer` (voir [couches](/interne/couches/)).
Pour s'en servir : [Construire une interface en C++](/guides/interface/premiere-interface/).

## Une image

```text
Ui::update(input, viewport)
  1. rechargement du thème si un fichier a changé (ThemeHost::reloadIfChanged)
  2. entrées appliquées à la mise en page AFFICHÉE (survol, appui, focus, molette, texte, touches, raccourcis)
  3. écritures des liaisons, puis événements, dans l'ordre où ils ont été programmés
  4. nouvelle mise en page (measure puis layout)
Ui::draw(list)  -> DrawList
CanvasRenderer::render(encoder, list, target, loadExisting)
```

Les entrées viennent de `readWindowInput(window, dt)` (`window_input.hpp`) qui lit une `platform::Window` ; le
presse-papiers du système se branche par `useSystemClipboard(ui, window)`.

## Fichiers

| En-tête | Contenu |
| --- | --- |
| `widget.hpp` | `Widget` (arbre, identifiant, style, tailles, marges, alignement, événements), `Binding<T>`, `numberBinding`, `intBinding`, `colorBinding`, `handler`, `text` |
| `layout.hpp` | `VBox`, `HBox`, `Panel`, `Grid`, `Overlay`, `ScrollView` |
| `widgets.hpp`, `controls.hpp`, `color.hpp`, `menus.hpp` | les [widgets](/guides/interface/widgets/) |
| `ui.hpp` | `Ui`, `InputFrame`, `Key`, `ActionRegistry`, `PopupPlacement`, `NotificationKind` |
| `theme.hpp`, `nine_slice.hpp` | `Theme`, `Style`, `WidgetState`, `ThemeHost`, fonds en neuf parties |
| `font.hpp`, `text_renderer.hpp` | `Font`, `FixedFont` (tests, sans GPU), `TextRendererFont`, `TextRenderer` (FreeType, atlas MSDF) |
| `reflect.hpp` | `reflectedPanel(objet)` : une structure `core::Reflect<T>` en widgets |
| `window_input.hpp` | `readWindowInput`, `useSystemClipboard` |

## Mise en page

Deux passes. `Widget::measure` va du bas vers le haut et donne la taille souhaitée ; `Widget::layout` va du haut vers le bas
et donne le rectangle final. Les coordonnées sont en pixels logiques, origine en haut à gauche, comme `DrawList`. Une taille est
un `SizeSpec` (`fit`, `fixed`, `fill` avec poids, minimum et maximum). La marge intérieure, l'espacement, les couleurs et la
taille de police viennent du thème, jamais du widget.

## Écritures différées

Un widget lit sa valeur par `Binding::get` à chaque image, mais écrit par `Binding::set` via `Ui::defer` : l'écriture est
appliquée à la fin de `update`. Les événements programmés (`Widget::emit`) sont appelés après les écritures qui les précèdent.

Contrats de robustesse, testés par `tests/ui/robustness_tests.cpp` et `review3_runtime_tests.cpp` :

- un événement différé dont la source a été détruite n'est pas livré (jeton de durée de vie dans `Widget`) ;
- les actions différées au-delà de huit vagues passent à l'image suivante ;
- le focus quitte un widget inactif, masqué ou hors de la fenêtre modale ;
- `clearChildren` garde la structure des widgets composés (`Tabs`, `Section`, éditeurs) ;
- `setRoot` appelé depuis un événement est appliqué à la fin de l'image.

## Fenêtres surgissantes et modales

`Ui` tient une pile de fenêtres surgissantes (`openPopup(owner, content, anchor, PopupPlacement)` : liste déroulante, menu,
sous-menu, fenêtre de la roue de couleur) et une pile de fenêtres modales (`showModal`, utilisée par `Dialog`). Un clic hors de
la fenêtre ferme les surgissantes ; une modale ouverte ne laisse passer que ses propres raccourcis.

## Thèmes

`Theme::load` lit un fichier et ses parents, résout les jetons et précalcule le `Style` de chaque type de widget dans chaque
état. `ThemeHost` garde le thème courant, incrémente `revision()` à chaque installation (les widgets refont leur mise en page)
et observe les fichiers par `core::FileWatcher`. Format : [référence des thèmes](/reference/themes-json/).

## Rendu économe

- Un widget caché ne dessine rien : `Widget::draw` retourne dès que `visible()` est faux (onglet inactif et section repliée compris).
- `Widget::layout` calcule l'englobant du sous-arbre, ombre comprise ; `draw` l'élimine s'il ne rencontre pas l'intersection de la
  fenêtre et des zones de découpage des ancêtres. Une liste de 1000 lignes dans un `ScrollView` ne produit que les lignes visibles.
- Le cache de rendu par couche n'est pas livré.

Test : `tests/ui/lean_render_tests.cpp`.

## Tests

`tests/ui/` ne demande ni GPU ni fenêtre, sauf la suite `ui/render` (`ui_render_tests.cpp`, exécutée sur chaque backend) :
`ui_harness.hpp` fournit un `Ui` avec `FixedFont` et un thème de test, et simule clics, glissements et touches.
`make demo-test-unit` lance les autres suites sans GPU ; `make demo-test-gpu` lance `ui/render`. Les vues `.cui` valides sont compilées puis exécutées
(`tests/ui/cui/`, `cui_tests.cpp`).

## Limites

Composition IME non affichée ; une `ScrollView` ne défile pas vers le widget focalisé (G222) ; l'inspecteur ne montre pas
les fenêtres détachées (G296). Depuis M8 : [navigation au clavier et à la manette](/guides/interface/navigation/),
[outils de debug et inspecteurs](/guides/interface/outils-debug/), [vues rechargées à chaud](/guides/interface/rechargement-vues/)
(par la bibliothèque de jeu, `app::ViewHost`). Voir [l'état du projet](/projet/etat/).
