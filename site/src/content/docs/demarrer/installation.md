---
title: Installer Orosmo
description: Installer l'outil orosmo et le moteur précompilé avec une commande, à la main, ou le désinstaller.
sidebar:
  order: 1
  label: Installer
---

Cette page installe ce qu'il faut pour écrire un jeu : l'outil en ligne de commande `orosmo` et le moteur précompilé. Il n'y
a rien à compiler du moteur, ni vcpkg à installer. Pour compiler le dépôt du moteur lui-même (démo, tests, modifications),
voir [Compiler le moteur](/demarrer/compiler-le-moteur/).

## Commande d'installation

Linux et macOS, dans un terminal :

```bash
curl -fsSL https://github.com/TimotheSandt/Orosmo/releases/latest/download/install.sh | sh
```

Windows, dans PowerShell :

```powershell
irm https://github.com/TimotheSandt/Orosmo/releases/latest/download/install.ps1 | iex
```

Ouvrez ensuite un nouveau terminal (pour que le `PATH` soit relu) et vérifiez :

```bash
orosmo doctor
```

La suite est dans [Créer un projet](/demarrer/premier-projet/).

:::caution
Une commande `curl … | sh` exécute un script sans que vous l'ayez lu. Le plus prudent est de le télécharger, de le lire,
puis de le lancer :

```bash
curl -fsSLO https://github.com/TimotheSandt/Orosmo/releases/latest/download/install.sh
less install.sh
sh install.sh
```

Sous Windows : `irm <url> -OutFile install.ps1`, lecture, puis `.\install.ps1`.
:::

## macOS : Vulkan par MoltenVK

macOS n'a qu'OpenGL 4.1, trop ancien pour le moteur (4.3) : le rendu passe uniquement par Vulkan, traduit vers Metal par
MoltenVK. Ni MoltenVK ni le chargeur Vulkan ne sont dans le paquet du moteur : ils viennent de Homebrew. L'installeur les
propose ; à la main :

```bash
xcode-select --install                    # compilateur (Apple clang) et make
brew install vulkan-loader molten-vk      # chargeur Vulkan et pilote MoltenVK
orosmo doctor                             # lignes « Vulkan loader » et « Vulkan driver » en [ok]
```

Aucune variable d'environnement n'est nécessaire : les programmes liés avec `engine.mk` ou le paquet CMake cherchent le
chargeur dans `/opt/homebrew/lib` (Homebrew, puce Apple) et `/usr/local/lib` (Homebrew Intel, SDK Vulkan), et le chargeur de
Homebrew trouve seul MoltenVK. Pour un chargeur installé ailleurs, donnez son dossier dans le `config.local.mk` du projet
(`ENGINE_VULKAN_RPATHS := /chemin/lib`), et un autre pilote par la variable `VK_DRIVER_FILES`.

Si Vulkan ne démarre pas, le programme s'arrête avec la raison et la commande à lancer : il n'y a pas de repli sur OpenGL.
Il n'existe pas de backend Metal natif.

