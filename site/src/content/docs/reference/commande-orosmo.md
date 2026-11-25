---
title: Commande orosmo
description: Sous-commandes, options, fichiers créés et contrôles de l'outil orosmo (init, update, doctor).
sidebar:
  order: 2
---

`orosmo` est l'outil en ligne de commande livré avec chaque [release](https://github.com/TimotheSandt/Orosmo/releases)
(`orosmo-<plateforme>`, `.exe` sous Windows). Il est écrit en Rust : `tools/engine-init-rs/src/main.rs` et `doctor.rs`. Usage
pas à pas : [Créer un projet](/demarrer/premier-projet/).

```text
orosmo init [nom]                 crée un projet
orosmo update [chemin]            met à jour le paquet et les modèles
orosmo doctor [--json] [chemin]   vérifie la toolchain, les dépendances et le SDK
--package <orosmo-pkg.zip>        paquet local au lieu de la dernière release
-h, --help, help                  usage
```

Une commande inconnue écrit l'usage et sort avec le code 1. Toute erreur s'écrit `error: …` sur la sortie d'erreur et donne
le code 1.

## init

| Argument | Défaut | Effet |
|---|---|---|
| `nom` | nom du dossier courant | nom du projet ; le dossier est créé et ne doit pas exister. Sans `nom`, le dossier courant est initialisé. |

Le nom ne peut pas contenir `/ \ : * ? " < > |`. L'outil interroge la dernière release sur GitHub, télécharge
`orosmo-pkg-<plateforme>.zip`, et retombe sur le paquet embarqué dans l'outil si GitHub est injoignable ou si l'asset manque.
Avec `--package`, aucune requête réseau n'est faite.

| Créé | Écrit à nouveau par `update` ? |
|---|---|
| `orosmo/` (paquet du moteur) | oui, remplacé par-dessus |
| `src/` (projet de démarrage) | non |
| `Makefile`, `.gitignore` | oui, s'ils sont intacts ; sinon confirmation `y/N` |
| `config.mk` | nouveaux champs ajoutés seulement |
| `config.local.mk` | non (écrit une fois) |

Chaque archive du paquet est vérifiée : un chemin absolu, un `..` ou un lecteur dans l'archive est refusé
(`unsafe path in the engine package`).

## update

`orosmo update [chemin]` exige un dossier `orosmo/` dans le projet (`does not look like an Orosmo project` sinon). Il
met à jour le paquet, les deux modèles suivis, `config.mk`, et un binaire `orosmo` placé dans le projet si la release est plus
récente que l'outil.

## doctor

`orosmo doctor [--json] [chemin]` : le chemin est celui du projet (défaut : `.`). Code de sortie 1 s'il y a au moins une
erreur (un prérequis obligatoire manque), 0 sinon ; les avertissements ne changent pas le code.

| Contrôle | Obligatoire | Réussi si |
|---|---|---|
| C++23 compiler | oui | `$CXX` sinon `g++` puis `clang++` ; GCC ≥ 13, Clang ≥ 17, Apple Clang ≥ 15 |
| GNU Make | oui | `make --version` répond |
| Archiver (ar) | oui, sauf macOS | `ar --version` répond |
| vcpkg | non | `$VCPKG`, `vcpkg` ou `~/vcpkg/vcpkg` répond |
| Python 3 | non | `python3 --version` (`python` sous Windows) |
| CMake | non | `cmake --version` |
| Orosmo SDK | oui, si `orosmo/` existe | `orosmo/includes/orosmo` et `lib/debug` ou `lib/release/libcore.a` |
| CUI compiler | oui si `orosmo/` existe | `orosmo/tools/cui` |
| SDK dependencies | non | `orosmo/vcpkg_installed/<triplet>` |
| CMake package | non | `orosmo/cmake/OrosmoConfig.cmake` |
| Display server (Linux) | non | `DISPLAY` ou `WAYLAND_DISPLAY` défini |
| Vulkan runtime (Linux) | non | `vulkaninfo --summary` ou `libvulkan.so.1` présent |

Dans le dépôt du moteur (un `mk/engine.mk` mais pas de `orosmo/`), il contrôle `vcpkg_installed/<triplet>` et `tools/cui`.
Le triplet est `x64-linux`, `arm64-linux`, `x64-osx`, `arm64-osx` ou `x64-mingw-static` selon la machine.

Sortie texte : une ligne `[ok]`, `[warn]` ou `[!!]` par contrôle, avec une ligne `->` d'indication, puis
`Summary: N ok, N warning(s), N error(s)`.

Sortie `--json` :

```json
{
  "engineVersion": "0.6.0",
  "platform": "linux-x64",
  "vcpkgTriplet": "x64-linux",
  "ok": true,
  "checks": [
    { "name": "GNU Make", "required": true, "status": "ok", "detail": "GNU Make 4.3", "hint": null }
  ]
}
```

`status` vaut `ok`, `warn` ou `error` ; `hint` est `null` quand il n'y a rien à suggérer. Les valeurs ci-dessus sont
un exemple de forme.

## Limites

L'outil n'a que ces trois commandes : pas de gestion de modules ni d'addons (`orosmo addon …`) pour l'instant. Voir [État du moteur](/projet/etat/).
