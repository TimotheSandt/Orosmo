# Orosmo

**Orosmo** est un moteur de jeu 3D et 2D en **C++23** (rendu OpenGL et Vulkan, Metal prévu), avec une interface décrite dans un langage maison (`.cui`) précompilé en C++. Le premier produit visé est un générateur de terrain et de planètes procédural.

> Première release utilisable : M3 (guide : [docs/USING_THE_ENGINE.md](docs/USING_THE_ENGINE.md)). L'architecture évolue encore ;
> les changements incompatibles sont listés dans [docs/releases/](docs/releases/). Spécification, checklist et plan : [docs/](docs/README.md). Règles de travail : [CLAUDE.md](CLAUDE.md).

## Prérequis

- **Compilateur** : GCC 13+ ou Clang 17+ (testé : GCC 13.3, Linux), MSVC 2022 / MinGW sous Windows.
- **Outils** : GNU Make, CMake, Rust (`cargo`, pour `tools/cui-rs` et `tools/engine-init-rs`), Python 3 (contrôles et comparaison d'images).
- **Pilotes** : OpenGL 4.3+ avec `glClipControl` (OpenGL 4.5 ou `GL_ARB_clip_control`, présent sur les pilotes actuels) ; Vulkan à
  partir de M4, couches de validation conseillées.
- **Dépendances** : glfw3, glm, stb, freetype, vulkan, shaderc, glad (v1), msdfgen, nlohmann-json (JSON), doctest (tests) — liste dans [vcpkg.json](vcpkg.json).

## Compiler

### Linux (apt d'abord, vcpkg pour glad, msdfgen et nlohmann-json)

```bash
sudo apt install build-essential cmake cargo python3 \
    libglfw3-dev libglm-dev libstb-dev libshaderc-dev glslc doctest-dev libfreetype-dev \
    libvulkan-dev vulkan-validationlayers libgl-dev libx11-dev \
    autoconf autoconf-archive automake libtool

# glad (v1), msdfgen et nlohmann-json (JSON) ne sont pas pris dans apt : vcpkg, en UNE commande qui lit vcpkg.json (12 s)
git clone https://github.com/microsoft/vcpkg ~/vcpkg && ~/vcpkg/bootstrap-vcpkg.sh
~/vcpkg/vcpkg install                    # à la racine du dépôt

make all-debug                           # relie les paquets apt tout seul, puis moteur -> démo (debug)
```

Dans un terminal, `make` affiche une barre de progression (`tools/build_progress.py`) au lieu des commandes : fichiers compilés,
en cours, restants et à jour (sautés), puis un résumé ; les avertissements, erreurs et sorties des tests s'affichent au-dessus.
`make V=1 …` montre les commandes complètes ; `ORS_NO_PROGRESS=1 make …` garde l'affichage habituel. Hors terminal (CI,
redirection vers un fichier), rien ne change. Il faut Python 3 (sinon, affichage habituel).

`vcpkg.json` marque `glfw3`, `glm`, `stb`, `freetype`, `vulkan`, `shaderc` et `doctest` en `"platform": "!linux"` (apt les fournit) ; sous Linux `vcpkg install` n'installe donc que `glad`, `msdfgen`, `nlohmann-json` et `spirv-cross` (sans options par défaut : noyau seulement, sans skia). Il les place dans `vcpkg_installed/x64-linux/`, où `tools/setup-linux-deps.sh` (lancé par `make`) ajoute les paquets apt.

`make clean-all` supprime `vcpkg_installed/` : relance ensuite `~/vcpkg/vcpkg install` (12 s, les archives sont en cache), `make` relie le reste.

### Windows / macOS

```bash
make install-deps-all            # vcpkg (triplets x64-mingw-static, x64-osx, arm64-osx)
make all-debug
```
(Windows : vérifié avant M00 seulement ; macOS : non vérifié, voir [docs/GAPS.md](docs/GAPS.md).)

## Lancer, tester

