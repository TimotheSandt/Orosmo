# M3 bis : release cœur générique et confort de build (entre M3 et M4)

Jalon intermédiaire décidé par l'auteur le 2026-11-21 (`docs/05-PLAN_PAR_JALONS.md`, « M3 bis »). Il part de `dev` après les
revues de la PR #8 (M3), fusionnée dans `main` le 2026-11-21, et a été fusionné dans `dev` ensuite. Version proposée : `v0.3.1` (A35, à
trancher ; les fichiers de version restent à `0.3.0`).

## Ce qui change

| Sujet | Changement | Branche |
| --- | --- | --- |
| Release générique (A34) | `make dist` ne copie que le moteur générique : en-têtes `core`, `platform`, `rhi`, `renderer` (sans `terrain_renderer.hpp`), `ui` ; archives `libui`, `libcore`, `libplatform`, `librhi`, `librenderer`, `libthirdparty` ; shaders sans `terrain.*` ; exemples `minimal` et `ui_gallery` | `chore/release-core-only` (Gemini) |
| Modules du dépôt | `TerrainRenderer` sort de `librenderer.a` vers `libterrain_renderer.a` ; `world` et `terrain_renderer` sont liés par `ENGINE_EXTRA_MODULES` (démo, `terrain-gen`, `terrain-viewer`) ; `sync-demo` et `install-demo` les superposent dans `apps/demo/orosmo/` | `chore/release-core-only` |
| `orosmo init` | Nouveau modèle générique `tools/engine-sdk/template/src/` (fenêtre, RHI, cube éclairé, HUD `.cui`, `--headless`, `--capture`) au lieu des sources de la démo terrain | `chore/release-core-only` |
| Exemple `minimal` | Sans fenêtre ni GPU, seulement `orosmo::core` (tâches, réflexion, JSON ; somme vérifiée 524800) | `chore/release-core-only` |
| Plan | Jalon **M11 · Système de modules et d'addons** (`MOD-01` à `MOD-04`, modes `bundled` et `addon`) ; `docs/02` section M, `docs/03` §15 | `chore/release-core-only` |
| Affichage de `make` | Dans un terminal : barre de progression (compilés, en cours, restants, à jour), avertissements et erreurs au-dessus, résumé final ; `V=1` montre les commandes, `ORS_NO_PROGRESS=1` garde l'ancien affichage ; hors terminal (CI) inchangé | `feat/build-progress` |
| Projets | La même barre dans les projets créés par `orosmo init` : le script est livré dans `orosmo/tools/` du paquet et le `Makefile` du modèle passe par lui dans un terminal ; `make e2e-init` vérifie sa présence | `feat/build-progress-projects` |
| `make clean` / `fclean` | Nettoient debug, dev et release (seulement release avant : le `BUILD_TYPE` du `Makefile` racine n'était pas transmis) ; `BUILD_TYPE=x` pour un seul type | `fix/fclean-all-build-types` |
| `sync-demo` | Les copies gardent leurs dates (`cp -p`) : un `make all-debug` sans changement ne recompile ni ne relie plus la démo | `feat/build-progress` |
| CI | L'étape « Plan » saute aussi la CI d'une PR dont la fusion testée a le même contenu que la tête (le commit de fusion de la PR précédente sur `main` faisait relancer toute la CI en double) | `docs/M3bis-release` |
| CI : ccache | Chaque job garde ses résultats de compilation d'un run à l'autre (`ccache`, cache GitHub par job et par plateforme, 500 Mo) : un fichier inchangé n'est plus recompilé. Les étapes de tests compilent aussi en parallèle (`-j`), elles compilaient en série (release du moteur et tests) | `chore/ci-ccache` |

Fusion : `chore/M3-M4-bridge` (conflits résolus : la décision du périmètre de la release devient A34, A33 étant déjà la question
`generateRegion` ; l'archive `libterrain_renderer.a` passe aussi par la barre de progression).

## Changements incompatibles (par rapport à M3)

- Le paquet `orosmo-pkg-<plateforme>.zip` ne contient plus `orosmo/world/`, `renderer/terrain_renderer.hpp`, `libworld.a`,
  `libterrain_renderer.a`, `shaders/rhi/terrain.*`, ni les exemples `terrain_gen` et `terrain_viewer`. Aucun projet extérieur ne
  les utilisait.
- `orosmo init` crée un projet générique (plus la démo terrain).
- Dans le dépôt : un programme qui utilise `orosmo::world` ou `TerrainRenderer` ajoute `ENGINE_EXTRA_MODULES := world` (et
  `terrain_renderer`) avant d'inclure `make/engine.mk`.

## Identifiants

`MOD-01` à `MOD-04` ajoutés, **Absent**. Totaux : Fait 31, Partiel 43, Absent 81, Incompatible 1 (`docs/STATUS.md`).

## Preuves

Clone propre de `docs/M3bis-release` (contenu de `chore/M3-M4-bridge` + cette documentation), Linux x64, g++ 13.3, Mesa 26.1.6 :

| Commande | Résultat |
| --- | --- |
| `make all-debug` | OK, sans avertissement (`-Werror`) |
| `make demo-test` | 421 / 421 |
| `make demo-test SANITIZE=address,undefined` (arbre propre) | 421 / 421, aucun signalement |
| `make check` | OK (dépôt, couches, isolation des API, en-têtes autonomes) |
| `make cui-test` | OK |
| `make terrain-gen-check` | OK (somme `0xb6ca1edcbc7812da`, 1 et 4 threads identiques) |
| `make e2e-init` | OK : paquet sans `world` ni `terrain` (10 vérifications d'absence), `orosmo init`, projet générique compilé et lancé (`scene.png`, `ui.png`), exemples `minimal` (somme 524800) et `ui_gallery` |
| `unzip -l dist/orosmo-pkg-linux-x64.zip \| grep -ciE "world\|terrain"` | 0 |
| `make viewer-capture`, `make gallery-capture` | OK |
| `python3 -m unittest discover -s tests/tools` | 11 tests OK (dont 5 pour la barre de progression) |
| Capture de la démo contre `scene-opengl-seed1-1280x720.png` (llvmpipe) | 0 pixel de différence |

## Ce qui n'est pas prouvé

- Windows et macOS : la branche PowerShell de `make dist` / `install-demo` n'a jamais été exécutée (G126) ; la barre de progression
  n'est vérifiée que sous Linux (G127).
- `orosmo update` sur un projet créé par M3 (avec la démo terrain) : non essayé.
- ccache en CI : jamais exécuté avant la poussée (pas de ccache sur la machine de développement) ; le premier run remplit le cache, le
  gain se mesure au deuxième (`ccache --show-stats` en fin de job). Sous Windows, les lanceurs `lib/ccache/bin` de MSYS2 ne sont
  utilisés que s'ils existent (sinon compilation directe, journalisée).

## Commandes de démo

```bash
make all-debug                 # dans un terminal : barre de progression ; une 2e fois : rien à refaire
make e2e-init                  # paquet générique (sans world), orosmo init, compilation, lancement, exemples
unzip -l dist/orosmo-pkg-linux-x64.zip | grep -ciE "world|terrain"   # 0
```
