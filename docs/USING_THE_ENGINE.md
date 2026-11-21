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
| `orosmo-<plateforme>[.exe]` | outil en ligne de commande : `orosmo init` (nouveau projet), `orosmo update` (mise à jour) |
| `orosmo-pkg-<plateforme>.zip` | le paquet : en-têtes, bibliothèques statiques debug et release, `make/engine.mk`, compilateur `cui`, shaders du RHI, police et thèmes, exemples, `vcpkg.json` |

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
./orosmo-linux-x64 init mygame          # télécharge le paquet de la dernière release (repli : paquet intégré à l'outil)
./orosmo-linux-x64 init --package orosmo-pkg-linux-x64.zip mygame   # hors ligne, avec un paquet déjà téléchargé
cd mygame
make install                            # dépendances vcpkg du moteur (une fois)
make debug && make run                  # bin/debug/main
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
```

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

Pour l'interface, `examples/ui_gallery` montre chaque widget, conteneur, événement et thème d'`orosmo::ui` dans une seule fenêtre.
Le projet créé par `orosmo init` fournit quant à lui un squelette complet avec fenêtre, rendu 3D RHI (`SceneRenderer`, `Mesh`,
`LightManager`) et superposition `.cui`.

> La génération procédurale (`orosmo::world`, `TerrainRenderer`, `terrain-gen`, `terrain-viewer`) reste dans le dépôt source
> pour les démos internes ; elle n'est pas incluse dans la release générique (décision A34). Un système de modules/addons est
> prévu au jalon M11 (`docs/05-PLAN_PAR_JALONS.md`).

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
liste des changements incompatibles est dans `docs/releases/<jalon>.md`) ; un **correctif** ne la casse jamais. Première release
présentée comme utilisable : `v0.3.1` (M3 bis, moteur générique). M3 (`v0.3.0`) n'a pas été publiée.

| Module (`include/orosmo/…`) | Statut | Remarque |
| --- | --- | --- |
| `core`, `rhi`, `renderer`, `ui` | **figé pour 0.3.x** | livré dans la release ; noms selon `docs/03` §7 (`camelCase`, `kPascalCase`, fichiers `snake_case`) |
| `platform` (`Window`, `InputManager`) | **provisoire** | livré dans la release ; noms encore en `PascalCase`, hors de l'espace `orosmo::platform` : ils changeront (`docs/GAPS.md` G96) |
| `core` : `Logger`, `Profiler`, `FPSCounter`, `RingBuffer` | **repris tels quels** | anciens modules `Utilities` et `Profiler` déplacés dans `orosmo::core` (2026-11-20) : noms de méthodes encore en `PascalCase` (G122) |
| `world`, `TerrainRenderer` | **hors release (dépôt uniquement)** | conservés dans le dépôt pour la démo et les outils terrain (A34) ; futur module/addon en M11 |

## 8. Limites connues

- **Backend graphique** : OpenGL seulement ; `--api vulkan` / `--backend vulkan` sont refusés avec un message jusqu'à M4 ; pas de
  rendu sous macOS avant M4 (voir §2).
- **Texte** : `TextRendererFont` affiche l'ASCII (REN-36) ; pas de composition IME, de presse-papiers ni de sélection dans les
  champs de texte (PLAT-05, partiel).
- **Interface** : pas de rechargement à chaud de la vue elle-même (le thème se recharge) ; pas de navigation à la manette.
- **Windows, macOS** : construits en CI, jamais observés verts ; les problèmes sont suivis dans `docs/GAPS.md`.
