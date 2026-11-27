---
title: "Langage .cui : grammaire et composants"
description: "Grammaire, champs de vue, composants et leurs arguments, modificateurs, événements et messages d'erreur du langage .cui."
sidebar:
  order: 5
---

Référence du compilateur `tools/cui-rs/` (version du dépôt : v0.6.0). La liste des composants et des modificateurs est celle de
`cui spec`. Pour apprendre à s'en servir : [Écrire une vue en .cui](/guides/interface/langage-cui/).

Chaque fichier commence par `import "orosmo/ui"` (obligatoire : sinon l'erreur `missing import "orosmo/ui"`). Les autres
`import "x.hpp"` deviennent des `#include`. Commentaires `//` et `/* */`. Les nombres suffixés `s`, `ms`, `us`, `ns` sont des durées
en secondes.

## Grammaire

```text
fichier      := import* vue*
import       := "import" CHAÎNE
vue          := "view" NOM "{" (champ | let | func | cpp)* élément "}"      // exactement un élément racine
champ        := ("@Mutable" | "@Observed" | "@Snapshot") "var" NOM ":" TYPE ["=" expr]
let          := "let" NOM [":" TYPE] "=" expr
func         := "func" NOM "(" [NOM ":" TYPE ("," NOM ":" TYPE)*] ")" "->" TYPE "=" expr
cpp          := "@cui-cpp" "{" ... "}"                                        // recopié dans la structure (avertissement)
élément      := NOM ["(" args ")"] ["{" élément* "}"] modificateur*
modificateur := "." NOM ["(" args ")"]
args         := arg ("," arg)*     arg := [NOM ":"] expr
expr         := unaire (("+" | "-" | "*" | "/") unaire)*
unaire       := "$" chemin | ("once" | "always") primaire | postfixe
postfixe     := primaire ("." NOM ["(" args ")"])*
primaire     := ENTIER | FLOTTANT | "true" | "false" | CHAÎNE | "." NOM | NOM ("::" NOM)* ["(" args ")"] | "(" expr ")"
CHAÎNE       := '"' (texte | "\(" expr ")")* '"'
```

`"."NOM` seul est une valeur d'énumération : `.fit`, `.fill`, `.center`, `.start`, `.end`, `.stretch`, `.visible`, `.hidden`,
`.contain`, `.cover`. `$chemin` est une liaison dans les deux sens ; elle commence par un champ `@Mutable`.

## Champs de la vue

| Déclaration | C++ généré | Usage |
| --- | --- | --- |
| `@Mutable var state: State` | `State *state` | lu et écrit |
| `@Observed var stats: Stats` | `const Stats *stats` | lu seulement |
| `@Snapshot var title: string = "x"` | `std::string title` | copie figée à la construction |
| `let width = 320` | `static constexpr auto width = 320.0;` | constante |
| `func pct(v: double) -> double = v * 100.0` | `static double pct(double v)` | mise en forme d'un affichage |

`@State` et `@Binding` sont refusés. Dans `<fichier>.gen.h`, une vue `V` donne `cui::V` et
`cui::buildV(Ui &, <un paramètre par champ>)`.

## Composants

Arguments positionnels dans l'ordre du tableau, ou nommés (`min: 0`). Un argument avec une valeur par défaut peut être omis ;
sans défaut, il est obligatoire (`'Slider' requires the argument 'max'`).

| Composant | Arguments (défaut) | Enfants | Événements |
| --- | --- | --- | --- |
| `VBox`, `HBox`, `Panel`, `Overlay` | aucun | plusieurs | onHover, onFocus |
| `Grid` | `columns` | plusieurs (`.span(n)`) | onHover, onFocus |
| `ScrollView` | aucun | un seul | onHover, onFocus |
| `Tabs` | `selected` (entier lié, facultatif) | des `Page` seulement | onChange, onHover, onFocus |
| `Page` | `title` | le contenu de l'onglet | aucun |
| `Section` | `title`, `expanded` (`true`) | plusieurs | onHover, onFocus |
| `Label` | `text` (`""`) | aucun | onHover, onFocus |
| `Button` | `text` | aucun | onClick, onHover, onFocus |
| `Checkbox` | `text`, `checked` (booléen lié) | aucun | onClick, onChange, onHover, onFocus |
| `Slider` | `value` (nombre lié), `min`, `max`, `step` (`0` : continu), `default` (clic droit ; par défaut la valeur à la construction) | aucun | onChange, onHover, onFocus |
| `Dropdown` | `options` (expression vers `std::vector<std::string>`), `selected` (entier lié) | aucun | onChange, onHover, onFocus |
| `TextField` | `text` (chaîne liée) | aucun | onChange, onHover, onFocus |
| `NumberField` | `value` (nombre lié), `integer` (`false`), `min`, `max` (sans borne) | aucun | onChange, onHover, onFocus |
| `ColorPicker` | `color` (`glm::vec3` ou `glm::vec4` lié), `alpha` (`false`) | aucun | onChange, onHover, onFocus |
| `Plot` | `values` (expression vers une suite de nombres, relue chaque image) | aucun | onHover, onFocus |
| `Separator` | aucun | aucun | onHover, onFocus |
| `ParamPanel` | `target` : `$chemin` vers une structure `core::Reflect<T>` | aucun | onChange, onHover, onFocus |
| `RadioButton` | `text`, `value` (nombre), `selected` (entier lié, le même pour tout le groupe) | aucun | onClick, onChange, onHover, onFocus |
| `RangeSlider` | `low`, `high` (nombres liés), `min`, `max`, `step` (`0`) | aucun | onChange, onHover, onFocus |
| `SpinBox` | `value`, `min`, `max` (sans borne), `step` (`1`), `integer` (`true`) | aucun | onChange, onHover, onFocus |
| `TextArea` | `text` (chaîne liée), `lineNumbers` (`false`), `readOnly` (`false`), `wrap` (`true`) | aucun | onChange, onHover, onFocus |
| `ColorWheel` | `color` | aucun | onChange, onHover, onFocus |
| `ColorEditor` | `color`, `alpha` (`false`) | aucun | onChange, onHover, onFocus |
| `GradientEditor` | `gradient` (`orosmo::ui::Gradient` lié) | aucun | onChange, onHover, onFocus |
| `CurveEditor` | `curve` (`orosmo::ui::Curve` lié) | aucun | onChange, onHover, onFocus |
| `Dialog` | `title`, `open` (booléen lié), `dismiss` (`true` : un clic dehors ferme) | le contenu de la fenêtre | onChange (fermeture), onHover, onFocus |
| `MenuBar` | aucun | des `Menu` seulement | onHover, onFocus |
| `Menu` | `title` | `MenuItem`, `Menu`, `Separator` | onHover, onFocus |
| `MenuItem` | `text`, `shortcut` (`""`), `checked` (booléen lié, facultatif) | aucun | onClick, onHover, onFocus |
| `ContextMenu` | aucun | comme `Menu` | aucun |
| `Notifications` | `max` (`5`), `maxHeight` (`0` : sans limite), `width` (`0` : celle du texte) | aucun | aucun |
| `ImageView` | `image` (expression vers `DynamicImage` ou `std::shared_ptr<const renderer::AtlasImage>`) | aucun | onHover, onFocus |

Un `Dialog` ne prend pas de place dans l'arbre : `.width` et `.height` règlent la fenêtre. Un `MenuItem` avec `shortcut` installe
le raccourci (il marche menu fermé). Un `ContextMenu` est un enfant invisible : le clic droit sur son parent l'ouvre.

## Modificateurs

| Modificateur | Valeurs | Effet |
| --- | --- | --- |
| `.width(...)`, `.height(...)` | un nombre (fixe), `.fit`, `.fill`, avec `weight:`, `min:`, `max:` | `.width(.fill, weight: 2, min: 100, max: 400)` |
| `.margin(...)` | un nombre, ou `left:`, `top:`, `right:`, `bottom:`, `horizontal:`, `vertical:` | marge extérieure |
| `.align(...)` | `.start`, `.center`, `.end`, `.stretch` ; ou `x:` et `y:` | alignement dans la place du parent |
| `.span(n)` | entier | colonnes occupées dans une `Grid` |
| `.id("nom")` | chaîne | identifiant pour `Ui::find` et `Widget::find` |
| `.tooltip("texte")` | chaîne | infobulle |
| `.visible(e)`, `.enabled(e)` | `true`, `false` ou expression booléenne relue à chaque image | visibilité, activation |
| `.variant("nom")` | chaîne | style `widgets.<nom>` du thème |
| `.overflow(...)` | `.visible` (défaut), `.hidden` | ce qui dépasse : visible et cliquable, ou coupé |
| `.tabIndex(n)` | entier | ordre de tabulation : positif d'abord, croissant ; 0 dans l'ordre de l'arbre ; négatif hors de l'ordre ([Navigation](/guides/interface/navigation/)) |
| `.focusable(false)` | `true`, `false` | `false` : le widget ne prend jamais le focus (Tab, flèches, clic) |
| `.style(propriété: valeur, ...)` | une propriété de thème ; valeur : jeton, `"#rrggbb[aa]"`, nombre, nom d'image | surcharge locale |
| `.textAlign(...)` | `.start`, `.center`, `.end` | `Label` seulement |
| `.range(min, max)` | nombres | `Plot` seulement : bornes fixes |
| `.fit(...)` | `.contain`, `.cover`, `.stretch` | `ImageView` seulement |
| `.shortcut("Ctrl+R")` | chaîne | `Button` seulement : raccourci qui clique le bouton |
| `.onClick(f)`, `.onChange(f)`, `.onHover(f)`, `.onFocus(f)` | un nom de fonction | événements |

Propriétés acceptées par `.style(...)` : `background`, `foreground`, `border`, `accent`, `muted`, `shadowColor`,
`borderWidth`, `radius`, `padding`, `spacing`, `fontSize`, `shadowSize`, `shadowOffset`, `width`, `image`, `imageTint`, `font`
(voir [Thèmes : jetons et propriétés](/reference/themes-json/)).

## Événements

| Écriture | Appel généré |
| --- | --- |
| `.onClick(state.regenerate)` | `state->regenerate()` ou `state->regenerate(event)` |
| `.onClick(Tools::save)` | `Tools::save()` ou `Tools::save(event)` |
| `.onClick(regenerate)` | action nommée : `gui.actions().add("regenerate", ...)` ; `gui.actions().missing()` liste celles qui manquent |

`orosmo::ui::Event` : `kind`, `source`, `active` (survol : entrée ou sortie ; focus : pris ou perdu). Les événements sont appelés à
la fin de `Ui::update`, après les écritures de liaisons : un `onChange` lit la nouvelle valeur.

## Erreurs

Forme : `fichier:ligne:colonne: error: message`. La colonne est celle de l'élément ou du modificateur. Messages vérifiés par
`tests/ui/cui_errors/` (lancés par `make cui-test`) :

| Cas | Message |
| --- | --- |
| composant inconnu | `unknown component 'Buton' (components: VBox, ...)` |
| modificateur inconnu | `'Label' has no modifier 'colour' (modifiers: ...)` |
| événement non permis | `'Label' has no event 'onChange' (events: onHover, onFocus)` |
| événement qui est une expression | `'.onClick' names a C++ function (state.regenerate, Tools::save or a registered action name); it cannot be an expression or a call` |
| `func` comme événement | `'.onClick(helper)': 'helper' is a func of the view; an event must name a C++ function (no logic in the interface language)` |
| liaison vers `@Observed` | `'$stats.fps': 'stats' is @Observed (read only); a two-way binding needs a @Mutable field` |
| liaison hors des champs | `'$flag': a two-way binding must start with a @Mutable field of the view ('flag' is not a field)` |
| argument manquant | `'Slider' requires the argument 'max'` |
| argument inconnu | `'Button' has no argument 'color' (arguments: text)` |
| propriété de thème inconnue | `unknown theme property 'colour' (properties: background, ...)` |
| taille invalide | `'.width(.huge)': expected a number, .fit or .fill` |
| structure | `'Page' is only allowed directly inside 'Tabs'` ; `'ScrollView' takes exactly one child (put several in a VBox)` |
| état local | `@State is not supported with orosmo/ui: keep the state in C++ and bind it with @Mutable` |
| import manquant | `missing import "orosmo/ui"` (message complet dans le compilateur) |
| lexique | `Unexpected character '#'` ; `Unterminated string literal` ; `expected ')' (found '}')` |

## Limites

- Pas de boucle ni de condition : une liste de widgets variable se construit en C++, ou par `ParamPanel`.
- `once` et `always` sont acceptés devant une expression, sans effet.
- Le rechargement à chaud de la vue passe par la bibliothèque de jeu, en développement ([Vues rechargées à chaud](/guides/interface/rechargement-vues/)) ;
  un changement des champs liés demande un redémarrage.