:::caution
macOS n'est vérifié que par l'intégration continue, sur la machine virtuelle de GitHub (GPU paravirtualisé), jamais sur un
vrai Mac : fenêtre visible, saisie, écran Retina, GPU physique et performances ne sont pas essayés. Sur Mac Intel, le paquet
se construit mais aucun rendu n'a pu être observé (MoltenVK s'arrête sur la machine virtuelle de la CI). Voir G520 à G524 dans
[`docs/GAPS.md`](https://github.com/TimotheSandt/Orosmo/blob/dev/docs/GAPS.md)).
:::

## Options

| Option | Type | Défaut | Effet |
|---|---|---|---|
| `--yes` | drapeau | absent | ne pose aucune question : les paquets système sont installés sans confirmation |
| `--no-deps` | drapeau | absent | n'installe pas les paquets système (à faire soi-même, voir `orosmo doctor --fix`) |
| `--version` | `x.y.z` | dernière release | installe cette version du moteur |

Avec `curl … | sh`, les options passent après `sh -s --` :

```bash
curl -fsSL https://github.com/TimotheSandt/Orosmo/releases/latest/download/install.sh | sh -s -- --yes --version 0.7.0
```

Sous Windows, `irm … | iex` ne transmet pas d'options : utilisez les variables d'environnement `OROSMO_YES=1`,
`OROSMO_NO_DEPS=1` et `OROSMO_VERSION=x.y.z`, ou lancez le script téléchargé avec ses paramètres
`.\install.ps1 -Yes -NoDeps -Version 0.7.0`.

```powershell
$env:OROSMO_YES = "1"; irm https://github.com/TimotheSandt/Orosmo/releases/latest/download/install.ps1 | iex
```

Les deux scripts sont aussi servis par le site : `https://orosmo.gasshog.fr/install.sh` et `https://orosmo.gasshog.fr/install.ps1`.

## Ce que fait l'installeur

1. Il détecte le gestionnaire de paquets : apt, dnf ou pacman sous Linux, Homebrew sous macOS, winget ou MSYS2 sous Windows.
2. Il affiche la liste des paquets système à installer et demande confirmation (sauf `--yes`). Ce sont ceux que le moteur
   n'embarque pas : compilateur C++23, GNU Make, Python 3, en-têtes OpenGL, X11 et Wayland, chargeur Vulkan.
3. Il place l'outil dans `$OROSMO_HOME/bin` et ajoute ce dossier au `PATH` : une ligne dans le fichier de démarrage du shell
   (`~/.bashrc`, `~/.zshrc`…) sous Linux et macOS, le `PATH` de l'utilisateur sous Windows.
4. Il lance `orosmo install latest` (ou la version de `--version`) : le paquet du moteur est téléchargé, son empreinte est
   vérifiée contre `SHA256SUMS` de la release, puis il est extrait dans `$OROSMO_HOME/engines/<version>/`.

`OROSMO_HOME` vaut `~/.orosmo` sous Linux et macOS, `%LOCALAPPDATA%\Orosmo` sous Windows. Définir la variable d'environnement
`OROSMO_HOME` avant l'installation choisit un autre dossier ; elle doit alors rester définie.

```text
~/.orosmo/
  bin/orosmo                 l'outil
  engines/0.7.0/             un moteur : en-têtes, bibliothèques, deps/, make/engine.mk, cmake/, tools/cui, shaders/, res/
```

Les bibliothèques tierces du moteur (GLFW, glad, FreeType, msdfgen, shaderc, volk…) sont dans `deps/`, précompilées et
statiques. Ne sont jamais embarqués : le chargeur Vulkan, OpenGL, X11 et Wayland, la libc. Plusieurs versions du moteur
peuvent coexister ; chaque projet nomme la sienne.

## Installation manuelle

Sans l'installeur, par exemple sur un système dont le gestionnaire de paquets n'est pas reconnu :

1. Téléchargez l'outil de votre plateforme depuis la [dernière release](https://github.com/TimotheSandt/Orosmo/releases/latest) :
   `orosmo-linux-x64`, `orosmo-linux-arm64`, `orosmo-macos-arm64`, `orosmo-macos-x64` ou `orosmo-windows-x64.exe`.
2. Placez-le dans `~/.orosmo/bin/orosmo` (`%LOCALAPPDATA%\Orosmo\bin\orosmo.exe` sous Windows) et rendez-le exécutable
   (`chmod +x`).
3. Ajoutez ce dossier au `PATH`, par exemple `export PATH="$HOME/.orosmo/bin:$PATH"` dans `~/.bashrc`.
4. Installez les paquets système : `orosmo doctor --fix` affiche la commande du gestionnaire détecté et la lance après
   confirmation ; sinon, installez un compilateur C++23, GNU Make et les en-têtes de développement OpenGL et X11.
5. Installez le moteur : `orosmo install latest`.

Le même résultat s'obtient sans réseau avec un paquet déjà téléchargé : `orosmo install --package orosmo-pkg-linux-x64.zip`
(pas de vérification d'empreinte dans ce cas).

## Mettre à jour

```bash
orosmo self-update        # remplace l'outil par celui de la dernière release
orosmo install latest     # ajoute la dernière version du moteur, sans retirer les autres
orosmo list               # versions installées
orosmo remove 0.6.0       # supprime une version dont plus aucun projet ne se sert
```

Un projet ne change de version que par `orosmo update` (voir [Créer un projet](/demarrer/premier-projet/#mettre-à-jour-le-moteur-du-projet)).

## Désinstaller

1. Supprimez le dossier `OROSMO_HOME` : `rm -rf ~/.orosmo` sous Linux et macOS, `%LOCALAPPDATA%\Orosmo` sous Windows. L'outil
   et tous les moteurs installés disparaissent.
2. Retirez la ligne `PATH` ajoutée par l'installeur dans le fichier de démarrage du shell, ou l'entrée du `PATH` utilisateur
   sous Windows.

Les paquets système installés par l'installeur ne sont pas retirés. Vos projets ne sont pas touchés : ils se recompilent
après une nouvelle installation.

## Prérequis et limites

| Système | Exigence | Vérifié |
|---|---|---|
| Linux x64, arm64 | glibc 2.39 ou plus récent (Ubuntu 24.04 et suivants) ; GCC 13+ ou Clang 17+ | x64 seulement : Ubuntu 24.04, g++ 13.3 |
| Windows x64 | MinGW-w64 g++ 13+ (MSYS2 MINGW64) | non |
| macOS arm64 | outils en ligne de commande Xcode ; `vulkan-loader` et `molten-vk` (Homebrew, non embarqués) | CI seulement, sur machine virtuelle |
| macOS x64 (Intel) | idem | non : compile, rendu jamais observé |

Linux est la seule plateforme essayée de bout en bout sur une vraie machine. Sous Windows et macOS, l'outil, le paquet et les
installeurs sont construits par la CI mais n'ont pas été essayés sur une machine (G216 dans
[`docs/GAPS.md`](https://github.com/TimotheSandt/Orosmo/blob/dev/docs/GAPS.md)). Un `OROSMO_HOME` dont le chemin contient
une espace n'est pas vérifié (G214) : choisissez un chemin sans espace. Paquets natifs (`.deb`, Homebrew, winget) et
signature des binaires ne sont pas encore proposés (G218).
