---
title: Variables d'environnement
description: Variables lues par le moteur à l'exécution, par les tests et par le système de construction.
sidebar:
  order: 4
---

Ces variables se posent dans le shell avant de lancer le programme, par exemple `ENGINE_VULKAN_DEVICE=Intel apps/demo/bin/debug/main`.
Aucune n'est nécessaire en usage normal.

## Moteur (RHI)

| Variable | Valeurs | Défaut | Effet |
|---|---|---|---|
| `ENGINE_VULKAN_DEVICE` | un indice (`0`, `1`…) ou un fragment du nom du GPU | non défini | Vulkan : le GPU qui correspond est préféré. Sans variable, un GPU discret passe avant un intégré, puis un virtuel, puis un processeur (lavapipe). Un GPU qui ne satisfait pas les prérequis (Vulkan 1.3, `dynamicRendering`, `synchronization2`, `timelineSemaphore`, `hostQueryReset`, une file graphique et compute) reste exclu. |
| `ENGINE_VULKAN_PIPELINE_CACHE_DIR` | un dossier | `<données utilisateur>/Orosmo/pipeline_cache` | Dossier du cache de pipelines Vulkan sur disque (un fichier `vulkan_<vendor>_<device>.bin` par GPU). Un cache d'un autre GPU ou d'un autre pilote est ignoré. |
| `ENGINE_VULKAN_SYNC_VALIDATION` | `0`, `n`, `N`, `f`, `F` en tête : désactivé ; toute autre valeur : activé | activé | Validation de synchronisation de la couche Vulkan. N'a d'effet que si `DeviceDesc::validation` est vrai et que la couche l'offre. |
| `ENGINE_VK_DIRECT_SWAPCHAIN` | `0` | non défini | Force la copie de l'image hors écran à la présentation au lieu du dessin direct dans l'image de la fenêtre (comparaison, contournement d'un pilote). Toute autre valeur est sans effet ; l'option `--direct-present off` de la démo agit de même. |
| `ENGINE_GL_BUFFER_STORAGE` | `0` | non défini | OpenGL : force le repli `glBufferData` + `glBufferSubData` au lieu des tampons mappés en permanence (`glBufferStorage`). Toute autre valeur est sans effet. Sert aux tests et aux mesures. |

Le dossier de données utilisateur est `$XDG_DATA_HOME` (sinon `~/.local/share`) sous Linux, `~/Library/Application Support`
sous macOS, `%APPDATA%` sous Windows. La démo y place aussi le cache SPIR-V des shaders (`Orosmo/shader_cache`).

## Tests

| Variable | Effet |
|---|---|
| `ENGINE_TEST_BACKENDS` | liste de backends (`opengl,vulkan`) : restreint ceux que les tests essaient |
| `ENGINE_TEST_REQUIRE_GPU` | si définie et différente de `0`, un backend désigné mais indisponible, ou un test de fenêtre sans affichage, échoue au lieu d'être ignoré |
| `ENGINE_TEST_WAYLAND` | sous une session Wayland qui a aussi X11, les tests passent par X11 ; cette variable garde Wayland |
| `DOCTEST_TRACE`, `CI` | écrit `[trace] suite :: cas` sur la sortie d'erreur au début de chaque cas |

## Construction

Ce sont des variables de `make` (ligne de commande ou environnement) ; la liste est dans
[Compiler avec Make ou CMake](/reference/construction/) : `BUILD_TYPE`, `WERROR`, `SANITIZE`, `V`, `ORS_NO_PROGRESS`,
`VCPKG`, `CXX`, `E2E_RUN_GRAPHICS`. L'outil `orosmo doctor` lit `CXX` et `VCPKG`.
