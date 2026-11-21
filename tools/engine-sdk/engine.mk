# engine.mk — C++ Game Engine integration file.
#
# ── Setup ─────────────────────────────────────────────────────────────────────
# Download a release from GitHub and unpack it, then in your Makefile:
#
#   ENGINE_DIR   := path/to/engine   # directory containing includes/ lib/ make/ tools/
#   GAME_SRC_DIR := src              # directory with your .cpp and .cui files
#   BUILD_TYPE   := debug            # debug | release
#   include $(ENGINE_DIR)/make/engine.mk
#
# ── What this file provides ───────────────────────────────────────────────────
#   ENGINE_OS            — Windows | Linux | Darwin
#   ENGINE_EXE_EXT       — .exe (Windows) or empty
#   ENGINE_ARCH_FLAG     — -m64 or empty (ARM)
#   ENGINE_PLATFORM_DEFS — -DNOMINMAX -DWIN32_LEAN_AND_MEAN (Windows) or empty
#   ENGINE_CXXFLAGS      — recommended CXXFLAGS for the chosen BUILD_TYPE
#   ENGINE_INCLUDES      — all -I flags needed to use the engine headers
#   ENGINE_LIBS          — list of .a archive paths
#   ENGINE_LINK_LIBS     — $(ENGINE_LIBS) wrapped in --start-group (use in link command)
#   ENGINE_LDFLAGS       — -L and -l flags for all engine dependencies (vcpkg)
#   GEN_CPP_SOURCES      — .gen.cpp files produced by the CUI precompiler
#   GEN_REDIRECT_SOURCES — .cui redirect shims (must exist before compilation)
#   ENGINE_GEN_DIR       — Generated/ directory (already included in ENGINE_INCLUDES)
#
# ── Minimal Makefile example ──────────────────────────────────────────────────
#   ENGINE_DIR   := path/to/engine
#   GAME_SRC_DIR := src
#   BUILD_TYPE   := debug
#   include $(ENGINE_DIR)/make/engine.mk
#
#   SRCS    := $(shell find src -name "*.cpp")
#   OBJS    := $(SRCS:src/%.cpp=obj/%.o) \
#              $(GEN_CPP_SOURCES:$(ENGINE_GEN_DIR)/%.gen.cpp=obj/gen/%.o)
#   TARGET  := bin/mygame$(ENGINE_EXE_EXT)
#
#   $(TARGET): $(OBJS) | $(ENGINE_LIBS)
#       $(CXX) $(ENGINE_CXXFLAGS) $(OBJS) $(ENGINE_LINK_LIBS) $(ENGINE_LDFLAGS) -o $@
#
#   obj/%.o: src/%.cpp
#       @mkdir -p $(dir $@)
#       $(CXX) $(ENGINE_CXXFLAGS) -MMD -MP $(ENGINE_INCLUDES) -c $< -o $@
#
#   obj/gen/%.o: $(ENGINE_GEN_DIR)/%.gen.cpp
#       @mkdir -p $(dir $@)
#       $(CXX) $(ENGINE_CXXFLAGS) -MMD -MP $(ENGINE_INCLUDES) -c $< -o $@
#
#   $(OBJS): | $(GEN_CPP_SOURCES) $(GEN_REDIRECT_SOURCES)
#   -include $(wildcard $(OBJS:.o=.d))

# ── Defaults ──────────────────────────────────────────────────────────────────

ENGINE_DIR   ?= .
GAME_SRC_DIR ?= src
BUILD_TYPE   ?= release
CXX          ?= g++

