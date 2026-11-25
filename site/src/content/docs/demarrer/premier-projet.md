---
title: Créer un projet avec orosmo init
description: Écrire un programme avec une release du moteur, sans compiler le moteur lui-même.
sidebar:
  order: 2
---

Une release contient, pour chaque plateforme (`linux-x64`, `linux-arm64`, `windows-x64`, `macos-arm64`, `macos-x64`),
l'outil en ligne de commande `orosmo-<plateforme>` et le paquet `orosmo-pkg-<plateforme>.zip` : en-têtes, bibliothèques
statiques debug et release, `make/engine.mk`, `cmake/OrosmoConfig.cmake`, compilateur `cui`, shaders du RHI, police,
thèmes, exemples et `vcpkg.json`. Ni Rust ni Python ne sont nécessaires pour s'en servir. Téléchargement :
[releases GitHub](https://github.com/TimotheSandt/Orosmo/releases).

## Exemple minimal

```bash
./orosmo-linux-x64 doctor          # vérifie la toolchain
./orosmo-linux-x64 init mygame     # crée le projet
cd mygame
make install                       # dépendances vcpkg du moteur, une fois
make debug                         # compile -> bin/debug/main
./bin/debug/main                   # lance le projet de démarrage
```

`make install` appelle `vcpkg` (variable `VCPKG`, par défaut `vcpkg` dans le `PATH`) ; avec un vcpkg cloné dans votre
dossier personnel : `make install VCPKG=~/vcpkg/vcpkg`. Ce parcours est vérifié par `make e2e-init` en CI Linux.

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
le binaire release `bin/release/<PROJECT_NAME>`. `make run` ne transmet pas d'arguments : lancez le binaire.

Le projet de démarrage accepte `--headless`, `--capture <dossier>`, `--capture-frames <N>` (3 par défaut) et `--help`.
Son option `--api` n'accepte que `opengl` et il crée toujours un périphérique OpenGL ; le message d'erreur pour Vulkan
(« not available yet ») est faux, le backend Vulkan existe (G209). Voir
[Lire la boucle d'une application](/demarrer/premiere-application/).

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

- Versions `0.MINEUR.CORRECTIF` : tant que la majeure vaut 0, un changement mineur peut casser l'API (liste dans
  `docs/releases/`) ; un correctif ne la casse pas.
- Linux est la seule plateforme vérifiée ; Windows et macOS sont construits par la CI mais pas observés.
- Modules et addons (`orosmo addon …`, MOD-01 à MOD-09) : Absent.
