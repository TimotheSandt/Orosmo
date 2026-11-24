---
title: Le compilateur .cui (tools/cui-rs)
description: Comment l'outil cui transforme une vue .cui en C++, ses diagnostics et ses tests.
sidebar:
  order: 5
---

Le compilateur du langage d'interface est un programme Rust : `tools/cui-rs/` (binaire `cui`). Il est appelé par le build,
avec des dépendances déclarées pour ne recompiler que ce qui change, et livré compilé dans le paquet du moteur.
*(Sources : `tools/cui-rs/README.md`, `docs/UI_LANGUAGE.md`, `docs/03` §12.)*

## Chaîne

```text
fichier .cui ──cui run──▶ Generated/<nom>.gen.h + .gen.cpp ──compilateur C++──▶ programme
```

- `cui run --output Generated <fichier.cui>` : génère le C++. Chaque vue `V` produit une structure `cui::V` (un champ par
  déclaration de la vue) et une fonction `cui::buildV(ui, …)` qui construit l'arbre de widgets d'`orosmo::ui`.
- `cui check [--stdin] <fichier.cui>` : diagnostics en JSON (`file`, `line`, `col`, `endLine`, `endCol`, `message`,
  `severity`) pour l'extension VS Code.
- `cui spec` : description JSON des composants, modificateurs et propriétés de thème (complétion et survol de
  l'extension).

Un hachage du source et de la version du format de sortie, en tête du `.gen.h`, évite de réécrire un fichier inchangé.
Les noms commençant par `cui_` sont réservés au code généré.

## Une seule cible

Chaque fichier commence par `import "orosmo/ui"` ; un fichier sans cet import est refusé. Les autres `import` deviennent
des `#include`. L'ancienne cible de l'interface héritée a été supprimée avec elle.

## Diagnostics

Les erreurs ont la forme des compilateurs (`fichier:ligne:colonne: error: message`), cliquable dans un éditeur, et le
compilateur les signale **toutes**. Un fichier invalide ne produit aucun fichier généré (l'ancien est supprimé : le build
échoue au lieu de compiler une version périmée). Chaque instruction générée est précédée d'une directive `#line` : une
erreur du compilateur C++ désigne la ligne du `.cui` qui l'a produite.

Le compilateur fait respecter la règle « le langage décrit, il ne décide pas » : un événement qui est une expression ou un
appel, une `func` de la vue utilisée comme événement, une liaison vers un champ en lecture seule, ou un `@State` local
sont refusés avec un message qui dit quoi faire.

## Tests

```bash
make cui-test      # cargo test -p cui + les entrées invalides de tests/ui/cui_errors/ via le vrai binaire
make demo-test     # une vue valide compilée puis exécutée (tests/ui/cui/test_panel.cui, tests/ui/cui_tests.cpp)
```