# ── Affichage de la compilation ───────────────────────────────────────────────
# Sous `tools/build_progress.py` (ORS_PROGRESS=1, lancé par `make` dans un terminal) : chaque étape annonce son début et sa fin
# (`@@ors begin <cible> <affiché> <étiquette>` / `@@ors end <cible>`) et la commande n'est plus affichée (V=1 la montre). Sans
# ORS_PROGRESS (CI, redirection) : commande complète et étiquette `  CXX fichier`, comme avant.
ORS_Q := $(if $(ORS_PROGRESS),$(if $(filter 1,$(V)),,@))
ors_compile = @printf '%s\n' '$(if $(ORS_PROGRESS),@@ors begin $@ $< $(1),  $(1) $<)'
ors_link = @printf '%s\n' '$(if $(ORS_PROGRESS),@@ors begin $@ $@ $(1),  $(1) $@)'
ors_end = $(if $(ORS_PROGRESS),@printf '%s\n' '@@ors end $@')

# ── Platform detection ────────────────────────────────────────────────────────

_ENG_UNAME := $(shell uname -s 2>/dev/null || echo Unknown)

ifeq ($(OS),Windows_NT)
    ENGINE_OS := Windows
else ifneq ($(filter Linux,$(_ENG_UNAME)),)
    ENGINE_OS := Linux
else ifneq ($(filter Darwin,$(_ENG_UNAME)),)
    ENGINE_OS := Darwin
else ifneq ($(filter MSYS% MINGW% CYGWIN%,$(_ENG_UNAME)),)
    ENGINE_OS := Windows
else
    ENGINE_OS := Linux
endif

ifeq ($(ENGINE_OS),Windows)
    _ENG_ARCH            := x86_64
    ENGINE_VCPKG_TRIPLET ?= x64-mingw-static
    ENGINE_EXE_EXT       := .exe
    ENGINE_ARCH_FLAG     := -m64
    ENGINE_PLATFORM_DEFS := -DNOMINMAX -DWIN32_LEAN_AND_MEAN
    _ENG_VCPKG_LIBS      := -lglfw3 -lglad -lmsdfgen-core -lfreetype \
                            -lpng16 -lzs -lbz2 -lbrotlidec -lbrotlienc -lbrotlicommon \
                            -lshaderc -lshaderc_util -lglslang \
                            -lMachineIndependent -lGenericCodeGen -lOSDependent \
                            -lspirv-cross-reflect -lspirv-cross-glsl -lspirv-cross-core \
                            -lSPIRV -lSPIRV-Tools-opt -lSPIRV-Tools \
                            -lpsapi -lwinmm -lgdi32 -luser32 -lshell32 -lopengl32 -lstdc++exp
    _ENG_LD_START        := -Wl,--start-group
    _ENG_LD_END          := -Wl,--end-group

else ifeq ($(ENGINE_OS),Linux)
    _ENG_ARCH := $(shell uname -m 2>/dev/null)
    ifeq ($(_ENG_ARCH),aarch64)
        ENGINE_VCPKG_TRIPLET ?= arm64-linux
        ENGINE_ARCH_FLAG     :=
    else
        ENGINE_VCPKG_TRIPLET ?= x64-linux
        ENGINE_ARCH_FLAG     := -m64
    endif
    ENGINE_EXE_EXT       :=
    ENGINE_PLATFORM_DEFS :=
    # <stacktrace> : GCC 13 -> -lstdc++_libbacktrace, GCC 14+ -> -lstdc++exp
    _ENG_GCC_MAJOR       := $(shell $(CXX) -dumpversion 2>/dev/null | cut -d. -f1)
    _ENG_STACKTRACE_LIB  := $(if $(filter 13,$(_ENG_GCC_MAJOR)),-lstdc++_libbacktrace,-lstdc++exp)
    _ENG_VCPKG_LIBS      := -lglfw -lglad -lGL -lmsdfgen-core $(_ENG_STACKTRACE_LIB) \
                            -lfreetype -lpng16 -lz -lbz2 -lbrotlidec -lbrotlicommon \
                            -lshaderc -lshaderc_util -lglslang \
                            -lspirv-cross-reflect -lspirv-cross-glsl -lspirv-cross-core \
                            -lSPIRV-Tools-opt -lSPIRV-Tools -lSPIRV \
                            -lpthread -lX11 -ldl -lm
    _ENG_LD_START        := -Wl,--start-group
    _ENG_LD_END          := -Wl,--end-group

