# demo/Makefile — game project build system
#
# The engine is found in ENGINE_DIR, by priority:
#   1. ENGINE_DIR given (command line, environment, config.local.mk);
#   2. orosmo/ in the project (vendored engine: older projects, `orosmo init --vendor`, the demo in the engine repository);
#   3. `orosmo path $(OROSMO_VERSION)`: the engine installed by `orosmo install <version>` (OROSMO_VERSION in config.mk);
#   otherwise a clear error. Nothing is written into ENGINE_DIR: the CUI output goes to Generated/ in the project.
#
# ── Quick start ───────────────────────────────────────────────────────────────
#   make debug        build debug binary
#   make release      build release binary
#   make run          run last built binary
#   make clean        remove objects + Generated/
#   make fclean       remove objects, Generated/, and bin/
#
# ── Game dependencies (optional, vcpkg) ───────────────────────────────────────
#   The engine's own libraries come prebuilt with the engine (ENGINE_DIR/deps): no vcpkg, no `make install` needed.
#   make add-dep DEP=nlohmann-json   add & install a package for the game (vcpkg.json of the project)
#   make install-deps                install the packages of the project's vcpkg.json
#   make search-dep Q=json           search vcpkg catalog

SHELL := sh
-include config.mk
-include config.local.mk

# ── Toolchain ─────────────────────────────────────────────────────────────────
CXX          ?= g++
CC           ?= gcc
RC           ?= windres
CLANG_FORMAT ?= clang-format
CLANG_TIDY   ?= clang-tidy
VCPKG        ?= vcpkg

# ── Engine integration ────────────────────────────────────────────────────────
# ENGINE_DIR resolution (see the top of this file). The orosmo tool: in PATH, else $(OROSMO_HOME)/bin (default ~/.orosmo/bin,
# %LOCALAPPDATA%/Orosmo/bin on Windows).
OROSMO_TOOL ?= orosmo
ifeq ($(strip $(ENGINE_DIR)),)
    ifneq ($(wildcard orosmo/make/engine.mk),)
        ENGINE_DIR := orosmo
    else ifneq ($(strip $(OROSMO_VERSION)),)
        _OROSMO_HOME := $(subst \,/,$(or $(OROSMO_HOME),$(if $(filter Windows_NT,$(OS)),$(LOCALAPPDATA)/Orosmo,$(HOME)/.orosmo)))
        _OROSMO_BIN  := $(or $(shell command -v $(OROSMO_TOOL) 2>/dev/null),$(wildcard $(_OROSMO_HOME)/bin/orosmo$(if $(filter Windows_NT,$(OS)),.exe)))
        _OROSMO_PATH := $(if $(_OROSMO_BIN),$(subst \,/,$(shell "$(_OROSMO_BIN)" path $(OROSMO_VERSION) 2>/dev/null)))
        # make splits words on spaces: a path with one (and no 8.3 short form, see `orosmo path`) is refused below (G214).
        ENGINE_DIR   := $(if $(word 2,$(_OROSMO_PATH)),,$(if $(wildcard $(_OROSMO_PATH)/make/engine.mk),$(_OROSMO_PATH)))
    endif
endif
_ENGINE_MK  ?= $(ENGINE_DIR)/make/engine.mk
_VCPKG_ROOT ?= vcpkg_installed

# ── Affichage : dans un terminal, `make <buts>` passe par $(ENGINE_DIR)/tools/build_progress.py (barre de progression : fichiers
#    compilés, en cours, à jour, restants). Commandes complètes : `make V=1 …` ; sans barre : `ORS_NO_PROGRESS=1 make …`. Hors
#    terminal, sous-make, paquet sans le script ou sans Python 3 : make habituel. ──
_ORS_SCRIPT := $(wildcard $(ENGINE_DIR)/tools/build_progress.py)
_ORS_FIRST  := $(firstword $(MAKEFLAGS))
_ORS_DRY    := $(if $(filter -%,$(_ORS_FIRST)),,$(findstring n,$(_ORS_FIRST))$(findstring q,$(_ORS_FIRST)))
_ORS_WRAP   := $(if $(or $(ORS_PROGRESS),$(ORS_NO_PROGRESS),$(filter 1,$(V)),$(_ORS_DRY),$(filter-out 0,$(MAKELEVEL)),$(if $(_ORS_SCRIPT),,none)),,$(shell test -t 2 && command -v python3 >/dev/null 2>&1 && echo 1))
ifeq ($(_ORS_WRAP),1)
_ORS_GOALS := $(or $(MAKECMDGOALS),ors-default-goal)
.PHONY: $(_ORS_GOALS)
$(firstword $(_ORS_GOALS)):
	@python3 "$(_ORS_SCRIPT)" $(MAKE) $(MAKECMDGOALS)
