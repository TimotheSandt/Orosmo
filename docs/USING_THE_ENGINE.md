# Utiliser le moteur

DOC-01 / BLD-01. Guide pour écrire un programme avec une **release** du moteur, sans compiler le moteur lui-même. Pour
travailler sur le moteur (le dépôt), voir le [README](../README.md).

Ce qui est décrit ici est vérifié par `make e2e-init` (CI Linux x64, à chaque poussée) : paquet construit, `orosmo init
--package` d'un projet neuf, vérification que le paquet ne livre que le moteur générique (sans module procédural), compilation,
lancement sans fenêtre, et les exemples compilés depuis le paquet. Sous Windows et macOS, la même étape tourne en CI mais n'a pas
encore été observée verte (`docs/GAPS.md`).

## 1. Ce que contient une release

| Fichier | Rôle |
| --- | --- |
| `orosmo-<plateforme>[.exe]` | outil en ligne de commande : `orosmo init` (nouveau projet), `orosmo update` (mise à jour), `orosmo doctor` (vérification de la toolchain et du SDK) |
| `orosmo-pkg-<plateforme>.zip` | le paquet : en-têtes, bibliothèques statiques debug et release, `make/engine.mk`, `cmake/OrosmoConfig.cmake`, compilateur `cui`, `gen_compile_commands.py`, shaders du RHI, police et thèmes, exemples, `vcpkg.json` |

Plateformes : `linux-x64`, `linux-arm64`, `windows-x64` (MinGW), `macos-arm64`, `macos-x64`. Le contenu détaillé du paquet est dans
son `README.md` (copie de `tools/engine-sdk/dist-README.md`).

## 2. Prérequis

| Système | Compilateur (C++23) | Outils | Graphique |
| --- | --- | --- | --- |
| Linux (vérifié : Ubuntu 24.04, g++ 13.3) | g++ 13+ ou clang++ 17+ | GNU Make, vcpkg (`make install`) ou les paquets apt du README | OpenGL 4.5 (ou 4.3 + `GL_ARB_clip_control`), X11 |
| Windows (non vérifié depuis M00) | MinGW-w64 g++ 13+ (MSYS2 MINGW64) | `mingw32-make`, vcpkg (triplet `x64-mingw-static`) | OpenGL 4.5 |
| macOS (non vérifié) | clang++ 17+ (Homebrew LLVM ; le clang d'Apple doit fournir `std::expected`) | GNU Make, vcpkg | **aucun rendu avant M4** : macOS s'arrête à OpenGL 4.1, le RHI demande 4.3 ; ce qui se passe de GPU (`orosmo::core`, `examples/minimal`) devrait fonctionner, **non vérifié** (la CI macOS compile et teste le dépôt, pas le paquet ; G126) |

Ni Rust ni Python ne sont nécessaires pour utiliser une release (`cui` est livré compilé).

## 3. Créer un projet

### Avec l'outil `orosmo` (recommandé)

```bash
./orosmo-linux-x64 doctor               # vérifie g++/clang++ C++23, make, vcpkg, GPU (option --json)
./orosmo-linux-x64 init mygame          # télécharge le paquet de la dernière release (repli : paquet intégré à l'outil)
./orosmo-linux-x64 init --package orosmo-pkg-linux-x64.zip mygame   # hors ligne, avec un paquet déjà téléchargé
cd mygame
make install                            # dépendances vcpkg du moteur (une fois)
make debug && make run                  # bin/debug/main
make compile-commands                   # génère compile_commands.json pour clangd / VS Code
```

Dans un terminal, `make` affiche une barre de progression (fichiers compilés, en cours, restants et à jour) au lieu des commandes ;
`make V=1 …` montre les commandes, `ORS_NO_PROGRESS=1 make …` garde l'affichage habituel. Il faut Python 3 (sinon, affichage
habituel) ; le script est livré dans `orosmo/tools/build_progress.py`.

`orosmo init` crée `orosmo/` (le paquet du moteur générique), `src/` (un projet de démarrage générique : scène 3D RHI + panneau `.cui`),
`Makefile`, `.gitignore`, `config.mk` (nom, version : à vous) et `config.local.mk` (chemins de la machine, ignoré par git).
`orosmo update` écrit la dernière release dans `orosmo/` (par-dessus le contenu existant), met à jour `Makefile` et `.gitignore` s'ils n'ont pas été modifiés,
et ajoute à `config.mk` les champs nouveaux sans toucher aux vôtres ; `--package` marche aussi.

