# Makefile — Orosmo engine root
#
# ── vcpkg ─────────────────────────────────────────────────────────────────────
#   make install-deps          install engine vcpkg deps
#   make install-deps-demo     install demo vcpkg deps
#   make install-deps-all      both
#
# ── Engine (mk/, src/, include/) ───────────────────────────────────────────────────────
#   make debug                 build engine archives (debug)
#   make release               build engine archives (release)
#   make engine                build debug+release, dist, tools, and apps/demo/orosmo/
#   make dist                  package engine → dist/orosmo-pkg-<platform>.zip
#   make engine-tool           build the orosmo scaffolding tool (dist/orosmo-<platform>)
#   make sync-demo             fast sync of current BUILD_TYPE into demo/orosmo/
#   make install-demo          copy full dist package into demo/orosmo/
#   make clean / fclean        remove engine build artifacts (debug, dev and release; BUILD_TYPE=x: that one only)
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
# ── Affichage : dans un terminal, `make <buts>` passe par tools/build_progress.py (barre de progression, fichiers compilés, en
#    cours, à jour, restants). Commandes complètes : `make V=1 …` ; sans barre : `ORS_NO_PROGRESS=1 make …`. Hors terminal (CI,
#    redirection) et dans un sous-make : make habituel. ──
_ORS_FIRST := $(firstword $(MAKEFLAGS))
_ORS_DRY   := $(if $(filter -%,$(_ORS_FIRST)),,$(findstring n,$(_ORS_FIRST))$(findstring q,$(_ORS_FIRST)))
_ORS_WRAP  := $(if $(or $(ORS_PROGRESS),$(ORS_NO_PROGRESS),$(filter 1,$(V)),$(_ORS_DRY),$(filter-out 0,$(MAKELEVEL))),,$(shell test -t 2 && command -v python3 >/dev/null 2>&1 && echo 1))
ifeq ($(_ORS_WRAP),1)
_ORS_GOALS := $(or $(MAKECMDGOALS),ors-default-goal)
.PHONY: $(_ORS_GOALS)
$(firstword $(_ORS_GOALS)):
	@python3 tools/build_progress.py $(MAKE) $(MAKECMDGOALS)
# Les autres buts ne font rien (le script les transmet tous) ; le premier, même répété (`make check check`), garde sa recette.
_ORS_OTHERS := $(filter-out $(firstword $(_ORS_GOALS)),$(_ORS_GOALS))
ifneq ($(_ORS_OTHERS),)
$(sort $(_ORS_OTHERS)):
	@:
endif
else

# Les cibles de ce fichier appellent des sous-make (parallèles chacun en interne) : elles-mêmes s'exécutent l'une après l'autre,
# car plusieurs synchronisent le même SDK partagé (sync-demo remplace apps/demo/orosmo/includes) et `make -j2 viewer gallery`
# ferait disparaître les en-têtes sous la compilation de l'autre.
.NOTPARALLEL:

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

# In-repo layout: demo resources, shaders, tests and in-repo procedural modules live at the repository root.
DEMO_ARGS  := RES_DIR=../../res SHADER_DIR=../../shaders TEST_DIR=../../tests ICON_NAME=../../res/public/icon.ico ENGINE_EXTRA_MODULES="world terrain_renderer"

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

# Sans BUILD_TYPE sur la ligne de commande : debug, dev et release (le BUILD_TYPE par défaut de ce fichier, debug, n'était pas
# transmis et mk/engine.mk nettoyait seulement release). `make fclean BUILD_TYPE=debug` : ce type seulement.
_CLEAN_TYPES := $(if $(filter command line,$(origin BUILD_TYPE)),$(BUILD_TYPE),debug dev release)
clean:
	@for t in $(_CLEAN_TYPES); do $(MAKE) -f $(ENGINE_MK) clean BUILD_TYPE=$$t || exit 1; done

fclean:
	@for t in $(_CLEAN_TYPES); do $(MAKE) -f $(ENGINE_MK) fclean BUILD_TYPE=$$t || exit 1; done

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

