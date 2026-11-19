# Makefile — ProceduralGeneration engine root
#
# ── vcpkg ─────────────────────────────────────────────────────────────────────
#   make install-deps          install engine vcpkg deps
#   make install-deps-demo     install demo vcpkg deps
#   make install-deps-all      both
#
# ── Engine (mk/, src/, include/) ───────────────────────────────────────────────────────
#   make debug                 build engine archives (debug)
#   make release               build engine archives (release)
#   make engine                build debug+release, dist, tools, and apps/demo/engine/
#   make dist                  package engine → dist/engine-<platform>/
#   make engine-tool           build the engine scaffolding binary
#   make sync-demo             fast sync of current BUILD_TYPE into demo/engine/
#   make install-demo          copy full dist package into demo/engine/
#   make clean / fclean        remove engine build artifacts
#
# ── Demo (apps/demo/) ──────────────────────────────────────────────────────────────
#   make demo-debug            build demo (debug)
#   make demo-release          build demo (release)
#   make run                   run last built binary (debug or release, whichever is newer); builds debug if none exists
#   make run-debug             build debug if needed, then run it
#   make run-release           build release if needed, then run it
#   make demo-run              alias for make run
#   make demo-install [pkg…]   add vcpkg dep(s) to demo
#   make demo-search kw        search vcpkg catalog
#   make demo-clean / fclean   remove demo build artifacts
#
# ── Architecture checks (docs/03 §8) — all blocking since M2: layers, API isolation (graphics APIs, GLFW, <windows.h>),
#    self-contained headers. Each script exits 1 on a violation (same commands as the CI). ──
.PHONY: check check-repo check-layers check-api check-headers
check: check-repo check-layers check-api check-headers

check-repo:
	@sh tools/check-repo.sh

check-layers:
	@python3 tools/check_layers.py --strict

check-api:
	@python3 tools/check_api_isolation.py --strict

check-headers:
	@python3 tools/check_headers.py --strict

# ── Combined ──────────────────────────────────────────────────────────────────
#   make all-debug             engine debug → sync → demo debug
#   make all-release           engine release → sync → demo release
#   make clean-all             remove all engine/demo artifacts and vcpkg installs
#   make setup                 install engine deps + make engine + demo deps + demo debug
#   make setup-run             setup + run demo
#
# ── Release ───────────────────────────────────────────────────────────────────
#   make publish               bump patch (0.1.0 → 0.1.1), commit, tag, push
#   make publish VERSION=x.y.z bump to exact version, commit, tag, push
#                              Must be on main/master. VERSION must be > current.

SHELL      := sh
VCPKG      ?= vcpkg
BUILD_TYPE ?= debug
ENGINE_MK  := mk/engine.mk

# ── Linux (x86_64, arm64): link the apt packages into vcpkg_installed/ automatically ────────────
# Prerequisites (README.md): the apt packages, then `vcpkg install` at the repository root (vcpkg.json: glad, msdfgen).
# tools/setup-linux-deps.sh adds the apt packages to vcpkg_installed/<triplet>/ (marker file .apt-linked); make runs it
# when the marker is missing (first build, after `make clean-all`). `make setup` runs it again after `vcpkg install`.
_DEPS_OS   := $(shell uname -s 2>/dev/null)
_DEPS_ARCH := $(shell uname -m 2>/dev/null)
ifeq ($(_DEPS_OS),Linux)
_DEPS_TRIPLET := $(if $(filter x86_64,$(_DEPS_ARCH)),x64-linux,$(if $(filter aarch64 arm64,$(_DEPS_ARCH)),arm64-linux))
ifneq ($(_DEPS_TRIPLET),)
ifeq ($(filter clean clean-all fclean demo-clean demo-fclean check check-repo check-layers check-api check-headers info,$(MAKECMDGOALS)),)
ifeq ($(wildcard vcpkg_installed/$(_DEPS_TRIPLET)/.apt-linked),)
$(info [deps] apt packages not linked yet: running tools/setup-linux-deps.sh)
_DEPS_RC := $(shell TRIPLET=$(_DEPS_TRIPLET) sh tools/setup-linux-deps.sh 1>&2; echo $$?)
ifneq ($(_DEPS_RC),0)
$(warning tools/setup-linux-deps.sh failed (see above): install the apt packages and run 'vcpkg install' as described in README.md)
endif
endif
endif
endif
endif

