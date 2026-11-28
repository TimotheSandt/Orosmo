# Utiliser le moteur

DOC-01 / BLD-01. Guide pour écrire un programme avec une **release** du moteur, sans compiler le moteur lui-même. Pour
travailler sur le moteur (le dépôt), voir le [README](../README.md).

Depuis la `v0.6.2` (décision A76, `docs/03` §16), le moteur s'installe **une fois** par machine : l'outil `orosmo` et les
moteurs précompilés vivent dans `OROSMO_HOME` ; un projet ne copie plus le moteur, il nomme sa version (`OROSMO_VERSION` dans
`config.mk`). Ni vcpkg ni `make install` ne sont nécessaires.

Ce qui est décrit ici est vérifié par `make e2e-init` (CI Linux x64, à chaque poussée) : paquet construit puis extrait **en lecture
seule** (comme `orosmo install`), contenu contrôlé (`deps/` sans bibliothèque partagée ni chargeur Vulkan, aucun module procédural),
`orosmo init --package` d'un projet neuf, compilation **sans vcpkg**, sortie du compilateur `.cui` dans le projet, lancement sans
fenêtre, `orosmo doctor`, exemple minimal avec Make et CMake. Sous Windows et macOS, la même étape tourne en CI mais n'a pas été
observée sur une machine réelle (G216).

## 1. Installer

```bash
curl -fsSL https://github.com/TimotheSandt/Orosmo/releases/latest/download/install.sh | sh          # Linux, macOS
```
```powershell
irm https://github.com/TimotheSandt/Orosmo/releases/latest/download/install.ps1 | iex              # Windows (PowerShell)
```

L'installeur détecte le gestionnaire de paquets (apt, dnf, pacman, brew, winget / MSYS2), affiche la liste des paquets système
(compilateur, make, Python, en-têtes de fenêtrage, chargeur Vulkan) et les installe après confirmation ; il place l'outil dans
`$OROSMO_HOME/bin`, ajoute ce dossier au `PATH` (une ligne dans le fichier de démarrage du shell ; le `PATH` utilisateur sous
Windows), puis lance `orosmo install latest`.

| Option | Effet |
| --- | --- |
| `--yes` | aucune question (paquets système installés sans confirmation) |
| `--no-deps` | n'installe pas les paquets système |
| `--version x.y.z` | installe cette version du moteur au lieu de la dernière |

Avec `curl … | sh`, les options passent après `sh -s --` (`… | sh -s -- --yes`). Le plus prudent est de télécharger le script, de
le lire, puis de le lancer : `curl -fsSLO …/install.sh`, lecture, puis `sh install.sh`.

**Sans installeur** : téléchargez `orosmo-<plateforme>[.exe]` d'une release, placez-le dans `~/.orosmo/bin/orosmo` (rendez-le
exécutable), ajoutez ce dossier au `PATH`, puis `orosmo doctor --fix` (paquets système) et `orosmo install latest`.
**Désinstaller** : supprimez `$OROSMO_HOME` (`~/.orosmo`, `%LOCALAPPDATA%\Orosmo`) et la ligne `PATH` ajoutée.

## 2. Ce que contient une release

| Fichier | Rôle |
| --- | --- |
| `install.sh`, `install.ps1` | installeurs (§1) |
| `orosmo-<plateforme>[.exe]` | l'outil en ligne de commande (§3) |
| `orosmo-pkg-<plateforme>.zip` | le moteur : en-têtes, bibliothèques statiques debug et release, **`deps/`** (bibliothèques tierces précompilées, statiques : GLFW, glad, FreeType, msdfgen, shaderc, volk, ktx, meshoptimizer…), `make/engine.mk`, `cmake/OrosmoConfig.cmake`, compilateur `cui`, shaders du RHI, police et thèmes, exemples, `VERSION` |
| `SHA256SUMS` | empreintes des fichiers ci-dessus, vérifiées par `orosmo install` et `orosmo self-update` |

Plateformes : `linux-x64`, `linux-arm64`, `windows-x64` (MinGW), `macos-arm64`, `macos-x64`. Jamais embarqués dans `deps/` :
chargeur Vulkan, OpenGL, X11 et Wayland, libc (bibliothèques du système). Le contenu détaillé du paquet est dans son `README.md`
(copie de `tools/engine-sdk/dist-README.md`).

Disposition sur le disque (`OROSMO_HOME`, sinon `~/.orosmo` ou `%LOCALAPPDATA%\Orosmo`) :