# Les autres buts ne font rien (le script les transmet tous) ; le premier, même répété (`make check check`), garde sa recette.
_ORS_OTHERS := $(filter-out $(firstword $(_ORS_GOALS)),$(_ORS_GOALS))
ifneq ($(_ORS_OTHERS),)
$(sort $(_ORS_OTHERS)):
	@:
endif
else

GAME_SRC_DIR := src
BUILD_TYPE   ?= release

ifneq ($(findstring debug,$(MAKECMDGOALS)),)
    BUILD_TYPE := debug
else ifneq ($(findstring dev,$(MAKECMDGOALS)),)
    BUILD_TYPE := dev
else ifneq ($(findstring release,$(MAKECMDGOALS)),)
    BUILD_TYPE := release
endif

_ENGINE_MK_FOUND := $(wildcard $(_ENGINE_MK))

ifeq ($(_ENGINE_MK_FOUND),)
    _DEMO_ENGINELESS_GOALS := install install-deps add-dep search search-dep clean fclean info
    ifeq ($(filter $(_DEMO_ENGINELESS_GOALS),$(MAKECMDGOALS)),)
        ifneq ($(strip $(ENGINE_DIR)),)
            $(error Engine not found: $(_ENGINE_MK) does not exist. Check ENGINE_DIR=$(ENGINE_DIR) (in the Orosmo repository: run "make sync-demo" from the root first))
        else ifeq ($(strip $(OROSMO_VERSION)),)
            $(error Engine not found: no orosmo/ folder and OROSMO_VERSION is not set in config.mk (add OROSMO_VERSION := x.y.z, or pass ENGINE_DIR=/path/to/engine))
        else ifeq ($(_OROSMO_BIN),)
            $(error Orosmo $(OROSMO_VERSION) not found: the orosmo tool is not installed (not in PATH nor in $(_OROSMO_HOME)/bin). Install it, then run "orosmo install $(OROSMO_VERSION)" (or pass ENGINE_DIR=/path/to/engine))
        else ifneq ($(word 2,$(_OROSMO_PATH)),)
            $(error Orosmo $(OROSMO_VERSION) is installed in "$(_OROSMO_PATH)", but make cannot use a path with spaces: set OROSMO_HOME to a folder without spaces and run "orosmo install $(OROSMO_VERSION)" again (or pass ENGINE_DIR=/path/without/spaces))
        else
            $(error Orosmo $(OROSMO_VERSION) is not installed: run "orosmo install $(OROSMO_VERSION)" (or pass ENGINE_DIR=/path/to/engine))
        endif
    endif

    _DEMO_UNAME := $(shell uname -s 2>/dev/null || echo Unknown)
    ifeq ($(OS),Windows_NT)
        ENGINE_OS := Windows
    else ifneq ($(filter Linux,$(_DEMO_UNAME)),)
        ENGINE_OS := Linux
    else ifneq ($(filter Darwin,$(_DEMO_UNAME)),)
        ENGINE_OS := Darwin
    else ifneq ($(filter MSYS% MINGW% CYGWIN%,$(_DEMO_UNAME)),)
        ENGINE_OS := Windows
    else
        ENGINE_OS := Linux
    endif

    ifeq ($(ENGINE_OS),Windows)
        ENGINE_VCPKG_TRIPLET ?= x64-mingw-static
        ENGINE_EXE_EXT       := .exe
    else ifeq ($(ENGINE_OS),Darwin)
        _DEMO_ARCH := $(shell uname -m 2>/dev/null)
        ifeq ($(_DEMO_ARCH),arm64)
            ENGINE_VCPKG_TRIPLET ?= arm64-osx
        else
            ENGINE_VCPKG_TRIPLET ?= x64-osx
        endif
        ENGINE_EXE_EXT :=
    else
        _DEMO_ARCH := $(shell uname -m 2>/dev/null)
        ifeq ($(_DEMO_ARCH),aarch64)
            ENGINE_VCPKG_TRIPLET ?= arm64-linux
        else
            ENGINE_VCPKG_TRIPLET ?= x64-linux
        endif
        ENGINE_EXE_EXT :=
    endif

    ENGINE_CXXFLAGS      :=
    ENGINE_INCLUDES      := -I$(GAME_SRC_DIR)
    ENGINE_LIBS          :=
    ENGINE_LINK_LIBS     :=
    ENGINE_LDFLAGS       :=
    ENGINE_GEN_DIR       := Generated
    GEN_CPP_SOURCES      :=
    GEN_REDIRECT_SOURCES :=
else
    include $(_ENGINE_MK)
endif

