# Langage d'interface (`.cui`) — spécification

UI-11. Référence du langage d'interface du moteur, écrite à partir du compilateur (`tools/cui-rs/`) et vérifiée par ses tests :
`cargo test` (`tools/cui-rs/src/tests.rs`), `make cui-test` (entrées invalides de bout en bout, `tests/ui/cui_errors/`) et
`make demo-test` (une vue valide compilée puis exécutée, `tests/ui/cui/test_panel.cui` et `tests/ui/cui_tests.cpp`).

Principe (`docs/03` §12, règle 12 de `CLAUDE.md`) : le langage **décrit** l'arbre de widgets, les liaisons et les événements ; il
ne **décide** rien. La logique est en C++ (les événements nomment des fonctions C++), l'apparence est dans un thème JSON (les
surcharges locales nomment des jetons du thème). Un fichier `.cui` est compilé en C++ par `cui`, puis par le compilateur C++ avec le
reste du programme : aucun interprète à l'exécution.

Exemples : `apps/demo/src/UI/Views/DemoView.cui` (vue de la démo, `make run`) et `tests/ui/cui/test_widgets.cui` (composants et modificateurs). La galerie `apps/ui_gallery` a été retirée du dépôt (A37).

## 1. Cible

Un seul produit : `struct Vue` + `cui::buildVue(ui, …)` pour le module `orosmo::ui` (`include/orosmo/ui/`). Chaque fichier
commence par `import "orosmo/ui"` ; un fichier sans cet import est refusé (`missing `import "orosmo/ui"`…`). L'ancienne cible
`UI::View` (interface héritée `include/UI/`, registre des composants lu dans les en-têtes, options `--headers` et `--registry`)
a été supprimée le 2026-11-20 avec l'interface héritée ; le panneau de la démo (`apps/demo/src/UI/Views/PerformanceView.cui`)
utilise cette cible.

## 2. Grammaire

```
fichier     := import* vue*
import      := "import" CHAÎNE                    // "orosmo/ui" est obligatoire ; les autres deviennent des #include
vue         := "view" NOM "{" (champ | let | func | cpp)* élément "}"     // exactement un élément racine
champ       := ("@Mutable" | "@Observed" | "@Snapshot") "var" NOM ":" TYPE ["=" expr]
let         := "let" NOM [":" TYPE] "=" expr       // constante : static constexpr
func        := "func" NOM "(" [NOM ":" TYPE ("," NOM ":" TYPE)*] ")" "->" TYPE "=" expr   // une expression, pour l'affichage
cpp         := "@cui-cpp" "{" … "}"               // échappatoire : recopié dans la structure (avertissement)
élément     := NOM ["(" args ")"] ["{" élément* "}"] modificateur*
modificateur:= "." NOM ["(" args ")"]
args        := arg ("," arg)* ;  arg := [NOM ":"] expr
expr        := unaire (("+" | "-" | "*" | "/") unaire)*
unaire      := "$" chemin                         // liaison dans les deux sens
             | ("once" | "always") primaire | postfixe
postfixe    := primaire ("." NOM ["(" args ")"])*
primaire    := ENTIER | FLOTTANT | "true" | "false" | CHAÎNE | "." NOM | NOM ("::" NOM)* ["(" args ")"] | "(" expr ")"
CHAÎNE      := '"' (texte | "\(" expr ")")* '"'   // interpolation
```

Commentaires `//` et `/* */`. Les nombres suffixés `s`, `ms`, `us`, `ns` sont des durées en secondes.

## 3. Champs de la vue

| Déclaration | C++ généré | Usage |
| --- | --- | --- |
| `@Mutable var state: State` | `State *state` | lu et **écrit** (liaisons `$state.x`, événements `state.f`) |
| `@Observed var stats: Stats` | `const Stats *stats` | lu seulement ; `$stats.x` est une erreur |
| `@Snapshot var title: string = "x"` | `std::string title` (copie) | valeur figée à la construction |
| `let width = 320` | `static constexpr auto width = 320.0;` | constante |
| `func pct(v: double) -> double = v * 100.0` | `static double pct(double v)` | mise en forme d'une valeur affichée |

