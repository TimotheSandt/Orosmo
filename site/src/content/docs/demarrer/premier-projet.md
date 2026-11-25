---
title: Créer un projet avec orosmo init
description: Écrire un programme avec une release du moteur, sans compiler le moteur lui-même.
sidebar:
  order: 1
---

Cette page est le point de départ pour écrire un jeu ou une application : vous téléchargez une release, vous ne compilez
pas le moteur. Pour compiler le dépôt du moteur lui-même (démo, tests, modifications), voir
[Compiler le moteur](/demarrer/installation/). Ordre de lecture : cette page, puis
[Lire la boucle d'une application](/demarrer/premiere-application/) (le code du projet créé, et les conventions des
exemples du wiki), puis [Explorer la démo](/demarrer/demo/) pour voir chaque fonction, puis les guides.

Une release contient, pour chaque plateforme (`linux-x64`, `linux-arm64`, `windows-x64`, `macos-arm64`, `macos-x64`),
l'outil en ligne de commande `orosmo-<plateforme>` et le paquet `orosmo-pkg-<plateforme>.zip` : en-têtes, bibliothèques
statiques debug et release, `make/engine.mk`, `cmake/OrosmoConfig.cmake`, compilateur `cui`, shaders du RHI, police,
thèmes, exemples et `vcpkg.json`. Ni Rust ni Python ne sont nécessaires.

## Exemple minimal, sous Linux (x64)

```bash
# 1. Paquets système (Ubuntu / Debian) : compilateur, make, bibliothèques de développement
sudo apt install build-essential cmake python3 pkg-config autoconf autoconf-archive automake libtool \
    libglm-dev libstb-dev libshaderc-dev glslc libfreetype-dev libvulkan-dev vulkan-validationlayers \
    libgl-dev libx11-dev libwayland-dev libxkbcommon-dev wayland-protocols \
    libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev

# 2. vcpkg (une fois) : il installe glad, msdfgen, GLFW, cgltf, ktx, meshoptimizer…
git clone https://github.com/microsoft/vcpkg ~/vcpkg && ~/vcpkg/bootstrap-vcpkg.sh

# 3. L'outil orosmo, depuis les releases (la dernière version de https://github.com/TimotheSandt/Orosmo/releases)
curl -L -o orosmo https://github.com/TimotheSandt/Orosmo/releases/latest/download/orosmo-linux-x64
chmod +x orosmo

# 4. Vérifier, créer, compiler, lancer
./orosmo doctor
./orosmo init mygame           # télécharge le paquet de la dernière release (ou utilise celui de l'outil hors ligne)
cd mygame
make install VCPKG=~/vcpkg/vcpkg   # dépendances vcpkg du moteur, une fois
make debug                         # compile -> bin/debug/main
./bin/debug/main                   # lance le projet de démarrage
```

Les paquets de l'étape 1 sont ceux de [Compiler le moteur](/demarrer/installation/#linux), moins `cargo` (inutile avec une
release) et `doctest-dev` (tests du moteur). Ce parcours est vérifié par `make e2e-init` en CI Linux. Sous Windows et
macOS, remplacez les étapes 1 et 3 par les outils de votre système (voir [Compiler le moteur](/demarrer/installation/)) ;
ces plateformes ne sont pas vérifiées.

:::note
`orosmo doctor` signale vcpkg comme facultatif, car `doctor` ne sert qu'à vérifier l'environnement, mais `make install` en a
besoin : sans vcpkg, il n'y a pas de GLFW ni de glad à lier.
:::

## doctor

`orosmo doctor [--json] [chemin]` contrôle l'environnement et sort avec une erreur si un prérequis obligatoire manque.

