---
title: Installer et compiler
description: Prérequis, dépendances et premier build du moteur depuis le dépôt, sous Linux (vérifié), Windows et macOS.
sidebar:
  order: 1
---

Cette page concerne le **dépôt du moteur** (travailler sur Orosmo lui-même). Pour écrire un programme avec une release,
sans compiler le moteur, voir [Créer un projet](/utiliser/projet/).
*(Sources : `CLAUDE.md`, `README.md`.)*

## Prérequis

- **Compilateur C++23** : GCC 13+ ou Clang 17+ (vérifié : g++ 13.3 sous Ubuntu) ; MinGW-w64 sous Windows.
- **Outils** : GNU Make, CMake, Rust (`cargo`, pour le compilateur `.cui` et l'outil `orosmo`), Python 3 (contrôles,
  comparaison d'images, barre de progression).
- **Pilotes** : OpenGL 4.3+ avec `glClipControl` (OpenGL 4.5 ou `GL_ARB_clip_control`) ; Vulkan 1.3, avec les couches de
  validation conseillées.
- **Dépendances** : GLFW, glm, stb, FreeType, Vulkan, shaderc, glad (v1), msdfgen, nlohmann-json, doctest — liste dans
  `vcpkg.json`.

## Linux (vérifié)

Paquets apt pour ce qu'Ubuntu/Debian fournit, puis **une** commande vcpkg (qui lit `vcpkg.json`) pour glad v1, msdfgen,
nlohmann-json et GLFW 3.4+ (Wayland natif) :

```bash
sudo apt install build-essential cmake cargo python3 libglm-dev libstb-dev libshaderc-dev glslc \
    doctest-dev libfreetype-dev libvulkan-dev vulkan-validationlayers libgl-dev libx11-dev \
    autoconf autoconf-archive automake libtool pkg-config libwayland-dev libxkbcommon-dev wayland-protocols \
    libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev
git clone https://github.com/microsoft/vcpkg ~/vcpkg && ~/vcpkg/bootstrap-vcpkg.sh
~/vcpkg/vcpkg install                   # à la racine du dépôt ; make relie ensuite les paquets apt tout seul
```

Si le dépôt est sur un disque monté sans bit exécutable, lancez les scripts avec `sh`.

## Windows et macOS (non vérifiés)

```bash
make install-deps-all            # vcpkg (triplets x64-mingw-static, x64-osx, arm64-osx)
make all-debug
```

Windows n'a été vérifié qu'avant le nettoyage M00 ; macOS ne l'a pas été. Les deux sont construits par la CI, et les
problèmes connus sont suivis dans `docs/GAPS.md`. Sous macOS, OpenGL s'arrête à 4.1 : le backend OpenGL ne vise pas
macOS (Vulkan via MoltenVK puis Metal natif sont prévus au jalon M9).

## Build et démo

```bash
make all-debug     # moteur (debug, -Wpedantic -Werror) -> apps/demo/orosmo -> démo unique
make run           # lance la démo (Vulkan par défaut, repli sur OpenGL ; --api opengl pour forcer OpenGL)
```

Dans la démo, `Escape` ouvre ou ferme l'interface et `Ctrl+F` change le niveau du panneau de performances. Options utiles
du programme (`apps/demo/bin/<type>/main`) : `--ui off|hud|full`, `--theme dark|light|framed`,
`--tab scene|widgets|controls|color|windows|layout|themes`, `--size WxH`, `--headless`, `--vsync`,
`--capture <dossier>`.

Dans un terminal, `make` affiche une barre de progression ; `make V=1` montre les commandes complètes et `WERROR=0` rend les
avertissements non bloquants.

## Tests et contrôles

```bash
make demo-test                               # tests doctest
make demo-test SANITIZE=address,undefined    # ASan + UBSan (partir d'un arbre propre : make fclean)
make check                                   # dépôt, couches, isolation des API, en-têtes autonomes (bloquant)
make cui-test                                # tests du compilateur .cui
make e2e-init                                # paquet + orosmo init + compilation + exemple minimal
make demo-capture                            # captures hors écran de la démo
python3 tools/compare_png.py A.png B.png     # comparaison d'images avec tolérance
```
