# C++ Game Engine — Release Package

This directory is a self-contained engine release. It contains everything needed
to build a game project against this engine without having to compile the engine itself.

## Contents

```
engine-<platform>/
  includes/
    engine/          — public API (stable within a minor version, see docs/USING_THE_ENGINE.md in the repository):
      core/            errors (std::expected), logger, tasks, file watcher, reflection + JSON/MessagePack serialization
      platform/        Window, InputManager (names still PascalCase: will change, see "API stability")
      rhi/             graphics abstraction (Device, CommandEncoder, handles) — OpenGL 4.3+ backend before M4
      renderer/        SceneRenderer, Camera (reverse-Z), CanvasRenderer, DrawList, TerrainRenderer, DebugDraw
      world/           deterministic noise, terrain tiles, LOD streaming, GPU noise, export (PNG16/EXR/OBJ/glTF/STL)
      ui/              engine::ui — widgets, layout, JSON themes, reflected parameter panels
  lib/
    debug/, release/ — static archives: libcore, libplatform, librhi, librenderer, libworld, libui, … (link them all:
                       ENGINE_LINK_LIBS)
  make/engine.mk     — include this in your project Makefile
  tools/cui[.exe]    — interface language compiler (.cui → C++), run by engine.mk
  shaders/rhi/       — GLSL 450 shaders of the RHI renderers: copy them next to your executable (res/shader/rhi/)
  res/fonts, res/themes — default font and the dark/light themes of engine::ui
  examples/
    minimal/         — the smallest program linked to the engine (one terrain tile, no window): make -C examples/minimal
    terrain_gen/     — command-line generator (no window, no GPU):  make -C examples/terrain_gen
    terrain_viewer/  — viewer with a .cui control panel:            make -C examples/terrain_viewer
    ui_gallery/      — every widget, layout, event and theme of engine::ui: make -C examples/ui_gallery
  vcpkg.json         — third-party dependencies (make install in a project created by `engine init`)
  .gitignore, README.md
```

## Prerequisites

| Tool | Minimum version | Notes |
|------|----------------|-------|
| GNU Make | 4.x | `make --version` |
| g++ or clang++ | g++ 13 (verified: 13.3) or clang++ 17 | C++23 (`std::expected`, `<stacktrace>`); set `CXX` to override |
| vcpkg | any | only needed for `make install` |

No Python, no Rust toolchain required. The CUI precompiler ships as a native binary.

## Quick start

### 1 — Point your Makefile at the engine

```makefile
ENGINE_DIR   := path/to/engine-<platform>   # this directory
GAME_SRC_DIR := src
BUILD_TYPE   ?= release                      # debug | dev | release
include $(ENGINE_DIR)/make/engine.mk
```

### 2 — Build

```sh
make release    # optimised build  →  bin/release/<name>
make debug      # debug build      →  bin/debug/main
make run        # build + run
```

## Complete Makefile example

Save this as `Makefile` at the root of your game project:

```makefile
SHELL := /bin/sh
-include config.mk          # optional: PROJECT_NAME, VERSION, …

CXX ?= g++
CC  ?= gcc

ENGINE_DIR   := path/to/engine-<platform>
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

Both examples build from this package alone: `ENGINE_DIR` defaults to the package root when they sit in `examples/`.
A program that renders needs the RHI shaders and, for `engine::ui`, a font and a theme at run time: the examples copy
`shaders/rhi`, `res/fonts` and `res/themes` into `bin/res/`. The template project created by `engine init` does the same.

## Interface language

`.cui` files (each starts with `import "engine/ui"`, the only target) describe an `engine::ui` panel: widgets, two-way bindings (`$state.value`),
events that name C++ functions (`.onClick(state.regenerate)`), local theme overrides by token. Errors are reported as
`file:line:column: error: message`, and `#line` directives map C++ errors back to the `.cui` line. Specification:
`docs/UI_LANGUAGE.md` in the engine repository.
