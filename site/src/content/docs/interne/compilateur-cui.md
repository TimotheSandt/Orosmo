---
title: "Le compilateur .cui (tools/cui-rs)"
description: "Comment l'outil cui transforme une vue .cui en C++ : commandes, fichiers générés, diagnostics et tests."
sidebar:
  order: 6
---

Le compilateur du langage d'interface est un programme Rust, `tools/cui-rs/` (binaire `cui`). Le build l'appelle ; il est livré
compilé dans le paquet du moteur, donc ni Rust ni Python ne sont nécessaires pour l'utiliser. Pour écrire des vues :
[Écrire une vue en .cui](/guides/interface/langage-cui/) ; langage complet : [référence](/reference/langage-cui/).

## Construire l'outil

```bash
make cui-tool       # installe tools/cui (tools/cui.exe sous Windows)
cargo build --release --manifest-path tools/Cargo.toml -p cui    # ou directement : tools/target/release/cui
```

## Commandes

| Commande | Effet |
| --- | --- |
| `cui run --output <dossier> <fichier.cui>...` | écrit `<nom>.gen.h`, `<nom>.gen.cpp` et une copie `<nom>.cui` (redirection) dans le dossier |
| `cui redirect --output <dossier> <fichier.cui>...` | écrit seulement la redirection `<nom>.cui` |
| `cui check [--stdin] <fichier.cui>` | diagnostics en JSON : `file`, `line`, `col`, `endLine`, `endCol`, `message`, `severity` (pour l'extension VS Code) |
| `cui spec` | composants, modificateurs et propriétés de thème en JSON (complétion et survol de l'extension) |
| `cui --version` | version du compilateur, version du format de sortie et empreinte du build |

Source du binaire : `main.rs` (commandes), `lexer.rs`, `parser.rs`, `ast.rs`, `engine.rs` (validation et génération du C++),
`tests.rs`.

## Fichiers générés

Chaque vue `V` donne dans `<nom>.gen.h` une structure `cui::V` (un champ par déclaration de la vue, une constante par `let`, une
fonction statique par `func`) et `cui::buildV(Ui &, <champs>)`. `<nom>.gen.cpp` contient `V::build`, qui construit le même arbre
que l'on écrirait à la main avec `emplace` et `Binding` : un `Label` interpolé devient `bindText([=] { return ui::text(...); })`,
un événement `state.f` devient `ui::handler(...)`, une action nommée `f` devient `ui.actions().bind("f")`.

Chaque instruction est précédée d'une directive `#line` vers le `.cui` : une erreur du compilateur C++ désigne la ligne de la
vue. La première ligne du `.gen.h` est `// @cui-hash: ...` : un hachage du source, de la version du format de sortie
(`OUTPUT_FORMAT_VERSION`, dans `main.rs`) et de l'empreinte du compilateur ; un fichier inchangé n'est pas réécrit. Changer le
générateur sans changer la syntaxe impose d'incrémenter cette constante. Les noms qui commencent par `cui_` sont réservés.

## Intégration au build

- Make : `mk/engine.mk` (copié en `engine.mk` dans le paquet) trouve les `.cui` de `GAME_SRC_DIR`, produit
  `GEN_CPP_SOURCES` et `GEN_REDIRECT_SOURCES` dans `ENGINE_GEN_DIR`, et relance `cui run` quand le `.cui` ou l'outil change.
  Le projet doit déclarer que ses objets dépendent de ces fichiers (`$(OBJS): | $(GEN_CPP_SOURCES) $(GEN_REDIRECT_SOURCES)`).
- CMake : `orosmo_add_cui_views(cible src/Vue.cui ...)` (`tools/engine-sdk/cmake/OrosmoConfig.cmake`).

## Diagnostics

`fichier:ligne:colonne: error: message`. Le compilateur signale toutes les erreurs d'un fichier, pas seulement la première. Un
fichier invalide ne laisse aucun fichier généré : les anciens sont supprimés, le build échoue au lieu de compiler une version
périmée. La règle « le langage décrit, il ne décide pas » est appliquée ici : événement qui est une expression ou un appel,
`func` utilisée comme événement, liaison vers un champ `@Observed`, `@State` local sont refusés avec un message qui dit quoi faire.

## Tests

```bash
make cui-test    # cargo test -p cui -p orosmo, puis tools/check_cui_errors.py sur tests/ui/cui_errors/
make demo-test   # vues valides compilées puis exécutées : tests/ui/cui/*.cui, tests/ui/cui_tests.cpp
```

Chaque fichier de `tests/ui/cui_errors/` annonce son résultat en commentaire :

- `// expect: LIGNE:COLONNE: error: MESSAGE` : `cui` doit échouer avec exactement cette ligne, et ne rien laisser de généré ;
- `// expect-cxx: LIGNE` : `cui` réussit, mais le C++ généré ne compile pas, et l'erreur du compilateur C++ désigne cette ligne du `.cui`.

Pour ajouter un cas : créer `eNN_nom.cui` avec son `expect`, puis `make cui-test`.

## Limites

Les positions d'erreur sont celles de l'élément ou du modificateur, pas de l'argument fautif (les expressions n'ont pas de
position). `once` n'est pas figé par la cible `orosmo/ui`.
