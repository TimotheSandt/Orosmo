---
title: Compiler avec Make ou CMake
description: Construire un projet (orosmo build ou make), résolution du moteur, variables de engine.mk, bibliothèques du jeu, find_package(Orosmo) pour CMake, cibles du dépôt.
sidebar:
  order: 3
---

Le moteur se construit avec Make. Un projet qui l'utilise peut le consommer avec Make (`engine.mk`) ou avec CMake
(`OrosmoConfig.cmake`). Les deux fichiers sont dans `tools/engine-sdk/` et livrés dans le paquet, donc dans chaque moteur
installé (`$OROSMO_HOME/engines/<version>/make/engine.mk`, `cmake/`).

## Projet créé par orosmo init

`orosmo build` lance `make` ; `make` reste utilisable directement (il appelle seulement `orosmo path` pour trouver le moteur).

| Commande | Équivalent make | Effet |
|---|---|---|
| `orosmo build` | `make debug` | `bin/debug/main` (`-O1 -g -DDEBUG`) |
| `orosmo build --release` | `make release` | `bin/release/<PROJECT_NAME>` (`-O3 -DNDEBUG`) |
| `orosmo build --game` | `make game` | bibliothèque de jeu seule (`bin/debug/game.so`, `.dylib` ou `.dll`) |
| `orosmo run [-- args]` | `make run-debug` | compile si besoin, puis lance le programme |
| `orosmo clean` | `make clean` | supprime `obj/` et `Generated/` |

Autres cibles du `Makefile` : `dev` (`-O0 -g3`), `run` (le binaire le plus récent, debug ou release), `run-release`, `test`,
`benchmark`, `compile-commands` (écrit `compile_commands.json` pour clangd), `fclean` (supprime aussi `bin/`), `info` (moteur
trouvé, version, archives), `add-dep`, `install-deps`, `search-dep` (bibliothèques du jeu, plus bas). `orosmo build --json`
donne la même compilation en événements JSON : voir [Commande orosmo](/reference/commande-orosmo/#format-de-build-json).

### Logique de jeu rechargeable

Les sources de `src/game/` sont compilées en une bibliothèque que le programme (build debug ou dev) recharge à chaud.
Programme lancé, après une modification, `orosmo build --game` (ou `make game`) ne refait qu'elle. Incluez
`<orosmo/game.hpp>`, qui ne tire ni le rendu ni `glm/glm.hpp`. Si `ccache` est installé, le `Makefile` l'utilise
(`make CCACHE=` le désactive). Pour un programme lancé par `make dev`, la commande est
`make game BUILD_TYPE=dev` (`make game` seul refait la bibliothèque debug, que ce programme ne charge pas). Si un en-tête partagé avec `src/main.cpp` change les données d'un composant, reconstruisez tout
et relancez le programme. `make release` lie le jeu statiquement.

### Trouver le moteur

Le `Makefile` choisit `ENGINE_DIR` dans cet ordre :

1. `ENGINE_DIR` donnée sur la ligne de commande, dans l'environnement ou dans `config.local.mk` ;
2. le dossier `orosmo/` du projet, s'il existe (projets d'avant la `v0.6.2`, `orosmo init --vendor`, démo du dépôt) ;
3. `orosmo path $(OROSMO_VERSION)`, avec `OROSMO_VERSION` lu dans `config.mk` ; l'outil est cherché dans le `PATH`, puis
   dans `$OROSMO_HOME/bin` ;
4. sinon, `make` s'arrête avec la commande à lancer (`orosmo install x.y.z`).

