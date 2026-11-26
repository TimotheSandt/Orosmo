# Orosmo — Release Package

This directory is a self-contained engine release. It contains everything needed
to build a game project against this engine without having to compile the engine itself.

## Contents

```
orosmo-pkg-<platform>/
  includes/
    orosmo/          — public API (stable within a minor version, see docs/USING_THE_ENGINE.md in the repository):
      core/            errors (std::expected), logger, tasks, file watcher, reflection + JSON/MessagePack serialization
      platform/        orosmo::platform — Window, InputManager (camelCase API, kPascalCase enum values)
      rhi/             graphics abstraction (Device, CommandEncoder, handles) — OpenGL 4.3+ backend before M4
      renderer/        SceneRenderer, Camera (reverse-Z), CanvasRenderer, DrawList, Mesh, LightManager, DebugDraw
      ui/              orosmo::ui — widgets, layout, JSON themes, reflected parameter panels
  lib/
    debug/, release/ — static archives: libcore, libplatform, librhi, librenderer, libassets, libanim, libui, libthirdparty (link them all:
                       ENGINE_LINK_LIBS)
  deps/
    include/, lib/   — the engine's third-party libraries, prebuilt and static (GLFW, glad, volk, shaderc/glslang/SPIRV-Tools,
                       spirv-cross, freetype, msdfgen, ktx, meshoptimizer, glm, stb…): no vcpkg, no `make install`.
                       Never included: the Vulkan loader, OpenGL, X11/Wayland and libc (system packages)
    licenses/        — their licence files
  make/engine.mk     — include this in your project Makefile
  cmake/             — optional CMake package (`OrosmoConfig.cmake`, `find_package(Orosmo REQUIRED)`)
  tools/cui[.exe]    — interface language compiler (.cui → C++), run by engine.mk or CMake
  tools/gen_compile_commands.py — generates `compile_commands.json` (`make compile-commands`)
  shaders/rhi/       — GLSL 450 shaders of the RHI renderers: copy them next to your executable (res/shader/rhi/)
  res/fonts, res/themes — default font and the dark/light themes of orosmo::ui
  examples/
    minimal/         — the smallest program linked to the engine (orosmo::core tasks + reflection, no window): copy the folder
                       out of the package, then make ENGINE_DIR=/path/to/this/package
  VERSION            — engine version (x.y.z)
  vcpkg.json         — the vcpkg manifest deps/ was built from (reference only)
  .gitignore, README.md

Installed with `orosmo install <version>` in `$OROSMO_HOME/engines/<version>/` (default `~/.orosmo`, `%LOCALAPPDATA%\Orosmo` on
Windows), this directory is read-only and shared by every project: nothing is ever written into it (the CUI output goes to
`Generated/` in the project).
```

## Prerequisites

| Tool | Minimum version | Notes |
|------|----------------|-------|
| GNU Make | 4.x | `make --version` |
| g++ or clang++ | g++ 13 (verified: 13.3) or clang++ 17 | C++23 (`std::expected`, `<stacktrace>`); set `CXX` to override |
| vcpkg | any | optional: only for the game's own libraries (`make add-dep`) |

No Python, no Rust toolchain required. The CUI precompiler ships as a native binary.

## Quick start

### 1 — Point your Makefile at the engine

```makefile
ENGINE_DIR   := path/to/orosmo-pkg-<platform>   # this directory (`orosmo path <version>`)
GAME_SRC_DIR := src
BUILD_TYPE   ?= release                      # debug | dev | release
include $(ENGINE_DIR)/make/engine.mk
```

### 2 — Build

```sh
make release           # optimised build  →  bin/release/<name>
make debug             # debug build      →  bin/debug/main
make run               # build + run
make compile-commands  # generate compile_commands.json for clangd / IDEs
```

### Alternative: CMake (`find_package(Orosmo)`)

```cmake
cmake_minimum_required(VERSION 3.20)
project(mygame LANGUAGES CXX)

list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/orosmo")
find_package(Orosmo REQUIRED)

add_executable(mygame src/main.cpp)
orosmo_add_cui_views(mygame src/HudView.cui)
target_link_libraries(mygame PRIVATE Orosmo::Orosmo)
```

## Complete Makefile example

Save this as `Makefile` at the root of your game project:

```makefile
SHELL := /bin/sh
-include config.mk          # optional: PROJECT_NAME, VERSION, …

CXX ?= g++
CC  ?= gcc

ENGINE_DIR   := path/to/orosmo-pkg-<platform>
GAME_SRC_DIR := src
BUILD_TYPE   ?= release

ifneq ($(findstring debug,$(MAKECMDGOALS)),)
    BUILD_TYPE := debug
else ifneq ($(findstring dev,$(MAKECMDGOALS)),)
    BUILD_TYPE := dev
endif

include $(ENGINE_DIR)/make/engine.mk

SRC_DIR := src
OBJ_DIR := obj/$(BUILD_TYPE)
BIN_DIR := bin/$(BUILD_TYPE)

PROJECT_NAME ?= mygame
TARGET := $(BIN_DIR)/$(PROJECT_NAME)$(ENGINE_EXE_EXT)

SRCS_CPP := $(shell find $(SRC_DIR) -name "*.cpp")
OBJS_CPP := $(SRCS_CPP:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/src/%.o)
GEN_OBJS := $(GEN_CPP_SOURCES:$(ENGINE_GEN_DIR)/%.gen.cpp=$(OBJ_DIR)/gen/%.o)
ALL_OBJS := $(OBJS_CPP) $(GEN_OBJS)

.PHONY: all debug dev release run clean fclean

all debug dev release: $(TARGET)

run: $(TARGET)
	@./$(TARGET)

$(TARGET): $(ALL_OBJS) $(ENGINE_LIBS) | $(BIN_DIR)
	$(CXX) $(ENGINE_CXXFLAGS) $(ALL_OBJS) $(ENGINE_LINK_LIBS) $(ENGINE_LDFLAGS) -o $@
	@echo "Built: $@"

$(OBJ_DIR)/src/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p "$(dir $@)"
	$(CXX) $(ENGINE_CXXFLAGS) -MMD -MP $(ENGINE_INCLUDES) -c $< -o $@

$(OBJ_DIR)/gen/%.o: $(ENGINE_GEN_DIR)/%.gen.cpp
	@mkdir -p "$(dir $@)"
	$(CXX) $(ENGINE_CXXFLAGS) -MMD -MP $(ENGINE_INCLUDES) -c $< -o $@

# Generated sources must exist before any object is compiled
$(ALL_OBJS): | $(GEN_CPP_SOURCES) $(GEN_REDIRECT_SOURCES)

$(BIN_DIR):
	@mkdir -p "$@"

clean:
	@rm -rf obj/ Generated/ bin/

fclean: clean

-include $(wildcard $(ALL_OBJS:.o=.d))
```

## CUI precompiler

`.cui` files are a declarative DSL for describing UI views. The precompiler
generates `.gen.cpp` / `.gen.h` pairs consumed by the C++ build.

`engine.mk` handles CUI generation automatically — any `.cui` file found under
`GAME_SRC_DIR` is compiled before the first C++ object is built.

You never call `tools/cui` directly; Make handles it.

## Integrating with a team repository

The files in this directory are **compiled artifacts** — do not commit them to
version control. The bundled `.gitignore` already ignores everything here.

Recommended workflows:

- **GitHub Releases** (recommended): CI publishes each engine release as an
  artifact. Each developer downloads and unpacks it next to their game project,
  then sets `ENGINE_DIR` accordingly. Nothing engine-related lives inside the
  game repository.

- **Submodule**: reference the engine source repository as a submodule and run
  `make -f mk/engine.mk dist` locally. Keep the `dist/` output gitignored.

## Examples and resources

The `examples/minimal` example builds from this package alone (`ENGINE_DIR` defaults to the package root when it sits in `examples/`). For a full 3D + UI showcase, see `apps/demo/` in the Orosmo repository.
A program that renders needs the RHI shaders and, for `orosmo::ui`, a font and a theme at run time: the examples copy
`shaders/rhi`, `res/fonts` and `res/themes` into `bin/res/`. The template project created by `orosmo init` does the same.

## Interface language

`.cui` files (each starts with `import "orosmo/ui"`, the only target) describe an `orosmo::ui` panel: widgets, two-way bindings (`$state.value`),
events that name C++ functions (`.onClick(state.regenerate)`), local theme overrides by token. Errors are reported as
`file:line:column: error: message`, and `#line` directives map C++ errors back to the `.cui` line. Specification:
`docs/UI_LANGUAGE.md` in the engine repository.
