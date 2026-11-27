# engine.mk — C++ Game Engine integration file.
#
# ── Setup ─────────────────────────────────────────────────────────────────────
# Install an engine version with `orosmo install <version>` (or unpack a release package), then in your Makefile:
#
#   ENGINE_DIR   := path/to/engine   # directory containing includes/ lib/ deps/ make/ tools/ (`orosmo path <version>`)
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
#   ENGINE_LDFLAGS       — -L and -l flags for all engine dependencies (third-party libraries of ENGINE_DEPS_DIR)
#   ENGINE_DEPS_DIR      — third-party libraries of the engine ({include,lib}/): $(ENGINE_DIR)/deps in a release package;
#                          without deps/ (engine repository, older packages): $(ENGINE_DIR)/vcpkg_installed/<triplet>;
#                          ENGINE_VCPKG_ROOT=<dir> forces <dir>/<triplet>
#   GEN_CPP_SOURCES      — .gen.cpp files produced by the CUI precompiler
#   GEN_REDIRECT_SOURCES — .cui redirect shims (must exist before compilation)
#   ENGINE_GEN_DIR       — output of the CUI precompiler, in the PROJECT (default: Generated, overridable; already included in
#                          ENGINE_INCLUDES). Nothing is ever written under ENGINE_DIR: one installed engine serves every project.
#   ENGINE_MODULE_EXT, ENGINE_MODULE_CXXFLAGS, engine_host_ldflags, engine_module_ldflags, engine_count_exports
#                        — a library loaded at run time that uses the host's engine (see "Library loaded at run time" below)
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
# (`@@ors begin <cible absolue> <affiché> <étiquette>` / `@@ors end <cible>`) et la commande n'est plus affichée (V=1 la montre). Sans
# ORS_PROGRESS (CI, redirection) : commande complète et étiquette `  CXX fichier`, comme avant.
ORS_Q := $(if $(ORS_PROGRESS),$(if $(filter 1,$(V)),,@))
# Apostrophes des valeurs (chemin du dépôt, noms de fichiers) protégées : le texte reste entre apostrophes dans le printf.
_ors_sq = $(subst ','\'',$(1))
ors_compile = @printf '%s\n' '$(call _ors_sq,$(if $(ORS_PROGRESS),@@ors begin $(abspath $@) $< $(1),  $(1) $<))'
ors_link = @printf '%s\n' '$(call _ors_sq,$(if $(ORS_PROGRESS),@@ors begin $(abspath $@) $@ $(1),  $(1) $@))'
ors_end = $(if $(ORS_PROGRESS),@printf '%s\n' '$(call _ors_sq,@@ors end $(abspath $@))')

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
    _ENG_VCPKG_LIBS      := -lvolk -lglfw3 -lglad -lmsdfgen-core -lfreetype \
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
    _ENG_VCPKG_LIBS      := -lvolk -lglfw3 -lglad -lGL -lmsdfgen-core $(_ENG_STACKTRACE_LIB) \
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
    _ENG_VCPKG_LIBS      := -lvolk -lglfw3 -lglad -lmsdfgen-core -lfreetype -lpng16 -lz -lbz2 \
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
FPFLAGS    := -ffp-contract=off # déterminisme flottant (même réglage que le moteur)

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
# Third-party libraries, by priority: ENGINE_VCPKG_ROOT given (vcpkg shared elsewhere); deps/ of a release package (prebuilt
# by the CI, static, no vcpkg needed); vcpkg_installed/<triplet> (engine repository, packages older than deps/).
ifdef ENGINE_VCPKG_ROOT
    ENGINE_DEPS_DIR := $(ENGINE_VCPKG_ROOT)/$(ENGINE_VCPKG_TRIPLET)
else ifneq ($(wildcard $(ENGINE_DIR)/deps/lib),)
    ENGINE_DEPS_DIR := $(ENGINE_DIR)/deps
    _ENG_DEPS_PACKAGED := 1