# ── Game-specific vcpkg (optional) ────────────────────────────────────────────
# Game deps are installed to $(_VCPKG_ROOT)/ (vcpkg_installed/ at the project root), separate from the engine's
# libraries (ENGINE_DIR/deps, read-only).
_GAME_VCPKG_DIR       = $(_VCPKG_ROOT)/$(ENGINE_VCPKG_TRIPLET)
_ADD_DEP_VCPKG_ROOT   = $(_VCPKG_ROOT)
_ADD_DEP_VCPKG_LIBDIR = $(_VCPKG_ROOT)/$(ENGINE_VCPKG_TRIPLET)/lib

GAME_CXXFLAGS = $(EXTRA_CXXFLAGS) \
    $(if $(wildcard $(_GAME_VCPKG_DIR)/include),-isystem $(_GAME_VCPKG_DIR)/include)
GAME_LDFLAGS  = \
    $(if $(wildcard $(_GAME_VCPKG_DIR)/lib),-L$(_GAME_VCPKG_DIR)/lib) \
    $(EXTRA_LDLIBS)

# ── Directories ───────────────────────────────────────────────────────────────
SRC_DIR  := src
OBJ_DIR  := obj/$(BUILD_TYPE)
BIN_DIR  := bin/$(BUILD_TYPE)
TEST_DIR  ?= tests
# Projet créé par `orosmo init` : sans res/ propre, les ressources du paquet (police, thèmes) ; shaders du RHI du paquet.
RES_DIR   ?= $(if $(wildcard res),res,$(if $(wildcard $(ENGINE_DIR)/res),$(ENGINE_DIR)/res,res))
# Shaders, copied to $(BIN_DIR)/res/shader (runtime path "res/shader/...").
# In the engine repo they live at the repository root; see DEMO_ARGS in the root Makefile.
SHADER_DIR ?= $(if $(wildcard $(ENGINE_DIR)/shaders),$(ENGINE_DIR)/shaders,)

# ── Build target name ─────────────────────────────────────────────────────────
ifeq ($(BUILD_TYPE),release)
    TARGET_NAME := $(PROJECT_NAME)
else
    TARGET_NAME := main
endif
TARGET := $(BIN_DIR)/$(TARGET_NAME)$(ENGINE_EXE_EXT)

# Fixed paths used by 'make run' to detect the newest built binary
_DBG_BIN := bin/debug/main$(ENGINE_EXE_EXT)
_REL_BIN := bin/release/$(PROJECT_NAME)$(ENGINE_EXE_EXT)

# ── Sources ───────────────────────────────────────────────────────────────────
SRCS_CPP := $(shell find $(SRC_DIR) -type f -name "*.cpp" 2>/dev/null)
SRCS_C   := $(shell find $(SRC_DIR) -type f -name "*.c"   2>/dev/null)

OBJS_CPP := $(SRCS_CPP:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/src/%.o)
OBJS_C   := $(SRCS_C:$(SRC_DIR)/%.c=$(OBJ_DIR)/src/%.o)
GEN_OBJS := $(GEN_CPP_SOURCES:$(ENGINE_GEN_DIR)/%.gen.cpp=$(OBJ_DIR)/gen/%.o)
ALL_OBJS := $(OBJS_CPP) $(OBJS_C) $(GEN_OBJS)
TEST_GAME_OBJS := $(filter-out $(OBJ_DIR)/src/main.o,$(OBJS_CPP) $(OBJS_C))

# Windows icon resource
ICON_RC :=
ifneq ($(strip $(ICON_NAME)),)
ifneq ($(wildcard $(ICON_NAME)),)
ifeq ($(ENGINE_OS),Windows)
    ICON_OBJ := $(OBJ_DIR)/src/icon.o
    ALL_OBJS += $(ICON_OBJ)
endif
endif
endif

# Test sources
TEST_SRCS   := $(shell find $(TEST_DIR) -type f -name "*.cpp" 2>/dev/null)
# UI-11 : les vues .cui des tests (sauf les cas d'erreur de cui_errors/) sont compilées par cui, puis avec les tests.
TEST_CUI_SRCS := $(shell find $(TEST_DIR) -type f -name "*.cui" -not -path "*/cui_errors/*" 2>/dev/null)
TEST_GEN_CPP  := $(patsubst $(TEST_DIR)/%.cui,$(ENGINE_GEN_DIR)/tests/%.gen.cpp,$(TEST_CUI_SRCS))
TEST_GEN_OBJS := $(TEST_GEN_CPP:$(ENGINE_GEN_DIR)/%.gen.cpp=$(OBJ_DIR)/gen/%.o)
TEST_OBJS   := $(TEST_SRCS:$(TEST_DIR)/%.cpp=$(OBJ_DIR)/tests/%.o) $(TEST_GAME_OBJS) $(GEN_OBJS) $(TEST_GEN_OBJS)
TEST_TARGET := bin/tests/tests$(ENGINE_EXE_EXT)

