# ui (`orosmo::ui`)

Interface du moteur selon `docs/03` §12 : arbre de widgets, mise en page, thèmes JSON, événements nommés ; produit des listes de
dessin (`orosmo::renderer::DrawList`, UI-02) et ne connaît ni le RHI ni le backend. Aucun singleton : chaque application possède
son `Ui` (l'interface héritée `UI::`, son singleton `UI::Manager` et `src/ui/legacy/` ont été supprimés le 2026-11-20, la démo
utilise ce module).

- **Couche** : `ui` (`docs/03` §1) ; dépend de `core` et de `renderer` (liste de dessin, texte).
- **Image** : `ui.update(input, viewport)` (rechargement du thème ; entrées appliquées à la mise en page AFFICHÉE ; écritures des
  liaisons puis événements, dans l'ordre ; nouvelle mise en page), puis `ui.draw(list)` et `CanvasRenderer::render(list)`.
- **API publique** : `include/orosmo/ui/`.

| En-tête | Contenu |
| --- | --- |
| `theme.hpp` | UI-07 : `Theme` (jetons, propriétés par widget et par état, `extends`, erreurs `fichier: chemin.json: message`), `Style`, `WidgetState`, `ThemeHost` (thème courant, rechargement à chaud par `core::FileWatcher`, dernier thème valide gardé) |
| `widget.hpp` | `Widget` (arbre, identifiant, type de style et surcharges locales, tailles `fit` / `fixed` / `fill` avec min et max, marges, alignement, `columnSpan`, visibilité et activation liées, infobulle, événements `onClick` / `onChange` / `onHover` / `onFocus`), `Binding<T>` (liaison dans les deux sens, écriture différée), `numberBinding`, `handler` |
| `layout.hpp` | UI-06 : `VBox`, `HBox`, `Panel`, `Grid` (colonnes, `fill`, `columnSpan`), `Overlay`, `ScrollView` (molette, barre, découpage) |
| `widgets.hpp` | UI-03 : `Label`, `Button`, `Checkbox`, `Slider`, `Dropdown` (fenêtre surgissante), `TextField`, `NumberField`, `ColorPicker` (le carré d'aperçu ouvre la roue `ColorEditor` en fenêtre surgissante), `Plot`, `Tabs`, `Section`, `Separator` ; infobulles par `Widget::setTooltip` |
| `controls.hpp` | UI-03 : `RadioButton`, `RangeSlider` (deux poignées), `SpinBox` (champ numérique à − / +), `TextArea` (éditeur multiligne, numéros de ligne, lecture seule) ; `TextEditState` (sélection, presse-papiers) partagé avec `TextField` |
| `color.hpp` | UI-03 : conversions RGB / HSV / HSL / hexadécimal ; `Gradient`, `Curve` (données évaluables par le jeu) ; `ColorWheel`, `ColorEditor` (formats, alpha, hexadécimal), `GradientEditor`, `CurveEditor` |
| `window_input.hpp` | `readWindowInput(window, dt)` : entrées de l'image lues sur une fenêtre `platform` (souris, texte, touches répétées, raccourcis) ; `useSystemClipboard(ui, window)` |
| `menus.hpp` | UI-03 : `Dialog` (fenêtre modale), `MenuBar`, `Menu` (sous-menus), `MenuItem` (raccourci, case cochée), `ContextMenu` (clic droit), `Notifications` (nombre et hauteur maximaux), `DynamicImage` + `ImageView` (image produite par le jeu : mini-carte, boussole) |
| `ui.hpp` | `Ui` : racine, thème, police, `ActionRegistry` (actions par nom), entrées de l'image (`InputFrame`, `Key`), survol / appui / focus / tabulation, pile de fenêtres surgissantes (menus, sous-menus), fenêtres modales, notifications (`notify`), presse-papiers (`setClipboard`), infobulle, `wantsMouse` / `wantsKeyboard`, écritures et événements différés |
| `font.hpp` | `Font` ; `FixedFont` (tests, sans GPU) ; `TextRendererFont` (adapte `TextRenderer` à `Font`) |
| `text_renderer.hpp` | `TextRenderer` : polices FreeType, atlas MSDF construit à la demande (ASCII, puis Latin-1 / Latin étendu A au premier texte qui en contient), mesure, mise en page (retour à la ligne, alignement, débordement), séries de glyphes ajoutées à la `DrawList` donnée par `setDrawListSink` |
| `nine_slice.hpp` | UI-07 : fonds en neuf parties (`ImageStyle` du thème : coins fixes, bords et centre étirés, répétés ou supprimés) → `DrawImage` |
| `reflect.hpp` | UI-12 : `reflectedPanel(objet)` : une structure `core::Reflect<T>` en widgets adaptés au type et à la plage ; `fieldLabel` ; `core::EnumNames<E>` pour les listes |

Thèmes livrés : `res/themes/base.json` (structure : chaque propriété de chaque widget renvoie à un jeton), `dark.json` et
`light.json` (jetons seulement), `framed.json` (hérite de `dark.json` et ajoute des cadres en neuf parties, `res/themes/images/`),
`hud.json` (hérite de `dark.json` : panneau de performances de la démo, styles `hud`, `hudTitle`, `hudMetric`).
Rechargement : `ThemeHost::load` qui échoue garde le thème courant mais observe le thème demandé (et les parents qu'il cherchait) ;
`ThemeHost::set` installe un thème sans fichier à recharger. Format : `docs/UI_LANGUAGE.md`, section « Thèmes ».

```cpp
orosmo::ui::ThemeHost themes;
if (auto r = themes.load("res/themes/dark.json"); !r) { /* r.error().message : "res/themes/dark.json: widgets.button.radius: ..." */ }
themes.watch(watcher);                       // watcher.poll() puis themes.reloadIfChanged() à chaque image
const orosmo::ui::Style &s = themes.current().style("button", orosmo::ui::WidgetState::kHover);
```

Le canevas (`CanvasRenderer`) dessine les formes du thème : `DrawRect::radius` (coins arrondis), `borderWidth` / `borderColor`
(bordure intérieure), `softness` (ombre portée).

```cpp
orosmo::ui::FixedFont font;               // ou TextRendererFont(textRenderer)
orosmo::ui::Ui ui(font);
auto panel = std::make_unique<orosmo::ui::Panel>();
panel->emplace<orosmo::ui::Slider>(orosmo::ui::numberBinding(params.heightScale), 0.0, 500.0).onChange(regenerate);
panel->add(orosmo::ui::reflectedPanel(params));     // UI-12
ui.setRoot(std::move(panel));
// chaque image : ui.update(input, viewport) ; ui.draw(list)
```

Image du jeu : le jeu écrit les pixels d'une `DynamicImage` puis appelle `commit()` ; la texture est mise à jour en place à la
prochaine image (même taille). Le presse-papiers du système est branché par l'application (`ui.setClipboard(get, set)`, par
exemple `platform::Window::getClipboardText` / `setClipboardText`) ; sans lui, un presse-papiers interne.

```cpp
orosmo::ui::DynamicImage minimap(128, 128);
root->emplace<orosmo::ui::ImageView>([&] { return minimap.image(); }, orosmo::ui::ImageFit::kContain);
// chaque image : minimap.setPixel(x, y, couleur)... ; minimap.commit();
ui.notify("Terrain enregistré", orosmo::ui::NotificationKind::kSuccess);   // affichée par un widget Notifications
```

Clavier : `InputFrame::shortcuts` porte les combinaisons de l'image (« Ctrl+S », forme de `normalizeShortcut`). Avec le focus dans
un champ de texte, Ctrl+A / C / X / V / Z / Y et Ctrl+Maj+Z (sur macOS : Super+A / C / X / V / Z et Super+Maj+Z pour rétablir)
y sont des commandes d'édition (`TextEditState` : sélection, presse-papiers, annuler / rétablir par mots ; un `TextArea` en lecture
seule ou lié sans setter garde la sélection et la copie, `Widget::handlesEditCommands`) ; sinon l'interface les propose aux widgets (`Widget::onShortcut`) : les `MenuItem`
(raccourci affiché ET installé, menu fermé compris) et les boutons (`Button::setShortcut`). Les menus se parcourent au clavier
(flèches, Entrée, Échap). `InputFrame::events` (rempli par `readWindowInput`) garde l'ordre réel du texte et des touches, et
l'état de Maj de chaque appui.

Contrats de robustesse : un événement différé dont la source a été détruite n'est pas livré ; les actions différées au-delà de
huit vagues passent à l'image suivante ; le focus quitte un widget inactif, masqué ou hors de la fenêtre modale ;
`clearChildren` garde la structure des widgets composés (Tabs, Section, éditeurs).

Limites (docs/GAPS.md) : pas de navigation à la manette (UI-09) ; `Plot` en barres seulement ; composition IME non affichée (G107).

## Rendu économe (UI-13)

- Caché = rien : `Widget::draw` retourne dès que `visible()` est faux (onglet inactif, section repliée compris).
- Hors découpage = rien : `Widget::layout` calcule l'englobant du sous-arbre (ombre comprise, descendants découpés exclus) ; `draw`
  l'élimine s'il ne rencontre pas l'intersection de la fenêtre et des zones de découpage des ancêtres (`clipsChildren`/`childClip`,
  `overflow(.hidden)`). Une liste de 1000 lignes dans un `ScrollView` ne produit que les lignes visibles.
- Cache par couche : non livré (voir `docs/GAPS.md`).