# ── TOOL-01 : terrain-viewer ──
#   make viewer           construit apps/terrain_viewer/bin/terrain-viewer (et copie ses ressources dans bin/res)
#   make viewer-run       lance le visualiseur (ARGS="--seed 42 --preset mountains --theme light")
#   make viewer-capture   images sans fenêtre, thèmes sombre, clair et à cadres : build/viewer-capture/{dark,light,framed}/viewer.png
VIEWER := apps/terrain_viewer/bin/terrain-viewer$(if $(filter Windows_NT,$(OS)),.exe)
.PHONY: viewer viewer-run viewer-capture
viewer:
	$(MAKE) -f $(ENGINE_MK) BUILD_TYPE=release release
	$(MAKE) -f $(ENGINE_MK) sync-demo BUILD_TYPE=release
	$(MAKE) -C apps/terrain_viewer BUILD_TYPE=release
viewer-run: viewer
	./$(VIEWER) $(ARGS)
viewer-capture: viewer
	./$(VIEWER) --headless --seed 42 --preset mountains --theme dark --capture build/viewer-capture/dark --capture-frames 5
	./$(VIEWER) --headless --seed 42 --preset mountains --theme light --capture build/viewer-capture/light --capture-frames 5
	./$(VIEWER) --headless --seed 42 --preset mountains --theme framed --capture build/viewer-capture/framed --capture-frames 5
	python3 tools/compare_png.py build/viewer-capture/dark/viewer.png build/viewer-capture/light/viewer.png > /dev/null \
	    && { echo "viewer-capture : les deux thèmes donnent la même image"; exit 1; } || echo "viewer-capture : OK (deux thèmes, deux images)"

# ── Galerie de l'interface (exemple : tous les widgets, conteneurs, événements et thèmes d'orosmo::ui) ──
#   make gallery-run ARGS="--theme framed"   lance la galerie
#   make gallery-capture                     images sans fenêtre de chaque onglet : build/gallery-capture/<thème>-<onglet>/gallery.png
GALLERY := apps/ui_gallery/bin/ui-gallery$(if $(filter Windows_NT,$(OS)),.exe)
.PHONY: gallery gallery-run gallery-capture
gallery:
	$(MAKE) -f $(ENGINE_MK) BUILD_TYPE=release release
	$(MAKE) -f $(ENGINE_MK) sync-demo BUILD_TYPE=release
	$(MAKE) -C apps/ui_gallery BUILD_TYPE=release
gallery-run: gallery
	./$(GALLERY) $(ARGS)
gallery-capture: gallery
	for tab in widgets controls color windows layout parameters themes; do \
	    ./$(GALLERY) --headless --theme dark --tab $$tab --capture build/gallery-capture/dark-$$tab || exit 1; done
	./$(GALLERY) --headless --theme light --tab widgets --capture build/gallery-capture/light-widgets
	./$(GALLERY) --headless --theme dark --tab widgets --click fruit --capture build/gallery-capture/dark-dropdown-open
	./$(GALLERY) --headless --theme light --tab widgets --hover volume --capture-frames 60 --capture build/gallery-capture/light-tooltip
	./$(GALLERY) --headless --theme framed --tab themes --capture build/gallery-capture/framed-themes
	./$(GALLERY) --headless --theme dark --tab windows --click openDialog --capture build/gallery-capture/dark-dialog
	./$(GALLERY) --headless --theme dark --tab windows --click menuFile --capture build/gallery-capture/dark-menu
	./$(GALLERY) --headless --theme light --tab windows --click notifyWarning --capture build/gallery-capture/light-notification

