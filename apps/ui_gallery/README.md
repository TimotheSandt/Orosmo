# ui-gallery : galerie de l'interface (exemple)

Tout ce que l'interface du moteur sait faire, dans une seule fenêtre, décrit dans le langage d'interface (`src/Gallery.cui`) sur un
état C++ (`src/gallery_state.hpp/.cpp`) :

| Onglet | Ce qu'il montre |
| --- | --- |
| Widgets | `Button` (dont un désactivé par une expression et une action nommée), `Checkbox`, `Slider` (clic, glissement relatif, Maj, clic droit = défaut), curseur à pas, curseur en lecture seule, `Dropdown`, `TextField`, `NumberField`, `ColorPicker` (avec alpha), `Section` repliable, infobulles |
| Controls | `RadioButton`, `RangeSlider` (deux poignées), `SpinBox` (− / + et molette), `TextField` (sélection, presse-papiers), `TextArea` (éditeur multiligne, numéros de ligne) |
| Color | `ColorEditor` (roue, RGB / HSV / HSL, alpha, hexadécimal, avant / après), `ColorWheel` seule, `GradientEditor`, `CurveEditor` |
| Windows | `Dialog` modale, `ContextMenu` (clic droit), boutons de notification (`Ui::notify`), `ImageView` de deux `DynamicImage` dessinées par l'état C++ à chaque image : mini-carte (hauteurs → courbe → dégradé de la page Color, joueur qui tourne) et boussole ; `.fit(.cover)` |
| Layout | `HBox` à poids de remplissage et taille fixe, `Grid` avec une cellule sur deux colonnes, `Overlay` (alignements sur les deux axes), marges, tailles min et max, `ScrollView` |
| Parameters | `ParamPanel` : une structure C++ réfléchie affichée sans code d'interface (case, liste, curseurs, champ numérique, texte, couleur, section imbriquée) |
| Themes | surcharges locales `.style(...)` (jeton, couleur, rayon, taille de police), images en neuf parties (`frame.button` à deux tailles, `frame.border` sans centre), `.variant(...)` |

En haut : `MenuBar` (File avec un sous-menu Notify, View avec des éléments cochés, Help). En bas à droite : `Notifications` (au plus
4, au plus 300 px de haut ; les plus anciennes partent ; clic pour fermer).

Colonne de gauche : choix du thème (`dark`, `light`, `framed`, rechargés à chaud si `bin/res/themes/*.json` change), journal des
événements (`onClick`, `onChange`, `onHover`, `onFocus` appelés sur l'état C++), graphique `Plot` animé.

```bash
make gallery-run                               # à la racine du dépôt
make gallery-run ARGS="--theme framed --tab themes"
make gallery-capture                           # images sans fenêtre : build/gallery-capture/<thème>-<onglet>/gallery.png
apps/ui_gallery/bin/ui-gallery --headless --tab widgets --click fruit --capture /tmp/out   # liste déroulante ouverte
```

Clavier : Tab / Maj+Tab pour le focus, Entrée / Espace pour activer, Échap pour quitter (hors saisie) ou fermer un menu, une
fenêtre ; Ctrl+A / C / X / V / Z / Y dans les champs de texte (presse-papiers du système, annuler, rétablir), Page précédente /
suivante dans l'éditeur ; touches répétées quand elles sont tenues. Menus : Tab jusqu'à « File », puis flèches, Entrée, Échap.
Raccourcis installés : Ctrl+O (dialogue), Ctrl+I (notification), Ctrl+L (vider le journal), F5 (« Click me »).