# Style sources
BENCH_DIR    := benchmark
BENCH_SRCS   := $(shell find $(BENCH_DIR) -type f -name "*.cpp" 2>/dev/null)
BENCH_OBJS   := $(BENCH_SRCS:$(BENCH_DIR)/%.cpp=$(OBJ_DIR)/benchmark/%.o)
BENCH_TARGET := bin/benchmark/benchmark$(ENGINE_EXE_EXT)

STYLE_SRCS := $(shell find $(SRC_DIR) $(TEST_DIR) $(BENCH_DIR) -type f \( -name "*.h" -o -name "*.hpp" -o -name "*.cpp" -o -name "*.c" \) 2>/dev/null)
LINT_FLAGS  := $(filter-out -flto=jobserver,$(ENGINE_CXXFLAGS))

# vcpkg platform-specific copy targets
ifeq ($(ENGINE_OS),Windows)
    COPY_EXTRA := copy_libs
else
    COPY_EXTRA :=
endif

# ── Main targets ──────────────────────────────────────────────────────────────
.PHONY: all debug dev release run run-debug run-dev run-release

all debug dev release: $(TARGET)

# run-debug / run-dev / run-release: build the requested variant if needed, then run it.
# BUILD_TYPE is set at parse time via the findstring block above, so $(TARGET) already
# points to the correct binary (bin/debug/main or bin/release/<name>).
run-debug run-dev: $(TARGET)
	@./$(TARGET)

run-release: $(TARGET)
	@./$(TARGET)

# run: launch the most recently compiled binary (debug or release).
# Recompiles it if sources are out-of-date. Builds debug from scratch if nothing exists.
run:
	@set -e; \
	DBG="$(_DBG_BIN)"; REL="$(_REL_BIN)"; \
	if [ -f "$$REL" ] && { [ ! -f "$$DBG" ] || [ "$$REL" -nt "$$DBG" ]; }; then \
	    $(MAKE) release; \
	    printf "Running [release] $$REL\n"; \
	    "./$$REL"; \
	else \
	    $(MAKE) debug; \
	    printf "Running [debug]   $$DBG\n"; \
	    "./$$DBG"; \
	fi

# ── Link ──────────────────────────────────────────────────────────────────────
$(TARGET): $(ALL_OBJS) $(ENGINE_LIBS) | $(BIN_DIR) copy_res $(COPY_EXTRA)
	$(call ors_link,LD)
	$(ORS_Q)$(CXX) $(ENGINE_CXXFLAGS) $(ALL_OBJS) \
	    $(ENGINE_LINK_LIBS) $(ENGINE_LDFLAGS) $(GAME_LDFLAGS) -o $@
	$(ors_end)
	@echo "Compiled: $@"

# ── Compilation rules ─────────────────────────────────────────────────────────
$(OBJ_DIR)/src/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p "$(dir $@)"
	$(call ors_compile,CXX)
	$(ORS_Q)$(CXX) $(ENGINE_CXXFLAGS) $(GAME_CXXFLAGS) -MMD -MP $(ENGINE_INCLUDES) -c $< -o $@
	$(ors_end)

$(OBJ_DIR)/src/%.o: $(SRC_DIR)/%.c
	@mkdir -p "$(dir $@)"
	$(call ors_compile,CC)
	$(ORS_Q)$(CC) $(ENGINE_CXXFLAGS) $(GAME_CXXFLAGS) -MMD -MP $(ENGINE_INCLUDES) -c $< -o $@
	$(ors_end)

$(OBJ_DIR)/gen/%.o: $(ENGINE_GEN_DIR)/%.gen.cpp
	@mkdir -p "$(dir $@)"
	$(call ors_compile,CXX [gen])
	$(ORS_Q)$(CXX) $(ENGINE_CXXFLAGS) -MMD -MP $(ENGINE_INCLUDES) -c $< -o $@
	$(ors_end)

# Ensure CUI files are generated before any object is compiled
$(ALL_OBJS): | $(GEN_CPP_SOURCES) $(GEN_REDIRECT_SOURCES)

# Windows icon resource
$(OBJ_DIR)/src/icon.o: $(BIN_DIR)/icon.rc
	@mkdir -p "$(dir $@)"
	$(RC) -i $< -o $@

$(BIN_DIR)/icon.rc: $(ICON_NAME) | $(BIN_DIR)
	@printf '1 ICON "%s"\n' "$(ICON_NAME)" > "$@"

# ── Tests ─────────────────────────────────────────────────────────────────────
.PHONY: test
TEST_ARGS ?=
test: $(TEST_TARGET)
	@./$(TEST_TARGET) $(TEST_ARGS)

$(TEST_TARGET): $(TEST_OBJS) $(ENGINE_LIBS) | bin/tests
	$(call ors_link,LD)
	$(ORS_Q)$(CXX) $(ENGINE_CXXFLAGS) $(TEST_OBJS) $(ENGINE_LINK_LIBS) $(ENGINE_LDFLAGS) -o $@
	$(ors_end)

