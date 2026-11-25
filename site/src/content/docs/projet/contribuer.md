---
title: Contribuer
description: Branches, commits, vérifications et définition de « terminé » pour modifier le dépôt d'Orosmo.
sidebar:
  order: 3
---

Les règles complètes sont dans [`CLAUDE.md`](https://github.com/TimotheSandt/Orosmo/blob/dev/CLAUDE.md) et
[`docs/06-WORKFLOW_GIT_ET_NETTOYAGE.md`](https://github.com/TimotheSandt/Orosmo/blob/dev/docs/06-WORKFLOW_GIT_ET_NETTOYAGE.md).
Le dépôt n'a pas encore de fichier de licence (à décider par l'auteur) : demandez avant de proposer des changements.

## Branches

| Branche | Rôle |
|---|---|
| `main` | version validée par jalon ; seul l'auteur y écrit, par merge request |
| `dev` | intégration ; jamais de commit direct |
| `feat/<ID>-<sujet>` | une fonctionnalité de la checklist (`feat/RHI-15-mrt`) |
| `fix/<sujet>` | un bug |
| `chore/<sujet>`, `docs/<sujet>` | nettoyage, construction, documentation |

Toute branche part de `dev` et y revient par `git merge --no-ff`, quand la compilation et les tests passent. Pas de
`git push --force`, de `--no-verify`, ni de réécriture de l'historique de `main` ou `dev`. La merge request `dev` vers `main` se
fait en « Create a merge commit », jamais en squash.

## Commits

Petits, un sujet chacun, qui compilent. Format `type(ID): résumé`, avec `feat`, `fix`, `refactor`, `perf`, `test`, `docs`,
`build`, `ci`, `chore`. Exemple : `fix(UI-05): report line and column of source errors`. Les déplacements de fichiers
(`git mv`) sont dans des commits séparés.

## Avant de proposer un changement

```bash
make all-debug
make demo-test                               # tests doctest
make demo-test SANITIZE=address,undefined    # après make fclean
make check                                   # couches, isolation des API graphiques, en-têtes autonomes
sh tools/format_sources.sh                   # clang-format
```

Un travail est terminé quand :

- il compile sans avertissement (`-Wall -Wextra -Wpedantic -Werror`) ;
- des tests unitaires sont ajoutés et verts, sans signalement de sanitizer ;
- les couches de validation graphique (Vulkan, `GL_KHR_debug`) ne signalent rien sur la démo ;
- le module a son `README.md` et `docs/STATUS.md` est à jour, avec la commande de démo.

## Règles de code qui comptent

- Aucun appel d'API graphique (`gl*`, `Vk*`, `MTL*`) hors de `src/rhi/<backend>/` ; `make check` le vérifie.
- Un module ne dépend que des modules d'en dessous (couches de `docs/03`).
- Erreurs attendues par `std::expected` et le logger, pas d'exception dans le flux normal.
- Une fonction optionnelle d'un GPU se lit dans `Capabilities`, jamais supposée.
- Code neuf : en-têtes dans `include/orosmo/<module>/`, fichiers en `snake_case`, types en `PascalCase`, fonctions en
  `camelCase`, constantes en `kPascalCase`.

## Poser une question

Un désaccord, une ambiguïté ou un choix d'architecture non prévu s'écrit dans
[`docs/QUESTIONS.md`](https://github.com/TimotheSandt/Orosmo/blob/dev/docs/QUESTIONS.md), pas se choisit en silence. Un manque connu
va dans `docs/GAPS.md`.