**Projet créé avant M3 bis** (paquet avec `world`) : `orosmo update` écrit le nouveau paquet **par-dessus** `orosmo/` ; les
fichiers retirés de la release (`orosmo/includes/orosmo/world/`, `libworld.a`, `libterrain_renderer.a`, `terrain_renderer.hpp`,
`shaders/rhi/terrain.*`, `examples/terrain_*`) restent alors en place et masquent une dépendance au terrain. Pour repartir d'un SDK
identique à une installation neuve, sans toucher aux sources du projet ni aux dépendances vcpkg déjà installées :

```bash
mv orosmo/vcpkg_installed vcpkg_installed.keep 2>/dev/null   # dépendances du SDK, gardées
rm -rf orosmo && mkdir orosmo                                # SDK vidé (src/, Makefile, config*.mk ne sont pas dedans) ;
                                                             # le dossier vide reste : orosmo update le demande
./orosmo-linux-x64 update                                    # ou : orosmo update --package orosmo-pkg-<plateforme>.zip
mv vcpkg_installed.keep orosmo/vcpkg_installed 2>/dev/null
make install                                                 # applique le vcpkg.json de la nouvelle release (sans retirer l'existant)
```

**Mise à niveau vers 0.4.0** : `engine.mk` relie désormais `-lvolk`, même pour un projet qui n'utilise qu'OpenGL. Des dépendances
installées avec la version 0.3.1 ne contiennent pas `volk` : sans `make install` entre la restauration et la compilation,
l'édition de liens échoue (`cannot find -lvolk`).

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

### Avec CMake (`find_package(Orosmo)`)

Le paquet inclut `cmake/OrosmoConfig.cmake` et `cmake/OrosmoConfigVersion.cmake` (consommation optionnelle sans remplacer le build Make interne du moteur) :

```cmake
cmake_minimum_required(VERSION 3.20)
project(mygame LANGUAGES CXX)

list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/orosmo")
find_package(Orosmo REQUIRED)

add_executable(mygame src/main.cpp)
orosmo_add_cui_views(mygame src/HudView.cui)
target_link_libraries(mygame PRIVATE Orosmo::Orosmo)
```

Variables utiles : `OROSMO_VCPKG_ROOT` (si les dépendances vcpkg sont partagées ailleurs), `OROSMO_VCPKG_TRIPLET`, `OROSMO_SHADERS_DIR`, `OROSMO_RES_DIR`.

## 4. Projet minimal

`examples/minimal/` du paquet (source : `tools/engine-sdk/examples/minimal/` du dépôt), compilé et lancé par `make e2e-init` :

