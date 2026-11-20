# Utiliser le moteur

DOC-01 / BLD-01. Guide pour écrire un programme avec une **release** du moteur, sans compiler le moteur lui-même. Pour
travailler sur le moteur (le dépôt), voir le [README](../README.md).

Ce qui est décrit ici est vérifié par `make e2e-init` (CI Linux x64, à chaque poussée) : paquet construit, `orosmo init
--package` d'un projet neuf, compilation, lancement sans fenêtre, et les trois exemples compilés depuis le paquet. Sous Windows
et macOS, la même étape tourne en CI mais n'a pas encore été observée verte (`docs/GAPS.md`).

## 1. Ce que contient une release

| Fichier | Rôle |
| --- | --- |
| `orosmo-<plateforme>[.exe]` | outil en ligne de commande : `orosmo init` (nouveau projet), `orosmo update` (mise à jour) |
| `orosmo-pkg-<plateforme>.zip` | le paquet : en-têtes, bibliothèques statiques debug et release, `make/engine.mk`, compilateur `cui`, shaders du RHI, police et thèmes, exemples, `vcpkg.json` |

Plateformes : `linux-x64`, `linux-arm64`, `windows-x64` (MinGW), `macos-arm64`, `macos-x64`. Le contenu détaillé du paquet est dans
son `README.md` (copie de `tools/engine-sdk/dist-README.md`).

## 2. Prérequis

