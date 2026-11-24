---
title: Compiler avec Make ou CMake
description: Brancher le paquet du moteur dans un projet par engine.mk (Make) ou find_package(Orosmo) (CMake).
sidebar:
  order: 3
---

Le moteur se construit avec Make ; un projet utilisateur peut le consommer avec **Make** (`engine.mk`) ou **CMake**
(`OrosmoConfig.cmake`). Les deux fichiers sont dans `tools/engine-sdk/` du dépôt et livrés dans le paquet.
*(Source : `docs/USING_THE_ENGINE.md` §3, `tools/engine-sdk/`.)*

## Make : `engine.mk`

Décompressez `orosmo-pkg-<plateforme>.zip`, puis dans le `Makefile` du projet :

```makefile
ENGINE_DIR   := chemin/vers/engine-pkg      # le dossier décompressé
GAME_SRC_DIR := src                          # les .cui de ce dossier sont compilés automatiquement
BUILD_TYPE   ?= release                      # debug | dev | release
include $(ENGINE_DIR)/make/engine.mk
```

`engine.mk` fournit `ENGINE_CXXFLAGS`, `ENGINE_INCLUDES`, `ENGINE_LIBS` / `ENGINE_LINK_LIBS`, `ENGINE_LDFLAGS`,
`ENGINE_EXE_EXT` et les règles du langage d'interface (`GEN_CPP_SOURCES` : un `.cui` modifié régénère
`Generated/<nom>.gen.cpp`). Les dépendances tierces sont cherchées dans `$(ENGINE_DIR)/vcpkg_installed/<triplet>` (c'est
ce que remplit `make install` dans un projet créé par `orosmo init`).

## CMake : `find_package(Orosmo)`

Consommation optionnelle, sans remplacer le build Make interne du moteur :

```cmake
cmake_minimum_required(VERSION 3.20)
project(mygame LANGUAGES CXX)

list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/orosmo")
find_package(Orosmo REQUIRED)

add_executable(mygame src/main.cpp)
orosmo_add_cui_views(mygame src/HudView.cui)
target_link_libraries(mygame PRIVATE Orosmo::Orosmo)
```

Variables utiles : `OROSMO_VCPKG_ROOT` (dépendances vcpkg partagées ailleurs), `OROSMO_VCPKG_TRIPLET`,
`OROSMO_SHADERS_DIR`, `OROSMO_RES_DIR`.

## Ressources à l'exécution

Un programme qui dessine charge les **shaders du RHI** (`shaders/rhi/` du paquet) ; une interface a besoin d'une
**police** (`res/fonts/Roboto-Regular.ttf`) et d'un **thème** (`res/themes/dark.json`, `light.json`). Les exemples et le
projet d'`orosmo init` les copient dans `bin/res/` ; le chemin est donné au moteur
(`SceneRenderer::create(..., {.shaderDirectory = ...})`, `ThemeHost::load(...)`, `TextRenderer::loadFont(...)`).