else # Darwin
    _ENG_ARCH := $(shell uname -m 2>/dev/null)
    ifeq ($(_ENG_ARCH),arm64)
        ENGINE_VCPKG_TRIPLET ?= arm64-osx
        ENGINE_ARCH_FLAG     :=
    else
        ENGINE_VCPKG_TRIPLET ?= x64-osx
        ENGINE_ARCH_FLAG     := -m64
    endif
    ENGINE_EXE_EXT       :=
    ENGINE_PLATFORM_DEFS :=
    _ENG_VCPKG_LIBS      := -lglfw3 -lglad -lmsdfgen-core -lfreetype -lpng16 -lz -lbz2 \
                            -lbrotlidec -lbrotlicommon \
                            -lshaderc -lshaderc_util -lglslang \
                            -lspirv-cross-reflect -lspirv-cross-glsl -lspirv-cross-core \
                            -lSPIRV-Tools-opt -lSPIRV-Tools -lSPIRV \
                            -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo \
                            -framework QuartzCore -framework Metal -framework CoreFoundation
    _ENG_LD_START        :=
    _ENG_LD_END          :=
    _ENG_DARWIN          := 1   # vcpkg's vulkan-loader is a dylib: the executables need an rpath to it
endif

# ── Compiler flags ────────────────────────────────────────────────────────────
# Same switches as mk/engine.mk: WERROR=0 keeps warnings non-fatal, SANITIZE=address,undefined.
WERROR     ?= 1
WARNFLAGS  ?= -Wall -Wextra -Wpedantic $(if $(filter 1,$(WERROR)),-Werror)
SANFLAGS   := $(if $(SANITIZE),-fsanitize=$(SANITIZE) -fno-omit-frame-pointer)
FPFLAGS    := -ffp-contract=off # déterminisme de la génération procédurale (même réglage que le moteur)

ifeq ($(BUILD_TYPE),debug)
    ENGINE_CXXFLAGS := -std=c++23 $(WARNFLAGS) $(SANFLAGS) $(FPFLAGS) $(ENGINE_ARCH_FLAG) -O1 -g -DDEBUG \
                       $(ENGINE_PLATFORM_DEFS) -DGLM_ENABLE_EXPERIMENTAL
else ifeq ($(BUILD_TYPE),dev)
    ENGINE_CXXFLAGS := -std=c++23 $(WARNFLAGS) $(SANFLAGS) $(FPFLAGS) $(ENGINE_ARCH_FLAG) -g3 -O0 -DDEBUG \
                       $(ENGINE_PLATFORM_DEFS) -DGLM_ENABLE_EXPERIMENTAL
else
    ENGINE_CXXFLAGS := -std=c++23 $(WARNFLAGS) $(SANFLAGS) $(FPFLAGS) $(ENGINE_ARCH_FLAG) -O3 -DNDEBUG \
                       $(ENGINE_PLATFORM_DEFS) -DGLM_ENABLE_EXPERIMENTAL
endif

# ── Include paths ─────────────────────────────────────────────────────────────
# Support two layouts:
#   Release package:  ENGINE_DIR/includes/          (dist output)
#   Engine repo:      ENGINE_DIR/include/ (source tree)

ifneq ($(wildcard $(ENGINE_DIR)/includes),)
    _ENG_INC_BASE := $(ENGINE_DIR)/includes
else
    _ENG_INC_BASE := $(ENGINE_DIR)/include
endif

