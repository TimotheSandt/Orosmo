---
title: Compiler avec Make ou CMake
description: Cibles du Makefile du dépôt, variables de engine.mk pour un projet Make, et find_package(Orosmo) pour CMake.
sidebar:
  order: 3
---

Le moteur se construit avec Make. Un projet qui l'utilise peut le consommer avec Make (`engine.mk`) ou avec CMake
(`OrosmoConfig.cmake`). Les deux fichiers sont dans `tools/engine-sdk/` et livrés dans le paquet.

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
| `e2e-init` | paquet, `orosmo init`, compilation, lancement et exemple minimal |
| `demo-capture` | captures hors écran de la démo, comparées aux références |
| `demo-vulkan-check` | parité OpenGL / Vulkan et validation Vulkan à zéro message |
| `dist` | paquet `dist/orosmo-pkg-<plateforme>.zip` |
| `engine-tool` | outil `orosmo` (`dist/orosmo-<plateforme>`) |
| `compile-commands` | `compile_commands.json` pour clangd |
| `install-deps`, `install-deps-demo`, `install-deps-all` | dépendances vcpkg |
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

Dans le `Makefile` du projet, après avoir décompressé le paquet :

```makefile
ENGINE_DIR   := chemin/vers/engine-pkg      # le dossier décompressé
GAME_SRC_DIR := src                          # les .cui de ce dossier sont compilés automatiquement
BUILD_TYPE   ?= release                      # debug | dev | release
include $(ENGINE_DIR)/make/engine.mk

SRCS := $(shell find src -name "*.cpp")
OBJS := $(SRCS:src/%.cpp=obj/%.o) $(GEN_CPP_SOURCES:$(ENGINE_GEN_DIR)/%.gen.cpp=obj/gen/%.o)

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
| `ENGINE_LIBS` | archives : `libui`, `libcore`, `libplatform`, `librhi`, `librenderer`, `libassets`, `libanim`, `libthirdparty` |
| `ENGINE_LINK_LIBS` | `ENGINE_LIBS` entre `--start-group` et `--end-group` : à utiliser à l'édition de liens |
| `ENGINE_LDFLAGS` | `-L` et `-l` des dépendances (vcpkg, KTX, meshoptimizer, GLFW, `<stacktrace>`…) |
| `ENGINE_GEN_DIR` | `$(ENGINE_DIR)/Generated` : sortie du compilateur `.cui` |
| `GEN_CPP_SOURCES`, `GEN_REDIRECT_SOURCES` | un `.gen.cpp` et un fichier de redirection par `.cui` de `GAME_SRC_DIR` ; à lister comme prérequis d'ordre des objets |
| `ENGINE_EXTRA_MODULES` | archives supplémentaires de `lib/<type>/` à lier |
| `ENGINE_VCPKG_ROOT` | dossier des paquets vcpkg si ce n'est pas `$(ENGINE_DIR)/vcpkg_installed` |

Un `.cui` modifié régénère son `.gen.cpp` ; si une archive manque, `engine.mk` s'arrête avec un message qui renvoie vers la
page des releases. Les dépendances tierces sont cherchées dans `$(ENGINE_DIR)/vcpkg_installed/<triplet>` ; `make install` d'un projet
créé par `orosmo init` les y installe.

## CMake : find_package(Orosmo)

```cmake
cmake_minimum_required(VERSION 3.20)
project(mygame LANGUAGES CXX)

list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/orosmo")
find_package(Orosmo REQUIRED)

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
| `OROSMO_VCPKG_ROOT` | `<paquet>/vcpkg_installed` | dossier des dépendances vcpkg |
| `OROSMO_SHADERS_DIR`, `OROSMO_RES_DIR` | `<paquet>/shaders`, `<paquet>/res` | à copier à côté de l'exécutable |
| `OROSMO_CUI_EXECUTABLE` | `<paquet>/tools/cui` | compilateur `.cui` |
| `OROSMO_GEN_DIR` | `<paquet>/Generated` | sortie générique du compilateur |

Le moteur lui-même ne se construit pas avec CMake : cette consommation est facultative.

## Ressources à l'exécution

Un programme qui dessine charge les shaders du RHI (`shaders/rhi/` du paquet) ; une interface a besoin d'une police
(`res/fonts/Roboto-Regular.ttf`) et d'un thème (`res/themes/dark.json`, qui étend `base.json`). Le `Makefile` d'un projet
`orosmo init` les copie dans `bin/<type>/res/` à chaque compilation, avec la règle suivante :

| Source | Destination | Choix |
|---|---|---|
| `RES_DIR` | `bin/<type>/res/` | `res/` du projet s'il existe, sinon `orosmo/res/` |
| `SHADER_DIR` | `bin/<type>/res/shader/` | `orosmo/shaders/` |

Un seul dossier de ressources est copié : dès que le projet a un `res/`, celui de `orosmo/res/` est ignoré. Pour ajouter vos
assets, copiez d'abord `orosmo/res/` dans `res/` : voir les
[conventions des exemples](/demarrer/premiere-application/#chemins-des-ressources). Vous pouvez aussi fixer `RES_DIR` dans
`config.mk`. Avec CMake, copiez vous-même `OROSMO_SHADERS_DIR` et `OROSMO_RES_DIR` à côté de l'exécutable. Les chemins sont
ensuite écrits avec `GET_RESOURCE_PATH("x")` (`orosmo/core/paths.hpp`) ; le moteur reçoit le dossier des shaders par
`SceneRenderer::create(…, {.shaderDirectory = …})`, les thèmes par `Ui::themes().load(…)` et la police par
`TextRenderer::loadFont(…)`.
