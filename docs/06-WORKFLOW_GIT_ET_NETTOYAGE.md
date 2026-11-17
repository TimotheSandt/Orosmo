# 06 · Flux Git et nettoyage du dépôt

Ce document dit **comment l'IA travaille avec Git** et **ce qu'elle fait en tout premier** : nettoyer le projet et le dépôt, sans perdre le code actuel ni son historique.

## 1. Branches

| Branche | Rôle | Qui y écrit |
| --- | --- | --- |
| `main` | Version validée, une par jalon terminé | **Seul l'auteur** (par merge request) |
| `dev` | Intégration : l'auteur y teste et cherche les bugs | L'IA, par fusion de branches de fonctionnalité |
| `feat/<ID>-<sujet>` | Une fonctionnalité (ex. `feat/RHI-15-mrt`) | L'IA |
| `fix/<sujet>` | Un bug signalé par l'auteur | L'IA |
| `chore/<sujet>` | Nettoyage, build, CI, outils | L'IA |
| `docs/<sujet>` | Documentation seule | L'IA |
| `backup/<date>-<sujet>` | Sauvegarde avant une opération risquée | L'IA, jamais modifiée ensuite |

Règles :
1. **Jamais de commit direct sur `main` ni sur `dev`.** Tout passe par une branche créée depuis `dev`.
2. **Une branche = une fonctionnalité** (un ou quelques identifiants de la checklist qui vont ensemble). Courte durée : on fusionne dès que c'est terminé et testé.
3. **Jamais de `git push --force`**, jamais de `--no-verify`, jamais de suppression d'une branche non fusionnée, jamais de réécriture de l'historique de `main` ou `dev`.
4. **Fusion dans `dev`** : seulement quand la branche (a) compile, (b) passe tous les tests, (c) respecte la définition de « terminé » de `CLAUDE.md`, (d) a `docs/STATUS.md` à jour. Fusion avec `git merge --no-ff` pour garder la trace de la fonctionnalité. Si `dev` a avancé, l'IA met d'abord sa branche à jour (`git merge dev` dans la branche) et relance les tests.
5. **`main` ne bouge jamais par l'IA.** Quand un jalon est complet, l'IA prépare le terrain (voir §3) et **s'arrête**. L'auteur ouvre la merge request `dev` → `main`.

## 2. Commits

- Petits, un sujet à la fois, qui compilent chacun.
- Format : `type(ID): résumé à l'impératif`, avec les types `feat`, `fix`, `refactor`, `perf`, `test`, `docs`, `build`, `ci`, `chore`. Exemples : `feat(RHI-15): add multi-attachment render passes`, `fix(UI-05): report line and column of source errors`.
- Corps du message : ce qui change, pourquoi, comment c'est vérifié (commande de test).
- Les **déplacements de fichiers** (`git mv`) sont dans des commits **séparés** des modifications de contenu, pour que Git garde l'historique des fichiers.
- Pas de gros fichiers binaires, de sorties de compilation ni de données générées. Les ressources lourdes passent par Git LFS **si l'auteur l'approuve**.
- Aucun secret (clés, jetons, mots de passe) dans le dépôt, même supprimé ensuite. Si l'IA en trouve un, elle le signale tout de suite à l'auteur, sans le recopier.

## 3. Fin de jalon

Quand tous les identifiants du jalon sont `Fait` avec preuve et que `dev` est vert :
1. Mettre à jour `docs/STATUS.md` et écrire `docs/releases/<jalon>.md` : ce qui est livré, comment lancer la démo, ce qui n'est pas fait et pourquoi, bugs connus.
2. Vérifier que `dev` compile et que les tests passent **depuis un clone propre** (`git clone` dans un dossier vide, build, tests, démo).
3. Rédiger la description de la merge request (résumé, liste des identifiants, commande de démo, résultat des tests) dans `docs/releases/<jalon>-MR.md`.
4. **S'arrêter** et le dire. L'auteur teste, ouvre la merge request vers `main`, la fusionne, puis pose le tag `<jalon>` (ex. `M2`). L'IA ne pose pas ce tag.

