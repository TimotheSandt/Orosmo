# ui (`engine::ui`)

Interface du moteur selon `docs/03` §12 : arbre de widgets, mise en page, thèmes JSON, événements nommés ; produit des listes de
dessin (`engine::renderer::DrawList`, UI-02) et ne connaît ni le RHI ni le backend. L'interface héritée (`UI::`, `include/UI/`)
reste dans `src/ui/legacy/` tant que la démo l'utilise.

- **Couche** : `ui` (`docs/03` §1) ; dépend de `core` et de `renderer` (liste de dessin, texte).
- **Image** : `ui.update(input, viewport)` (rechargement du thème ; entrées appliquées à la mise en page AFFICHÉE ; écritures des
  liaisons puis événements, dans l'ordre ; nouvelle mise en page), puis `ui.draw(list)` et `CanvasRenderer::render(list)`.
- **API publique** : `include/engine/ui/`.

| En-tête | Contenu |
| --- | --- |
| `theme.hpp` | UI-07 : `Theme` (jetons, propriétés par widget et par état, `extends`, erreurs `fichier: chemin.json: message`), `Style`, `WidgetState`, `ThemeHost` (thème courant, rechargement à chaud par `core::FileWatcher`, dernier thème valide gardé) |
| `widget.hpp` | `Widget` (arbre, identifiant, type de style et surcharges locales, tailles `fit` / `fixed` / `fill` avec min et max, marges, alignement, `columnSpan`, visibilité et activation liées, infobulle, événements `onClick` / `onChange` / `onHover` / `onFocus`), `Binding<T>` (liaison dans les deux sens, écriture différée), `numberBinding`, `handler` |
| `layout.hpp` | UI-06 : `VBox`, `HBox`, `Panel`, `Grid` (colonnes, `fill`, `columnSpan`), `Overlay`, `ScrollView` (molette, barre, découpage) |
| `widgets.hpp` | UI-03 : `Label`, `Button`, `Checkbox`, `Slider`, `Dropdown` (fenêtre surgissante), `TextField`, `NumberField`, `ColorPicker`, `Plot`, `Tabs`, `Section`, `Separator` ; infobulles par `Widget::setTooltip` |
| `controls.hpp` | UI-03 : `RadioButton`, `RangeSlider` (deux poignées), `SpinBox` (champ numérique à − / +), `TextArea` (éditeur multiligne, numéros de ligne, lecture seule) ; `TextEditState` (sélection, presse-papiers) partagé avec `TextField` |
| `color.hpp` | UI-03 : conversions RGB / HSV / HSL / hexadécimal ; `Gradient`, `Curve` (données évaluables par le jeu) ; `ColorWheel`, `ColorEditor` (formats, alpha, hexadécimal), `GradientEditor`, `CurveEditor` |
| `menus.hpp` | UI-03 : `Dialog` (fenêtre modale), `MenuBar`, `Menu` (sous-menus), `MenuItem` (raccourci, case cochée), `ContextMenu` (clic droit), `Notifications` (nombre et hauteur maximaux), `DynamicImage` + `ImageView` (image produite par le jeu : mini-carte, boussole) |
| `ui.hpp` | `Ui` : racine, thème, police, `ActionRegistry` (actions par nom), entrées de l'image (`InputFrame`, `Key`), survol / appui / focus / tabulation, pile de fenêtres surgissantes (menus, sous-menus), fenêtres modales, notifications (`notify`), presse-papiers (`setClipboard`), infobulle, `wantsMouse` / `wantsKeyboard`, écritures et événements différés |
| `font.hpp` | `Font` ; `FixedFont` (tests, sans GPU) ; `TextRendererFont` (FreeType + MSDF de `UI::TextRenderer`) |
| `nine_slice.hpp` | UI-07 : fonds en neuf parties (`ImageStyle` du thème : coins fixes, bords et centre étirés, répétés ou supprimés) → `DrawImage` |
| `reflect.hpp` | UI-12 : `reflectedPanel(objet)` : une structure `core::Reflect<T>` en widgets adaptés au type et à la plage ; `fieldLabel` ; `core::EnumNames<E>` pour les listes |

Thèmes livrés : `res/themes/base.json` (structure : chaque propriété de chaque widget renvoie à un jeton), `dark.json` et
`light.json` (jetons seulement). Format : `docs/UI_LANGUAGE.md`, section « Thèmes ».

```cpp
engine::ui::ThemeHost themes;
if (auto r = themes.load("res/themes/dark.json"); !r) { /* r.error().message : "res/themes/dark.json: widgets.button.radius: ..." */ }
themes.watch(watcher);                       // watcher.poll() puis themes.reloadIfChanged() à chaque image
const engine::ui::Style &s = themes.current().style("button", engine::ui::WidgetState::kHover);
```

Le canevas (`CanvasRenderer`) dessine les formes du thème : `DrawRect::radius` (coins arrondis), `borderWidth` / `borderColor`
(bordure intérieure), `softness` (ombre portée).

```cpp
engine::ui::FixedFont font;               // ou TextRendererFont(textRenderer)
engine::ui::Ui ui(font);
auto panel = std::make_unique<engine::ui::Panel>();
panel->emplace<engine::ui::Slider>(engine::ui::numberBinding(params.heightScale), 0.0, 500.0).onChange(regenerate);
panel->add(engine::ui::reflectedPanel(params));     // UI-12
ui.setRoot(std::move(panel));
// chaque image : ui.update(input, viewport) ; ui.draw(list)
```

Image du jeu : le jeu écrit les pixels d'une `DynamicImage` puis appelle `commit()` ; la texture est mise à jour en place à la
prochaine image (même taille). Le presse-papiers du système est branché par l'application (`ui.setClipboard(get, set)`, par
exemple `Window::GetClipboardText` / `SetClipboardText`) ; sans lui, un presse-papiers interne.

```cpp
engine::ui::DynamicImage minimap(128, 128);
root->emplace<engine::ui::ImageView>([&] { return minimap.image(); }, engine::ui::ImageFit::kContain);
// chaque image : minimap.setPixel(x, y, couleur)... ; minimap.commit();
ui.notify("Terrain enregistré", engine::ui::NotificationKind::kSuccess);   // affichée par un widget Notifications
```

Limites (docs/GAPS.md) : pas de navigation à la manette (UI-09) ; `Plot` en barres seulement ; `TextArea` sans retour à la ligne
automatique ni annulation ; menus sans navigation au clavier (flèches) ; `MenuItem` affiche le raccourci sans l'installer.
