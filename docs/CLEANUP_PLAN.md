# CLEANUP_PLAN : plan de nettoyage M00

Statut : **exécuté** le 2026-11-17 sur `chore/cleanup-m00` avec ton accord (option A, apt d'abord / vcpkg en repli, licence plus tard). Le texte ci-dessous est le plan d'origine ; les écarts constatés à l'exécution sont au §9 et le bilan dans `docs/releases/M00.md`.

## 0. Sauvegarde (faite)

| Élément | Valeur |
| --- | --- |
| Tag | `pre-cleanup` → `899c349` (tête de `dev`) |
| Branche | `backup/2026-11-16-pre-cleanup` → `899c349` |
| Branche | `backup/2026-11-16-pre-cleanup-worktree` → `4fedb58` : instantané des fichiers **suivis** modifiés dans l'arbre de travail (`git stash create`, aucun stash ni fichier touché) |
| Bundle | `/mnt/data/Documents/Projet/codage/c++/projet/ProceduralGeneration-backups/pre-cleanup.bundle` (9,6 Mo, `git bundle verify` : OK, `--all`, contient le tag et les deux branches) |
| Archive | `…/ProceduralGeneration-backups/pre-cleanup-worktree.tar.gz` (975 Mo) : arbre de travail complet **sans** `.git`, `build/`, `dist/`, `lib/`, `obj/`, `vcpkg_installed/`. Contient les fichiers non suivis (module Benchmark) et les CRLF d'origine. Elle est grosse parce qu'elle inclut `demo/` (`demo/bin`, `demo/engine`, `tools/target`…) ; supprimable une fois M00 validé. |

Limite : la référence de comportement (`docs/BASELINE/`) est **incomplète**, le build Linux étant impossible aujourd'hui (GLFW/vcpkg absents). Détail et preuve dans `docs/BASELINE/README.md`.

## 1. Constats

### 1.1 État du dépôt
1. Branche courante `dev`, **197 fichiers modifiés** à l'état initial, mais :
   - **186 sont des différences de fins de ligne seules** : l'index est en LF, l'arbre de travail en CRLF (`git ls-files --eol` : 186 `i/lf w/crlf`, 10 `mixed`). `git diff --ignore-cr-at-eol` ne montre que 3 fichiers réels.
   - Modifications réelles non commitées : `Libraries/Makefile` (+ cible `libbenchmark.a`), `demo/Makefile` (+ cible `benchmark`), déplacement `Docs/UI_DSL_Spec.md` → `docs/UI_DSL_Spec.md` (le contenu diffère de la version suivie).
   - Non suivis : module **Benchmark** (`Libraries/includes/Benchmark/`, `Libraries/src/Benchmark/`, `demo/benchmark/` : 670 lignes), et `implementation_plan_benchmark.md` indexé (`A`). Les Makefiles modifiés en dépendent : un clone propre de `HEAD` n'a pas ce module (les `find` renvoient vide, donc ça compile quand même).
   - Non suivis, mes ajouts : `CLAUDE.md`, `docs/01…06`, `docs/README.md`.
2. Le dernier commit s'intitule « revert change benchmark and chunk optimisation » : le travail Benchmark non commité est probablement la reprise de ce sujet. À confirmer (QUESTIONS Q2).
3. **Branches** : locales `dev`, `main`, `UI`, `feat/engine-init-tool`, `feat/game-engine-preparation`, `feat/rust-cui-precompiler`, `feat/vulkan` (checkout dans un worktree : marquée `+`), `feature/graphics-backend-architecture`, `feature/ui-dsl-spec`, `fix/makefile-lint-check-format`. Distantes : `origin/{main,dev,UI,feat/*,feature/*}`.
4. **`main` local a 44 commits de plus que `origin/main`** et **`dev` local 19 de plus que `origin/dev`** (`git rev-list --left-right --count`). Rien n'est poussé. `main` est un ancêtre de `dev`.
5. **Tags** : aucun avant `pre-cleanup`. **Stashes** : 17 (`git stash list`), dont `wip/pre-backend-merge-local-transforms`, `add LOD`, etc. Non touchés.
6. `dev` existe déjà (le doc 06 prévoyait de la créer) : on la **garde** comme intégration ; la branche de nettoyage part d'elle.
7. `core.ignorecase=true` dans la config locale (héritage Windows) alors que le disque (NTFS via `fuseblk`) est sensible à la casse côté Linux. C'est ce qui produit le faux « `Docs/` supprimé / `docs/` non suivi ». Correction proposée : `git config core.ignorecase false` (local, non versionné) puis un commit de déplacement `Docs/UI_DSL_Spec.md` → `docs/UI_DSL_Spec.md` (voir §3).

### 1.2 Taille et historique
8. `.git` : 47 Mo sur disque, 178 commits (pack 11 Mo). Petit : aucune raison de réécrire.
9. Plus gros objets **dans l'historique uniquement** (absents de `HEAD`) :
   | Taille | Chemin |
   | --- | --- |
   | 2,6 Mo ×2, 1,3 Mo | `installers/windows/*-setup.exe` (3 versions) |
   | 1,1 Mo + 0,7 Mo | `tools/cui-rs/target/release/cui.pdb`, `cui.exe` |
   | 0,4 Mo + 0,2 Mo | `Libraries/libs/ThirdParty/glad.dll`, `glfw3.dll`, `*.lib` |
   Plus un fichier de `target/` Rust (`CACHEDIR.TAG`, `.rustc_info.json`). Total d'environ 10 Mo ; tout est dans le pack de 11 Mo. Verdict : pas de `filter-repo`.
10. Dans `HEAD`, seul gros fichier suivi : `demo/res/fonts/Roboto-Regular.ttf` (2,3 Mo, police ; garder, ajouter sa licence). 
11. Deux commits massifs : `b1fc1cf init` (463 fichiers, +77 167 lignes) et `500ef94 using vcpkg` (434 fichiers, −75 287 lignes : retrait des dépendances copiées au profit de vcpkg). Rapport seulement.
12. **Messages de commit** : 86 sur 178 respectent `type(: …)`, 92 non (« UI (#2) », « fixing merge », « improving jitter », « WIP: … »). Auteur unique, `TimotheSandt`. La convention `type(ID): résumé` s'applique à partir de M00.

### 1.3 Fichiers suivis (`git ls-files` : 210)
13. **`HEAD` est déjà propre** : aucun binaire, aucune sortie de compilation, aucun `.vs/`, aucun dossier vcpkg, aucune copie `.bak`, aucun `*.user` n'est suivi. Répartition : 82 `.h`, 73 `.cpp`, 11 `.rs`, 6 `.md`, 5 `.json`, 4 `.vert`, 4 `.frag`, 3 `.toml`, 3 `.mk`, etc.
14. **Beaucoup de binaires ignorés présents sur le disque** (non suivis, mais encombrants et trompeurs) : `vcpkg_installed/` 1,3 Go (triplets Windows), `demo/` 2,6 Go (`demo/bin/*/main.exe`, `vulkan-1.dll`, `demo/engine/`, `demo/vcpkg_installed`), `tools/target` 799 Mo, `build/` 876 Mo, `dist/` 124 Mo (`.zip`, `.exe` Windows), `lib/` 40 Mo, `obj/` 42 Mo, `tools/cui.exe`. Tous couverts par `.gitignore` (vérifié avec `git check-ignore`). Aucune action git ; je ne les supprime pas (ils sont de l'auteur et servent peut-être sous Windows).
15. `.gitignore` : motifs trop larges ou fragiles : `.*/` (ignore tout dossier caché, y compris un futur `.github/`, d'où `!.github/`), `lib/` et `bin/` ignorent **n'importe quel dossier de ce nom** (gênant pour `third_party/*/lib`), `logs/`. À réécrire (§4).

### 1.4 Doublons, code mort, non référencés
16. **Pas de doublon de nom** ni de collision de casse, ni dans `HEAD` ni dans tous les chemins de l'historique (vérifié : liste vide).
17. En-têtes jamais inclus nulle part : `Libraries/includes/UI/UI.h`, `Libraries/includes/Graphics/Presentation/PresentationController.h`. À conserver (règle 3) ; à classer à l'audit (QUESTIONS Q6).
18. Incohérence de nommage : en-têtes `Libraries/includes/Graphics/Backend/GraphicsAPI.h` (singulier) mais sources `Libraries/src/Graphics/Backends/GraphicsAPI.cpp` (pluriel) ; `Libraries/src/Utilities/Utilities.cpp` / `Libraries/includes/Utilities/utilities.h`.
19. Dossiers vides hors dossiers générés : aucun.
20. Aucun code ne référence `UI/Views/PerformanceView.gen.h` en dur à part `Game.cpp` : le fichier est **généré** par le précompilateur `cui` depuis `demo/src/UI/Views/PerformanceView.cui` (`Generated/` est ignoré). OK.

### 1.5 Portabilité
21. **Casse** : sur 112 `#include "…"` distincts, **1 seule incohérence** : `Libraries/includes/UI/Core/Container.h` inclut `"Utilities.h"` alors que le fichier est `utilities.h`. Compile sous Windows, **échoue sous Linux**. Correction : changer l'`#include` (contenu), dans un commit séparé. 4 autres non résolus sont des dépendances externes (`glm/...`) ou du code généré.
22. **Fins de ligne** : pas de `.gitattributes` ni `.editorconfig`. Index LF, arbre CRLF (cf. 1) ; 3 fichiers avec espaces en fin de ligne.
23. **Windows en dur** : 8 blocs `#ifdef _WIN32` + `<windows.h>` dans `Window.cpp`, `FPSCounter.{h,cpp}`, `InputManager.cpp`, `OpenGLGraphicsBackend.cpp` (propres, gardés) ; dans les Makefiles, `cui.exe`, `engine.exe` sous une condition de plateforme. Pas de lettre de lecteur ni de chemin absolu dans le code suivi. Chemins ≤ 69 caractères, aucun espace ni caractère non ASCII.
24. **Portabilité du build** : `tools/format_sources.ps1` (PowerShell uniquement), CI dans `.github/workflows/build-engine.yml` ne se déclenche que sur tag `v*` ou manuellement et **ne lance aucun test**. `clang`/`clang-format` absents de cette machine. g++ 13.3 est suffisant pour C++23.
25. **Appels API graphiques hors backends** : `git grep` de `gl*(`/`Vk*`/`vk*(` hors `Backends/` : **aucun fichier**. La règle 4 tient déjà (à confirmer par l'audit sur des appels d'une autre forme, p. ex. via `glad`).

### 1.6 Bibliothèques tierces
26. **Rien de tiers n'est copié dans le dépôt** : tout vient de vcpkg (`glad`, `glfw3`, `glm`, `stb`, `freetype`, `vulkan`, `shaderc`, `msdfgen`, versions non épinglées : pas de `builtin-baseline` dans `Libraries/vcpkg.json`). Seul `Libraries/src/ThirdParty/StbImage.cpp` est un fichier à soi (il instancie `stb_image`). Donc **pas de `third_party/` à créer** pour l'instant ; je le crée vide-documenté seulement si tu veux y mettre la police (Roboto) avec sa licence (Apache-2.0, à vérifier). Recommandation : épingler la baseline vcpkg (`docs/QUESTIONS.md`, à traiter en M0, pas en M00).
27. `Roboto-Regular.ttf` (suivie) : licence non documentée ; à noter dans `res/fonts/README.md`.

### 1.7 Secrets et données personnelles
28. **Aucun secret** (clé, jeton, mot de passe) : seuls des faux positifs (mots « token » du lexer Rust, un test « invalid api token »). Aucun chemin personnel absolu dans le code suivi.
29. Donnée personnelle : une adresse e-mail de maintenance dans `Libraries/config.mk` (ligne 8) et `demo/config.mk`, publique via les métadonnées de package (installeur). Je la signale sans la recopier ; à toi de dire si tu veux la remplacer par une adresse générique.

### 1.8 Build et tests existants
30. Build : `Makefile` racine → `Libraries/Makefile` (bibliothèques statiques `lib/<type>/*.a` : graphics, ui, noise, profiler, utilities, thirdparty, + benchmark en cours) → `demo/Makefile`. Outils Rust (`tools/cui-rs` précompilateur du langage, `tools/engine-init-rs` générateur de projet qui **embarque `demo/` comme modèle** et lit `Libraries/config.mk`). `engine-sdk/engine.mk` référence `ENGINE_DIR/Libraries/includes`.
31. Tests : `demo/tests/` (framework maison + 11 suites : binding, graphics api/core/foundation, noise, procedural, profiler, renderers, shader, ui, utilities). Pas de sanitizers, pas de CI de tests. **Non exécutables aujourd'hui** (dépendances manquantes).

## 2. Ce que le nettoyage fait, en une phrase
Ranger sans refondre : mêmes modules, mêmes noms de fichiers, mêmes `#include` (sauf 1 correction), nouveaux dossiers racine conformes à `docs/06`, fichiers de configuration du dépôt ajoutés.

## 3. Structure cible et déplacements

Deux options. **Je recommande A**, mais seulement une fois le build Linux fonctionnel (Q1), faute de quoi je ne peux pas vérifier « chaque commit compile ».

### Option A (recommandée) : structure de `docs/06`
Table complète, fichier par fichier (**182 déplacements**, 28 fichiers restent en place) : [`docs/CLEANUP_MOVES.tsv`](CLEANUP_MOVES.tsv) (colonnes : ancien chemin, nouveau chemin). Règles :

| Ancien | Nouveau | Fichiers |
| --- | --- | --- |
| `Libraries/includes/<Module>/…` | `include/<Module>/…` | 76 |
| `Libraries/src/<Module>/…` | `src/<Module>/…` | 55 (dont `ThirdParty/StbImage.cpp`, qui reste un fichier à soi, donc `src/ThirdParty/`) |
| `Libraries/Makefile`, `config.mk`, `vcpkg.json` | `mk/engine.mk`, `mk/config.mk`, `vcpkg.json` (racine) | 3 |
| `demo/src/**`, `demo/Makefile`, `demo/config.mk`, `demo/vcpkg.json`, `demo/.gitignore` | `apps/demo/…` | 14 |
| `demo/tests/**` | `tests/**` | 15 |
| `demo/res/shader/**` | `shaders/**` | 8 |
| `demo/res/fonts/**`, `demo/res/public/icon.ico` | `res/fonts/`, `res/public/` | 2 |
| `demo/installers/**` | `tools/installers/**` | 3 |
| `engine-sdk/**` | `tools/engine-sdk/**` | 3 |
| `Docs/UI_DSL_Spec.md` | `docs/UI_DSL_Spec.md` | 1 (déjà fait physiquement ; commit de `git mv`) |
| `README-backend.md` | `docs/GRAPHICS_BACKENDS.md` | 1 |
| `implementation_plan_benchmark.md` | `docs/notes/implementation_plan_benchmark.md` | 1 |
| Module Benchmark non suivi (selon Q2) | `include/Benchmark/`, `src/Benchmark/`, `apps/benchmark/` | 9 |

Restent en place : `.clang-format`, `.clang-tidy`, `.gitignore`, `.github/workflows/`, `Makefile`, `README.md`, `tools/{cui-rs,engine-init-rs,vscode-cui,Cargo.*,format_sources.ps1}`.

**Pourquoi c'est risqué, et comment c'est traité** :
- `tools/engine-init-rs/build.rs` copie `demo/` comme modèle de projet et lit `Libraries/config.mk` ; `engine-sdk/engine.mk` lit `ENGINE_DIR/Libraries/includes` ; la CI hache `Libraries/vcpkg.json` et lance `Libraries/Makefile`. Chaque déplacement s'accompagne, **dans le commit suivant**, de la mise à jour de ces chemins (Makefiles, `build.rs`, CI). L'équivalent « modèle de projet » deviendra `apps/demo/`.
- Les chemins de ressources à l'exécution (`GET_RESOURCE_PATH("shader/…")`, `("fonts/…")`) restent identiques : la règle de copie de `bin/<type>/res/` du Makefile est adaptée pour rassembler `res/` et `shaders/` (→ `res/shader/`). Aucune ligne de C++ ne change.
- Ordre des séries (un `git mv` par série, build entre chaque) : (1) `docs/` ; (2) `Libraries/includes` → `include/` ; (3) `Libraries/src` → `src/` ; (4) `demo/res` → `res/`+`shaders/` ; (5) `demo/tests` → `tests/` ; (6) `demo/src` + fichiers de build → `apps/demo/` ; (7) `Libraries/{Makefile,config.mk,vcpkg.json}` → `mk/`, racine ; (8) `engine-sdk`, `installers` → `tools/`.
- Le langage d'interface : `PerformanceView.cui` reste dans `apps/demo/src/UI/Views/` (le code généré `UI/Views/PerformanceView.gen.h` dépend de ce chemin). Son déplacement vers `res/ui/` est repoussé à M3, avec le compilateur d'interface.

### Option B (conservatrice)
Garder `Libraries/` et `demo/` ; ajouter seulement les fichiers de §5 et les corrections de contenu de §4. Aucun `git mv` sauf `Docs/` → `docs/` et les 2 `.md` de la racine. Risque très faible, mais l'arborescence ne suit pas `docs/03` ; il faudrait la refaire au jalon M2.

## 4. Ce qui est supprimé et pourquoi
**Rien dans l'historique ni dans `HEAD`.** Suppressions dans l'index :
- Aucune pour l'instant (aucun binaire ni fichier généré n'est suivi). Si la vérification en clone propre en révèle, je les ajoute à ce plan avant d'agir.
- Du disque, hors dépôt : **rien**. Les dossiers `build/ dist/ lib/ obj/ vcpkg_installed/ tools/target demo/{bin,obj,engine,vcpkg_installed}` sont ignorés ; je te laisse décider de les effacer (≈ 5,9 Go). L'archive de sauvegarde (975 Mo) se supprime après validation.

Corrections de contenu (commits séparés des déplacements) :
1. `Container.h` : `#include "Utilities.h"` → `"utilities.h"` (casse, §1.5.21).
2. `.gitignore` réécrit (motifs ancrés `/lib/`, `/bin/`, `/build/` ; fin du `.*/`).
3. Mise à jour des chemins de build après chaque série de déplacements.
4. `README.md` : le lien `Docs/UI_DSL_Spec.md` devient `docs/UI_DSL_Spec.md` ; commandes Linux ajoutées.

## 5. Ce qui est ajouté
| Fichier | Contenu |
| --- | --- |
| `.gitattributes` | `* text=auto eol=lf` ; `*.ps1 *.bat *.cmd *.nsi text eol=crlf` ; binaires (`*.ttf *.ico *.png *.exe *.dll *.lib`) ; pas de LFS (rien d'assez gros). L'index est déjà en LF : le « renormalize » ne produira **pas** de commit énorme ; l'arbre de travail CRLF sera régénéré. |
| `.editorconfig` | UTF-8, LF, 4 espaces, adapté à `.clang-format` existant (BasedOnStyle Microsoft, Allman, 140 colonnes). |
| `.clang-format` | **conservé tel quel** (déjà le style du code). |
| `README.md` (racine) | Réécrit : rôle, prérequis Linux/Windows/macOS, commande exacte vcpkg (à valider, Q1), compilation, tests, démo, liens `docs/`. |
| `LICENSE` | Après ta décision (Q5). |
| `tools/format_sources.sh` | Équivalent POSIX de `format_sources.ps1`. |
| `res/fonts/README.md` | Origine et licence de Roboto. |
| `apps/…`, `src/*/README.md` | Un README court par module existant (Graphics, UI, Noise, Profiler, Utilities, ThirdParty) : rôle, API publique, dépendances (règle 8). |
| `docs/BASELINE/` | Déjà créé (incomplet, Q1). |
| `docs/releases/M00.md` | En fin d'exécution. |
| `third_party/` | **Pas créé** : rien de tiers n'est copié (§1.6). |

## 6. Historique : options et recommandation
1. **Garder tel quel (recommandé).** 11 Mo, pas de secret, ~10 Mo de vieux binaires inoffensifs. Aucun gain à réécrire.
2. `git filter-repo` pour retirer `installers/windows/*.exe`, `tools/cui-rs/target/`, `Libraries/libs/` : économiserait < 10 Mo, imposerait de refaire tous les clones. **Non recommandé.**
3. Historique neuf : **non**.

Pousser (ou non) `main`/`dev` locales reste ta décision (Q3).

## 7. Risques et vérification

| Risque | Parade |
| --- | --- |
| Je ne peux pas compiler ici (§0) | Q1 : installer vcpkg ; sinon uniquement vérifications statiques (`make -n`, résolution des `#include`, comparaison des listes de sources) et je le dirai dans `docs/releases/M00.md` |
| Travail non commité de l'auteur (Benchmark, Makefiles) perdu ou mélangé | Sauvegardé (§0) ; Q2 avant toute branche |
| Casse/CRLF : conflits à la fusion avec les anciennes branches (`feat/vulkan`, etc.) | `git mv` seul (similarité > 90 % reconnue) ; `.gitattributes` appliqué **avant** les déplacements, dans un commit séparé |
| Le modèle `engine init` embarque `demo/` | Mise à jour de `build.rs`, test `engine init` dans un dossier vide |
| Windows non vérifiable | Noté dans `docs/GAPS.md` ; CI Windows existante sert de contrôle |
| Casse du dépôt à cause de `core.ignorecase=true` | Corrigé en local avant le premier `git mv` |

Vérification de fin de M00 : (a) à chaque série, `make -n` + diff de la liste des fichiers sources compilés avant/après (doit être identique à la renommée près) ; (b) build complet et tests dès que Q1 est réglé ; (c) clone propre dans un dossier vide ; (d) `git ls-files` sans binaire (script de contrôle) ; (e) comparaison avec `docs/BASELINE/`.

## 8. Décisions attendues de toi
1. Q1 : installer vcpkg (oui/non).
2. Q2 : sort des changements non commités.
3. Q4 : option A (après Q1) ou B.
4. Q5 : licence.
5. Q6 : garder les 2 en-têtes sans référence.
6. Accord général pour exécuter, sur `chore/cleanup-*` depuis `dev`, **sans commit sur `dev`** avant la fusion `--no-ff`.

## 9. Écarts entre le plan et l'exécution

1. **Installeurs** : `apps/demo/installers/` reste en place (pas `tools/installers/`) : le Makefile de la démo les référence en relatif. Chemins de l'installeur NSIS adaptés (non testé).
2. **Benchmark** : le module non commité est devenu un commit d'instantané (`chore(M00): snapshot unfinished benchmark work`), puis déplacé comme le reste : `include/Benchmark/`, `src/Benchmark/`, `apps/demo/benchmark/` (et non `apps/benchmark/`).
3. **Chaque déplacement est suivi d'un commit de chemins** : le commit de `git mv` seul ne compile pas (le Makefile pointe encore l'ancien chemin) ; la compilation et les tests sont vérifiés après chaque **paire** de commits (déplacement + chemins), sur une extraction propre (`git archive`) de la tête de branche.
4. **Ressources** : `demo/res/{fonts,public}` → `res/`, `demo/res/shader` → `shaders/`, recopiés sous `bin/<type>/res/` (identique à la référence, MD5 vérifiés). L'ancien comportement de chemins `GET_RESOURCE_PATH("shader/...")` est conservé.
5. **Correctifs de portabilité Linux** ajoutés (non prévus au plan, indispensables pour compiler) : `-lmsdfgen-core`, bibliothèque de `<stacktrace>` selon la version de GCC, casse de `utilities.h`, script `tools/setup-linux-deps.sh`.
6. **`dev` existait déjà** : le nettoyage y est fusionné avec `--no-ff`.
7. **Non fait** : `LICENSE` (décision reportée), `third_party/` (rien de tiers copié), correction de `core.ignorecase` dans le dépôt (réglage local fait), nettoyage des gros dossiers ignorés sur le disque, tri des stashes et anciennes branches.