```
$OROSMO_HOME/bin/orosmo[.exe]
$OROSMO_HOME/engines/<x.y.z>/     un paquet extrait tel quel ; rien ne l'écrit pendant un build
```

### Prérequis

| Système | Compilateur (C++23) | Outils | Graphique |
| --- | --- | --- | --- |
| Linux (vérifié : Ubuntu 24.04, g++ 13.3) | g++ 13+ ou clang++ 17+ | GNU Make ; **glibc ≥ 2.39** (Ubuntu 24.04 et plus récent, G217) | OpenGL 4.5 (ou 4.3 + `GL_ARB_clip_control`), Vulkan 1.3 conseillé, X11 ou Wayland |
| Windows (non vérifié, G216) | MinGW-w64 g++ 13+ (MSYS2 MINGW64) | `mingw32-make` | OpenGL 4.5 |
| macOS (CI sur machine virtuelle seulement, G520 à G524) | outils en ligne de commande Xcode (Apple clang) | GNU Make | Vulkan par MoltenVK seulement : `brew install vulkan-loader molten-vk` (non embarqués, A76) ; OpenGL s'arrête à 4.1, donc aucun repli |

Ni Rust, ni Python, ni vcpkg ne sont nécessaires (Python 3 ajoute la barre de progression de `make`).

**macOS** : aucune variable d'environnement à poser. `engine.mk` et `OrosmoConfig.cmake` donnent aux exécutables des chemins de
recherche (`ENGINE_VULKAN_RPATHS` / `OROSMO_VULKAN_RPATHS`, défaut `/opt/homebrew/lib /usr/local/lib`) par lesquels volk et GLFW
trouvent le chargeur ; le chargeur de Homebrew trouve seul MoltenVK. `orosmo doctor` contrôle les deux. Pour un chargeur ailleurs,
redéfinissez la variable ; pour un autre pilote, `VK_DRIVER_FILES`. Sans chargeur ou sans pilote, le jeu s'arrête avec la raison et
la commande à lancer.

## 3. Créer et construire un projet

```bash
orosmo doctor                 # compilateur C++23, make, moteurs installés, affichage, Vulkan ; --fix propose les paquets
orosmo init mygame            # installe la dernière version si besoin ; config.mk : OROSMO_VERSION := x.y.z
cd mygame
orosmo build                  # make -jN debug -> bin/debug/main (--release : bin/release/<PROJECT_NAME>)
orosmo run -- --headless      # build puis lance le programme ; les arguments après -- lui sont passés
make debug && make run        # make marche aussi ; il appelle seulement `orosmo path` pour trouver le moteur
make compile-commands         # compile_commands.json pour clangd / VS Code
```

`orosmo init` crée `src/` (projet de démarrage : scène 3D RHI + panneau `.cui`), `Makefile` et `.gitignore` (suivis par
`orosmo update`), `config.mk` (nom, version, `OROSMO_VERSION` : à vous) et `config.local.mk` (réglages de la machine, ignoré par
git). La sortie du compilateur `.cui` va dans `Generated/` du projet (ignoré par git).

Le `Makefile` trouve le moteur (`ENGINE_DIR`) dans cet ordre : `ENGINE_DIR` donnée (ligne de commande, `config.local.mk`) ; dossier
`orosmo/` du projet ; `orosmo path $(OROSMO_VERSION)` ; sinon il s'arrête avec la commande à lancer (`orosmo install x.y.z`).

### Logique de jeu (`src/game/`) et reconstruction rapide

`src/game/*.cpp` est compilé en bibliothèque rechargée à chaud (debug). Programme lancé, après une modification :
`orosmo build --game` (ou `make game`). Seule la bibliothèque est refaite ; `ccache` est utilisé s'il est installé
(`make CCACHE=` le désactive). Après `make dev` : `make game BUILD_TYPE=dev`. Incluez `<orosmo/game.hpp>` : composants, systèmes, actions et types mathématiques, sans le
rendu ni `glm` complet. Un en-tête partagé avec `src/main.cpp` (`src/game/spin.hpp`) dont on change les données demande un
`orosmo build` complet et un redémarrage. `core/reflection.hpp` et `scene/transform.hpp` ne tirent plus que les types de
`glm` : pour `glm::normalize`, les matrices de projection, etc., incluez `<orosmo/core/math.hpp>` ou `<glm/glm.hpp>`.

### Commandes de l'outil