# Les tests incluent aussi les vues générées de la démo (UI/Views/*.gen.h) : elles doivent exister avant leur compilation.
$(OBJ_DIR)/tests/%.o: $(TEST_DIR)/%.cpp | $(TEST_GEN_CPP) $(GEN_CPP_SOURCES) $(GEN_REDIRECT_SOURCES)
	@mkdir -p "$(dir $@)"
	$(call ors_compile,CXX)
	$(ORS_Q)$(CXX) $(ENGINE_CXXFLAGS) -MMD -MP $(ENGINE_INCLUDES) -I$(TEST_DIR) -c $< -o $@
	$(ors_end)

$(ENGINE_GEN_DIR)/tests/%.gen.cpp: $(TEST_DIR)/%.cui $(_ENG_CUI_TOOL)
	@mkdir -p "$(dir $@)"
	$(ORS_Q)$(_ENG_CUI_TOOL) run --output "$(dir $@)" $<

# Règle statique : prioritaire sur $(OBJ_DIR)/gen/%.o (sans -I$(TEST_DIR)) quel que soit make ; GNU make 3.81 (macOS) prend
# la première règle implicite qui correspond, pas la plus précise.
$(TEST_GEN_OBJS): $(OBJ_DIR)/gen/tests/%.o: $(ENGINE_GEN_DIR)/tests/%.gen.cpp
	@mkdir -p "$(dir $@)"
	$(call ors_compile,CXX)
	$(ORS_Q)$(CXX) $(ENGINE_CXXFLAGS) -MMD -MP $(ENGINE_INCLUDES) -I$(TEST_DIR) -c $< -o $@
	$(ors_end)

# ── Benchmark ─────────────────────────────────────────────────────────────────
.PHONY: benchmark
benchmark: $(BENCH_TARGET)
	@./$(BENCH_TARGET)

$(BENCH_TARGET): $(BENCH_OBJS) $(ENGINE_LIBS) | bin/benchmark
	$(call ors_link,LD)
	$(ORS_Q)$(CXX) $(ENGINE_CXXFLAGS) $(BENCH_OBJS) $(ENGINE_LINK_LIBS) $(ENGINE_LDFLAGS) -o $@
	$(ors_end)

$(OBJ_DIR)/benchmark/%.o: $(BENCH_DIR)/%.cpp
	@mkdir -p "$(dir $@)"
	$(call ors_compile,CXX)
	$(ORS_Q)$(CXX) $(ENGINE_CXXFLAGS) -MMD -MP -I$(BENCH_DIR) $(ENGINE_INCLUDES) -c $< -o $@
	$(ors_end)

# ── Resources & libraries ─────────────────────────────────────────────────────
.PHONY: copy_res copy_libs

copy_res: | $(BIN_DIR)
ifeq ($(ENGINE_OS),Windows)
	@if [ -d "$(RES_DIR)" ]; then mkdir -p "$(BIN_DIR)/res"; powershell -NoProfile -Command "Copy-Item -Path '$(RES_DIR)/*' -Destination '$(BIN_DIR)/res' -Recurse -Force"; fi
	@if [ -n "$(SHADER_DIR)" ] && [ -d "$(SHADER_DIR)" ]; then mkdir -p "$(BIN_DIR)/res/shader"; powershell -NoProfile -Command "Copy-Item -Path '$(SHADER_DIR)/*' -Destination '$(BIN_DIR)/res/shader' -Recurse -Force"; fi
else
	@# The engine (OROSMO_HOME/engines/<v>) may be read-only and cp keeps the source modes: the copies are made writable,
	@# or the next build could not overwrite them.
	@if [ -d "$(RES_DIR)" ]; then mkdir -p "$(BIN_DIR)/res" && cp -r "$(RES_DIR)/." "$(BIN_DIR)/res/" && chmod -R u+w "$(BIN_DIR)/res"; fi
	@if [ -n "$(SHADER_DIR)" ] && [ -d "$(SHADER_DIR)" ]; then mkdir -p "$(BIN_DIR)/res/shader" && cp -r "$(SHADER_DIR)/." "$(BIN_DIR)/res/shader/" && chmod -R u+w "$(BIN_DIR)/res/shader"; fi
endif

