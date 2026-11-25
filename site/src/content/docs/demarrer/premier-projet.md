---
title: Créer un projet (orosmo init, doctor, update)
description: Écrire un programme avec une release du moteur, sans compiler le moteur lui-même.
sidebar:
  order: 2
---

Une release contient deux fichiers par plateforme (`linux-x64`, `linux-arm64`, `windows-x64` MinGW, `macos-arm64`,
`macos-x64`) : l'outil en ligne de commande `orosmo-<plateforme>` et le paquet `orosmo-pkg-<plateforme>.zip` (en-têtes,
bibliothèques statiques debug et release, `make/engine.mk`, `cmake/OrosmoConfig.cmake`, compilateur `cui`, shaders du RHI,
police, thèmes, exemples, `vcpkg.json`). Ni Rust ni Python ne sont nécessaires pour utiliser une release.
*(Source : `docs/USING_THE_ENGINE.md`, vérifié par `make e2e-init` en CI Linux x64.)*

Téléchargement : [releases GitHub](https://github.com/TimotheSandt/Orosmo/releases).

## `orosmo doctor`

```bash
./orosmo-linux-x64 doctor          # g++/clang++ C++23, make, vcpkg, GPU, organisation du SDK
./orosmo-linux-x64 doctor --json   # même rapport, en JSON
```

## `orosmo init`

```bash
./orosmo-linux-x64 init mygame     # télécharge le paquet de la dernière release (repli : paquet intégré à l'outil)
./orosmo-linux-x64 init --package orosmo-pkg-linux-x64.zip mygame   # hors ligne, avec un paquet déjà téléchargé
cd mygame
make install                       # dépendances vcpkg du moteur (une fois)
make debug && make run             # bin/debug/main
make compile-commands              # compile_commands.json pour clangd / VS Code
```

`orosmo init` crée :

| Fichier | Rôle |
| --- | --- |
| `orosmo/` | le paquet du moteur générique |
| `src/` | un projet de démarrage : scène 3D sur le RHI (`SceneRenderer`, `Mesh`, `LightManager`) et panneau `.cui` |
| `Makefile`, `.gitignore` | build ; mis à jour par `orosmo update` s'ils n'ont pas été modifiés |
| `config.mk` | nom et version du projet (à vous) |
| `config.local.mk` | chemins de la machine, ignoré par Git |

`orosmo init` sans argument initialise le dossier courant, avec son nom comme nom de projet.

## `orosmo update`

`orosmo update` écrit la dernière release dans `orosmo/` (par-dessus le contenu existant), met à jour `Makefile` et
`.gitignore` s'ils n'ont pas été modifiés, et ajoute à `config.mk` les champs nouveaux sans toucher aux vôtres ;
`--package` marche aussi. Les changements incompatibles de chaque version sont listés dans `docs/releases/`.

## Versions et stabilité

Numérotation **0.MINEUR.CORRECTIF** : tant que la version majeure vaut 0, un changement mineur peut casser l'API (liste
dans les notes de version) ; un correctif ne la casse jamais.

## Plateformes

Linux est vérifié (Ubuntu 24.04, g++ 13.3). Windows (MinGW, MSYS2) et macOS (Clang 17+ de Homebrew) sont construits en CI
mais **non vérifiés** ; sous macOS, seul ce qui se passe de GPU devrait fonctionner tant que Metal ou MoltenVK manquent.