| Commande | Effet |
| --- | --- |
| `orosmo install [version\|latest] [--force]` | télécharge le paquet, vérifie `SHA256SUMS`, l'extrait dans `engines/<version>` (dossier temporaire puis renommage) |
| `orosmo list` / `orosmo remove <version>` | versions installées (celle du projet courant marquée) / suppression |
| `orosmo path <version>` | chemin du moteur ; code 1 s'il n'est pas installé (utilisé par le `Makefile`) |
| `orosmo init [nom] [--vendor]` | crée un projet (dossier courant sans nom) ; `--vendor` copie le moteur dans `orosmo/` (ancien mode) |
| `orosmo update [chemin]` | (projet Orosmo : `orosmo/`, `OROSMO_VERSION` dans `config.mk` ou `Makefile` d'`orosmo init`) passe le projet à la dernière version (`OROSMO_VERSION`), met à jour `Makefile` et `.gitignore` s'ils sont intacts (sinon confirmation `y/N`), ajoute à `config.mk` les champs nouveaux |
| `orosmo migrate [chemin]` | ancien projet avec `orosmo/` → moteur partagé ; `orosmo/` renommé `orosmo.old/`, supprimé seulement si le build (avec le moteur installé) réussit, sinon la migration est annulée |
| `orosmo build [--release] [--json] [-C dir]` | `make -jN debug` (ou `release`) ; `--json` : une ligne JSON par événement |
| `orosmo build --game [--json] [-C dir]` | `make -jN game` : ne reconstruit que la bibliothèque de jeu (`src/game/`), que le programme en cours recharge |
| `orosmo run [--release] [-C dir] [-- args…]` | build, puis lance le programme avec `args` |
| `orosmo clean [-C dir]` | `make clean` |
| `orosmo self-update [--force]` | remplace l'outil par celui de la dernière release (empreinte vérifiée) |
| `orosmo doctor [--json] [--fix] [chemin]` | contrôle de l'environnement ; `--fix` affiche la commande du gestionnaire de paquets et la lance après confirmation |
| `--package <orosmo-pkg.zip>` | (`install`, `init`, `update`) paquet local au lieu du téléchargement, sans réseau ni vérification d'empreinte |

Hors ligne, `install latest` échoue : sans réseau, un paquet s'installe avec `--package <zip>`. `init` et `update` (sans
`--package`) prennent la plus récente version déjà installée, avec un avertissement ; sans version installée, il faut fournir
le paquet avec `--package`. `OROSMO_DOWNLOAD_BASE` (miroir, tests) est un dossier local ou une URL `https://` : `http://` est
refusé, et l'outil ne suit pas une redirection vers `http://`. Format de `build --json` : une ligne par événement, `{"event":"begin","label":…,"shown":…,"target":…}`,
`{"event":"end","target":…}`, `{"event":"diagnostic","level":…,"text":…}` (avec `file`, `line`, `column`, `message` quand la
ligne a la forme `fichier:ligne:colonne: niveau: message`), puis `{"event":"done","code":…,"compiled":…,"failed":[…],"elapsed_ms":…}` ;
le code de sortie est celui de `make`.

### Mettre à jour, migrer, figer

- **Mettre à jour** : `orosmo update` change `OROSMO_VERSION` ; les autres projets gardent la leur. Changer la ligne à la main et
  lancer `orosmo install x.y.z` revient au même.