copy_libs: | $(BIN_DIR)
	@$(if $(ENGINE_DEPS_DIR),set -- "$(ENGINE_DEPS_DIR)/bin"/*.dll; \
	 if [ -e "$$1" ]; then cp -f "$$@" "$(BIN_DIR)/" 2>/dev/null || :; fi,:)
	@set -- "$(_GAME_VCPKG_DIR)/bin"/*.dll; \
	 if [ -e "$$1" ]; then cp -f "$$@" "$(BIN_DIR)/" 2>/dev/null || :; fi

# ── Directories ───────────────────────────────────────────────────────────────
$(BIN_DIR) bin/tests bin/benchmark:
	@mkdir -p "$@"

# ── vcpkg dependency management (game only, optional) ─────────────────────────
# The engine's libraries are NOT installed here: they come prebuilt in ENGINE_DIR/deps.
# install:   no args → install the project's vcpkg.json deps (incremental, skips installed; nothing without vcpkg.json)
#            with args → add packages, install them, auto-detect linker flags
# add-dep:   same as install with args: make add-dep DEP="fmt nlohmann-json"
# search:    search the vcpkg catalog
#
# Usage:
#   make add-dep DEP=fmt
#   make install fmt nlohmann-json
#   make install fmt LIBS="-lfmt"   # rare: manual linker flag override
#   make search json

# Capture positional args: 'make install <pkg>...' / 'make search <kw>'
ifeq ($(firstword $(MAKECMDGOALS)),install)
  _INSTALL_ARGS := $(wordlist 2,$(words $(MAKECMDGOALS)),$(MAKECMDGOALS))
  ifneq ($(_INSTALL_ARGS),)
    $(foreach _d,$(_INSTALL_ARGS),$(eval $(_d):;@true))
  endif
endif
ifeq ($(firstword $(MAKECMDGOALS)),add-dep)
  _INSTALL_ARGS := $(DEP)
endif
ifeq ($(firstword $(MAKECMDGOALS)),search)
  _Q_ARGS := $(wordlist 2,$(words $(MAKECMDGOALS)),$(MAKECMDGOALS))
  ifneq ($(_Q_ARGS),)
    Q ?= $(_Q_ARGS)
    $(foreach _q,$(_Q_ARGS),$(eval $(_q):;@true))
  endif
endif

.PHONY: i install install-deps add-dep search search-dep

install install-deps add-dep:
	@if [ "$@" = add-dep ] && [ -z "$(_INSTALL_ARGS)" ]; then printf "Usage: make add-dep DEP=<vcpkg package>\n"; exit 1; fi
	@if [ -z "$(_INSTALL_ARGS)" ]; then \
	    if [ -f vcpkg.json ]; then \
	        $(VCPKG) install \
	            --triplet=$(ENGINE_VCPKG_TRIPLET) \
	            --x-manifest-root=. \
	            --x-install-root=$(_ADD_DEP_VCPKG_ROOT) \
	            --no-print-usage; \
	    fi; \
	else \
	    [ -f vcpkg.json ] || printf '{\n  "name": "game",\n  "version": "0.1.0",\n  "dependencies": []\n}\n' > vcpkg.json; \
	    LIBDIR="$(_ADD_DEP_VCPKG_LIBDIR)"; \
	    for dep in $(_INSTALL_ARGS); do \
	        if command -v jq >/dev/null 2>&1; then \
	            if jq --arg d "$$dep" -e '.dependencies[] | select((type=="string" and .==$$d) or (.name?==$$d))' vcpkg.json >/dev/null 2>&1; then \
	                printf "  [vcpkg.json] %s already listed\n" "$$dep"; \
	            else \
	                tmp=$$(mktemp); jq --arg d "$$dep" '.dependencies += [$$d]' vcpkg.json > "$$tmp" && mv "$$tmp" vcpkg.json; \
	                printf "  [vcpkg.json] Added %s\n" "$$dep"; \
	            fi; \
	        elif command -v python3 >/dev/null 2>&1; then \
	            DEP="$$dep" python3 -c "import json,os; dep=os.environ['DEP']; d=json.load(open('vcpkg.json')); deps=d.setdefault('dependencies',[]); n=[x if isinstance(x,str) else x.get('name','') for x in deps]; deps.append(dep) if dep not in n else None; open('vcpkg.json','w').write(json.dumps(d,indent=2))"; \
	            printf "  [vcpkg.json] Updated (%s)\n" "$$dep"; \
	        else \
	            printf "Error: jq or python3 required to edit vcpkg.json\n"; exit 1; \
	        fi; \
	        b=$$(mktemp); a=$$(mktemp); \
	        ls "$$LIBDIR/"*.a 2>/dev/null | sort > "$$b" || :; \
	        $(VCPKG) install \
	            --triplet=$(ENGINE_VCPKG_TRIPLET) \
	            --x-manifest-root=. \
	            --x-install-root=$(_ADD_DEP_VCPKG_ROOT) \
	            --no-print-usage \
	            || { rm -f "$$b" "$$a"; printf "Error: vcpkg could not install the manifest (vcpkg.json, with %s)\n" "$$dep"; exit 1; }; \
	        ls "$$LIBDIR/"*.a 2>/dev/null | sort > "$$a" || :; \
	        if [ -n "$(LIBS)" ]; then \
	            NEW="$(LIBS)"; \
	        else \
	            NEW=$$(comm -13 "$$b" "$$a" \
	                | sed 's|.*/lib\([^.]*\)\.a|-l\1|' \
	                | tr '\n' ' ' | sed 's/[[:space:]]*$$//'); \
	        fi; \
	        rm -f "$$b" "$$a"; \
	        if [ -z "$$NEW" ]; then \
	            printf "  [config.mk] %s is header-only — no linker flags needed\n" "$$dep"; \
	        else \
	            printf "  Detected: %s\n" "$$NEW"; \
	            EXISTING=$$(grep '^EXTRA_LDLIBS' config.mk 2>/dev/null \
	                | sed 's/^EXTRA_LDLIBS[^=]*=[[:space:]]*//' \
	                | tr -s ' ' | sed 's/^[[:space:]]*//;s/[[:space:]]*$$//'); \
	            ALL=$$(printf '%s %s' "$$EXISTING" "$$NEW" \
	                | tr -s ' ' | sed 's/^[[:space:]]*//;s/[[:space:]]*$$//'); \
	            tmp=$$(mktemp); found=0; \
	            while IFS= read -r line; do \
	                case "$$line" in \
	                    EXTRA_LDLIBS*) printf 'EXTRA_LDLIBS ?= %s\n' "$$ALL" >> "$$tmp"; found=1;; \
	                    *) printf '%s\n' "$$line" >> "$$tmp";; \
	                esac; \
	            done < config.mk; \
	            [ "$$found" -eq 1 ] || printf '\nEXTRA_LDLIBS ?= %s\n' "$$ALL" >> "$$tmp"; \
	            mv "$$tmp" config.mk; \
	            printf "  [config.mk] EXTRA_LDLIBS = %s\n" "$$ALL"; \
	        fi; \
	    done; \
	fi