else
    ENGINE_DEPS_DIR := $(ENGINE_DIR)/vcpkg_installed/$(ENGINE_VCPKG_TRIPLET)
endif
# CUI output: in the project, never in ENGINE_DIR (the installed engine is read-only and shared).
ENGINE_GEN_DIR ?= Generated

ENGINE_INCLUDES := \
    -I$(_ENG_INC_BASE) \
    $(foreach d,$(_ENG_INC_DIRS),-I$(_ENG_INC_BASE)/$(d)) \
    -isystem $(ENGINE_DEPS_DIR)/include \
    -I$(ENGINE_GEN_DIR) \
    -I$(GAME_SRC_DIR)

# ── Archive paths ─────────────────────────────────────────────────────────────

_ENG_LIB_DIR := $(ENGINE_DIR)/lib/$(BUILD_TYPE)

# Optional extra static archives under $(ENGINE_DIR)/lib/$(BUILD_TYPE)/ (e.g. in-repo modules such as world terrain_renderer).
ENGINE_EXTRA_MODULES ?=

ENGINE_LIBS := \
    $(_ENG_LIB_DIR)/libui.a \
    $(_ENG_LIB_DIR)/libcore.a \
    $(_ENG_LIB_DIR)/libplatform.a \
    $(_ENG_LIB_DIR)/librhi.a \
    $(_ENG_LIB_DIR)/librenderer.a \
    $(_ENG_LIB_DIR)/libassets.a \
    $(_ENG_LIB_DIR)/libanim.a \
    $(_ENG_LIB_DIR)/libscene.a \
    $(_ENG_LIB_DIR)/libsim.a \
    $(_ENG_LIB_DIR)/libthirdparty.a \
    $(foreach m,$(ENGINE_EXTRA_MODULES),$(_ENG_LIB_DIR)/lib$(m).a)

# Wrap archives for cross-archive symbol resolution (--start-group / --end-group).
# Use ENGINE_LINK_LIBS in your link command instead of ENGINE_LIBS directly.
ENGINE_LINK_LIBS := $(_ENG_LD_START) $(ENGINE_LIBS) $(_ENG_LD_END)

comma := ,
# Assets (M6 lot G) : KTX2 (libktx, qui dépend d'astcenc et de zstd) et meshoptimizer. Le nom de l'archive astcenc dépend du jeu
# d'instructions choisi par vcpkg (avx2, sse4.1, neon…) : on prend celle qui est installée.
_ENG_ASTCENC_LIB := $(patsubst lib%.a,-l%,$(notdir $(firstword $(wildcard $(ENGINE_DEPS_DIR)/lib/libastcenc-*-static.a))))
_ENG_ASSET_LIBS  := -lktx $(_ENG_ASTCENC_LIB) -lzstd -lmeshoptimizer

# macOS rpath to vcpkg's vulkan-loader dylib: only without deps/ (the Vulkan loader is never shipped in deps/: system install).
ENGINE_LDFLAGS := -L$(ENGINE_DEPS_DIR)/lib $(_ENG_ASSET_LIBS) $(_ENG_VCPKG_LIBS) \
    $(if $(and $(_ENG_DARWIN),$(if $(_ENG_DEPS_PACKAGED),,1)),-Wl$(comma)-rpath$(comma)$(abspath $(ENGINE_DEPS_DIR))/lib)

