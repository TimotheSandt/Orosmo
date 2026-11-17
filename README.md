# ProceduralGeneration

Moteur de jeu 3D et 2D en **C++23** (rendu OpenGL et Vulkan, Metal prévu), avec une interface décrite dans un langage maison (`.cui`) précompilé en C++. Le premier produit visé est un générateur de terrain et de planètes procédural.

> Travail en cours : l'architecture et les API évoluent. Spécification, checklist et plan : [docs/](docs/README.md). Règles de travail : [CLAUDE.md](CLAUDE.md).

## Prérequis

- **Compilateur** : GCC 13+ ou Clang 17+ (testé : GCC 13.3, Linux), MSVC 2022 / MinGW sous Windows.
- **Outils** : GNU Make, CMake, Rust (`cargo`, pour `tools/cui-rs` et `tools/engine-init-rs`), Python 3.
- **Pilotes** : OpenGL 4.3+ et Vulkan ; couches de validation Vulkan conseillées.
- **Dépendances** : glfw3, glm, stb, freetype, vulkan, shaderc, glad (v1), msdfgen — liste dans [vcpkg.json](vcpkg.json).

## Compiler

### Linux (apt d'abord, vcpkg en repli)

```bash
sudo apt install build-essential cmake cargo python3 \
    libglfw3-dev libglm-dev libstb-dev libshaderc-dev glslc libfreetype-dev \
    libvulkan-dev vulkan-validationlayers libgl-dev libx11-dev \
    autoconf autoconf-archive automake libtool

# glad (v1) et msdfgen n'existent pas dans apt : vcpkg
git clone https://github.com/microsoft/vcpkg ~/vcpkg && ~/vcpkg/bootstrap-vcpkg.sh
~/vcpkg/vcpkg install glad msdfgen --triplet x64-linux

tools/setup-linux-deps.sh        # relie apt + vcpkg dans vcpkg_installed/x64-linux
make all-debug                   # moteur (debug) -> apps/demo/engine -> démo (debug)
```

### Windows / macOS

```bash
make install-deps-all            # vcpkg (triplets x64-mingw-static, x64-osx, arm64-osx)
make all-debug
```
(Windows : vérifié avant M00 seulement ; macOS : non vérifié, voir [docs/GAPS.md](docs/GAPS.md).)

## Lancer, tester

| Commande | Effet |
| --- | --- |
| `make run` | Construit si besoin puis lance la démo (`apps/demo/bin/<type>/`) |
| `make demo-test` | Construit et lance les tests (`tests/`, framework maison) |
| `make debug` / `make release` | Archives du moteur dans `lib/<type>/` |
| `make all-debug` / `make all-release` | Moteur → `apps/demo/engine/` → démo |
| `make sync-demo` | Copie le moteur construit dans `apps/demo/engine/` |
| `make dist` / `make engine-tool` | Paquet du moteur / outil `engine init` |
| `make clean` / `make fclean` / `make demo-fclean` | Nettoyage |
| `tools/check-repo.sh` | Contrôle d'hygiène du dépôt (binaires, casse, CRLF) |
| `tools/format_sources.sh` | Formatage (`clang-format`) |

## Organisation

```
include/      en-têtes publics du moteur, par module (Graphics, UI, Noise, Profiler, Utilities, Benchmark)
src/          implémentations, même découpage ; README.md par module
apps/demo/    application de démonstration (sources, Makefile, installeurs, benchmark)
tests/        tests (framework maison)
shaders/      shaders GLSL (copiés vers <bin>/res/shader/)
res/          polices, icône (copiés vers <bin>/res/)
mk/           Makefile du moteur (engine.mk) et configuration (config.mk)
tools/        précompilateur .cui (cui-rs), outil engine-init-rs, SDK (engine-sdk), scripts, extension VS Code
docs/         spécification, plan, rapports (voir docs/README.md)
```

Le moteur compile en bibliothèques statiques (`libgraphics.a`, `libui.a`, `libnoise.a`, `libprofiler.a`, `libutilities.a`, `libthirdparty.a`, `libbenchmark.a`) ; la démo les relie via `apps/demo/engine/`.

## Documentation

- Vision, checklist, architecture : [docs/](docs/README.md)
- Backends graphiques : [docs/GRAPHICS_BACKENDS.md](docs/GRAPHICS_BACKENDS.md)
- Langage d'interface : [docs/UI_DSL_Spec.md](docs/UI_DSL_Spec.md)
- Nettoyage M00 : [docs/CLEANUP_PLAN.md](docs/CLEANUP_PLAN.md), [docs/releases/M00.md](docs/releases/M00.md)

## Licence

À décider (Apache-2.0 envisagée). Aucun fichier `LICENSE` pour l'instant.
