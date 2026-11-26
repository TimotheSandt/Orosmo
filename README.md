# Orosmo

**Orosmo** est un moteur 3D et 2D générique en **C++23** (rendu OpenGL et Vulkan, Metal prévu), avec une interface décrite dans un langage maison (`.cui`) précompilé en C++. *(Note A37 : le code procédural de génération de terrain est archivé dans la branche `archive/procedural-world` et poursuivra sa vie dans un projet séparé / addon M11.)*

> Première release utilisable : M3 (guide : [docs/USING_THE_ENGINE.md](docs/USING_THE_ENGINE.md)). L'architecture évolue encore ;
> les changements incompatibles sont listés dans [docs/releases/](docs/releases/). Spécification, checklist et plan : [docs/](docs/README.md). Règles de travail : [CLAUDE.md](CLAUDE.md).

## Installer le SDK (écrire un jeu)

Pour écrire un jeu, il n'y a rien à compiler du moteur. L'installeur installe les paquets système (après confirmation),
l'outil `orosmo` dans `~/.orosmo/bin` (`%LOCALAPPDATA%\Orosmo\bin` sous Windows), l'ajoute au `PATH`, puis installe le
moteur précompilé de la dernière release :

```bash
curl -fsSL https://github.com/TimotheSandt/Orosmo/releases/latest/download/install.sh | sh          # Linux, macOS
```
```powershell
irm https://github.com/TimotheSandt/Orosmo/releases/latest/download/install.ps1 | iex              # Windows (PowerShell)
```

Options : `--yes` (sans questions), `--no-deps` (sans paquets système), `--version x.y.z` ; avec `curl … | sh`, elles
passent après `sh -s --`. Le plus prudent est de télécharger le script, de le lire, puis de le lancer (`sh install.sh`).
Ensuite :

```bash
orosmo init mygame && cd mygame         # config.mk nomme la version du moteur (OROSMO_VERSION)
orosmo run                              # compile (make debug) puis lance bin/debug/main
```

Ni vcpkg ni `make install` : les bibliothèques tierces du moteur sont dans le paquet (`deps/`). Linux : glibc 2.38 ou plus
récent (Ubuntu 24.04+) ; Windows et macOS ne sont pas vérifiés ([docs/GAPS.md](docs/GAPS.md), G214 à G219). Guide complet :
[docs/USING_THE_ENGINE.md](docs/USING_THE_ENGINE.md). La suite de ce fichier concerne la compilation du dépôt du moteur.

## Prérequis

- **Compilateur** : GCC 13+ ou Clang 17+ (testé : GCC 13.3, Linux), MSVC 2022 / MinGW sous Windows.
- **Outils** : GNU Make, CMake, Rust (`cargo`, pour `tools/cui-rs` et `tools/engine-init-rs`), Python 3 (contrôles et comparaison d'images).
- **Pilotes** : OpenGL 4.3+ avec `glClipControl` (OpenGL 4.5 ou `GL_ARB_clip_control`, présent sur les pilotes actuels) ; Vulkan à
  partir de M4, couches de validation conseillées.
- **Dépendances** : glfw3, glm, stb, freetype, vulkan, shaderc, glad (v1), msdfgen, nlohmann-json (JSON), cgltf (glTF), doctest (tests) — liste dans [vcpkg.json](vcpkg.json).

## Compiler

### Linux (apt d'abord, vcpkg pour glad, msdfgen, nlohmann-json et GLFW)

```bash
sudo apt install build-essential cmake cargo python3 \
    libglm-dev libstb-dev libshaderc-dev glslc doctest-dev libfreetype-dev \
    libvulkan-dev vulkan-validationlayers libgl-dev libx11-dev \
    autoconf autoconf-archive automake libtool \
    pkg-config libwayland-dev libxkbcommon-dev wayland-protocols libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev

# glad (v1), msdfgen, nlohmann-json (JSON) et GLFW (3.4 et plus : Wayland natif, X11 sinon ; le paquet apt d'Ubuntu 24.04
# est en 3.3, X11 seulement, donc XWayland sous Wayland) viennent de vcpkg, en UNE commande qui lit vcpkg.json
git clone https://github.com/microsoft/vcpkg ~/vcpkg && ~/vcpkg/bootstrap-vcpkg.sh
~/vcpkg/vcpkg install                    # à la racine du dépôt

make all-debug                           # relie les paquets apt tout seul, puis moteur -> démo (debug)
```

Dans un terminal, `make` affiche une barre de progression (`tools/build_progress.py`) au lieu des commandes : fichiers compilés,
en cours, restants et à jour (sautés), puis un résumé ; les avertissements, erreurs et sorties des tests s'affichent au-dessus.
`make V=1 …` montre les commandes complètes ; `ORS_NO_PROGRESS=1 make …` garde l'affichage habituel. Hors terminal (CI,
redirection vers un fichier), rien ne change. Il faut Python 3 (sinon, affichage habituel).