# ── BLD-02 : de bout en bout depuis le paquet (sans les sources du moteur) ──
#   make e2e-init   paquet + outil `orosmo` construits ; `orosmo init --package` d'un projet neuf dans build/e2e/<date>/mygame ;
#                   vérification que le paquet ne contient aucun module procédural (world, terrain_renderer, shaders terrain) ;
#                   compilation et lancement sans fenêtre (capture) ; exemples minimal et ui_gallery compilés depuis le paquet.
#   Chaque lancement a son dossier (rien n'est effacé). Les dépendances vcpkg du dépôt sont reliées au projet (équivalent de
#   `make install` dans le projet, sans le réseau).
E2E_RUN      := build/e2e/$(shell date +%Y%m%d-%H%M%S)
E2E_PLATFORM  = $(shell $(MAKE) -s -f $(ENGINE_MK) print-platform)
E2E_EXE       = $(if $(filter Windows_NT,$(OS)),.exe)
E2E_VCPKG    ?= $(abspath vcpkg_installed)
.PHONY: e2e-init
e2e-init:
	$(MAKE) -f $(ENGINE_MK) engine-tool
	mkdir -p $(E2E_RUN)
	cd $(E2E_RUN) && "$(abspath dist)/orosmo-$(E2E_PLATFORM)$(E2E_EXE)" init --package "$(abspath dist)/orosmo-pkg-$(E2E_PLATFORM).zip" mygame
	test -f $(E2E_RUN)/mygame/orosmo/tools/build_progress.py   # barre de progression du projet (Makefile du modèle)
	@test ! -e $(E2E_RUN)/mygame/orosmo/includes/orosmo/world
	@test ! -e $(E2E_RUN)/mygame/orosmo/includes/orosmo/renderer/terrain_renderer.hpp
	@test ! -e $(E2E_RUN)/mygame/orosmo/lib/debug/libworld.a
	@test ! -e $(E2E_RUN)/mygame/orosmo/lib/release/libworld.a
	@test ! -e $(E2E_RUN)/mygame/orosmo/lib/debug/libterrain_renderer.a
	@test ! -e $(E2E_RUN)/mygame/orosmo/lib/release/libterrain_renderer.a
	@test ! -e $(E2E_RUN)/mygame/orosmo/shaders/rhi/terrain.vert
	@test ! -e $(E2E_RUN)/mygame/orosmo/shaders/rhi/terrain.frag
	@test ! -e $(E2E_RUN)/mygame/orosmo/examples/terrain_gen
	@test ! -e $(E2E_RUN)/mygame/orosmo/examples/terrain_viewer
	ln -sfn "$(E2E_VCPKG)" $(E2E_RUN)/mygame/orosmo/vcpkg_installed
	$(MAKE) -C $(E2E_RUN)/mygame debug
	cd $(E2E_RUN)/mygame && ./bin/debug/main$(E2E_EXE) --api opengl --headless --capture "$(abspath $(E2E_RUN))/capture" --capture-frames 3
	test -s $(E2E_RUN)/capture/scene.png
	test -s $(E2E_RUN)/capture/ui.png
	$(MAKE) -C $(E2E_RUN)/mygame/orosmo/examples/minimal
	$(E2E_RUN)/mygame/orosmo/examples/minimal/bin/minimal$(E2E_EXE) > $(E2E_RUN)/minimal.txt
	grep -q '"sum":524800' $(E2E_RUN)/minimal.txt
	$(MAKE) -C $(E2E_RUN)/mygame/orosmo/examples/ui_gallery
	$(E2E_RUN)/mygame/orosmo/examples/ui_gallery/bin/ui-gallery$(E2E_EXE) --headless --capture $(E2E_RUN)/gallery --capture-frames 3
	test -s $(E2E_RUN)/gallery/gallery.png
	@echo "e2e-init : OK ($(E2E_RUN) : orosmo init, paquet cœur générique vérifié, compilation, lancement, exemples depuis le paquet)"

# ── UI-11 : tests du compilateur du langage d'interface ──
#   make cui-test   tests unitaires (cargo test) + entrées invalides de bout en bout (binaire cui : messages fichier:ligne:colonne ;
#                   une erreur du C++ généré ramenée à la ligne du .cui par #line). Les vues valides sont compilées avec `make demo-test`.
CUI_BIN := tools/cui$(if $(filter Windows_NT,$(OS)),.exe)
.PHONY: cui-test
cui-test:
	$(MAKE) -f $(ENGINE_MK) cui-tool
	cargo test --manifest-path tools/Cargo.toml -p cui -p orosmo
	python3 tools/check_cui_errors.py --cui $(CUI_BIN) --cases tests/ui/cui_errors --out build/cui-errors --cxx auto

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
	@rm -f $(TG_CHECK)/same.png
	@./$(TERRAIN_GEN) --seed 42 --size 8 --out $(TG_CHECK)/same.png --biomes $(TG_CHECK)/./same.png > /dev/null 2>&1; test $$? -eq 1
	@test ! -e $(TG_CHECK)/same.png
	@./$(TERRAIN_GEN) --seed 42 --size 8 --out $(TG_CHECK)/a.png --biomes $(TG_CHECK)/../terrain-gen-check/a.png > /dev/null 2>&1; test $$? -eq 1
	cmp $(TG_CHECK)/a.png $(TG_CHECK)/b.png
	@echo "terrain-gen-check : OK (mêmes octets, somme de contrôle de référence, codes de retour, destinations distinctes)"

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
	@rm -rf vcpkg_installed apps/demo/vcpkg_installed apps/demo/orosmo/vcpkg_installed
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

endif # _ORS_WRAP
