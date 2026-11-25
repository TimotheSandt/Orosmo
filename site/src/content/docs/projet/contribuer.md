---
title: Contribuer
description: "Proposer un changement au dépôt d'Orosmo : ticket, fork, pull request, vérifications et règles de code."
sidebar:
  order: 3
---

Le dépôt est [`TimotheSandt/Orosmo`](https://github.com/TimotheSandt/Orosmo) sur GitHub. Les règles de travail de l'auteur
sont dans [`CLAUDE.md`](https://github.com/TimotheSandt/Orosmo/blob/dev/CLAUDE.md) et
[`docs/06-WORKFLOW_GIT_ET_NETTOYAGE.md`](https://github.com/TimotheSandt/Orosmo/blob/dev/docs/06-WORKFLOW_GIT_ET_NETTOYAGE.md).

:::caution
Le dépôt n'a pas de fichier de licence pour l'instant (l'auteur n'a pas tranché). Tant qu'il n'y en a pas, rien ne vous
donne le droit de réutiliser ou de redistribuer le code ; une contribution sans licence claire est à discuter avec l'auteur
dans un ticket avant d'écrire du code.
:::

## Proposer un changement

1. Ouvrez un [ticket](https://github.com/TimotheSandt/Orosmo/issues) qui décrit le bug ou la fonction, avant d'écrire du code.
2. Faites un fork, puis une branche depuis `dev` : `fix/<sujet>`, `feat/<ID>-<sujet>` (l'ID est une ligne de la checklist,
   voir [État du moteur](/projet/etat/)), `chore/<sujet>` ou `docs/<sujet>`.
3. Ouvrez une pull request vers `dev`. `main` est réservé à l'auteur : il y fusionne `dev` à la fin de chaque jalon, en
   « Create a merge commit », jamais en squash.

Pas de `git push --force`, de `--no-verify`, ni de réécriture de l'historique de `main` ou `dev`.

## Commits

Petits, un sujet chacun, qui compilent. Format `type(ID): résumé`, avec `feat`, `fix`, `refactor`, `perf`, `test`, `docs`,
`build`, `ci`, `chore`. Les déplacements de fichiers (`git mv`) sont dans des commits séparés. Une partie du code est écrite
avec une IA : ces commits portent une ligne `Co-Authored-By` (voir [Histoire et méthode](/projet/histoire/)).

## Avant d'ouvrir la pull request

```bash
make all-debug
make demo-test                               # tous les tests doctest
make demo-test SANITIZE=address,undefined    # après make fclean
make check                                   # couches, isolation des API graphiques, en-têtes autonomes
sh tools/format_sources.sh                   # clang-format
```

Pour un seul test, passez un filtre doctest par `TEST_ARGS` (le moteur release est reconstruit d'abord) :

```bash
make demo-test TEST_ARGS="-tc=*vsync*"
```

ou construisez une fois, puis lancez le binaire de tests :

```bash
make demo-test-unit                                   # construit les tests et lance ceux qui n'ont pas besoin du GPU
apps/demo/bin/tests/tests -ts="platform/*"            # une suite (nom de la suite doctest)
apps/demo/bin/tests/tests -tc="*vsync*"               # un cas, par motif de son nom
```

Les tests sont dans `tests/<module>/` (un fichier `*_tests.cpp` par sujet) et sont compilés avec ceux de la démo : ajoutez
votre fichier dans le dossier du module qui change. Un travail est terminé quand :

- il compile sans avertissement (`-Wall -Wextra -Wpedantic -Werror`) ;
- des tests unitaires sont ajoutés et verts, sans signalement de sanitizer ;
- les couches de validation graphique (Vulkan, `GL_KHR_debug`) ne signalent rien sur la démo ;
- le module a son `README.md` et `docs/STATUS.md` est à jour.

## Règles de code qui comptent

- Aucun appel d'API graphique (`gl*`, `Vk*`, `MTL*`) hors de `src/rhi/<backend>/` ; `make check` le vérifie.
- Un module ne dépend que des modules d'en dessous (couches de `docs/03`).
- Erreurs attendues par `std::expected` et le logger, pas d'exception dans le flux normal.
- Une fonction optionnelle d'un GPU se lit dans `Capabilities`, jamais supposée.
- Code neuf : en-têtes dans `include/orosmo/<module>/`, fichiers en `snake_case`, types en `PascalCase`, fonctions en
  `camelCase`, constantes en `kPascalCase`.

Un désaccord, une ambiguïté ou un choix d'architecture non prévu s'écrit dans
[`docs/QUESTIONS.md`](https://github.com/TimotheSandt/Orosmo/blob/dev/docs/QUESTIONS.md) ; un manque connu va dans `docs/GAPS.md`.
