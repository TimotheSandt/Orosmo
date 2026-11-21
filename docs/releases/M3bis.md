# M3 bis : release cœur générique et confort de build (entre M3 et M4)

Jalon intermédiaire décidé par l'auteur le 2026-11-21 (`docs/05-PLAN_PAR_JALONS.md`, « M3 bis »). Il part de `dev` après les
revues de la PR #8 (M3), fusionnée dans `main` le 2026-11-21, et a été fusionné dans `dev` ensuite. Version : `v0.3.1`, **première release publiée** (M3 n'a pas de release ; A35, décidée
par l'auteur le 2026-11-21 ; `mk/config.mk`, `vcpkg.json`, `Cargo.toml`, extension VS Code).

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
| Release | Un tag `v*` posé sur un commit que `main` a déjà construit avec succès **réutilise** ses paquets (5 plateformes, gardés 7 jours) au lieu de tout recompiler : la release publie les fichiers prouvés sur `main` ; sinon (autre commit, build de `main` en échec ou paquets expirés), le tag recompile comme avant. Un build de `main` encore en cours est attendu (45 min au plus) | `ci/release-reuse-main-build` |
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
- Réutilisation du build de `main` par le tag : vérifiable seulement au premier tag (`v0.3.1`) ; en cas d'échec de la recherche,
  le tag recompile (pas de release perdue).
- ccache en CI : jamais exécuté avant la poussée (pas de ccache sur la machine de développement) ; le premier run remplit le cache, le
  gain se mesure au deuxième (`ccache --show-stats` en fin de job). Sous Windows, les lanceurs `lib/ccache/bin` de MSYS2 ne sont
  utilisés que s'ils existent (sinon compilation directe, journalisée).

## Commandes de démo

```bash
make all-debug                 # dans un terminal : barre de progression ; une 2e fois : rien à refaire
make e2e-init                  # paquet générique (sans world), orosmo init, compilation, lancement, exemples
unzip -l dist/orosmo-pkg-linux-x64.zip | grep -ciE "world|terrain"   # 0
```

## Revue de la PR #9 (CodeRabbit, 1re passe, 2026-11-21)

CI du push `b555c2d` : verte sur les cinq plateformes ; la CI de la PR a été **sautée** (même contenu que le push : correction de
« Plan » vérifiée en vrai). 13 retours, tous retenus :

- Build : lanceurs ccache remis en tête du PATH dans les étapes MSYS2 (le profil MINGW64 passait devant) ; `librenderer.a` refaite
  quand sa composition change (`mk/engine.mk` en prérequis, objets seuls passés à `ar`) ; `make -C apps/demo` lie `world` et
  `terrain_renderer` quand le SDK synchronisé les contient ; `make install` d'un projet sans `vcpkg.json` ; `build.rs` suit le
  dossier du modèle (fichier ajouté).
- Barre de progression : identité absolue des cibles (deux `obj/main.o` d'applications différentes), lignes toujours dans la
  largeur du terminal (même étroit), `make check check` garde sa recette.
- Modèle `orosmo init` : `HudState` détruit après l'interface, pointeur ramené à l'espace de l'interface pendant une capture avec
  fenêtre, erreurs de capture avec chemin et cause, message sans affichage (`xvfb-run`).
- Documentation : `orosmo update` écrit par-dessus `orosmo/` ; procédure de migration d'un projet M3 (vérifiée : SDK vidé puis
  mis à jour, `world` retiré, projet recompilé) ; macOS « non vérifié » pour `minimal`.

Aussi : `make clean` / `fclean` nettoient debug, dev et release (seulement release avant) ; barre de progression dans les projets
d'`orosmo init`.

Preuves (Linux x64) : `make demo-test` 421 / 421 ; `make check`, `cui-test`, `terrain-gen-check`, `e2e-init`, `viewer-capture`,
`gallery-capture` verts ; tests Python 12 / 12 ; `make check check` exécute les contrôles ; `make -C apps/demo debug` lié.

## Release macOS Intel

Le build de release sur `main` après M3 (`build-engine.yml`, run 44859222407) a échoué sur **macOS x64** seulement : le
`from_chars` des flottants de la libc++ d'Apple est supprimé pour une cible macOS ancienne (`src/ui/theme.cpp`). Nouveau
`core::parseFloat` / `parseDouble` (`include/orosmo/core/parse.hpp`) : `std::from_chars` ailleurs, `strtod_l` en locale « C » sur
macOS, mêmes refus ; `tests/core/parse_tests.cpp` (3 cas) passe sur les deux chemins (le chemin macOS forcé sous Linux par
`-DOROSMO_PARSE_STRTOD`). Utilisé par les thèmes et `NumberField`.