_ENG_INC_DIRS  := $(notdir $(wildcard $(_ENG_INC_BASE)/*))
# Allow caller to set ENGINE_VCPKG_ROOT to redirect the vcpkg installation
# (useful when the engine is installed into a project but vcpkg is shared elsewhere)
ifdef ENGINE_VCPKG_ROOT
    _ENG_VCPKG_DIR := $(ENGINE_VCPKG_ROOT)/$(ENGINE_VCPKG_TRIPLET)
else
    _ENG_VCPKG_DIR := $(ENGINE_DIR)/vcpkg_installed/$(ENGINE_VCPKG_TRIPLET)
endif
ENGINE_GEN_DIR := $(ENGINE_DIR)/Generated

ENGINE_INCLUDES := \
    -I$(_ENG_INC_BASE) \
    $(foreach d,$(_ENG_INC_DIRS),-I$(_ENG_INC_BASE)/$(d)) \
    -isystem $(_ENG_VCPKG_DIR)/include \
    -I$(ENGINE_GEN_DIR) \
    -I$(GAME_SRC_DIR)

# ── Archive paths ─────────────────────────────────────────────────────────────

_ENG_LIB_DIR := $(ENGINE_DIR)/lib/$(BUILD_TYPE)

ENGINE_LIBS := \
    $(_ENG_LIB_DIR)/libui.a \
    $(_ENG_LIB_DIR)/libcore.a \
    $(_ENG_LIB_DIR)/libplatform.a \
    $(_ENG_LIB_DIR)/librhi.a \
    $(_ENG_LIB_DIR)/librenderer.a \
    $(_ENG_LIB_DIR)/libworld.a \
    $(_ENG_LIB_DIR)/libthirdparty.a

# Wrap archives for cross-archive symbol resolution (--start-group / --end-group).
# Use ENGINE_LINK_LIBS in your link command instead of ENGINE_LIBS directly.
ENGINE_LINK_LIBS := $(_ENG_LD_START) $(ENGINE_LIBS) $(_ENG_LD_END)

comma := ,
ENGINE_LDFLAGS := -L$(_ENG_VCPKG_DIR)/lib $(_ENG_VCPKG_LIBS) $(if $(_ENG_DARWIN),-Wl$(comma)-rpath$(comma)$(abspath $(_ENG_VCPKG_DIR))/lib)

# ── Missing archive error ─────────────────────────────────────────────────────
# Archives must come from a GitHub release or be built with mk/engine.mk.
$(ENGINE_LIBS):
	$(error Missing engine archive: $@$(newline)Download a release at https://github.com/TimotheSandt/Orosmo/releases$(newline)or build locally: make -f path/to/engine/mk/engine.mk BUILD_TYPE=$(BUILD_TYPE))

# ── CUI precompiler ───────────────────────────────────────────────────────────

_ENG_CUI_SRCS    := $(shell find $(GAME_SRC_DIR) -type f -name "*.cui" 2>/dev/null)

GEN_CPP_SOURCES      := $(patsubst $(GAME_SRC_DIR)/%.cui,$(ENGINE_GEN_DIR)/%.gen.cpp,$(_ENG_CUI_SRCS))
GEN_REDIRECT_SOURCES := $(patsubst $(GAME_SRC_DIR)/%.cui,$(ENGINE_GEN_DIR)/%.cui,$(_ENG_CUI_SRCS))

ifeq ($(ENGINE_OS),Windows)
    _ENG_CUI_TOOL := $(ENGINE_DIR)/tools/cui.exe
else
    _ENG_CUI_TOOL := $(ENGINE_DIR)/tools/cui
endif

# .cui → .gen.cpp + .gen.h  (cible orosmo/ui ; relancé quand la vue ou l'outil cui change : un source inchangé, au même format de
# sortie, n'est pas réécrit, grâce au hachage en tête du .gen.h)
$(ENGINE_GEN_DIR)/%.gen.cpp: $(GAME_SRC_DIR)/%.cui $(_ENG_CUI_TOOL)
	@mkdir -p "$(dir $@)"
	$(ORS_Q)$(_ENG_CUI_TOOL) run --output "$(dir $@)" $<

# Redirect shim: Generated/Foo.cui → #include "Foo.gen.h"
$(ENGINE_GEN_DIR)/%.cui: $(GAME_SRC_DIR)/%.cui
	@mkdir -p "$(dir $@)"
	$(ORS_Q)$(_ENG_CUI_TOOL) redirect --output "$(dir $@)" $<

.PHONY: cui-gen
cui-gen: $(GEN_CPP_SOURCES) $(GEN_REDIRECT_SOURCES)