## 4. Bugs signalés par l'auteur

1. L'auteur décrit le bug sur `dev`.
2. L'IA crée `fix/<sujet>` depuis `dev`, écrit **d'abord un test qui échoue** (ou une capture de référence), corrige, vérifie que le test passe, fusionne dans `dev`.
3. Si le bug révèle un manque de la checklist ou de la spécification, l'IA l'écrit dans `docs/GAPS.md` ou `docs/QUESTIONS.md`.

## 5. Protection recommandée (à régler par l'auteur sur la plateforme d'hébergement)

- `main` : interdit de pousser directement, merge request obligatoire, intégration continue verte obligatoire.
- `dev` : intégration continue verte obligatoire avant fusion ; pas de push forcé.
- Suppression automatique des branches de fonctionnalité après fusion.

L'IA ne configure pas ces protections elle-même ; elle rappelle à l'auteur de les activer.

## 6. M00 · Nettoyage du projet et du dépôt (tout premier travail)

Objectif : repartir du **code actuel** et de son **historique Git**, mais rangés, structurés, reproductibles. Aucun changement de comportement. Aucune réécriture d'historique par défaut.

### Garde-fous

- **Sauvegarde d'abord** : créer le tag `pre-cleanup` et la branche `backup/<date>-pre-cleanup` sur l'état actuel, et un `git bundle` complet (`pre-cleanup.bundle`) dans un dossier hors du dépôt. Dire à l'auteur où il est.
- **Référence de comportement** : avant toute modification, builder l'état actuel, le lancer, et enregistrer captures d'écran et sorties dans `docs/BASELINE/`. Après le nettoyage, le même build doit donner le même résultat.
- **Une proposition, puis feu vert** : l'IA écrit `docs/CLEANUP_PLAN.md` (voir ci-dessous) et **attend l'accord de l'auteur** avant d'exécuter les suppressions, les déplacements et tout ce qui touche à l'historique.
- Tout se passe sur une branche `chore/cleanup-*` créée depuis la branche actuelle ; la fusion se fait dans `dev` (créée à ce moment-là), puis l'auteur décide quand `main` suit.

### Inventaire (lecture seule)

1. `git status`, branches existantes (locales et distantes), tags, taille du dépôt, plus gros objets de l'historique (`git rev-list --objects --all` + tailles).
2. `git ls-files` : fichiers suivis qui ne devraient pas l'être (sorties de compilation, binaires, `.vs/`, dossiers d'installation de vcpkg, caches, fichiers temporaires, copies de sauvegarde, `*.user`).
3. Fichiers en double, code mort, fichiers non référencés par le build, dossiers vides.
4. Noms qui ne diffèrent que par la casse, caractères non portables, chemins trop longs.
5. Fins de ligne mélangées (CRLF/LF), encodages, espaces en fin de ligne.
6. Bibliothèques tierces : ce qui est copié dans `Libraries/` (nom, version, licence), ce qui devrait venir de vcpkg ou d'un sous-module.
7. Secrets possibles (clés, jetons, chemins personnels) : recherche et rapport sans recopier les valeurs.
8. Messages de commit : conventions actuelles, présence de commits énormes ou de messages sans sens (rapport uniquement).

### Contenu de `docs/CLEANUP_PLAN.md`

- Constats (liste chiffrée avec exemples réels).
- Structure cible, **avec le plan de déplacement fichier par fichier** (ancien chemin → nouveau chemin).
- Ce qui est supprimé et pourquoi.
- Ce qui est ajouté.
- Options pour l'historique (voir plus bas) avec une recommandation.
- Risques, et comment le comportement sera vérifié.

### Structure cible (compatible avec `docs/03` §2)

