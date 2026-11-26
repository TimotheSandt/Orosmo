---
title: Compiler le moteur
description: Prérequis, dépendances et premier build du moteur depuis le dépôt, avec la démo. Linux est vérifié ; Windows et macOS ne le sont pas.
sidebar:
  order: 6
  label: Compiler le moteur
---

Cette page sert à compiler le dépôt du moteur, pour lancer la démo ou modifier Orosmo. Pour écrire un jeu, commencez par
[Installer Orosmo](/demarrer/installation/) : il n'y a rien à compiler du moteur, et vcpkg n'est pas nécessaire. Le dépôt,
lui, utilise vcpkg (ou les paquets du système) pour ses bibliothèques tierces ; `make dist` les copie ensuite dans le dossier
`deps/` du paquet de release.

## Prérequis

| Quoi | Version | Remarque |
|---|---|---|
| Compilateur C++23 | GCC 13+ ou Clang 17+ | vérifié : g++ 13.3 sous Ubuntu |
| GNU Make, CMake | récents | |
| Rust (`cargo`) | récent | construit le compilateur `.cui` et l'outil `orosmo` |
| Python 3 | 3.x | contrôles du dépôt, comparaison d'images, barre de progression |
| OpenGL | 4.3+ avec `glClipControl` | `glClipControl` vient de OpenGL 4.5 ou de `GL_ARB_clip_control` |
| Vulkan | 1.3 | optionnel : sans lui la démo retombe sur OpenGL ; les couches de validation sont conseillées |

Les bibliothèques tierces (GLFW, glm, stb, FreeType, shaderc, glad, msdfgen, nlohmann-json, cgltf, ktx, meshoptimizer,
doctest…) sont listées dans `vcpkg.json`.

## Linux

Les paquets apt fournissent ce qu'Ubuntu et Debian ont ; une seule commande vcpkg, qui lit `vcpkg.json`, ajoute glad v1,
msdfgen, nlohmann-json et GLFW 3.4+ (Wayland natif, X11 sinon).

```bash
sudo apt install build-essential cmake cargo python3 libglm-dev libstb-dev libshaderc-dev glslc \
    doctest-dev libfreetype-dev libvulkan-dev vulkan-validationlayers libgl-dev libx11-dev \
    autoconf autoconf-archive automake libtool pkg-config libwayland-dev libxkbcommon-dev wayland-protocols \
    libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev
git clone https://github.com/microsoft/vcpkg ~/vcpkg && ~/vcpkg/bootstrap-vcpkg.sh
~/vcpkg/vcpkg install                   # à la racine du dépôt
```

Au premier `make`, le script `tools/setup-linux-deps.sh` relie les paquets apt au dossier `vcpkg_installed/` ; il n'y a rien
d'autre à lancer. Si le dépôt est sur un disque monté sans bit exécutable, lancez les scripts avec `sh`.

## Windows et macOS

```bash
make install-deps-all            # vcpkg ; triplets x64-mingw-static, x64-osx, arm64-osx
make all-debug
```

:::caution
Ces deux plateformes sont compilées par la CI mais pas vérifiées pour le code récent (Windows l'a été avant le
nettoyage M00 seulement). Sous macOS, OpenGL s'arrête à la version 4.1 alors que le moteur demande 4.3, et il n'y a ni
Metal ni MoltenVK : seul ce qui ne touche pas au GPU devrait fonctionner. Les problèmes connus sont dans
[`docs/GAPS.md`](https://github.com/TimotheSandt/Orosmo/blob/dev/docs/GAPS.md).
:::

## Compiler et lancer

```bash
make all-debug     # moteur (debug, -Wall -Wextra -Wpedantic -Werror), puis démo
make run           # lance la démo
```

`make run` lance le dernier binaire construit (debug ou release, le plus récent) et reconstruit au besoin. La démo
utilise Vulkan par défaut et retombe sur OpenGL si Vulkan ne démarre pas. Dans la démo, `Échap` ouvre et ferme l'interface
(le bouton `Quit` quitte). Pour passer des options, voir [Explorer la démo](/demarrer/demo/).

Dans un terminal, `make` affiche une barre de progression à la place des commandes. `make V=1` montre les commandes
complètes, `ORS_NO_PROGRESS=1 make …` garde l'affichage habituel, `WERROR=0` rend les avertissements non bloquants.
Toutes les cibles sont dans [Compiler avec Make ou CMake](/reference/construction/).

## Vérifier

```bash
make demo-test                               # tests doctest (construit les archives release d'abord)
make demo-test SANITIZE=address,undefined    # avec ASan et UBSan ; partir d'un arbre propre (make fclean)
make check                                   # dépôt, couches, isolation des API graphiques, en-têtes autonomes
make cui-test                                # compilateur .cui
```

## Problèmes courants

- **`<stacktrace>` ne se lie pas.** `engine.mk` choisit `-lstdc++_libbacktrace` avec GCC 13 et `-lstdc++exp` avec GCC 14 et
  plus. Si vous écrivez votre propre commande d'édition de liens, ajoutez la bibliothèque qui correspond à votre version.
- **`Permission denied` sur un script.** Le dépôt est sur un disque sans bit exécutable : `sh tools/setup-linux-deps.sh`.
- **Vulkan ne démarre pas.** La démo l'écrit sur la sortie d'erreur et utilise OpenGL. `--strict-backend` interdit ce repli.
  `--list-apis` dit ce qui est compilé.
- **Pas d'affichage** (serveur sans écran) : `--headless` cache la fenêtre mais OpenGL demande quand même un affichage.
  Sous Linux, lancez sous `xvfb-run -a`.
- **`vcpkg_installed` supprimé** (`make clean-all`) : relancez `~/vcpkg/vcpkg install`, puis `make`.

## Limites

Seul Linux est observé de bout en bout. Voir [État du moteur](/projet/etat/).