```cpp
#include <orosmo/core/reflection.hpp>
#include <orosmo/core/serialization.hpp>
#include <orosmo/core/tasks.hpp>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>

struct AppSummary
{
    std::string name = "minimal";
    std::uint32_t tasksCompleted = 0;
    std::uint64_t sum = 0;
};

template <> struct orosmo::core::Reflect<AppSummary>
{
    static constexpr int kVersion = 1;
    static constexpr auto fields()
    {
        return std::make_tuple(field("name", &AppSummary::name), field("tasksCompleted", &AppSummary::tasksCompleted),
                               field("sum", &AppSummary::sum));
    }
};

int main()
{
    constexpr std::size_t kCount = 1024;
    orosmo::core::TaskSystem tasks;
    std::atomic<std::uint64_t> total{0};
    tasks.parallelFor(kCount, 64, [&](std::size_t i) { total.fetch_add(static_cast<std::uint64_t>(i + 1), std::memory_order_relaxed); });

    AppSummary summary;
    summary.tasksCompleted = static_cast<std::uint32_t>(kCount);
    summary.sum = total.load(std::memory_order_relaxed);

    const std::string json = orosmo::core::dumpJson(orosmo::core::saveDocument(summary), -1);
    std::printf("%s\n", json.c_str());
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

Résultat : `{"data":{"name":"minimal","sum":524800,"tasksCompleted":1024},"version":1}`.

Le projet créé par `orosmo init` fournit un squelette complet avec fenêtre, rendu 3D RHI (`SceneRenderer`, `Mesh`,
`LightManager`) et superposition `.cui`. Dans le dépôt du moteur, la démo unique `apps/demo/` rassemble la vitrine complète
d'`orosmo::ui` et des fonctionnalités du moteur (un sélecteur de modèles de démarrage `minimal`/`starter`/`demo` + modèle
utilisateur par défaut est prévu en M11, `MOD-09`).

> La génération procédurale (`orosmo::world`, `TerrainRenderer`, `terrain-gen`, `terrain-viewer`) a été archivée dans la
> branche `archive/procedural-world` (décision A37) et n'est pas incluse dans la release générique. Un système de
> modules/addons est prévu au jalon M11 (`docs/05-PLAN_PAR_JALONS.md`).

## 5. Ressources à l'exécution

Un programme qui dessine charge les **shaders du RHI** (`shaders/rhi/` du paquet) ; une interface `orosmo::ui` a besoin d'une
**police** (`res/fonts/Roboto-Regular.ttf`) et d'un **thème** (`res/themes/dark.json`, `light.json`, qui étendent `base.json`). Les
exemples et le projet d'`orosmo init` les copient dans `bin/res/` à la compilation ; le chemin est donné au moteur
(`SceneRenderer::create(..., {.shaderDirectory = ...})`, `ThemeHost::load(...)`, `orosmo::ui::TextRenderer::loadFont(...)`).

## 6. Interface

Une interface s'écrit dans le langage maison (`.cui`, `import "orosmo/ui"`) : la vue décrit les widgets, les liaisons (`$state.x`)
et les événements, qui nomment des fonctions C++ ; l'apparence est dans un thème JSON. Spécification et messages d'erreur :
[UI_LANGUAGE.md](UI_LANGUAGE.md). Une structure de paramètres réfléchie (`orosmo::core::Reflect<T>`) s'affiche sans code
d'interface : `ParamPanel($state.params)` dans le langage, `orosmo::ui::reflectedPanel(params)` en C++.

## 7. Stabilité de l'API et versions

Numérotation sémantique **0.MINEUR.CORRECTIF** : tant que la version majeure est 0, un changement **mineur** peut casser l'API (la
liste des changements incompatibles est dans `docs/releases/<jalon>.md`) ; un **correctif** ne la casse jamais. Dernière release : `v0.6.0` (M6, import glTF, PBR, animation). M3 (`v0.3.0`) n'a pas été publiée ; `v0.3.1` (M3 bis) est la première release publiée.

| Module (`include/orosmo/…`) | Statut | Remarque |
| --- | --- | --- |
| `core`, `rhi`, `renderer`, `ui`, `platform` | **figé pour 0.6.x** | livré dans la release ; noms selon `docs/03` §7 (`camelCase`, `kPascalCase`, fichiers `snake_case`) |
| `core` : `Logger`, `Profiler`, `FPSCounter`, `RingBuffer` | **repris tels quels** | anciens modules `Utilities` et `Profiler` déplacés dans `orosmo::core` (2026-11-20) : noms de méthodes encore en `PascalCase` (G122) |
| `world`, `TerrainRenderer` | **archivé (`archive/procedural-world`)** | retirés du dépôt principal (A37) ; futur module/addon en M11 |

## 8. Limites connues

- **Backend graphique** : OpenGL (défaut) et Vulkan ; Metal n'existe pas avant M9 et est refusé avec un message. Choix par
  `--backend <opengl|vulkan|metal>` (alias de `--api`, casse indifférente, `--backend=vulkan` accepté). Si Vulkan ne démarre pas (pas
  de chargeur, pas de GPU Vulkan 1.3), la raison s'affiche sur stderr et la démo retombe sur OpenGL ; `--strict-backend` supprime ce
  repli (code de sortie non nul). Rendu sous macOS : voir §2.
- **Texte** : `TextRendererFont` affiche l'ASCII (REN-36) ; pas de composition IME affichée (PLAT-05, partiel, G107) ; la sélection et le presse-papiers existent dans les champs de
  texte (`ui::useSystemClipboard`, voir `docs/UI_LANGUAGE.md`).
- **Interface** : pas de rechargement à chaud de la vue elle-même (le thème se recharge) ; pas de navigation à la manette.
- **Windows, macOS** : construits en CI, jamais observés verts ; les problèmes sont suivis dans `docs/GAPS.md`.