| Variable | Défaut | Effet |
|---|---|---|
| `OROSMO_VERSION` | écrite par `orosmo init` dans `config.mk` | version du moteur du projet ; `orosmo update` la change |
| `ENGINE_DIR` | résolue comme ci-dessus | dossier du moteur ; la donner court-circuite `orosmo/` et `OROSMO_VERSION` |
| `OROSMO_HOME` | `~/.orosmo`, `%LOCALAPPDATA%\Orosmo` | racine des moteurs installés (variable d'environnement) |
| `ENGINE_GEN_DIR` | `Generated` | sortie du compilateur `.cui`, dans le projet |
| `ENGINE_DEPS_DIR` | `$(ENGINE_DIR)/deps` | bibliothèques tierces du moteur (fournie par `engine.mk`, à lire seulement) |
| `RES_DIR`, `SHADER_DIR` | voir [Ressources](#ressources-à-lexécution) | dossiers copiés à côté de l'exécutable |

```bash
make debug ENGINE_DIR=$HOME/src/Orosmo/dist/pkg-linux-x64   # essayer un moteur construit depuis le dépôt
make info                                                    # moteur trouvé et version
```

Rien n'est écrit dans `ENGINE_DIR` : la sortie de `cui` va dans `Generated/`, les objets dans `obj/`, les binaires dans
`bin/`. Un même moteur installé sert tous les projets de cette version.

### Bibliothèques du jeu

Les bibliothèques tierces du moteur sont dans `deps/` du moteur : le projet n'installe rien pour elles. Pour une bibliothèque
propre au jeu, le `Makefile` peut utiliser vcpkg (à installer soi-même ; `VCPKG` donne son chemin) :

| Cible | Effet |
|---|---|
| `make add-dep DEP=<paquet>` | ajoute le paquet au `vcpkg.json` du projet, l'installe dans `vcpkg_installed/` du projet, ajoute ses `-l` à `EXTRA_LDLIBS` de `config.mk` |
| `make install-deps` | installe les paquets du `vcpkg.json` du projet (rien sans `vcpkg.json`) |
| `make search-dep Q=<mot>` | cherche dans le catalogue vcpkg |

```bash
make add-dep DEP=nlohmann-json VCPKG=~/vcpkg/vcpkg
```

Les en-têtes de `vcpkg_installed/<triplet>/include` et les archives de `vcpkg_installed/<triplet>/lib` sont ajoutés à la
compilation et à l'édition de liens. `EXTRA_CXXFLAGS` et `EXTRA_LDLIBS` de `config.mk` restent modifiables à la main pour une
bibliothèque installée autrement.

## Cibles du dépôt du moteur

À lancer à la racine du dépôt. Dans un terminal, `make` affiche une barre de progression.

| Cible | Effet |
|---|---|
| `all-debug`, `all-release` | moteur, copie dans `apps/demo/orosmo/`, puis démo |
| `debug`, `dev`, `release` | archives du moteur dans `lib/<type>/` |
| `run`, `run-debug`, `run-release` | lance la démo (construit au besoin ; options : voir [Explorer la démo](/demarrer/demo/)) |
| `demo-debug`, `demo-release` | construit seulement la démo |
| `demo-test` | construit les archives release et lance les tests doctest |
| `demo-test-unit`, `demo-test-gpu` | tests sans GPU ni fenêtre / seulement l'intégration graphique |
| `check` | `check-repo`, `check-layers`, `check-api`, `check-headers` : bloquants |
| `cui-test` | tests du compilateur `.cui` |
| `e2e-init` | paquet extrait en lecture seule, `orosmo init`, compilation sans vcpkg, lancement, exemple minimal, CMake |
| `demo-capture` | captures hors écran de la démo, comparées aux références |
| `demo-vulkan-check` | parité OpenGL / Vulkan et validation Vulkan à zéro message |
| `dist` | paquet `dist/orosmo-pkg-<plateforme>.zip`, avec `deps/` (bibliothèques tierces copiées par `tools/package-deps.sh`) et `VERSION` |
| `engine-tool` | outil `orosmo` (`dist/orosmo-<plateforme>`) |
| `compile-commands` | `compile_commands.json` pour clangd |
| `install-deps`, `install-deps-demo`, `install-deps-all` | dépendances vcpkg du dépôt (le dépôt en a besoin ; un projet de jeu, non) |
| `clean`, `fclean`, `clean-all` | nettoyage (`clean-all` supprime aussi `vcpkg_installed/`) |
| `info` | résumé de la configuration |

| Variable | Défaut | Effet |
|---|---|---|
| `BUILD_TYPE` | `debug` | `debug`, `dev` (`-O0 -g3`) ou `release` |
| `WERROR` | `1` | `0` : les avertissements ne bloquent plus |
| `SANITIZE` | vide | `address,undefined` : ASan et UBSan |
| `V` | vide | `1` : commandes complètes au lieu de la barre de progression |
| `ORS_NO_PROGRESS` | vide | toute valeur : pas de barre de progression |
| `VCPKG` | `vcpkg` | exécutable vcpkg |
| `CXX` | `g++` | compilateur |

Le compilateur est lancé avec `-std=c++23 -Wall -Wextra -Wpedantic` (plus `-Werror` sauf `WERROR=0`), `-ffp-contract=off`, et
`-O1 -g -DDEBUG` en debug ou `-O3 -DNDEBUG` en release.

## Make : engine.mk

Pour écrire son propre `Makefile` (sans le gabarit d'`orosmo init`), avec un moteur installé ou un paquet décompressé :

```makefile
ENGINE_DIR   := $(shell orosmo path 0.6.2)  # ou le dossier d'un paquet décompressé
GAME_SRC_DIR := src                          # les .cui de ce dossier sont compilés automatiquement
BUILD_TYPE   ?= release                      # debug | dev | release
include $(ENGINE_DIR)/make/engine.mk

SRCS := $(shell find src -name "*.cpp")
OBJS := $(SRCS:src/%.cpp=obj/%.o) $(GEN_CPP_SOURCES:$(ENGINE_GEN_DIR)/%.gen.cpp=obj/gen/%.o)

.DEFAULT_GOAL := bin/mygame$(ENGINE_EXE_EXT)   # sinon `make` choisit la première règle d'engine.mk (une archive .a)

bin/mygame$(ENGINE_EXE_EXT): $(OBJS) $(ENGINE_LIBS)
	@mkdir -p bin
	$(CXX) $(ENGINE_CXXFLAGS) $(OBJS) $(ENGINE_LINK_LIBS) $(ENGINE_LDFLAGS) -o $@

obj/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(ENGINE_CXXFLAGS) -MMD -MP $(ENGINE_INCLUDES) -c $< -o $@

obj/gen/%.o: $(ENGINE_GEN_DIR)/%.gen.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(ENGINE_CXXFLAGS) -MMD -MP $(ENGINE_INCLUDES) -c $< -o $@

$(OBJS): | $(GEN_CPP_SOURCES) $(GEN_REDIRECT_SOURCES)

-include $(wildcard $(OBJS:.o=.d))                # dépendances d'en-têtes produites par -MMD -MP
```

Adapté du commentaire d'en-tête de `tools/engine-sdk/engine.mk`. Le plus simple est de partir du `Makefile` d'un projet
créé par `orosmo init` ; l'exemple `examples/minimal/Makefile` du paquet est un `Makefile` complet de 20 lignes.

Variables fournies par `engine.mk` :

| Variable | Contenu |
|---|---|
| `ENGINE_OS`, `ENGINE_EXE_EXT` | `Windows`, `Linux` ou `Darwin` ; `.exe` ou vide |
| `ENGINE_VCPKG_TRIPLET` | `x64-linux`, `arm64-linux`, `x64-osx`, `arm64-osx`, `x64-mingw-static` ; modifiable avant l'inclusion |
| `ENGINE_CXXFLAGS` | options de compilation du `BUILD_TYPE` choisi |
| `ENGINE_INCLUDES` | tous les `-I` des en-têtes du moteur, des dépendances et de `Generated/` |
| `ENGINE_LIBS` | archives : `libui`, `libcore`, `libplatform`, `librhi`, `librenderer`, `libassets`, `libanim`, `libscene`, `libthirdparty` |
| `ENGINE_LINK_LIBS` | `ENGINE_LIBS` entre `--start-group` et `--end-group` : à utiliser à l'édition de liens |
| `ENGINE_LDFLAGS` | `-L` et `-l` des dépendances (`ENGINE_DEPS_DIR`, KTX, meshoptimizer, GLFW, shaderc, `<stacktrace>`…) et des bibliothèques du système (OpenGL, X11, chargeur Vulkan par volk) |
| `ENGINE_DEPS_DIR` | bibliothèques tierces : `$(ENGINE_DIR)/deps` d'un paquet ; `$(ENGINE_DIR)/vcpkg_installed/<triplet>` sans `deps/` (dépôt du moteur, paquets plus anciens) ; `<ENGINE_VCPKG_ROOT>/<triplet>` si `ENGINE_VCPKG_ROOT` est donnée |
| `ENGINE_GEN_DIR` | `Generated` (dans le projet, modifiable avant l'inclusion) : sortie du compilateur `.cui` |
| `GEN_CPP_SOURCES`, `GEN_REDIRECT_SOURCES` | un `.gen.cpp` et un fichier de redirection par `.cui` de `GAME_SRC_DIR` ; à lister comme prérequis d'ordre des objets |
| `ENGINE_EXTRA_MODULES` | archives supplémentaires de `lib/<type>/` à lier |
| `ENGINE_VCPKG_ROOT` | à donner seulement pour lire les dépendances ailleurs que dans `deps/` (une installation vcpkg partagée) |

Un `.cui` modifié régénère son `.gen.cpp` dans `ENGINE_GEN_DIR` ; si une archive manque, `engine.mk` s'arrête avec un message
qui renvoie vers la page des releases. Les dépendances tierces sont lues dans `ENGINE_DEPS_DIR` ; elles viennent avec le
moteur et ne s'installent pas à part. `engine.mk` n'écrit jamais dans `ENGINE_DIR`.

## CMake : find_package(Orosmo)

```cmake
cmake_minimum_required(VERSION 3.20)
project(mygame LANGUAGES CXX)

find_package(Orosmo REQUIRED)   # cmake -DCMAKE_PREFIX_PATH="$(orosmo path 0.6.2)" ...

add_executable(mygame src/main.cpp)
orosmo_add_cui_views(mygame src/HudView.cui)
target_link_libraries(mygame PRIVATE Orosmo::Orosmo)
```

`Orosmo::Orosmo` impose C++23, les chemins d'inclusion, les archives et les bibliothèques système. Chaque module a aussi sa cible
(`Orosmo::core`, `platform`, `rhi`, `renderer`, `assets`, `anim`, `ui`, `thirdparty`). `orosmo_add_cui_views(cible fichiers.cui…)`
compile les vues au moment du build et ajoute les sources générées à la cible.

| Variable | Défaut | Effet |
|---|---|---|
| `OROSMO_VCPKG_TRIPLET` | selon la machine | triplet vcpkg |
| `OROSMO_VCPKG_ROOT` | vide : `<paquet>/deps` | dossier `<racine>/<triplet>` des dépendances, si elles ne sont pas dans `deps/` |
| `OROSMO_SHADERS_DIR`, `OROSMO_RES_DIR` | `<paquet>/shaders`, `<paquet>/res` | à copier à côté de l'exécutable |
| `OROSMO_CUI_EXECUTABLE` | `<paquet>/tools/cui` | compilateur `.cui` |
| `OROSMO_GEN_DIR` | `<dossier de build>/orosmo_generated` | sortie du compilateur `.cui` (jamais dans le paquet) |

Le moteur lui-même ne se construit pas avec CMake : cette consommation est facultative.

## Ressources à l'exécution

Un programme qui dessine charge les shaders du RHI (`shaders/rhi/` du paquet) ; une interface a besoin d'une police
(`res/fonts/Roboto-Regular.ttf`) et d'un thème (`res/themes/dark.json`, qui étend `base.json`). Le `Makefile` d'un projet
`orosmo init` les copie dans `bin/<type>/res/` à chaque compilation, avec la règle suivante :

| Source | Destination | Choix |
|---|---|---|
| `RES_DIR` | `bin/<type>/res/` | `res/` du projet s'il existe, sinon `$(ENGINE_DIR)/res/` |
| `SHADER_DIR` | `bin/<type>/res/shader/` | `$(ENGINE_DIR)/shaders/` |

Un seul dossier de ressources est copié : dès que le projet a un `res/`, celui du moteur est ignoré. Pour ajouter vos
assets, copiez d'abord le `res/` du moteur dans le projet (le dossier `Engine:` de `make info` ; le test évite de créer un `res/` vide, qui masquerait celui du moteur) :

```bash
eng=$(ORS_NO_PROGRESS=1 make -s info | sed -n '/^Engine:/{s/^Engine: *//;s/ (OROSMO_VERSION.*//;p;}')
[ -n "$eng" ] && test -d "$eng/res" && mkdir -p res && cp -R "$eng/res/." res/
```

Voir les
[conventions des exemples](/demarrer/premiere-application/#chemins-des-ressources). Vous pouvez aussi fixer `RES_DIR` dans
`config.mk`. Avec CMake, copiez vous-même `OROSMO_SHADERS_DIR` et `OROSMO_RES_DIR` à côté de l'exécutable. Les chemins sont
ensuite écrits avec `GET_RESOURCE_PATH("x")` (`orosmo/core/paths.hpp`) ; le moteur reçoit le dossier des shaders par
`SceneRenderer::create(…, {.shaderDirectory = …})`, les thèmes par `Ui::themes().load(…)` et la police par
`TextRenderer::loadFont(…)`.