search search-dep:
	@[ -n "$(Q)" ] || { printf "Usage: make search <keyword>\n"; exit 1; }
	$(VCPKG) search "$(Q)"

# ── Clean ─────────────────────────────────────────────────────────────────────
.PHONY: clean fclean re re-debug re-dev re-release

clean:
	@rm -rf obj/ $(ENGINE_GEN_DIR)
	@echo "Cleaned"

fclean: clean
	@rm -rf bin/
	@echo "Full clean done"

re:         fclean all
re-debug:   fclean debug
re-dev:     fclean dev
re-release: fclean release

# ── Code quality ──────────────────────────────────────────────────────────────
.PHONY: check check-syntax check-format format lint

check: check-syntax check-format lint

check-syntax:
	@echo "Checking syntax..."
	@if [ -n "$(strip $(SRCS_CPP))" ]; then \
	    $(CXX) $(ENGINE_CXXFLAGS) $(GAME_CXXFLAGS) $(ENGINE_INCLUDES) -fsyntax-only $(SRCS_CPP); fi

check-format:
	@command -v $(CLANG_FORMAT) >/dev/null 2>&1 || { echo "clang-format not found"; exit 1; }
	@if [ -n "$(strip $(STYLE_SRCS))" ]; then \
	    $(CLANG_FORMAT) --dry-run --Werror $(STYLE_SRCS); fi

format:
	@command -v $(CLANG_FORMAT) >/dev/null 2>&1 || { echo "clang-format not found"; exit 1; }
ifeq ($(ENGINE_OS),Windows)
	@powershell -NoProfile -Command "& '..\tools\format_sources.ps1' '$(CLANG_FORMAT)'"
else
	@if [ -n "$(strip $(STYLE_SRCS))" ]; then $(CLANG_FORMAT) -i $(STYLE_SRCS); fi
endif

lint:
	@command -v $(CLANG_TIDY) >/dev/null 2>&1 || { echo "clang-tidy not found"; exit 1; }
	@if [ -n "$(strip $(SRCS_CPP))" ]; then \
	    $(CLANG_TIDY) $(SRCS_CPP) -- $(LINT_FLAGS) $(ENGINE_INCLUDES); fi

# ── Info ──────────────────────────────────────────────────────────────────────
.PHONY: info compile-commands

compile-commands:
	@if [ -f "$(ENGINE_DIR)/tools/gen_compile_commands.py" ]; then \
	    python3 "$(ENGINE_DIR)/tools/gen_compile_commands.py" --mode project --engine-dir "$(ENGINE_DIR)" --gen-dir "$(ENGINE_GEN_DIR)" --build-type "$(BUILD_TYPE)" --cxx "$(CXX)" --cc "$(CC)"; \
	else \
	    echo "gen_compile_commands.py not found in $(ENGINE_DIR)/tools/ (update SDK)"; exit 1; \
	fi