- **Ancien projet** (moteur copié dans `orosmo/`, dépendances vcpkg dans `orosmo/vcpkg_installed`) : `orosmo migrate`. La même
  version du moteur est installée si le paquet copié a un fichier `VERSION`, sinon la dernière. Le build de validation impose ce
  moteur (`ENGINE_DIR` sur la ligne de commande de `make`, même si `config.local.mk` en fixe un autre). Si le lancement de `make` ou le build échoue (ou une étape d'avant), la migration est annulée : `orosmo/`, `Makefile`, `.gitignore`, `config.mk` et `config.local.mk` reprennent leur état d'avant.
- **Projet figé ou hors ligne** : `orosmo init --vendor` (ou garder `orosmo/`). `orosmo update` remplace alors `orosmo/` par-dessus,
  comme avant la `v0.6.2` : les fichiers retirés d'une release restent en place ; pour un moteur identique à une installation neuve,
  videz `orosmo/` avant `update`.
- **Bibliothèques du jeu** : `make add-dep DEP=<paquet vcpkg>` (vcpkg requis seulement pour cela), installées dans `vcpkg_installed/`
  du projet ; `make install-deps` réinstalle celles du `vcpkg.json` du projet.

### Avec le paquet seul (`ENGINE_DIR`)

Décompressez `orosmo-pkg-<plateforme>.zip` où vous voulez, puis, dans le `Makefile` de votre projet :

```makefile
ENGINE_DIR   := chemin/vers/engine-pkg      # le dossier décompressé, ou $(shell orosmo path 0.6.2)
GAME_SRC_DIR := src                          # les .cui de ce dossier sont compilés automatiquement
BUILD_TYPE   ?= release                      # debug | dev | release
include $(ENGINE_DIR)/make/engine.mk
```

`engine.mk` fournit `ENGINE_CXXFLAGS`, `ENGINE_INCLUDES`, `ENGINE_LIBS` / `ENGINE_LINK_LIBS`, `ENGINE_LDFLAGS`, `ENGINE_EXE_EXT` et
les règles du langage d'interface (`GEN_CPP_SOURCES`, sortie dans `ENGINE_GEN_DIR`, `Generated` du projet par défaut). Les
bibliothèques tierces sont lues dans `ENGINE_DEPS_DIR` : `$(ENGINE_DIR)/deps` d'un paquet ; `ENGINE_VCPKG_ROOT=<dossier>` impose
`<dossier>/<triplet>` à la place.

### Avec CMake (`find_package(Orosmo)`)

Le paquet inclut `cmake/OrosmoConfig.cmake` et `cmake/OrosmoConfigVersion.cmake` (consommation optionnelle sans remplacer le build Make interne du moteur) :

```cmake
cmake_minimum_required(VERSION 3.20)
project(mygame LANGUAGES CXX)

find_package(Orosmo REQUIRED)   # cmake -DCMAKE_PREFIX_PATH="$(orosmo path 0.6.2)" ...

add_executable(mygame src/main.cpp)
orosmo_add_cui_views(mygame src/HudView.cui)
target_link_libraries(mygame PRIVATE Orosmo::Orosmo)
```

Variables utiles : `OROSMO_VCPKG_ROOT` (dépendances ailleurs que dans `deps/`), `OROSMO_VCPKG_TRIPLET`, `OROSMO_SHADERS_DIR`,
`OROSMO_RES_DIR`, `OROSMO_GEN_DIR` (défaut : `orosmo_generated` du dossier de build).

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
liste des changements incompatibles est dans `docs/releases/<jalon>.md`) ; un **correctif** ne la casse jamais. Dernière release : `v0.6.2` (SDK installé une fois par machine, A76) ; `v0.6.0` : M6, import glTF, PBR, animation. M3 (`v0.3.0`) n'a pas été publiée ; `v0.3.1` (M3 bis) est la première release publiée.

| Module (`include/orosmo/…`) | Statut | Remarque |
| --- | --- | --- |
| `core`, `rhi`, `renderer`, `ui`, `platform` | **figé pour 0.6.x** | livré dans la release ; noms selon `docs/03` §7 (`camelCase`, `kPascalCase`, fichiers `snake_case`) |
| `core` : `Logger`, `Profiler`, `FPSCounter`, `RingBuffer` | **repris tels quels** | anciens modules `Utilities` et `Profiler` déplacés dans `orosmo::core` (2026-11-20) : noms de méthodes encore en `PascalCase` (G122) |
| `world`, `TerrainRenderer` | **archivé (`archive/procedural-world`)** | retirés du dépôt principal (A37) ; futur module/addon en M11 |

## 8. Limites connues

- **Backend graphique** : OpenGL (défaut) et Vulkan ; Metal n'existe pas avant M9 et est refusé avec un message. Choix par
  `--backend <opengl|vulkan|metal>` (alias de `--api`, casse indifférente, `--backend=vulkan` accepté). Si Vulkan ne démarre pas (pas
  de chargeur, pas de GPU Vulkan 1.3), la raison s'affiche sur stderr et la démo retombe sur OpenGL ; `--strict-backend` supprime ce
  repli (code de sortie non nul). Sous macOS il n'y a jamais de repli (OpenGL 4.1) : voir §2.
- **Texte** : `TextRendererFont` affiche l'ASCII (REN-36) ; pas de composition IME affichée (PLAT-05, partiel, G107) ; la sélection et le presse-papiers existent dans les champs de
  texte (`ui::useSystemClipboard`, voir `docs/UI_LANGUAGE.md`).
- **Interface** : pas de rechargement à chaud de la vue elle-même (le thème se recharge) ; pas de navigation à la manette.
- **Windows, macOS** : construits et testés par la CI, jamais essayés sur une vraie machine ; les problèmes sont suivis dans `docs/GAPS.md`.