# In-repo layout: demo resources, shaders and tests live at the repository root.
DEMO_ARGS  := RES_DIR=../../res SHADER_DIR=../../shaders TEST_DIR=../../tests ICON_NAME=../../res/public/icon.ico

# ── vcpkg ─────────────────────────────────────────────────────────────────────
.PHONY: install-deps install-deps-demo install-deps-all

install-deps:
	$(MAKE) -f $(ENGINE_MK) install VCPKG=$(VCPKG)

install-deps-demo:
	$(MAKE) -C apps/demo $(DEMO_ARGS) install VCPKG=$(VCPKG)

install-deps-all: install-deps install-deps-demo

# ── Engine ────────────────────────────────────────────────────────────────────
.PHONY: debug dev release engine dist engine-tool sync-demo install-demo clean fclean info

debug dev release:
	$(MAKE) -f $(ENGINE_MK) BUILD_TYPE=$@ $@

engine:
	$(MAKE) -f $(ENGINE_MK) engine

dist:
	$(MAKE) -f $(ENGINE_MK) dist

engine-tool:
	$(MAKE) -f $(ENGINE_MK) engine-tool

sync-demo:
	$(MAKE) -f $(ENGINE_MK) sync-demo BUILD_TYPE=$(BUILD_TYPE)

install-demo:
	$(MAKE) -f $(ENGINE_MK) install-demo

clean:
	$(MAKE) -f $(ENGINE_MK) clean

fclean:
	$(MAKE) -f $(ENGINE_MK) fclean

# ── Demo ──────────────────────────────────────────────────────────────────────
# demo-test builds (release) and runs apps/demo's test suite
.PHONY: demo-test demo-debug demo-dev demo-release run run-debug run-release demo-run demo-clean demo-fclean
.PHONY: demo-install demo-search

demo-debug:
	$(MAKE) -C apps/demo $(DEMO_ARGS) debug

demo-dev:
	$(MAKE) -C apps/demo $(DEMO_ARGS) dev

demo-release:
	$(MAKE) -C apps/demo $(DEMO_ARGS) release

run:
	$(MAKE) -C apps/demo $(DEMO_ARGS) run

run-debug:
	$(MAKE) -C apps/demo $(DEMO_ARGS) run-debug

run-release:
	$(MAKE) -C apps/demo $(DEMO_ARGS) run-release

demo-run: run

demo-test:
	$(MAKE) -f $(ENGINE_MK) BUILD_TYPE=release release
	$(MAKE) -f $(ENGINE_MK) sync-demo BUILD_TYPE=release
	$(MAKE) -C apps/demo $(DEMO_ARGS) test

