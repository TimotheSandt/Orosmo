---
title: Commande orosmo
description: Toutes les commandes et options de l'outil orosmo (moteurs, projets, construction, doctor), et le format JSON de build --json.
sidebar:
  order: 2
---

`orosmo` est l'outil en ligne de commande d'Orosmo. L'installeur le place dans `$OROSMO_HOME/bin` (voir
[Installer Orosmo](/demarrer/installation/)) ; chaque [release](https://github.com/TimotheSandt/Orosmo/releases) le fournit
aussi seul (`orosmo-<plateforme>`, `.exe` sous Windows). Il est écrit en Rust : `tools/engine-init-rs/src/`. Usage pas à
pas : [Créer un projet](/demarrer/premier-projet/).

```text
Moteurs
  orosmo install [version|latest] [--force]      télécharge, vérifie (SHA256SUMS) et installe un moteur
  orosmo list                                    versions installées
  orosmo remove <version>                        supprime une version installée
  orosmo path <version>                          chemin du moteur (code 1 s'il n'est pas installé)
Projets
  orosmo init [nom] [--vendor]                   crée un projet ; installe le moteur au besoin
  orosmo update [chemin]                         passe le projet à la dernière version du moteur
  orosmo migrate [chemin]                        ancien projet avec orosmo/ -> moteur installé
  orosmo build [--release] [--json] [-C dossier] compile avec make (debug par défaut)
  orosmo run [--release] [-C dossier] [-- args]  compile, puis lance le programme
  orosmo clean [-C dossier]                      make clean
Outil
  orosmo self-update [--force]                   remplace l'outil par celui de la dernière release
  orosmo doctor [--json] [--fix] [chemin]        contrôle de l'environnement
  --package <orosmo-pkg.zip>                     (install, init, update) paquet local au lieu du téléchargement
  -V, --version, version                         version de l'outil
  -h, --help, help                               usage
```

Une commande ou une option inconnue écrit l'usage (ou un message) et sort avec le code 1. Toute erreur s'écrit `error: …`
sur la sortie d'erreur et donne le code 1. `build`, `run` et `clean` sortent avec le code de `make` (ou du programme lancé).

## Dossier des moteurs

| Variable | Défaut | Effet |
|---|---|---|
| `OROSMO_HOME` | `~/.orosmo` (Linux, macOS), `%LOCALAPPDATA%\Orosmo` (Windows) | racine : `bin/` (l'outil) et `engines/<version>/` (un moteur par version) |

Une version s'écrit `x.y.z` ; un `v` initial est retiré (`v0.6.2` vaut `0.6.2`). Un nom qui n'est pas une version simple est
refusé (`invalid engine version`).

```bash
OROSMO_HOME=/opt/orosmo orosmo install 0.6.2   # installe dans /opt/orosmo/engines/0.6.2
```

## install

| Argument | Type | Défaut | Effet |
|---|---|---|---|
| `version` | `x.y.z` ou `latest` | `latest` | version à installer |
| `--force` | drapeau | absent | réinstalle une version déjà présente |
| `--package` | chemin | absent | installe ce paquet local : pas de réseau, pas de vérification d'empreinte ; la version vient de son fichier `VERSION` |

L'outil interroge la release (`v<version>`, ou la dernière), télécharge `orosmo-pkg-<plateforme>.zip`, vérifie son empreinte
SHA-256 contre le fichier `SHA256SUMS` de la release, l'extrait dans un dossier temporaire puis le renomme en
`engines/<version>` : une installation interrompue ne laisse pas de moteur à moitié écrit. Une release sans `SHA256SUMS`
donne un avertissement, pas une erreur. Un chemin absolu, un `..` ou une lettre de lecteur dans l'archive est refusé.

Une version déjà installée n'est pas retéléchargée (sans `--force`). Si le réseau ne répond pas, `install latest` utilise le
paquet embarqué dans l'outil quand il en a un.

```bash
orosmo install                # dernière release
orosmo install 0.6.2 --force  # réinstalle 0.6.2
```

## list, remove, path

`orosmo list` écrit une version par ligne et marque `(this project)` celle du `config.mk` du dossier courant.
`orosmo remove <version>` supprime `engines/<version>` sans vérifier si un projet l'utilise. `orosmo path <version>` écrit
seulement le chemin absolu du moteur (séparateurs `/`, même sous Windows) et sort avec le code 1 s'il n'est pas installé :
c'est ce qu'appelle le `Makefile` d'un projet.

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(orosmo path 0.6.2)"
```

## init

| Argument | Type | Défaut | Effet |
|---|---|---|---|
| `nom` | texte | nom du dossier courant | nom du projet ; le dossier est créé et ne doit pas exister. Sans `nom`, le dossier courant est initialisé. |
| `--vendor` | drapeau | absent | copie le moteur dans `orosmo/` du projet au lieu d'utiliser le moteur installé (mode d'avant la `v0.6.2`) |
| `--package` | chemin | absent | paquet local au lieu de la dernière release |

Le nom ne peut pas contenir `/ \ : * ? " < > |`. Le moteur est préparé d'abord (dernière version, installée si elle manque ;
hors ligne, la plus récente déjà installée) : un échec ne laisse pas de projet à moitié créé.

| Créé | Écrit à nouveau par `update` ? |
|---|---|
| `src/` (projet de démarrage) | non |
| `Makefile`, `.gitignore` | oui, s'ils sont intacts ; sinon confirmation `y/N` |
| `config.mk` (avec `OROSMO_VERSION`) | `OROSMO_VERSION` et nouveaux champs seulement |
| `config.local.mk` | non (écrit une fois) |
| `orosmo/` (avec `--vendor` seulement) | oui, écrit par-dessus |

## update

`orosmo update [chemin]` (défaut : `.`) demande un `config.mk` ou un dossier `orosmo/` (`does not look like an Orosmo project`
sinon). Il installe la dernière version si besoin, l'écrit dans `OROSMO_VERSION` et met à jour `Makefile`, `.gitignore` et
`config.mk` comme le dit le tableau de `init`. Dans un projet avec `orosmo/`, il remplace d'abord `orosmo/` par la dernière
release, et met à jour un binaire `orosmo` placé dans le projet si la release est plus récente.

## migrate

`orosmo migrate [chemin]` passe un projet qui contient `orosmo/` au moteur installé :

1. il installe la version du fichier `orosmo/VERSION`, ou la dernière si ce fichier manque ou si cette version n'est pas
   disponible ;
2. il met à jour `Makefile`, `.gitignore` et `config.mk` (`OROSMO_VERSION`) ;
3. il renomme `orosmo/` en `orosmo.old/` et lance `make debug` ;
4. si le build réussit, il supprime `orosmo.old/` ; sinon il le garde et sort avec le code de `make`. Renommer `orosmo.old/`
   en `orosmo/` annule la migration.

La commande refuse un projet sans `orosmo/`, ou qui a déjà un `orosmo.old/`.

## build, run, clean

| Option | Type | Défaut | Effet |
|---|---|---|---|
| `--release` | drapeau | absent (debug) | `make release` au lieu de `make debug` ; `--debug` revient au défaut |
| `--json` | drapeau | absent | (`build` seulement) une ligne JSON par événement, voir plus bas |
| `-C` | dossier | `.` | dossier du projet (il doit contenir un `Makefile`) |
| `--` | arguments | aucun | (`run` seulement) tout ce qui suit est passé au programme |

`build` lance `make -jN debug` (ou `release`), avec N le nombre de cœurs, et affiche une ligne par fichier compilé, puis les
messages du compilateur. `run` fait de même, puis lance `bin/debug/main` (ou `bin/release/<PROJECT_NAME>`) depuis le dossier
du projet. `clean` lance `make clean`. Le programme `make` est `$MAKE` s'il est défini ; sous Windows, `mingw32-make` puis
`make`.

```bash
orosmo run --release -- --headless --capture out
```

### Format de build --json

Chaque ligne de la sortie standard est un objet JSON complet ; le champ `event` donne son type. Le code de sortie est celui de
`make`.

| `event` | Champs | Sens |
|---|---|---|
| `begin` | `label`, `shown`, `target` | une étape commence : `label` (par exemple `CXX`, `LD`), fichier affiché `shown`, cible absolue `target` |
| `end` | `target` | l'étape de cette cible est finie |
| `diagnostic` | `level`, `text`, et `file`, `line`, `column`, `message` quand la ligne les contient | une ligne de sortie ; `level` vaut `error`, `warning`, `note` ou `output` |
| `done` | `code`, `compiled`, `failed`, `elapsed_ms` | dernière ligne : code de `make`, nombre d'étapes finies, fichiers en échec, durée |

Une ligne de la forme `fichier:ligne:colonne: error: message` donne `file`, `line`, `column` et `message` ; sans colonne,
`column` est absent. Une ligne `make: *** …` ou `undefined reference` a le niveau `error`. Exemple d'un build qui échoue :

```json
{"event":"begin","label":"CXX","shown":"src/main.cpp","target":"/p/obj/main.o"}
{"column":5,"event":"diagnostic","file":"src/main.cpp","level":"error","line":12,"message":"'foo' was not declared in this scope","text":"src/main.cpp:12:5: error: 'foo' was not declared in this scope"}
{"event":"diagnostic","level":"error","text":"make: *** [Makefile:40: /p/obj/main.o] Error 1"}
{"code":2,"compiled":0,"elapsed_ms":1500,"event":"done","failed":["src/main.cpp"]}
```

Les clés sont écrites dans l'ordre alphabétique ; un lecteur ne doit pas dépendre de leur ordre. Ce format est prévu pour une
future interface graphique ; il peut gagner des champs.

## self-update

`orosmo self-update [--force]` compare la version de l'outil à la dernière release ; si elle est plus récente (ou avec
`--force`), il télécharge `orosmo-<plateforme>`, vérifie son empreinte contre `SHA256SUMS`, écrit le nouveau fichier à côté
de l'outil puis le renomme. Sous Windows, l'ancien exécutable est d'abord renommé en `.old`.