| Système | Compilateur (C++23) | Outils | Graphique |
| --- | --- | --- | --- |
| Linux (vérifié : Ubuntu 24.04, g++ 13.3) | g++ 13+ ou clang++ 17+ | GNU Make, vcpkg (`make install`) ou les paquets apt du README | OpenGL 4.5 (ou 4.3 + `GL_ARB_clip_control`), X11 |
| Windows (non vérifié depuis M00) | MinGW-w64 g++ 13+ (MSYS2 MINGW64) | `mingw32-make`, vcpkg (triplet `x64-mingw-static`) | OpenGL 4.5 |
| macOS (non vérifié) | clang++ 17+ (Homebrew LLVM ; le clang d'Apple doit fournir `std::expected`) | GNU Make, vcpkg | **aucun rendu avant M4** : macOS s'arrête à OpenGL 4.1, le RHI demande 4.3 ; ce qui se passe de GPU (`engine::world`, `terrain-gen`, l'export) fonctionne |

Ni Rust ni Python ne sont nécessaires pour utiliser une release (`cui` est livré compilé).

## 3. Créer un projet

### Avec l'outil `orosmo` (recommandé)

```bash
./orosmo-linux-x64 init mygame          # télécharge le paquet de la dernière release (repli : paquet intégré à l'outil)
./orosmo-linux-x64 init --package orosmo-pkg-linux-x64.zip mygame   # hors ligne, avec un paquet déjà téléchargé
cd mygame
make install                            # dépendances vcpkg du moteur (une fois)
make debug && make run                  # bin/debug/main
```

`orosmo init` crée `engine/` (le paquet), `src/` (le projet de référence : la démo du moteur), `Makefile`, `.gitignore`,
`config.mk` (nom, version : à vous) et `config.local.mk` (chemins de la machine, ignoré par git). `orosmo update` remplace `engine/`
par la dernière release, met à jour `Makefile` et `.gitignore` s'ils n'ont pas été modifiés, et ajoute à `config.mk` les champs
nouveaux sans toucher aux vôtres ; `--package` marche aussi.

### Avec le paquet seul (`ENGINE_DIR`)

Décompressez `orosmo-pkg-<plateforme>.zip` où vous voulez, puis, dans le `Makefile` de votre projet :

```makefile
ENGINE_DIR   := chemin/vers/engine-pkg      # le dossier décompressé
GAME_SRC_DIR := src                          # les .cui de ce dossier sont compilés automatiquement
BUILD_TYPE   ?= release                      # debug | dev | release
include $(ENGINE_DIR)/make/engine.mk
```

`engine.mk` fournit `ENGINE_CXXFLAGS`, `ENGINE_INCLUDES`, `ENGINE_LIBS` / `ENGINE_LINK_LIBS`, `ENGINE_LDFLAGS`, `ENGINE_EXE_EXT` et
les règles du langage d'interface (`GEN_CPP_SOURCES`). Les dépendances tierces sont cherchées dans
`$(ENGINE_DIR)/vcpkg_installed/<triplet>` : installez-y les dépendances du `vcpkg.json` du paquet (c'est ce que fait `make install`
dans un projet créé par `orosmo init`), ou faites-en un lien vers une installation existante (ce que fait `make e2e-init`).

## 4. Projet minimal

`examples/minimal/` du paquet (source : `apps/minimal/` du dépôt), compilé et lancé par `make e2e-init` :

```cpp
#include <engine/world/checksum.hpp>
#include <engine/world/terrain.hpp>
#include <cstdio>

int main()
{
    engine::world::TerrainParams params;
    params.heightScale = 80.0f;
    const engine::world::Heightmap tile = engine::world::generateTile(params, /*seed=*/42, {.x = 0, .z = 0, .lod = 0});
    std::printf("%zu samples, checksum %016llx\n", tile.heights.size(),
                static_cast<unsigned long long>(engine::world::checksum64(tile.heights)));
}
```

```makefile
ENGINE_DIR   ?= ../..
GAME_SRC_DIR := .
BUILD_TYPE   ?= release
include $(ENGINE_DIR)/make/engine.mk

.DEFAULT_GOAL := all
all: bin/minimal$(ENGINE_EXE_EXT)
bin/minimal$(ENGINE_EXE_EXT): obj/main.o $(ENGINE_LIBS)
	@mkdir -p bin
	$(CXX) $(ENGINE_CXXFLAGS) obj/main.o $(ENGINE_LINK_LIBS) $(ENGINE_LDFLAGS) -o $@
obj/main.o: main.cpp
	@mkdir -p obj
	$(CXX) $(ENGINE_CXXFLAGS) -MMD -MP $(ENGINE_INCLUDES) -c $< -o $@
```

Résultat (Linux x64) : `4225 samples, height at the centre = -6.899 m, checksum a7317b0c30548c23` — la même somme sur toutes les
plateformes (TEST-03).

Deux exemples plus complets sont dans le paquet : `examples/terrain_gen` (ligne de commande, exports) et
`examples/terrain_viewer` (fenêtre, rendu, panneau en langage d'interface ; voir son `README.md`). Pour l'interface,
`examples/ui_gallery` montre chaque widget, conteneur, événement et thème dans une seule fenêtre.

## 5. Ressources à l'exécution

Un programme qui dessine charge les **shaders du RHI** (`shaders/rhi/` du paquet) ; une interface `engine::ui` a besoin d'une
**police** (`res/fonts/Roboto-Regular.ttf`) et d'un **thème** (`res/themes/dark.json`, `light.json`, qui étendent `base.json`). Les
exemples et le projet d'`orosmo init` les copient dans `bin/res/` à la compilation ; le chemin est donné au moteur
(`SceneRenderer::create(..., {.shaderDirectory = ...})`, `ThemeHost::load(...)`, `engine::ui::TextRenderer::loadFont(...)`).

## 6. Interface

Une interface s'écrit dans le langage maison (`.cui`, `import "engine/ui"`) : la vue décrit les widgets, les liaisons (`$state.x`)
et les événements, qui nomment des fonctions C++ ; l'apparence est dans un thème JSON. Spécification et messages d'erreur :
[UI_LANGUAGE.md](UI_LANGUAGE.md). Une structure de paramètres réfléchie (`engine::core::Reflect<T>`) s'affiche sans code
d'interface : `ParamPanel($state.params)` dans le langage, `engine::ui::reflectedPanel(params)` en C++.

## 7. Stabilité de l'API et versions

Numérotation sémantique **0.MINEUR.CORRECTIF** : tant que la version majeure est 0, un changement **mineur** peut casser l'API (la
liste des changements incompatibles est dans `docs/releases/<jalon>.md`) ; un **correctif** ne la casse jamais. Première release
présentée comme utilisable : celle de M3 (proposée : `v0.3.0`).

| Module (`include/engine/…`) | Statut | Remarque |
| --- | --- | --- |
| `core`, `rhi`, `renderer`, `world`, `ui` | **figé pour 0.3.x** | noms selon `docs/03` §7 (`camelCase`, `kPascalCase`, fichiers `snake_case`) |
| `platform` (`Window`, `InputManager`) | **provisoire** | noms encore en `PascalCase`, hors de l'espace `engine::platform` : ils changeront (`docs/GAPS.md` G96) |
| `core` : `Logger`, `Profiler`, `FPSCounter`, `RingBuffer` | **repris tels quels** | anciens modules `Utilities` et `Profiler` déplacés dans `engine::core` (2026-11-20) : noms de méthodes encore en `PascalCase` (G122) |

## 8. Limites connues

- **Backend graphique** : OpenGL seulement ; `--api vulkan` / `--backend vulkan` sont refusés avec un message jusqu'à M4 ; pas de
  rendu sous macOS avant M4 (voir §2).
- **Texte** : `TextRendererFont` affiche l'ASCII (REN-36) ; pas de composition IME, de presse-papiers ni de sélection dans les
  champs de texte (PLAT-05, partiel).
- **Interface** : pas de rechargement à chaud de la vue elle-même (le thème se recharge) ; pas de navigation à la manette.
- **Windows, macOS** : construits en CI, jamais observés verts ; les problèmes sont suivis dans `docs/GAPS.md`.