# Tests sans GPU ni fenêtre (unitaires) / seulement ceux qui en demandent un (intégration graphique : contexte OpenGL hors écran,
# fenêtre). `make demo-test` exécute les deux. ENGINE_TEST_REQUIRE_GPU=1 fait échouer (au lieu d'ignorer) un test GPU sans pilote.
GPU_TEST_SUITES := rhi/opengl*,rhi/shader/opengl,renderer/*,platform/window,world/gpu,world/render,ui/render
.PHONY: demo-test-unit demo-test-gpu
demo-test-unit:
	$(MAKE) -f $(ENGINE_MK) BUILD_TYPE=release release
	$(MAKE) -f $(ENGINE_MK) sync-demo BUILD_TYPE=release
	$(MAKE) -C apps/demo $(DEMO_ARGS) TEST_ARGS='--test-suite-exclude=$(GPU_TEST_SUITES)' test
demo-test-gpu:
	$(MAKE) -f $(ENGINE_MK) BUILD_TYPE=release release
	$(MAKE) -f $(ENGINE_MK) sync-demo BUILD_TYPE=release
	$(MAKE) -C apps/demo $(DEMO_ARGS) TEST_ARGS='--test-suite=$(GPU_TEST_SUITES)' test

# ── TOOL-02 : terrain-gen (sans fenêtre ni GPU) ──
#   make terrain-gen         construit apps/terrain_gen/bin/terrain-gen
#   make terrain-gen-check   déterminisme : deux exports identiques (1 et 4 threads), somme de contrôle de référence, codes de retour
TERRAIN_GEN := apps/terrain_gen/bin/terrain-gen$(if $(filter Windows_NT,$(OS)),.exe)
TG_CHECK    := build/terrain-gen-check
.PHONY: terrain-gen terrain-gen-check
terrain-gen:
	$(MAKE) -f $(ENGINE_MK) BUILD_TYPE=release release
	$(MAKE) -f $(ENGINE_MK) sync-demo BUILD_TYPE=release
	$(MAKE) -C apps/terrain_gen BUILD_TYPE=release all
terrain-gen-check: terrain-gen
	@mkdir -p $(TG_CHECK)
	./$(TERRAIN_GEN) --seed 42 --size 256 --out $(TG_CHECK)/a.png --threads 1 > $(TG_CHECK)/a.txt
	./$(TERRAIN_GEN) --seed 42 --size 256 --out $(TG_CHECK)/b.png --threads 4 > $(TG_CHECK)/b.txt
	cmp $(TG_CHECK)/a.png $(TG_CHECK)/b.png
	grep -F "$$(cat docs/BASELINE/terrain-gen-seed42-256.txt)" $(TG_CHECK)/a.txt
	./$(TERRAIN_GEN) --seed 42 --size 64 --out $(TG_CHECK)/t.stl > /dev/null
	@./$(TERRAIN_GEN) --seed 42 --size 1 --out $(TG_CHECK)/x.png > /dev/null 2>&1; test $$? -eq 1
	@./$(TERRAIN_GEN) --seed 42 --size 8 --params $(TG_CHECK)/absent.json --out $(TG_CHECK)/y.png > /dev/null 2>&1; test $$? -eq 2
	@echo "terrain-gen-check : OK (mêmes octets, somme de contrôle de référence, codes de retour)"

# ── Core demo (M1): 10 000 tasks, hot-reloaded config, measurements; no window, no graphics ──
#   make core-demo                     build and run          make core-demo CORE_DEMO_ARGS="--tasks 20000 --trace t.json"
.PHONY: core-demo
core-demo:
	$(MAKE) -f $(ENGINE_MK) BUILD_TYPE=release release
	$(MAKE) -f $(ENGINE_MK) sync-demo BUILD_TYPE=release
	$(MAKE) -C apps/core_demo BUILD_TYPE=release all
	./apps/core_demo/bin/core_demo $(CORE_DEMO_ARGS)

# ── RHI demo (M2 lot A): compute image, MRT render pass, instancing, GPU timestamps, offscreen; writes a PNG ──
#   make rhi-demo                      build and run          make rhi-demo RHI_DEMO_ARGS="--out /tmp/rhi.png"
.PHONY: rhi-demo
rhi-demo:
	$(MAKE) -f $(ENGINE_MK) BUILD_TYPE=release release
	$(MAKE) -f $(ENGINE_MK) sync-demo BUILD_TYPE=release
	$(MAKE) -C apps/rhi_demo BUILD_TYPE=release all
	./apps/rhi_demo/bin/rhi_demo $(RHI_DEMO_ARGS)

demo-clean:
	$(MAKE) -C apps/demo $(DEMO_ARGS) clean

demo-fclean:
	$(MAKE) -C apps/demo $(DEMO_ARGS) fclean

demo-install:
	$(MAKE) -C apps/demo $(DEMO_ARGS) install $(ARGS) LIBS=$(LIBS) VCPKG=$(VCPKG)

demo-search:
	$(MAKE) -C apps/demo $(DEMO_ARGS) search Q=$(Q) VCPKG=$(VCPKG)

# ── Combined ──────────────────────────────────────────────────────────────────
.PHONY: all-debug all-release clean-all setup setup-run

all-debug:
	$(MAKE) -f $(ENGINE_MK) sync-demo BUILD_TYPE=debug
	$(MAKE) -C apps/demo $(DEMO_ARGS) debug

all-release:
	$(MAKE) -f $(ENGINE_MK) sync-demo BUILD_TYPE=release
	$(MAKE) -C apps/demo $(DEMO_ARGS) release

clean-all:
	$(MAKE) -f $(ENGINE_MK) fclean BUILD_TYPE=debug
	$(MAKE) -f $(ENGINE_MK) fclean BUILD_TYPE=dev
	$(MAKE) -f $(ENGINE_MK) fclean BUILD_TYPE=release
	$(MAKE) -C apps/demo $(DEMO_ARGS) fclean
	@rm -rf vcpkg_installed apps/demo/vcpkg_installed apps/demo/engine/vcpkg_installed
	@echo "Removed all engine/demo build artifacts and vcpkg installs"

# install engine deps → prepare full engine → install demo deps → build demo
setup:
	$(MAKE) -f $(ENGINE_MK) install VCPKG=$(VCPKG)
	@if [ "$$(uname -s)" = Linux ]; then TRIPLET=$(_DEPS_TRIPLET) sh tools/setup-linux-deps.sh; fi
	$(MAKE) -f $(ENGINE_MK) engine
	$(MAKE) -C apps/demo $(DEMO_ARGS) install VCPKG=$(VCPKG)
	$(MAKE) -C apps/demo $(DEMO_ARGS) debug

setup-run: setup demo-run

# ── Publish ───────────────────────────────────────────────────────────────────
# make publish              — bump patch version (0.1.0 → 0.1.1)
# make publish VERSION=x.y.z — bump to exact version (must be > current)
# Requires: clean working tree, main or master branch.
.PHONY: publish
publish:
	@set -e; \
	CONFIG=mk/config.mk; \
	\
	BRANCH=$$(git rev-parse --abbrev-ref HEAD 2>/dev/null); \
	if [ "$$BRANCH" != "main" ] && [ "$$BRANCH" != "master" ]; then \
	    printf "error: publish requires main or master branch (current: %s)\n" "$$BRANCH"; \
	    exit 1; \
	fi; \
	\
	if ! git diff --quiet || ! git diff --cached --quiet; then \
	    printf "error: working tree has uncommitted changes -- commit or stash first\n"; \
	    exit 1; \
	fi; \
	\
	CURRENT=$$(awk '/^ENGINE_VERSION[[:space:]]*\?=/ { \
	    match($$0, /\?=[[:space:]]*/); \
	    print substr($$0, RSTART+RLENGTH) }' "$$CONFIG" | tr -d ' \t\r'); \
	if [ -z "$$CURRENT" ]; then \
	    printf "error: ENGINE_VERSION not found in %s\n" "$$CONFIG"; \
	    exit 1; \
	fi; \
	\
	if [ -n "$(VERSION)" ]; then \
	    NEW="$(VERSION)"; \
	    if ! printf '%s' "$$NEW" | grep -qE '^[0-9]+\.[0-9]+\.[0-9]+$$'; then \
	        printf "error: VERSION=%s is not valid semver (expected X.Y.Z)\n" "$$NEW"; \
	        exit 1; \
	    fi; \
	    VALID=$$(awk -v cur="$$CURRENT" -v new="$$NEW" 'BEGIN { \
	        split(cur,c,"."); split(new,n,"."); \
	        if (n[1]+0>c[1]+0 || \
	            (n[1]+0==c[1]+0 && n[2]+0>c[2]+0) || \
	            (n[1]+0==c[1]+0 && n[2]+0==c[2]+0 && n[3]+0>c[3]+0)) \
	            print "ok"; else print "fail" }'); \
	    if [ "$$VALID" != "ok" ]; then \
	        printf "error: VERSION=%s is not greater than current %s\n" "$$NEW" "$$CURRENT"; \
	        exit 1; \
	    fi; \
	else \
	    MAJ=$$(printf '%s' "$$CURRENT" | cut -d. -f1); \
	    MIN=$$(printf '%s' "$$CURRENT" | cut -d. -f2); \
	    PAT=$$(printf '%s' "$$CURRENT" | cut -d. -f3); \
	    NEW="$$MAJ.$$MIN.$$((PAT + 1))"; \
	fi; \
	\
	printf "Bumping %s -> %s\n" "$$CURRENT" "$$NEW"; \
	awk -v ver="$$NEW" '/^ENGINE_VERSION[[:space:]]*\?=/ \
	    { sub(/\?=.*/, "?= " ver) } 1' "$$CONFIG" > "$$CONFIG.tmp" \
	    && mv "$$CONFIG.tmp" "$$CONFIG"; \
	\
	git add "$$CONFIG"; \
	git commit -m "update version to $$NEW"; \
	git tag "v$$NEW"; \
	git push; \
	git push origin "v$$NEW"; \
	printf "Published v%s\n" "$$NEW"

# ── Info ──────────────────────────────────────────────────────────────────────
.PHONY: info
info:
	@printf "\n=== Engine ===\n"
	@$(MAKE) -f $(ENGINE_MK) info --no-print-directory
	@printf "=== Demo ===\n"
	@$(MAKE) -C apps/demo $(DEMO_ARGS) info --no-print-directory
	@printf "BUILD_TYPE  : $(BUILD_TYPE)\n"
	@printf "VCPKG       : $(VCPKG)\n\n"