# ── Library loaded at run time by a host executable (A79 lot L1; the game library of GAME-01) ─────────────────────────────
# The library does NOT embed the engine: its engine symbols stay undefined and are resolved in the host executable when it is
# loaded (platform::DynamicLibrary), so both share one engine (same Logger, same registries). Proof:
# tests/platform/dynamic_library_tests.cpp with tests/platform/test_module/.
#   Host executable: add $(call engine_host_ldflags,<host exe path>) to its link command.
#   Library: compile with $(ENGINE_MODULE_CXXFLAGS), link with $(call engine_module_ldflags,<host exe path>), no ENGINE_LIBS;
#            file name <name>$(ENGINE_MODULE_EXT). On Windows the host must be linked first (its import library is used).
#   Linux   host -rdynamic (exports its symbols); library -shared, undefined symbols allowed.
#   macOS   an executable exports its global symbols by default; library -undefined dynamic_lookup (looked up in the host
#           when loaded; -no_fixup_chains: dynamic_lookup is not reliable with chained fixups).
#   Windows host --export-all-symbols and an import library (<exe without .exe>.host.a); the library links against it. A PE
#           image exports at most 65,535 symbols: `engine_count_exports` prints the count (apps/demo/Makefile, test link).
ifeq ($(ENGINE_OS),Windows)
    ENGINE_MODULE_EXT      := .dll
    ENGINE_MODULE_CXXFLAGS :=
    engine_host_implib      = $(basename $(1)).host.a
    engine_host_ldflags     = -Wl,--export-all-symbols -Wl,--out-implib,$(call engine_host_implib,$(1))
    engine_module_ldflags   = -shared $(call engine_host_implib,$(1))
    # objdump -p : « Export Address Table  0000xxxx » (hexadécimal)
    engine_count_exports    = printf '%d\n' 0x$$(objdump -p "$(1)" | sed -n 's/^[[:space:]]*Export Address Table[[:space:]]*\([0-9a-fA-F]*\).*/\1/p' | head -n 1)
else ifeq ($(ENGINE_OS),Darwin)
    ENGINE_MODULE_EXT      := .dylib
    ENGINE_MODULE_CXXFLAGS := -fPIC
    engine_host_implib      =
    engine_host_ldflags     =
    engine_module_ldflags   = -dynamiclib -undefined dynamic_lookup -Wl,-no_fixup_chains
    engine_count_exports    = nm -gU "$(1)" | wc -l | tr -d ' '
else
    ENGINE_MODULE_EXT      := .so
    # GCC: no STB_GNU_UNIQUE symbols (static locals of inline functions, static members of templates). They stay unique in
    # the process despite RTLD_LOCAL and make dlclose a no-op: a reloaded library would keep the old one's state. Clang
    # does not emit them (and does not know the option).
    _ENG_CXX_IS_GCC        := $(shell $(CXX) -v 2>&1 | grep -q '^gcc version' && echo 1)
    ENGINE_MODULE_CXXFLAGS := -fPIC $(if $(_ENG_CXX_IS_GCC),-fno-gnu-unique)
    engine_host_implib      =
    engine_host_ldflags     = -rdynamic
    engine_module_ldflags   = -shared
    engine_count_exports    = nm -D --defined-only "$(1)" | wc -l | tr -d ' '
endif

# ── Missing archive error ─────────────────────────────────────────────────────
# Archives must come from a GitHub release or be built with mk/engine.mk.
$(ENGINE_LIBS):
	$(error Missing engine archive: $@ -- install the engine with `orosmo install <version>` (releases: https://github.com/TimotheSandt/Orosmo/releases) or build it in the engine repository: make -f mk/engine.mk BUILD_TYPE=$(BUILD_TYPE))

# ── CUI precompiler ───────────────────────────────────────────────────────────

# Generated/ itself is skipped (its redirect shims are .cui files) when it lies under GAME_SRC_DIR (GAME_SRC_DIR := . or
# ENGINE_GEN_DIR := src/Generated). ENGINE_GEN_DIR is relative to the project root, like the paths find prints (`src/…`, or
# `./…` when GAME_SRC_DIR starts with `.`): both spellings are pruned.
_ENG_GEN_PRUNE   := $(patsubst ./%,%,$(ENGINE_GEN_DIR))
_ENG_CUI_SRCS    := $(shell find $(GAME_SRC_DIR) \( -path "$(_ENG_GEN_PRUNE)" -o -path "./$(_ENG_GEN_PRUNE)" \) -prune -o -type f -name "*.cui" -print 2>/dev/null)

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