| Contrôle | Obligatoire | Détail |
|---|---|---|
| Compilateur C++23 | oui | `$CXX`, sinon `g++` puis `clang++` ; GCC 13+, Clang 17+, Apple Clang 15+ |
| GNU Make | oui | |
| Archiveur `ar` | oui, sauf macOS | |
| vcpkg | non | `$VCPKG`, `vcpkg` dans le `PATH`, ou `~/vcpkg` |
| Python 3, CMake | non | |
| Orosmo SDK, compilateur CUI | oui, dans un projet | cherche `orosmo/includes/orosmo`, `orosmo/lib/{debug,release}/libcore.a`, `orosmo/tools/cui` |
| Dépendances du SDK, paquet CMake | non | `orosmo/vcpkg_installed/<triplet>`, `orosmo/cmake/OrosmoConfig.cmake` |
| Affichage, Vulkan (Linux) | non | `DISPLAY` ou `WAYLAND_DISPLAY` ; `vulkaninfo` ou `libvulkan.so.1` |

`--json` écrit le même rapport en JSON : `engineVersion`, `platform`, `vcpkgTriplet`, `ok` et un tableau `checks`
(`name`, `required`, `status`, `detail`, `hint`). Voir [Commande orosmo](/reference/commande-orosmo/).

## init

| Fichier créé | Rôle |
|---|---|
| `orosmo/` | le paquet du moteur |
| `src/` | le projet de démarrage : `main.cpp`, `hud_state.hpp`, `HudView.cui` |
| `Makefile`, `.gitignore` | modèles suivis par `orosmo update` |
| `config.mk` | nom, version, paquets : à vous |
| `config.local.mk` | chemins de la machine, à ne pas versionner |

`orosmo init` sans argument initialise le dossier courant, qui donne son nom au projet. Un dossier existant est refusé
quand un nom est donné. Hors ligne, ou si GitHub répond mal, l'outil utilise le paquet embarqué ; `--package <zip>` impose
un paquet déjà téléchargé.

Cibles du `Makefile` du projet : `debug`, `dev`, `release`, `run`, `run-debug`, `run-release`, `install`, `test`, `benchmark`,
`compile-commands` (écrit `compile_commands.json` pour clangd), `clean`, `fclean`. Le binaire debug est `bin/debug/main`,
le binaire release `bin/release/<PROJECT_NAME>`. 
Pour lancer le binaire avec des options, appelez-le directement (voir [Explorer la démo](/demarrer/demo/)). Le projet de démarrage accepte `--headless`, `--capture <dossier>`, `--capture-frames <N>` (3 par défaut) et `--help`.
Son option `--api` n'accepte que `opengl` : il crée toujours un périphérique OpenGL (pour Vulkan, voir
[Choisir le backend graphique](/guides/application/peripherique/)).

## update

```bash
./orosmo-linux-x64 update          # dans le dossier du projet (ou : update chemin/du/projet)
```

`orosmo update` remplace `orosmo/` par la dernière release. `Makefile` et `.gitignore` sont réécrits s'ils n'ont pas été
modifiés ; sinon l'outil demande confirmation (`y/N`). `config.mk` reçoit les nouveaux champs sans toucher aux vôtres,
`config.local.mk` n'est pas écrasé, et un binaire `orosmo` placé dans le projet est mis à jour si la release est plus
récente. Votre dossier `src/` n'est jamais modifié.

:::caution
Les fichiers retirés d'une release restent dans `orosmo/`, car l'archive est écrite par-dessus. Pour repartir d'un SDK
propre, videz `orosmo/` (en gardant `orosmo/vcpkg_installed`) avant `update`, puis relancez `make install`.
:::

## Sans l'outil

Décompressez le paquet et pointez votre `Makefile` dessus (`ENGINE_DIR`), ou utilisez `find_package(Orosmo)` avec CMake :
voir [Compiler avec Make ou CMake](/reference/construction/).

## Limites

- Les versions sont `0.MINEUR.CORRECTIF` : tant que la majeure vaut 0, un changement mineur peut casser l'API (la liste est
  dans `docs/releases/` du dépôt) ; un correctif ne la casse pas.
- Seul Linux est vérifié ; Windows et macOS sont construits par la CI mais pas observés.
- Pas de système de modules ni d'addons : `orosmo` n'a que `init`, `update` et `doctor`.

Détails : [État du moteur](/projet/etat/).