| Commande | Effet |
| --- | --- |
| `make run` | Construit si besoin puis lance la démo (`apps/demo/bin/<type>/`) |
| `make demo-test` | Construit et lance les tests (`tests/`, doctest ; filtres : `-ts=<suite>`, `-tc=<cas>`) |
| `make demo-test SANITIZE=address,undefined` | Idem sous ASan + UBSan (arbre propre : `make fclean` avant) |
| `make demo-test-unit` / `make demo-test-gpu` | Seulement les tests sans GPU ni fenêtre / seulement l'intégration graphique (contexte OpenGL, fenêtre). `ENGINE_TEST_REQUIRE_GPU=1` : un test graphique sans pilote ni affichage échoue au lieu d'être ignoré |
| `make check` | Dépôt, couches, isolation des API (y compris GLFW et `<windows.h>`), en-têtes autonomes : **bloquant** (échoue à la première violation, comme la CI) |
| `main --api opengl --capture <dossier>` | Rend 10 images à 1280x720 (graine 1), écrit `scene.png` (reproductible) et `ui.png` ; OpenGL seulement |
| `python3 tools/compare_png.py A.png B.png` | Compare deux images (tolérance, budget de pixels, image de différence) |
| `python3 -m unittest discover -s tests/tools` | Tests des outils Python |
| `make debug` / `make release` | Archives du moteur dans `lib/<type>/` |
| `make all-debug` / `make all-release` | Moteur → `apps/demo/orosmo/` → démo |
| `make sync-demo` | Copie le moteur construit dans `apps/demo/orosmo/` |
| `make dist` / `make engine-tool` | Paquet du moteur / outil `orosmo init` |
| `make terrain-gen` / `make terrain-gen-check` | Générateur en ligne de commande (TOOL-02) / son déterminisme et ses codes de retour |
| `make viewer-run ARGS="--seed 42 --preset mountains"` / `make viewer-capture` | Visualiseur de terrain (TOOL-01) / images sans fenêtre, thèmes sombre et clair |
| `make gallery-run` / `make gallery-capture` | Galerie de l'interface : chaque widget, conteneur, événement et thème (exemple) |
| `make cui-test` | Tests du compilateur du langage d'interface (cargo test + entrées invalides de bout en bout) |
| `make e2e-init` | De bout en bout depuis le paquet : `orosmo init`, compilation, lancement, exemples (BLD-02) |
| `make clean` / `make fclean` / `make demo-fclean` | Nettoyage |
| `sh tools/check-repo.sh` | Contrôle d'hygiène du dépôt (binaires, casse, CRLF) |
| `sh tools/format_sources.sh` | Formatage (`clang-format`) |

## Organisation

```
include/orosmo/  en-têtes publics du moteur, un dossier par module : core, platform, rhi, renderer, world, ui
src/          implémentations, même découpage (src/rhi/<backend>/ pour les API graphiques) ; README.md par module
apps/         demo (terrain orosmo::world + panneau de performances), terrain_viewer, terrain_gen, ui_gallery, minimal, core_demo, rhi_demo
tests/        tests (doctest) et tests des outils Python (tests/tools)
shaders/      shaders GLSL (copiés vers <bin>/res/shader/)
res/          polices, icône (copiés vers <bin>/res/)
mk/           Makefile du moteur (engine.mk) et configuration (config.mk)
tools/        précompilateur .cui (cui-rs), outil engine-init-rs, SDK (engine-sdk), scripts, extension VS Code
docs/         spécification, plan, rapports (voir docs/README.md)
```

Le moteur compile en bibliothèques statiques, une par module :
- **Cœur distribué en release** (`dist/orosmo-pkg-<plateforme>.zip`, `orosmo init`) : `libcore.a`, `libplatform.a`, `librhi.a`, `librenderer.a`, `libui.a`, `libthirdparty.a`.
- **Modules procéduraux internes au dépôt** (utilisés par `apps/demo`, `apps/terrain_gen`, `apps/terrain_viewer` et les tests, exclus de la release — décision A34) : `libworld.a`, `libterrain_renderer.a`.

Dans le dépôt, la démo les relie via `apps/demo/orosmo/` (avec `ENGINE_EXTRA_MODULES="world terrain_renderer"`).

## Documentation

- Vision, checklist, architecture : [docs/](docs/README.md)
- Couche graphique (RHI) : [docs/RHI.md](docs/RHI.md)
- **Utiliser le moteur depuis une release** (`orosmo init`, paquet, projet minimal, stabilité de l'API) : [docs/USING_THE_ENGINE.md](docs/USING_THE_ENGINE.md)
- Langage d'interface : [docs/UI_LANGUAGE.md](docs/UI_LANGUAGE.md) (cible `orosmo/ui`)
- Nettoyage M00 : [docs/CLEANUP_PLAN.md](docs/CLEANUP_PLAN.md), [docs/releases/M00.md](docs/releases/M00.md)

## Licence

À décider (Apache-2.0 envisagée). Aucun fichier `LICENSE` pour l'instant.
