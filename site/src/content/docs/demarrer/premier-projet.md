---
title: Créer un projet avec orosmo init
description: Créer, compiler et lancer un projet, choisir la version du moteur, mettre à jour et migrer un ancien projet.
sidebar:
  order: 2
  label: Créer un projet
---

Cette page suppose qu'Orosmo est installé (voir [Installer Orosmo](/demarrer/installation/)). Ordre de lecture : cette
page, puis [Lire la boucle d'une application](/demarrer/premiere-application/) (le code du projet créé, et les conventions
des exemples du wiki), puis [Explorer la démo](/demarrer/demo/) pour voir chaque fonction, puis les guides.

## Créer, compiler, lancer

```bash
orosmo init mygame      # crée le dossier mygame/ ; installe la dernière version du moteur si elle manque
cd mygame
orosmo build            # compile en debug -> bin/debug/main
orosmo run              # compile si besoin, puis lance le programme
```

Le projet de démarrage ouvre une fenêtre avec un cube coloré, une lumière et un petit panneau `.cui`. Ce parcours (création,
compilation sans vcpkg, lancement sans fenêtre) est vérifié par `make e2e-init` en CI Linux.

`orosmo init` sans nom initialise le dossier courant, qui donne son nom au projet. Quand un nom est donné, le dossier ne doit
pas exister.

| Fichier créé | Rôle |
|---|---|
| `src/` | le projet de démarrage : `main.cpp` (écrit avec `app::Application`, voir [Lire la boucle d'une application](/demarrer/premiere-application/)), `hud_state.hpp`, `HudView.cui`, et la logique de jeu rechargeable `game/` |
| `config.mk` | nom, version du jeu, `OROSMO_VERSION` : à vous, à versionner |
| `config.local.mk` | réglages de la machine (par exemple `ENGINE_DIR`), ignoré par git |
| `Makefile`, `.gitignore` | modèles suivis par `orosmo update` |

Pendant la compilation apparaissent `Generated/` (code C++ produit par le compilateur `.cui`), `obj/` et `bin/`. Les trois
sont ignorés par git. Rien n'est écrit dans le moteur installé.

## Compiler avec make

L'outil ne fait que lancer `make`. Les commandes suivantes donnent le même résultat :

| Avec l'outil | Avec make |
|---|---|
| `orosmo build` | `make debug` |
| `orosmo build --release` | `make release` (binaire `bin/release/<PROJECT_NAME>`) |
| `orosmo run -- --headless` | `make debug && ./bin/debug/main --headless` |
| `orosmo clean` | `make clean` |

Les autres cibles du `Makefile` (`dev`, `test`, `benchmark`, `compile-commands`, `fclean`…) sont décrites dans
[Compiler avec Make ou CMake](/reference/construction/#projet-créé-par-orosmo-init). Le projet de démarrage accepte
`--api vulkan|opengl` (Vulkan par défaut, avec repli sur OpenGL s'il ne démarre pas ; `--strict-backend` supprime le
repli ; sous macOS il n'y a pas de repli, seul Vulkan y fonctionne), `--headless`, `--size <L>x<H>`, `--frames <N>`, `--capture <dossier>`, `--capture-frames <N>` (3 par défaut) et
`--help` (options de `app::Application::main`).

## Version du moteur

Le projet nomme sa version du moteur dans `config.mk` :

```makefile title="config.mk"
OROSMO_VERSION := 0.7.0
```

Le `Makefile` demande son chemin à l'outil (`orosmo path 0.7.0`). Si cette version n'est pas installée, il s'arrête avec la
commande à lancer :

```text
Orosmo 0.7.0 is not installed: run "orosmo install 0.7.0" (or pass ENGINE_DIR=/path/to/engine)
```

Deux projets peuvent utiliser deux versions différentes : chacune est installée une fois dans `~/.orosmo/engines/`. Pour
essayer un autre moteur sans toucher à `config.mk`, donnez `ENGINE_DIR` (ligne de commande ou `config.local.mk`) :
`make debug ENGINE_DIR=/chemin/vers/moteur`.

## Mettre à jour le moteur du projet

```bash
orosmo update           # dans le dossier du projet ; ou : orosmo update chemin/du/projet
```

`orosmo update` installe la dernière version si besoin, écrit sa valeur dans `OROSMO_VERSION`, et réécrit `Makefile` et
`.gitignore` s'ils n'ont pas été modifiés ; sinon l'outil demande confirmation (`y/N`). `config.mk` reçoit les nouveaux champs
sans toucher aux vôtres, `config.local.mk` n'est pas écrasé et le dossier `src/` n'est jamais modifié. Les autres projets
gardent leur version.

Revenir à une version précise : changez `OROSMO_VERSION` à la main, puis `orosmo install <version>`.

## Migrer un ancien projet

Avant la `v0.6.2`, `orosmo init` copiait le moteur dans un dossier `orosmo/` du projet, et `make install` y installait les
dépendances par vcpkg. Un tel projet compile toujours tel quel : le `Makefile` préfère `orosmo/` quand il existe. Pour passer
au moteur installé :

```bash
orosmo migrate          # dans le dossier du projet
```

L'outil installe la même version que celle du dossier `orosmo/` (fichier `VERSION` du paquet), ou la dernière s'il ne la
trouve pas, met à jour `Makefile`, `.gitignore` et `config.mk`, renomme `orosmo/` en `orosmo.old/`, puis compile avec le moteur
installé. Si la compilation réussit, `orosmo.old/` est supprimé. Sinon la migration est annulée : `orosmo/`, `Makefile`,
`.gitignore`, `config.mk` et `config.local.mk` reprennent leur état d'avant.

## Garder le moteur dans le projet

```bash
orosmo init mygame --vendor
```

`--vendor` copie le moteur dans `orosmo/` comme avant : utile pour un projet qui doit compiler sans l'outil ni le réseau, ou
dont le moteur est figé dans le dépôt. Dans ce mode, `orosmo update` écrit la nouvelle release par-dessus `orosmo/`.

:::caution
En mode `--vendor`, les fichiers retirés d'une release restent dans `orosmo/`, car l'archive est écrite par-dessus. Pour un
moteur identique à une installation neuve, videz `orosmo/` avant `orosmo update`.
:::

## Bibliothèques du jeu

Le moteur n'a pas besoin de vcpkg. Pour ajouter une bibliothèque propre au jeu, le `Makefile` sait utiliser vcpkg (à
installer vous-même) :

```bash
make add-dep DEP=nlohmann-json
```

Le paquet est ajouté au `vcpkg.json` du projet, installé dans `vcpkg_installed/` du projet, et ses options d'édition de liens
dans `EXTRA_LDLIBS` de `config.mk`. Voir [Compiler avec Make ou CMake](/reference/construction/#bibliothèques-du-jeu).

## Sans l'outil

Décompressez le paquet `orosmo-pkg-<plateforme>.zip` d'une release et pointez votre `Makefile` dessus (`ENGINE_DIR`), ou
utilisez `find_package(Orosmo)` avec CMake : voir [Compiler avec Make ou CMake](/reference/construction/).

## Limites

- Les versions sont `0.MINEUR.CORRECTIF` : tant que la majeure vaut 0, un changement mineur peut casser l'API (la liste est
  dans `docs/releases/` du dépôt) ; un correctif ne la casse pas.
- Seul Linux est vérifié ; Windows et macOS sont construits par la CI mais pas essayés.
- Pas de système de modules ni d'addons pour l'instant.

Toutes les commandes et options de l'outil : [Commande orosmo](/reference/commande-orosmo/). Détails :
[État du moteur](/projet/etat/).