`vcpkg.json` marque `glm`, `stb`, `freetype`, `vulkan`, `shaderc` et `doctest` en `"platform": "!linux"` (apt les fournit) ; sous Linux `vcpkg install` n'installe donc que `cgltf` (en-têtes seuls), `glad`, `msdfgen` (sans options par défaut : noyau seulement, sans skia), `nlohmann-json`, `spirv-cross`, `volk`, `vulkan-memory-allocator` et `glfw3` (avec la fonctionnalité `wayland`, A67). Il les place dans `vcpkg_installed/x64-linux/`, où `tools/setup-linux-deps.sh` (lancé par `make`) ajoute les paquets apt.

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
| `make run` | Construit si besoin puis lance la démo unique (`apps/demo/bin/<type>/` ; `Escape` ouvre/ferme l'UI, `Ctrl+F` fait défiler les niveaux du panneau de performances, bouton `Quit` ou `File > Quit` pour quitter) |
| `make demo-capture` | Capture hors écran de la démo unifiée (scène 3D, thèmes `dark`/`light`/`framed`, onglets UI) |
| `make demo-test` | Construit et lance les tests (`tests/`, doctest ; filtres : `-ts=<suite>`, `-tc=<cas>`) |
| `make demo-test SANITIZE=address,undefined` | Idem sous ASan + UBSan (arbre propre : `make fclean` avant) |
| `make demo-test-unit` / `make demo-test-gpu` | Seulement les tests sans GPU ni fenêtre / seulement l'intégration graphique (contexte OpenGL, fenêtre). `ENGINE_TEST_REQUIRE_GPU=1` : un test graphique sans pilote ni affichage échoue au lieu d'être ignoré |
| `make check` | Dépôt, couches, isolation des API (y compris GLFW et `<windows.h>`), en-têtes autonomes : **bloquant** (échoue à la première violation, comme la CI) |
| `main --api opengl --headless --capture <dossier>` | Rend 10 images à 1280x720 (options : `--ui off|hud|full`, `--hud off|fps|full|graph` (panneau de performances, défaut `full`), `--theme dark|light|framed`, `--tab scene|widgets|controls|color|windows|layout|themes`, `--size WxH`, `--vsync [on|off]` (synchronisation verticale, active par défaut ; sans effet avec `--headless`), `--fps-limit N` (10 à 1000, coupe la vsync), `--direct-present on|off` (Vulkan : dessin direct dans l'image de la fenêtre, actif par défaut ; `off` revient à la copie si l'image scintille à très haute cadence sans vsync, G193), `--gpu-timings auto|on|off` (minuteries GPU : `auto`, défaut, seulement quand le panneau affiche des temps GPU, c'est-à-dire `--hud full|graph` ; les outils qui lisent les lignes `Perf: RenderGraph/...` passent `on`), `--capture-frames N`, `--hover <id>`, `--click <id>`) ; Vulkan par défaut, avec repli sur OpenGL s'il ne démarre pas ; `--backend` est un alias de `--api`, `--strict-backend` pour l'interdire |
| `python3 tools/compare_png.py A.png B.png` | Compare deux images (tolérance, budget de pixels, image de différence) |
| `python3 -m unittest discover -s tests/tools` | Tests des outils Python |
| `make debug` / `make release` | Archives du moteur dans `lib/<type>/` |
| `make all-debug` / `make all-release` | Moteur → `apps/demo/orosmo/` → démo |
| `make sync-demo` | Copie le moteur construit dans `apps/demo/orosmo/` |
| `make dist` / `make engine-tool` | Paquet du moteur (`dist/orosmo-pkg-<plateforme>.zip`, avec `deps/`) / outil `orosmo` |
| `make cui-test` | Tests du compilateur du langage d'interface (cargo test + entrées invalides de bout en bout) |
| `make e2e-init` | De bout en bout depuis le paquet extrait en lecture seule : `orosmo init`, compilation sans vcpkg, lancement, exemple `minimal`, CMake (BLD-02) |
| `make clean` / `make fclean` / `make demo-fclean` | Nettoyage |
| `sh tools/check-repo.sh` | Contrôle d'hygiène du dépôt (binaires, casse, CRLF) |
| `sh tools/format_sources.sh` | Formatage (`clang-format`) |

## Organisation

```
include/orosmo/  en-têtes publics du moteur, un dossier par module : core, platform, rhi, renderer, ui
src/          implémentations, même découpage (src/rhi/<backend>/ pour les API graphiques) ; README.md par module
apps/         demo (démo unique : scène 3D statique + vitrine complète d'orosmo::ui et des fonctionnalités moteur)
tests/        tests (doctest) et tests des outils Python (tests/tools)
shaders/      shaders GLSL (copiés vers <bin>/res/shader/)
res/          polices, thèmes, icône (copiés vers <bin>/res/)
mk/           Makefile du moteur (engine.mk) et configuration (config.mk)
tools/        précompilateur .cui (cui-rs), outil engine-init-rs, SDK (engine-sdk, dont examples/minimal/ et template/), scripts, extension VS Code
site/         site vitrine et wiki (Astro + Starlight, image Docker nginx ; M14)
docs/         spécification, plan, rapports (voir docs/README.md)
```

Le moteur compile en bibliothèques statiques, une par module (`libcore.a`, `libplatform.a`, `librhi.a`, `librenderer.a`, `libassets.a`, `libanim.a`, `libui.a`, `libthirdparty.a`), distribuées dans `dist/orosmo-pkg-<plateforme>.zip` avec leurs bibliothèques tierces précompilées (`deps/`, installé par `orosmo install` dans `$OROSMO_HOME/engines/<version>`) et copiées dans `apps/demo/orosmo/` pour la démo du dépôt. *(Le code procédural `orosmo::world` / `TerrainRenderer` et les anciennes applications terrain ont été archivés dans la branche `archive/procedural-world`, décision A37.)*

## Documentation

- Vision, checklist, architecture : [docs/](docs/README.md)
- Couche graphique (RHI) : [docs/RHI.md](docs/RHI.md)
- **Utiliser le moteur depuis une release** (installation, commandes `orosmo`, paquet, projet minimal, stabilité de l'API) : [docs/USING_THE_ENGINE.md](docs/USING_THE_ENGINE.md)
- Langage d'interface : [docs/UI_LANGUAGE.md](docs/UI_LANGUAGE.md) (cible `orosmo/ui`)
- Nettoyage M00 : [docs/CLEANUP_PLAN.md](docs/CLEANUP_PLAN.md), [docs/releases/M00.md](docs/releases/M00.md)
- Site vitrine et wiki (Astro, M14) : [site/](site/README.md) ; image Docker et publication : [docs/DEPLOIEMENT_SITE.md](docs/DEPLOIEMENT_SITE.md)

## Origine et méthode de développement

Orosmo a été **commencé par son auteur, avec OpenGL**. L'auteur a utilisé l'IA dès son apparition, mais **mal** à l'époque : sans plan ni structure clairs, en lui demandant simplement « fais tel truc », tout en continuant à coder lui-même un peu. Le projet a été **mis en pause, puis arrêté**. Il a **repris récemment** et utilise désormais **Claude** (assistant de programmation d'Anthropic) **correctement** : l'auteur décide de la direction, fixe les règles (`CLAUDE.md`, `docs/`), relit et fusionne ; l'IA écrit du code et des tests, que la CI et des preuves vérifient (aucune fonctionnalité n'est déclarée « Fait » sans test ni commande de démo). Les commits écrits avec l'IA portent la ligne `Co-Authored-By`. Version longue : la page « Histoire et méthode de développement » du site ([site/src/content/docs/projet/histoire.md](site/src/content/docs/projet/histoire.md)).

## Licence

[Apache-2.0](LICENSE). Les bibliothèques tierces gardent leurs propres licences.