## doctor

`orosmo doctor [--json] [--fix] [chemin]` : le chemin est celui du projet (défaut : `.`). Code de sortie 1 s'il y a au moins
une erreur (un prérequis obligatoire manque), 0 sinon ; les avertissements ne changent pas le code.

| Contrôle | Obligatoire | Réussi si |
|---|---|---|
| C++23 compiler | oui | `$CXX` sinon `g++` puis `clang++` ; GCC ≥ 13, Clang ≥ 17, Apple Clang ≥ 15 |
| GNU Make | oui | `make --version` répond (`$MAKE`, `mingw32-make` sous Windows) |
| Archiver (ar) | oui, sauf macOS | `ar --version` répond |
| Python 3 | non | `python3 --version` (`python` sous Windows) |
| CMake | non | `cmake --version` |
| Installed engines | non | au moins un moteur dans `$OROSMO_HOME/engines` |
| Project engine | oui, si `config.mk` a `OROSMO_VERSION` | cette version est installée |
| Orosmo SDK | oui, pour le moteur du projet | `includes/orosmo` et `lib/debug` ou `lib/release/libcore.a` |
| CUI compiler | oui, pour le moteur du projet | `tools/cui` |
| SDK dependencies | non | `deps/` (ou `vcpkg_installed/<triplet>` d'un paquet plus ancien) |
| CMake package | non | `cmake/OrosmoConfig.cmake` |
| Display server (Linux) | non | `DISPLAY` ou `WAYLAND_DISPLAY` défini |
| Vulkan runtime (Linux) | non | `vulkaninfo --summary` ou `libvulkan.so.1` présent |

Le moteur du projet est `orosmo/` s'il existe, sinon celui d'`OROSMO_VERSION`. Dans le dépôt du moteur (un `mk/engine.mk`),
`doctor` contrôle `vcpkg_installed/<triplet>` et `tools/cui`. vcpkg n'est plus contrôlé : le moteur n'en a pas besoin.

Sortie texte : une ligne `[ok]`, `[warn]` ou `[!!]` par contrôle, avec une ligne `->` d'indication, puis
`Summary: N ok, N warning(s), N error(s)`.

`--fix` détecte le gestionnaire de paquets (apt, dnf, pacman sous Linux ; Homebrew sous macOS ; MSYS2 puis winget sous
Windows), affiche la commande qui installe les paquets système du développement, et la lance seulement après une réponse `y`
tapée dans un terminal. Hors terminal, il l'affiche sans la lancer. `--fix` ne se combine pas avec `--json`.

Sortie `--json` :

```json
{
  "engineVersion": "0.6.2",
  "platform": "linux-x64",
  "vcpkgTriplet": "x64-linux",
  "ok": true,
  "checks": [
    { "name": "GNU Make", "required": true, "status": "ok", "detail": "GNU Make 4.3", "hint": null }
  ]
}
```

`status` vaut `ok`, `warn` ou `error` ; `hint` est `null` quand il n'y a rien à suggérer. Les valeurs ci-dessus sont un
exemple de forme.

## Limites

Pas de gestion de modules ni d'addons (`orosmo addon …`) pour l'instant. Windows et macOS ne sont pas vérifiés (G216 dans
[`docs/GAPS.md`](https://github.com/TimotheSandt/Orosmo/blob/dev/docs/GAPS.md)). Voir [État du moteur](/projet/etat/).
