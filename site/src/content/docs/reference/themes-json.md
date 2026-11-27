---
title: "Thèmes : jetons et propriétés"
description: "Toutes les clés d'un thème JSON : jetons, widgets, états, propriétés, images en neuf parties, polices, héritage et erreurs."
sidebar:
  order: 6
---

Format lu par `orosmo::ui::Theme` (`src/ui/theme.cpp`). Pour écrire un thème pas à pas : [Changer l'apparence avec un thème JSON](/guides/interface/themes/).
Les thèmes livrés sont dans `res/themes/`. Les commentaires `//` et `/* */` sont acceptés dans le JSON.

## Clés de premier niveau

| Clé | Type | Effet |
| --- | --- | --- |
| `name` | chaîne | nom du thème |
| `extends` | chaîne | fichier parent, relatif au fichier qui l'écrit ; le parent est lu d'abord, ce fichier le surcharge. Un cycle est une erreur |
| `tokens` | objet | jetons : nom vers couleur `"#rrggbb"` ou `"#rrggbbaa"`, nombre, ou nom d'un autre jeton |
| `images` | objet | images en neuf parties (voir plus bas) |
| `fonts` | objet | nom vers fichier de police, relatif au thème |
| `widgets` | objet | style par type de widget |
| `description`, `$schema` | chaîne | acceptées, sans effet |

Toute autre clé est refusée : `unknown key (expected name, extends, tokens, images, fonts, widgets)`.

## Jetons

Les couleurs et les nombres sont des jetons du même objet `tokens` ; un jeton qui en nomme un autre est un alias (un cycle d'alias
est une erreur). Une propriété de widget peut aussi recevoir directement une couleur `"#rrggbb[aa]"` ou un nombre.
Les composantes d'une couleur sont en sRGB 0..255, sans conversion de profil.

Jetons de forme, dans `base.json` (communs à tous les thèmes livrés) :

| Jeton | Valeur |
| --- | --- |
| `radius.small` | 3 |
| `radius.medium` | 6 |
| `space.small` | 4 |
| `space.medium` | 8 |
| `space.large` | 12 |
| `font.size.small` | 13 |
| `font.size.body` | 15 |
| `font.size.title` | 19 |
| `border.width` | 1 |
| `border.focus` | 2 |
| `focus.ringGap` | 2 |
| `shadow.size` | 10 |
| `shadow.offset` | 3 |
| `control.size` | 18 |
| `scrollbar.size` | 6 |

Jetons de couleur, dans `dark.json` et `light.json` :

| Jeton | `dark.json` | `light.json` |
| --- | --- | --- |
| `color.background` | `#121419` | `#dfe3ea` |
| `color.surface` | `#1d2028e8` | `#f5f6f9eb` |
| `color.surfaceRaised` | `#272b35` | `#ffffff` |
| `color.outline` | `#3a3f4d` | `#c9cdd6` |
| `color.outlineHover` | `#59607a` | `#9aa1b2` |
| `color.text` | `#e6e8ef` | `#1c1f26` |
| `color.textMuted` | `#9aa1b2` | `#5b6170` |
| `color.textDisabled` | `#5d6373` | `#a4a9b5` |
| `color.muted` | `#3a3f4d` | `#d5d9e1` |
| `color.control` | `#2c303b` | `#e8eaef` |
| `color.controlHover` | `#363b48` | `#dde1e8` |
| `color.controlDisabled` | `#23262e` | `#eff0f3` |
| `color.field` | `#15171d` | `#ffffff` |
| `color.primary` | `#4c8dff` | `#2f6fe0` |
| `color.primaryHover` | `#6ea3ff` | `#4c8dff` |
| `color.onPrimary` | `#ffffff` | `#ffffff` |
| `color.focus` | `#8ab4ff` | `#2f6fe0` |
| `color.scrollbar` | `#59607a` | `#9aa1b2` |
| `color.tooltip` | `#e6e8ef` | `#1c1f26` |
| `color.onTooltip` | `#15171d` | `#f5f6f9` |
| `color.success` | `#3fb950` | `#1a7f37` |
| `color.warning` | `#d29922` | `#9a6700` |
| `color.error` | `#f85149` | `#cf222e` |
| `color.backdrop` | `#00000099` | `#1c1f2666` |
| `color.shadow` | `#00000080` | `#00000040` |

`hud.json` ajoute `color.hud`, `color.hudPlot`, `color.hudCpu`, `color.hudGpu`, `font.size.hudTitle`, `font.size.hudMetric` et
`font.size.hudDetail`. Les noms des jetons sont libres : ceux-ci sont une convention des thèmes livrés, pas une liste fermée.

## Widgets et états

`widgets.<type>` est un objet de propriétés, plus `states`, un objet dont les clés sont des états :

| État | Quand |
| --- | --- |
| `normal` | cas général ; ses propriétés valent aussi pour les autres états |
| `hover` | pointeur dessus |
| `pressed` | bouton enfoncé, pointeur toujours dessus |
| `disabled` | le widget ou un de ses parents est désactivé |
| `focus` | focus clavier |

Un autre nom d'état est refusé (`unknown state (known: ...)`). Priorité de l'état : `disabled`, `pressed`, `focus`, `hover`, `normal`.
Ordre d'application : `default`, le type de widget, puis (hors `normal`) l'état `normal` de `default` et du type, puis l'état courant de
`default` et du type. Un type absent du thème prend le style de `default`.

Types de widget de `base.json` : `default`, `panel`, `box`, `label`, `title`, `caption`, `button`, `checkbox`, `slider`, `textField`,
`textArea`, `dropdown`, `popup`, `item`, `tabs`, `tab`, `scroll`, `tooltip`, `plot`, `section`, `sectionHeader`, `colorPicker`, `separator`,
`radio`, `colorWheel`, `gradient`, `curve`, `image`, `modalBackdrop`, `focusRing`, `dialog`, `dialogTitle`, `dialogClose`, `menuBar`, `menu`, `menuItem`,
`toast`, `toastSuccess`, `toastWarning`, `toastError`, et ceux des outils de debug : `debugPanel`, `debugRow`, `debugRowSelected`,
`debugText`, `debugInputLine`, `debugWarning`, `debugError`, `debugInput`, `debugHighlight`. `focusRing` est l'anneau dessiné autour du
widget focalisé après une navigation au clavier ou à la manette (`border`, `borderWidth`, `radius` ; `padding` : écart au widget,
jeton `focus.ringGap`) ; un thème sans `focusRing` n'en dessine pas ([Navigation](/guides/interface/navigation/)). Tout autre nom est un style à vous, utilisable par `.variant("nom")`
dans une vue ou `setStyleName("nom")` en C++ (le deuxième argument de `Label` et de `Button` est un nom de style).

## Propriétés

| Propriété | Type | Rôle |
| --- | --- | --- |
| `background` | couleur | fond |
| `foreground` | couleur | texte, icônes |
| `border` | couleur | bordure |
| `accent` | couleur | partie active : remplissage d'un curseur, coche, onglet choisi |
| `muted` | couleur | partie inactive : piste d'un curseur |
| `shadowColor` | couleur | ombre portée |
| `imageTint` | couleur | multipliée par l'image (défaut blanc) |
| `borderWidth` | pixels | épaisseur de la bordure |
| `radius` | pixels | coins arrondis |
| `padding` | pixels | marge intérieure |
| `spacing` | pixels | espace entre enfants |
| `fontSize` | pixels | taille du texte (défaut 16 si rien n'est défini) |
| `shadowSize` | pixels | flou de l'ombre |
| `shadowOffset` | pixels | décalage vertical de l'ombre |
| `width` | pixels | taille de référence : piste d'un curseur, case à cocher, barre de défilement |
| `image` | nom d'image, ou `"none"` | fond en neuf parties, dessiné par-dessus `background` |
| `font` | nom de police, ou `"default"` | police du texte |

Une propriété inconnue donne `unknown property (known: ...)`. Une couleur donnée à une propriété numérique donne `expects a number`, et
inversement `expects a color`.

## Images en neuf parties

`images.<nom>` :

| Clé | Type | Défaut | Effet |
| --- | --- | --- | --- |
| `file` | chaîne | obligatoire | PNG, relatif au fichier de thème qui le nomme |
| `slice` | nombre ou `[gauche, haut, droite, bas]` | 0 | taille des coins en texels ; ils doivent tenir dans l'image |
| `edges` | `"stretch"`, `"repeat"`, `"none"` | `"stretch"` | remplissage des quatre bords |
| `center` | `"stretch"`, `"repeat"`, `"none"` | `"stretch"` | remplissage du centre |
| `scale` | nombre de 0,01 à 100 | 1 | pixels logiques par texel |

`"repeat"` répète à la taille de l'image, la dernière répétition est coupée ; `"none"` supprime la bande (un cadre sans centre).
Si le widget est plus petit que deux coins, ils sont réduits ensemble. Une autre clé donne `unknown key (expected file, slice, edges, center, scale)`.

## Polices

`fonts.<nom>` : fichier TrueType ou OpenType, relatif au thème ; il doit exister au chargement (`cannot read ...`). `"font": "<nom>"`
dans un widget ou un état l'applique ; un nom inconnu donne `unknown font '...' (section "fonts", or "default")`.

## Rechargement

`ThemeHost::watch(FileWatcher &)` observe le thème, ses parents, ses images et ses polices ; `Ui::update` applique le changement à
l'image suivant `FileWatcher::poll()`. Un fichier invalide laisse le thème précédent en place.

## Messages d'erreur

Forme : `fichier: chemin.json: message`, par exemple `res/themes/dark.json: widgets.button.radius: expects a number`.

| Message | Cause |
| --- | --- |
| `cannot read the theme file` | fichier absent ou illisible |
| `not a JSON object` | JSON invalide |
| `cycle in "extends"` | un thème s'étend lui-même |
| `unknown key (expected ...)` | clé de premier niveau inconnue |
| `tokens.<nom>: invalid color '...' (expected #rrggbb or #rrggbbaa)` | couleur mal écrite |
| `tokens.<nom>: unknown token '...'` | alias vers un jeton absent |
| `tokens.<nom>: alias cycle (...)` | alias circulaire |
| `widgets.<type>.states.<état>: unknown state` | nom d'état inconnu |
| `images.<nom>.slice: the corners do not fit in the WxH image` | coins plus grands que l'image |
| `...: expects an image name (section "images") or "none"` | `image` reçoit autre chose |

## Voir aussi

[Guide des thèmes](/guides/interface/themes/), [langage .cui](/reference/langage-cui/) (`.variant` et `.style`).