`@State` et `@Binding` ne sont pas acceptés par cette cible : l'état vit en C++ (règle 13), la vue le lie par `@Mutable`.

Chaque vue `V` produit, dans le fichier `<nom du fichier>.gen.h` :

```cpp
namespace cui {
struct V { State *state = nullptr; const Stats *stats = nullptr; /* let, func */ std::unique_ptr<orosmo::ui::Widget> build(orosmo::ui::Ui &) const; };
inline std::unique_ptr<orosmo::ui::Widget> buildV(orosmo::ui::Ui &ui, State &state, const Stats &stats);
}
ui.setRoot(cui::buildV(ui, state, stats));   // les objets liés doivent vivre plus longtemps que l'interface
```

## 4. Composants

Arguments positionnels dans l'ordre du tableau, ou nommés (`min: 0`). Un argument avec valeur par défaut peut être omis.

| Composant | Arguments | Enfants | Événements |
| --- | --- | --- | --- |
| `VBox`, `HBox`, `Panel`, `Overlay` | — | plusieurs | onHover, onFocus |
| `Grid` | `columns` (nombre) | plusieurs (`.span(n)`) | onHover, onFocus |
| `ScrollView` | — | **un** | onHover, onFocus |
| `Tabs` | `selected` (entier lié, facultatif) | des `Page` seulement | onChange |
| `Page` | `title` (texte) | le contenu de l'onglet | — |
| `Section` | `title` (texte), `expanded` (= true) | le contenu repliable | onHover, onFocus |
| `Label` | `text` (texte, = "") | — | onHover, onFocus |
| `Button` | `text` (texte) | — | onClick |
| `Checkbox` | `text` (texte), `checked` (booléen lié) | — | onClick, onChange |
| `Slider` | `value` (nombre lié), `min`, `max`, `step` (= 0 : continu), `default` (clic droit ; = la valeur liée à la construction) | — | onChange |
| `Dropdown` | `options` (expression → `std::vector<std::string>`), `selected` (entier lié) | — | onChange |
| `TextField` | `text` (chaîne liée) | — | onChange |
| `NumberField` | `value` (nombre lié), `integer` (= false), `min`, `max` | — | onChange |
| `ColorPicker` | `color` (couleur liée : `glm::vec3` ou `glm::vec4`), `alpha` (= false) | — | onChange (un clic sur le carré d'aperçu ouvre une fenêtre surgissante avec la roue `ColorEditor` liée à la même couleur ; Échap, clic extérieur ou second clic la ferme ; apparence : style `popup`) |
| `Plot` | `values` (expression → suite de nombres, relue à chaque image) | — | onHover, onFocus |
| `Separator` | — | — | onHover, onFocus |
| `ParamPanel` | `target` : `$chemin` vers une structure réfléchie (`core::Reflect<T>`, UI-12) | — | onChange (tout champ) |
| `RadioButton` | `text`, `value` (nombre), `selected` (entier lié : les boutons d'un groupe lient le même entier) | — | onClick, onChange |
| `RangeSlider` | `low`, `high` (nombres liés), `min`, `max`, `step` (= 0) | — | onChange |
| `SpinBox` | `value` (nombre lié), `min`, `max` (= sans borne), `step` (= 1), `integer` (= true) | — | onChange |
| `TextArea` | `text` (chaîne liée), `lineNumbers` (= false), `readOnly` (= false), `wrap` (= true : retour à la ligne automatique) | — | onChange |
| `ColorWheel` | `color` (couleur liée) | — | onChange |
| `ColorEditor` | `color` (couleur liée), `alpha` (= false) | — | onChange |
| `GradientEditor` | `gradient` (`orosmo::ui::Gradient` lié) | — | onChange |
| `CurveEditor` | `curve` (`orosmo::ui::Curve` lié) | — | onChange |
| `Dialog` | `title`, `open` (booléen lié : ouverte tant qu'il vaut vrai), `dismiss` (= true : un clic dehors ferme) | le contenu de la fenêtre | onChange (fermeture) |
| `MenuBar` | — | des `Menu` seulement | onHover, onFocus |
| `Menu` | `title` | `MenuItem`, `Menu` (sous-menu), `Separator` | onHover, onFocus |
| `MenuItem` | `text`, `shortcut` (= "" ; affiché ET installé : « Ctrl+S » déclenche l'élément, menu fermé compris), `checked` (booléen lié, facultatif) | — (dans `Menu` ou `ContextMenu`) | onClick |
| `ContextMenu` | — | comme `Menu` ; ouvert par un clic droit sur son parent | — |
| `Notifications` | `max` (= 5), `maxHeight` (pixels, = 0 : sans limite), `width` (= 0 : celle du texte) | — | — |
| `ImageView` | `image` (expression → `orosmo::ui::DynamicImage` ou `std::shared_ptr<const renderer::AtlasImage>`, relue à chaque image) | — | onHover, onFocus |

onHover et onFocus sont acceptés partout sauf sur `Page`.

**Texte** : une chaîne (`"Graine"`), une chaîne interpolée (`"FPS \(stats.fps)"`, relue à chaque image : un flottant s'affiche
avec au plus deux décimales, `orosmo::ui::text`) ou une expression C++ (`state.title`).

**Curseur** (`Slider`) : un clic sans glisser place la valeur sous le pointeur ; un glissement part de la valeur courante et la
déplace de la distance parcourue (toute la largeur = toute la plage ; Maj : dix fois plus fin) ; clic droit : valeur par défaut.

**Fenêtres et menus** : `.width` / `.height` d'un `Dialog` règlent la fenêtre (le `Dialog` ne prend pas de place dans l'arbre).
Échap ferme le menu ouvert, sinon la fenêtre modale (un champ de saisie prend d'abord Échap pour perdre le focus). Au clavier :
Tab jusqu'à un titre de la barre, Bas / Entrée l'ouvrent, Haut / Bas parcourent, Droite / Gauche entrent dans un sous-menu ou en
sortent (ou passent au menu voisin de la barre), Entrée active. Une fenêtre modale ouverte ne laisse passer que ses propres
raccourcis. Les
notifications viennent de `Ui::notify(texte, genre, secondes)` côté C++ ; au-delà de `max` ou de `maxHeight`, les plus anciennes
sont retirées.

**Valeur liée** : `$chemin` (deux sens : le widget écrit dans `state->chemin`, écriture différée à la fin de `Ui::update`) ou une
expression (lecture seule). Le chemin commence par un champ `@Mutable`.

## 5. Modificateurs

| Modificateur | Effet |
| --- | --- |
| `.width(320)`, `.width(.fill)`, `.width(.fit)`, `.width(.fill, weight: 2, min: 100, max: 400)` ; `.height(...)` | taille fixe, remplissage pondéré, ajustée (UI-06) |
| `.margin(8)`, `.margin(left:, top:, right:, bottom:, horizontal:, vertical:)` | marge extérieure |
| `.align(.center)`, `.align(x: .end, y: .start)` (`.start`, `.center`, `.end`, `.stretch`) | alignement dans la place du parent |
| `.span(2)` | colonnes occupées dans une `Grid` |
| `.id("seed")` | identifiant (`Widget::find`) |
| `.tooltip("texte")` | infobulle |
| `.tabIndex(2)`, `.tabIndex(-1)` | ordre de tabulation (UI-09) : positif d'abord, par valeur croissante ; `0` (défaut) dans l'ordre de l'arbre ; négatif : hors de l'ordre de tabulation (le clic et les flèches l'atteignent encore) |
| `.focusable(false)` | le widget ne prend jamais le focus (ni Tab, ni flèches, ni clic) ; `true` : comportement par défaut du widget |
| `.visible(expr)`, `.enabled(expr)` | `true`/`false`, ou expression relue à chaque image |
| `.variant("title")` | type de style du thème (`widgets.title`) |
| `.overflow(.visible)` / `.overflow(.hidden)` | ce qui dépasse du widget : visible et cliquable (défaut), ou coupé à son rectangle ; pour faire défiler : `ScrollView` |
| `.style(background: "color.danger", radius: 4)` | surcharge locale : jeton, couleur `"#rrggbb[aa]"` ou nombre (§7) |
| `.textAlign(.center)` | `Label` seulement |
| `.range(0, 100)` | `Plot` seulement : bornes fixes |
| `.fit(.contain)`, `.fit(.cover)`, `.fit(.stretch)` | `ImageView` seulement : image entière, remplissage coupé, ou déformée |
| `.shortcut("Ctrl+R")` | `Button` seulement : raccourci clavier qui clique le bouton (modificateurs Ctrl, Alt, Shift, Super ; touche : lettre, chiffre, `F1`–`F25`, `Delete`, `PageUp`...) |
| `.onClick(f)`, `.onChange(f)`, `.onHover(f)`, `.onFocus(f)` | événements (§6) |

## 6. Événements

Un événement **nomme** une fonction C++ ; ce n'est jamais une expression ni un appel (`.onClick(state.go(1))` est refusé).

| Écriture | Appel généré |
| --- | --- |
| `.onClick(state.regenerate)` | appel direct de `state->regenerate()` ou `state->regenerate(event)` (selon la signature) |
| `.onClick(Tools::save)` | appel direct de la fonction libre `Tools::save()` ou `Tools::save(event)` |
| `.onClick(regenerate)` | action **nommée**, retrouvée dans `ui.actions()` au moment de l'événement (`ui.actions().add("regenerate", …)` ; `ui.actions().missing()` liste celles qui manquent) |

Une `func` de la vue ne peut pas servir d'événement (pas de logique dans le langage). `orosmo::ui::Event` : `kind`, `source`,
`active` (survol : entrée ou sortie ; focus : pris ou perdu). Les événements sont appelés à la fin de `Ui::update`, après les
écritures des liaisons qui les précèdent : le gestionnaire de `onChange` lit la nouvelle valeur.

## 7. Thèmes

Fichier JSON (`res/themes/`) : des **jetons** et, par type de widget, des propriétés qui renvoient à ces jetons, avec des variantes
par **état**. Un thème peut en étendre un autre ; les variantes claire et sombre (`light.json`, `dark.json`) étendent
`base.json` et ne redéfinissent que les jetons.

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

- **Jeton** : couleur `"#rrggbb"` / `"#rrggbbaa"`, nombre, ou nom d'un autre jeton (alias).
- **Images en neuf parties** (section `images`) : un fond qui n'est pas une simple couleur, valable pour toutes les tailles sans
  rien déformer. L'image est découpée par `slice` (un nombre, ou `[gauche, haut, droite, bas]` en texels) en quatre coins (taille
  fixe), quatre bords et un centre ; `edges` et `center` valent `"stretch"` (étiré), `"repeat"` (répété à sa taille, la dernière
  répétition est coupée) ou `"none"` (supprimé : un cadre sans centre, ou des coins seuls) ; `scale` = pixels par texel (de 0,01 à 100 ; une répétition reste bornée à 4096 motifs par bande, au-delà la bande est étirée). Si le
  widget est plus petit que deux coins, ils sont réduits ensemble. Un widget la prend par `"image": "<nom>"` (par état ;
  `"none"` la retire), teintée par `"imageTint"` ; elle se dessine par-dessus `background` (souvent transparent). Exemple livré :
  `res/themes/framed.json` (cadres biseautés, `res/themes/images/`).

```json
"images": { "frame.button": { "file": "images/button.png", "slice": 8, "edges": "repeat", "center": "stretch", "scale": 1 } },
"widgets": { "button": { "background": "#00000000", "image": "frame.button",
                         "states": { "hover": { "image": "frame.buttonHover" }, "disabled": { "imageTint": "#ffffff80" } } } }
```
- **Polices** (section `fonts`) : `"fonts": {"title": "fonts/MaPolice-Bold.ttf"}` (fichier TrueType / OpenType relatif au thème),
  puis `"font": "title"` dans un widget ou un état (`"default"` : la police de l'application) ; dans une vue :
  `.style(font: "title")`. Le texte est en UTF-8 : ASCII, Latin-1 et Latin étendu A (accents, ç, œ, ß…), guillemets, tirets,
  points de suspension, euro ; les glyphes étendus sont chargés au premier texte qui en contient.
- **Propriétés** : `background`, `foreground`, `border`, `accent`, `muted`, `shadowColor`, `imageTint` (couleurs) ; `borderWidth`,
  `radius`, `padding`, `spacing`, `fontSize`, `shadowSize`, `shadowOffset`, `width` (pixels) ; `image` (nom d'image ou `"none"`) ;
  `font` (nom de police ou `"default"`).
- **États** : `normal`, `hover`, `pressed`, `disabled`, `focus`. Ordre d'application : `default`, le widget, l'état de
  `default`, l'état du widget.
- **Types de widget** : `panel`, `box`, `label`, `title`, `caption`, `button`, `checkbox`, `slider`, `textField`, `dropdown`, `popup`,
  `item`, `tabs`, `tab`, `scroll`, `tooltip`, `plot`, `section`, `sectionHeader`, `colorPicker`, `separator`, `radio`, `textArea`,
  `colorWheel`, `gradient`, `curve`, `image`, `modalBackdrop`, `dialog`, `dialogTitle`, `dialogClose`, `menuBar`, `menu`, `menuItem`,
  `toast`, `toastSuccess`, `toastWarning`, `toastError`, `focusRing` (et tout nom choisi par
  `.variant`).
- **Anneau de focus** (UI-09) : le style `focusRing` (état `normal`) dessine un contour autour du widget focalisé après une
  navigation au clavier ou à la manette (pas après un clic) : `border` (couleur), `borderWidth`, `radius`, `padding` (écart
  entre le widget et l'anneau ; négatif : à l'intérieur). Un thème sans `focusRing`, ou avec `borderWidth` à 0, n'en dessine pas.
  `base.json` le relie aux jetons `color.focus`, `border.focus`, `radius.medium` et `focus.ringGap`.
- **Rechargement à chaud** : `ThemeHost::watch(fileWatcher)` ; un fichier invalide laisse le thème précédent en place, avec l'erreur
  `fichier: widgets.button.radius: expects a number` dans le journal.

## 8. Erreurs

`cui` écrit chaque erreur sous la forme des compilateurs, cliquable dans un éditeur, et les signale **toutes** (pas seulement la
première) ; un fichier invalide ne produit aucun fichier généré (l'ancien est supprimé : le build échoue au lieu de compiler une
version périmée) :

```
tests/ui/cui_errors/e03_unknown_modifier.cui:5:19: error: 'Label' has no modifier 'colour' (modifiers: width, height, …)
```

La colonne est celle de l'élément (son nom) ou du modificateur (son point) ; un symbole manquant est signalé juste après le jeton
qui le précède (`5:18: error: expected ')' (found '}')`). `cui check [--stdin] fichier.cui` écrit les mêmes diagnostics en JSON
(`file`, `line`, `col`, `endLine`, `endCol`, `message`, `severity`) pour l'extension VS Code (`tools/vscode-cui/` : diagnostics pendant la frappe, complétion et survol tirés de `cui spec`, la
description JSON des composants, modificateurs et propriétés de thème).

Chaque instruction C++ générée est précédée d'une directive `#line` vers le `.cui` : une erreur du **compilateur C++** (méthode
inexistante, type faux) désigne la ligne du langage d'interface qui l'a produite (`tests/ui/cui_errors/e18_cxx_error_points_to_cui.cui`).

Messages vérifiés par les tests (extrait) :

| Cas | Message |
| --- | --- |
| composant inconnu | `unknown component 'Buton' (components: VBox, …)` |
| modificateur inconnu | `'Label' has no modifier 'colour' (modifiers: …)` |
| événement non permis | `'Label' has no event 'onChange' (events: onHover, onFocus)` |
| événement qui n'est pas un nom | `'.onClick' names a C++ function (…); it cannot be an expression or a call` |
| `func` comme événement | `'.onClick(helper)': 'helper' is a func of the view; an event must name a C++ function (…)` |
| liaison vers un champ en lecture seule | `'$stats.fps': 'stats' is @Observed (read only); a two-way binding needs a @Mutable field` |
| liaison hors des champs | `'$flag': a two-way binding must start with a @Mutable field of the view ('flag' is not a field)` |
| argument manquant / inconnu | `'Slider' requires the argument 'max'` ; `'Button' has no argument 'color' (arguments: text)` |
| propriété de thème inconnue | `unknown theme property 'colour' (properties: background, …)` |
| structure | `'Page' is only allowed directly inside 'Tabs'` ; `'ScrollView' takes exactly one child (put several in a VBox)` |
| état local | `@State is not supported with orosmo/ui: keep the state in C++ and bind it with @Mutable` |
| import manquant | `missing `import "orosmo/ui"`: it is the only target of the interface language (…)` |
| lexique | `Unexpected character '#'` ; `Unterminated string literal` |

## 9. Intégration au build

```bash
cui run --output Generated/ src/Panel.cui   # Generated/Panel.gen.h, Panel.gen.cpp (+ Generated/Panel.cui)
```

Le SDK (`tools/engine-sdk/engine.mk`) régénère `Generated/%.gen.cpp` quand le `.cui` change ; un hachage du source (et de la version du format de sortie) en tête du
`.gen.h` évite de réécrire un fichier inchangé.

## 10. Limites

- Les positions d'erreur sont celles de l'élément ou du modificateur, pas de l'argument fautif (les expressions n'ont pas de position).
- `once` dans une interpolation n'est pas figé par cette cible : le texte est relu à chaque image.
- Pas de boucle ni de condition dans le langage (règle 12) : une liste de widgets variable se construit en C++, ou par `ParamPanel`.
- Pas de rechargement à chaud de la vue elle-même (UI-08, P1) : le thème se recharge, la vue se recompile.
- Navigation (UI-09) : Tab / Maj+Tab suivent l'ordre de tabulation ; une flèche que le widget focalisé n'utilise pas (bouton, case
  à cocher...) passe le focus au voisin le plus proche dans sa direction (sans boucler) ; un curseur, un champ de texte, une
  liste déroulante et les menus gardent leurs flèches. Manette (`ui::GamepadNavigator`, `platform::Gamepads`) : croix et stick
  gauche = flèches (répétées tant qu'elles sont tenues ; un curseur horizontal laisse Haut et Bas à la navigation), A = Entrée,
  B = Échap, RB / LB = Tab / Maj+Tab. Le défilement d'une `ScrollView` vers le widget focalisé n'est pas fait (G222, docs/GAPS.md). La sélection de texte et le presse-papiers existent (champs de texte, éditeur
  multiligne ; presse-papiers du système par `ui::useSystemClipboard`) ; la composition IME n'est pas affichée (G107).
