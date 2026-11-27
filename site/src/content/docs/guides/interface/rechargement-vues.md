---
title: "Vues rechargées à chaud"
description: "Modifier un fichier .cui pendant que le programme tourne et voir l'écran se mettre à jour, en gardant focus, défilement et sections ouvertes ; ce qui demande un redémarrage."
sidebar:
  order: 5
---

En développement, une vue `.cui` enregistrée pendant que le programme tourne est recompilée, et l'interface est recréée
sans redémarrage. Une erreur garde l'ancienne vue et s'affiche en notification. En version finale, le C++ généré est lié
directement : aucun code de rechargement n'est compilé.

En-tête : `orosmo/app/view_host.hpp` (`app::ViewHost`). Avec la façade, il suffit de `a.setView(cui::HudView{...})` et
des réglages `app::defaultSettings(titre)` (`orosmo/app/default_game.hpp`), comme dans le projet d'`orosmo init`.

## Comment ça marche

1. En debug et dev, le C++ généré des vues est compilé **dans la bibliothèque de jeu** (et dans l'exécutable) ; le
   `Makefile` du projet définit `OROSMO_CUI_RELOADABLE`.
2. `ViewHost` observe les `.cui` de `src/`. À l'enregistrement, il lance la commande de construction (`make game
   BUILD_TYPE=<type>` dans le projet) sur un thread, sans bloquer l'image.
3. La bibliothèque reconstruite est rechargée ([Logique de jeu rechargeable](/guides/scene/logique-rechargeable/)) ; la
   vue y est retrouvée par son nom (`orosmoCuiView_<Vue>`) et l'arbre est recréé.
4. Focus, anneau de focus, défilement (`ScrollView`, `TextArea`), sections ouvertes et onglet non lié sont gardés pour les
   widgets qui ont un identifiant (`.id("…")`).

```bash
orosmo run
# programme lancé : ajoutez Label("Added while running").id("added") à src/HudView.cui et enregistrez
```

L'état lié à la vue (`@Mutable var state`, la structure passée à `setView`) vit dans le programme : il survit au
rechargement.

## Erreurs

Un `.cui` invalide (composant inconnu, syntaxe) ne change rien à l'écran : l'ancienne vue reste, l'erreur est écrite dans
le journal et montrée par `Ui::notify` (si l'arbre a un widget `Notifications`, comme le gabarit). `ViewHost::lastError()`
la rend aussi. Le fichier corrigé est pris à l'enregistrement suivant.

## Ce qui demande un redémarrage

- Un champ lié ajouté ou retiré (`@Mutable`, `@Observed`, `@Snapshot`) ou un bloc `@cui-cpp` modifié : la vue n'est pas
  recréée, l'ancienne reste (avec un message) : relancez le programme reconstruit (`make`). Une disposition changée d'un
  type lié (`HudState` de `hud_state.hpp`) n'est pas détectée : reconstruisez tout et relancez (G361).
- `make CUI_IN_GAME=0` garde les vues dans l'exécutable seulement : plus de rechargement des vues.

## Limites

- Une seule construction à la fois, sans annulation (G363) ; l'état gardé se limite à la liste ci-dessus (G362).
- Les objets des vues sont liés deux fois en développement (bibliothèque et exécutable, G366).
- Vérifié sous Linux ; Windows et macOS par la CI (G360).

## Voir aussi

- [Compiler avec Make ou CMake](/reference/construction/#logique-de-jeu-rechargeable)
- [Écrire une vue en .cui](/guides/interface/langage-cui/)
- Les thèmes JSON se rechargent aussi à chaud : [Thèmes](/guides/interface/themes/)