info:
	@echo "Project:      $(PROJECT_NAME)"
	@echo "Engine:       $(or $(ENGINE_DIR),not found)$(if $(OROSMO_VERSION), (OROSMO_VERSION $(OROSMO_VERSION)))"
	@echo "Build type:   $(BUILD_TYPE)"
	@echo "Platform:     $(ENGINE_OS)"
	@echo "Target:       $(TARGET)"
	@echo "Game sources: $(words $(SRCS_CPP)) C++ / $(words $(SRCS_C)) C"
	@echo "CUI views:    $(words $(GEN_CPP_SOURCES)) generated"
	@echo "Engine libs:  $(notdir $(ENGINE_LIBS))"
	@echo "Compiler:     $(CXX)"
	@echo ""
	@echo "Required tools:"
	@if command -v $(CXX) >/dev/null 2>&1; then printf "  [ok] %s\n" "$(CXX)"; \
	    else printf "  [!!] %-24s -- scoop install mingw / brew install gcc / apt install g++\n" "$(CXX)"; fi
	@if command -v $(AR) >/dev/null 2>&1; then printf "  [ok] %s\n" "$(AR)"; \
	    else printf "  [!!] %-24s -- scoop install mingw / brew install binutils / apt install binutils\n" "$(AR)"; fi
	@if command -v $(VCPKG) >/dev/null 2>&1; then printf "  [ok] %s\n" "$(VCPKG)"; \
	    else printf "  [--] %-24s -- optional (make add-dep): https://vcpkg.io/en/getting-started\n" "$(VCPKG)"; fi

# ── Dependency files ──────────────────────────────────────────────────────────
-include $(wildcard $(ALL_OBJS:.o=.d))
-include $(wildcard $(TEST_OBJS:.o=.d))
-include $(wildcard $(BENCH_OBJS:.o=.d))

# ── Installer (packaging) ─────────────────────────────────────────────────────
.PHONY: installer create_windows_installer create_linux_installer

BUILD_ARTIFACTS_DIR := build
TMP_DEB_DIR         := /tmp/$(PACKAGE)_deb

ifeq ($(ENGINE_OS),Windows)
    INSTALLER_FILE := $(PROJECT_NAME)-$(VERSION)-x64-setup.exe
    NSIS_COMPILER  := makensis
    installer: create_windows_installer
else ifeq ($(ENGINE_OS),Linux)
    INSTALLER_FILE := $(PROJECT_NAME)_$(VERSION)_amd64.deb
    installer: create_linux_installer
else
    installer:
	    @echo "No installer target for this platform"
endif

create_windows_installer: | $(BUILD_ARTIFACTS_DIR)
	$(NSIS_COMPILER) \
	    -DPRODUCT_NAME="$(PROJECT_NAME)" \
	    -DVERSION="$(VERSION)" \
	    -DARCH="x64" \
	    -DICON_NAME="$(ICON_NAME)" \
	    -DOUTPUT_FILE="$(INSTALLER_FILE)" \
	    installers/windows/installer.nsi
	@mv -f "installers/windows/$(INSTALLER_FILE)" "$(BUILD_ARTIFACTS_DIR)/"
	@echo "Installer: $(BUILD_ARTIFACTS_DIR)/$(INSTALLER_FILE)"

create_linux_installer: | $(BUILD_ARTIFACTS_DIR)
	@rm -rf "$(TMP_DEB_DIR)"
	@mkdir -p "$(TMP_DEB_DIR)/usr/bin" "$(TMP_DEB_DIR)/usr/share/$(PROJECT_NAME)" "$(TMP_DEB_DIR)/DEBIAN"
	@printf 'Package: %s\nVersion: %s\nArchitecture: amd64\nMaintainer: %s\nDescription: %s\n' \
	    "$(PROJECT_NAME)" "$(VERSION)" "$(MAINTAINER)" "$(PROJECT_DESCRIPTION)" \
	    > "$(TMP_DEB_DIR)/DEBIAN/control"
	@cp -f "$(TARGET)" "$(TMP_DEB_DIR)/usr/bin/$(PROJECT_NAME)"
	@cp -R "$(RES_DIR)/." "$(TMP_DEB_DIR)/usr/share/$(PROJECT_NAME)/"
	@if [ -n "$(SHADER_DIR)" ] && [ -d "$(SHADER_DIR)" ]; then mkdir -p "$(TMP_DEB_DIR)/usr/share/$(PROJECT_NAME)/shader" && cp -R "$(SHADER_DIR)/." "$(TMP_DEB_DIR)/usr/share/$(PROJECT_NAME)/shader/"; fi
	@dpkg-deb --build "$(TMP_DEB_DIR)" "$(BUILD_ARTIFACTS_DIR)/$(INSTALLER_FILE)"
	@rm -rf "$(TMP_DEB_DIR)"
	@echo "Package: $(BUILD_ARTIFACTS_DIR)/$(INSTALLER_FILE)"

$(BUILD_ARTIFACTS_DIR):
	@mkdir -p "$@"

endif # _ORS_WRAP