```
/                          README.md, LICENSE (à décider avec l'auteur), CLAUDE.md, Makefile (ou CMakePresets.json)
.gitignore  .gitattributes  .editorconfig  .clang-format  vcpkg.json (si vcpkg)
src/                       code du moteur (rangé par module si c'est déjà le cas ; sinon regroupé, la migration en modules se fait aux jalons suivants)
include/                   en-têtes publics (si l'organisation actuelle le permet sans casser le build)
apps/                      exécutables
shaders/                   GLSL source
res/                       ressources : fonts/, textures/, models/, ui/ (fichiers du langage d'interface), themes/
tests/                     tests
tools/                     scripts de build, de vérification, générateurs
third_party/               code tiers copié, chacun avec son README (version, licence, origine) ; pas de binaires
docs/                      cette spécification, rapports, notes de version
```

Règles de migration :
- Le code existant garde son organisation tant qu'il n'y a pas de bonne raison de la changer ; l'objectif est de **ranger**, pas de refondre l'architecture (c'est le rôle des jalons M2 et suivants).
- Les dossiers `Libraries/src` et `Libraries/includes` sont clarifiés (code tiers vs code à soi) : ce qui est à l'auteur rejoint `src/` ; ce qui est tiers va dans `third_party/<nom>/` ou passe en dépendance gérée, avec la décision justifiée dans `docs/CLEANUP_PLAN.md`.
- Chaque déplacement est fait avec `git mv`, dans un commit séparé, et le build est relancé après chaque série.
- Le build (Makefile, vcpkg) est mis à jour en même temps pour que **chaque commit compile**.
- Noms de fichiers : uniformes, sans espaces, **sans deux noms ne différant que par la casse**.

### Fichiers à ajouter

| Fichier | Contenu |
| --- | --- |
| `.gitignore` | Sorties de build, caches, dossiers d'IDE locaux, `vcpkg_installed/`, journaux, fichiers générés (y compris le C++ produit par le langage d'interface si ce n'est pas versionné) |
| `.gitattributes` | Fins de ligne (`* text=auto eol=lf`, sauf scripts Windows), fichiers binaires, LFS si approuvé |
| `.editorconfig` et `.clang-format` | Style de code partagé, **adapté à la convention déjà présente** dans le code |
| `README.md` (racine) | Ce qu'est le projet, comment compiler (Linux, Windows, macOS), comment lancer, comment tester, liens vers `docs/` |
| `third_party/*/README.md` | Version, licence, origine de chaque bibliothèque copiée |
| `docs/BASELINE/` | Captures et sorties de l'application avant nettoyage |

### Historique : options (à trancher avec l'auteur)

1. **Garder l'historique tel quel (par défaut).** On range avec de nouveaux commits. Rien n'est réécrit. C'est le plus sûr.
2. **Réécrire une partie de l'historique** avec `git filter-repo`, uniquement pour retirer des fichiers énormes ou des secrets. Demande : sauvegarde en miroir, accord explicite de l'auteur, et consigne que tous les clones doivent être refaits. Jamais de push forcé sans ordre explicite.
3. **Repartir d'un historique neuf** (un seul commit initial) en gardant l'ancien dépôt en archive. À n'envisager que si l'auteur le demande clairement.

### Fin de M00

- Le build donne le même résultat que la référence (`docs/BASELINE/`) sur Linux (et Windows si vérifiable).
- `git ls-files` ne contient plus de fichiers générés ni de binaires inutiles.
- `.gitignore`, `.gitattributes`, `.editorconfig`, `.clang-format`, `README.md` racine, `third_party/` documenté.
- Branches `dev` et la branche de nettoyage créées ; la fusion se fait vers `dev`.
- `docs/releases/M00.md` écrit ; l'IA s'arrête et attend l'auteur, qui fusionne vers `main` s'il est satisfait.

L'audit de `docs/04` commence **après** M00, sur le dépôt rangé.
