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

.PHONY: check check-repo check-layers check-api check-headers check-determinism check-game-static compile-commands
check: check-repo check-layers check-api check-headers check-determinism check-game-static

compile-commands:
	@python3 tools/gen_compile_commands.py --build-type $(BUILD_TYPE) --cxx "$(CXX)" --cc "$(CC)"

check-repo:
	@sh tools/check-repo.sh

check-layers:
	@python3 tools/check_layers.py --strict

check-api:
	@python3 tools/check_api_isolation.py --strict

check-headers:
	@python3 tools/check_headers.py --strict

# SIM-01 (A79 L3) : ni fonction transcendante, ni rand(), ni horloge dans le chemin rejoué de la simulation.
check-determinism:
	@python3 tools/check_deterministic_math.py

# GAME-02 (A79 L5, règle 13) : aucun état modifiable dans la bibliothèque de jeu, vérifié sur le binaire (nm).
check-game-static:
	@python3 tools/check_game_static.py

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

# In-repo layout: demo resources, shaders and tests live at the repository root; the engine is the SDK synced into
# apps/demo/orosmo/ (sync-demo), never an installed version.
DEMO_ARGS  := ENGINE_DIR=orosmo RES_DIR=../../res SHADER_DIR=../../shaders TEST_DIR=../../tests ICON_NAME=../../res/public/icon.ico

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
GPU_TEST_SUITES := rhi/opengl*,rhi/vulkan*,rhi/backend*,renderer/*,platform/window,ui/render
.PHONY: demo-test-unit demo-test-gpu
demo-test-unit:
	$(MAKE) -f $(ENGINE_MK) BUILD_TYPE=release release
	$(MAKE) -f $(ENGINE_MK) sync-demo BUILD_TYPE=release
	$(MAKE) -C apps/demo $(DEMO_ARGS) TEST_ARGS='--test-suite-exclude=$(GPU_TEST_SUITES)' test
demo-test-gpu:
	$(MAKE) -f $(ENGINE_MK) BUILD_TYPE=release release
	$(MAKE) -f $(ENGINE_MK) sync-demo BUILD_TYPE=release
	$(MAKE) -C apps/demo $(DEMO_ARGS) TEST_ARGS='--test-suite=$(GPU_TEST_SUITES)' test

# ── Demo headless captures (HUD, UI tabs, themes, dropdown/tooltip/dialog/menu/notification) ──
DEMO_BIN := apps/demo/bin/debug/main$(if $(filter Windows_NT,$(OS)),.exe)
.PHONY: demo-capture demo-vulkan-check
demo-capture: all-debug
	./$(DEMO_BIN) --api opengl --headless --ui hud --capture build/demo-capture/hud --capture-frames 5
	./$(DEMO_BIN) --api opengl --headless --ui off --shadows on --capture build/demo-capture/shadows --capture-frames 5
	for tab in scene widgets controls color windows layout themes; do \
	    ./$(DEMO_BIN) --api opengl --headless --ui full --theme dark --tab $$tab --capture build/demo-capture/dark-$$tab --capture-frames 5 || exit 1; done
	./$(DEMO_BIN) --api opengl --headless --ui full --theme light --tab widgets --capture build/demo-capture/light-widgets --capture-frames 5
	./$(DEMO_BIN) --api opengl --headless --ui full --theme dark --tab widgets --click fruit --capture build/demo-capture/dark-dropdown-open --capture-frames 5
	./$(DEMO_BIN) --api opengl --headless --ui full --theme light --tab widgets --hover volume --capture-frames 60 --capture build/demo-capture/light-tooltip
	./$(DEMO_BIN) --api opengl --headless --ui full --theme framed --tab themes --capture build/demo-capture/framed-themes --capture-frames 5
	./$(DEMO_BIN) --api opengl --headless --ui full --theme dark --tab windows --click openDialog --capture build/demo-capture/dark-dialog --capture-frames 5
	./$(DEMO_BIN) --api opengl --headless --ui full --theme dark --tab windows --click menuFile --capture build/demo-capture/dark-menu --capture-frames 5
	./$(DEMO_BIN) --api opengl --headless --ui full --theme light --tab windows --click notifyWarning --capture build/demo-capture/light-notification --capture-frames 5
	@# REN-17/18 : post-traitements (--post hdr|bloom|full) comparés à leurs références (tolérance de llvmpipe contre un GPU).
	for post in hdr bloom full; do \
	    ./$(DEMO_BIN) --api opengl --headless --ui off --post $$post --capture build/demo-capture/post-$$post --capture-frames 5 || exit 1; \
	    python3 tools/compare_png.py docs/BASELINE/reference/scene-opengl-post-$$post-1280x720.png build/demo-capture/post-$$post/scene.png \
	        --tolerance 8 --max-diff-percent 2 || exit 1; done
	./$(DEMO_BIN) --api opengl --headless --ui off --scene mixed --post full --capture build/demo-capture/mixed-post-full --capture-frames 5
	python3 tools/compare_png.py docs/BASELINE/reference/scene-opengl-mixed-post-full-1280x720.png build/demo-capture/mixed-post-full/scene.png \
	    --tolerance 8 --max-diff-percent 2
	@# REN-39 : ombre des vitres tramée (--transparent-shadow dithered) comparée à sa référence.
	./$(DEMO_BIN) --api opengl --headless --ui off --scene mixed --shadows on --transparent-shadow dithered --capture build/demo-capture/mixed-shadow-dithered --capture-frames 5
	python3 tools/compare_png.py docs/BASELINE/reference/scene-opengl-mixed-shadow-dithered-1280x720.png build/demo-capture/mixed-shadow-dithered/scene.png \
	    --tolerance 8 --max-diff-percent 2
	@# M6 : personnage skinné animé (glTF généré puis importé) et foule de 1000 agents (un draw call par primitive skinnée).
	./$(DEMO_BIN) --api opengl --headless --ui hud --scene anim --capture build/demo-capture/anim --capture-frames 30
	test -s build/demo-capture/anim/scene.png
	@# UI-04, UI-10 (A79 lot L10) : outils de debug (F12) ouverts sur chaque onglet, au-dessus de l'interface complète.
	for tab in widgets console entities profiler; do \
	    ./$(DEMO_BIN) --api opengl --headless --ui full --theme dark --tab widgets --debug-tab $$tab --capture build/demo-capture/debug-tools-$$tab --capture-frames 30 || exit 1; \
	    test -s build/demo-capture/debug-tools-$$tab/ui.png || exit 1; done
	test -s build/demo-capture/dark-widgets/ui.png
	test -s build/demo-capture/light-widgets/ui.png
	@# compare_png.py : code 0 = identiques (erreur ici) ; 1 + ligne DIFFERENT = images différentes (attendu) ; autre = erreur d'exécution.
	@out=$$(python3 tools/compare_png.py build/demo-capture/dark-widgets/ui.png build/demo-capture/light-widgets/ui.png); rc=$$?; \
	if [ $$rc -eq 0 ]; then echo "demo-capture : les captures dark et light sont identiques"; exit 1; \
	elif [ $$rc -eq 1 ] && printf '%s\n' "$$out" | grep -q '^DIFFERENT'; then echo "demo-capture : OK (dark != light)"; \
	else echo "demo-capture : erreur du comparateur (code $$rc) : $$out"; exit 1; fi
	@# UI-09 : focus clavier sur un bouton radio de l'onglet Contrôles, anneau de focus du thème comparé à sa référence, et absent sans focus.
	./$(DEMO_BIN) --api opengl --headless --ui full --theme dark --tab controls --focus detailMedium --capture build/demo-capture/dark-controls-focus --capture-frames 5
	python3 tools/compare_png.py docs/BASELINE/reference/ui-opengl-controls-focus-1280x720.png build/demo-capture/dark-controls-focus/ui.png \
	    --tolerance 8 --max-diff-percent 2
	@out=$$(python3 tools/compare_png.py build/demo-capture/dark-controls/ui.png build/demo-capture/dark-controls-focus/ui.png); rc=$$?; \
	if [ $$rc -eq 0 ]; then echo "demo-capture : l'anneau de focus n'est pas dessiné"; exit 1; \
	elif [ $$rc -eq 1 ] && printf '%s\n' "$$out" | grep -q '^DIFFERENT'; then echo "demo-capture : OK (anneau de focus)"; \
	else echo "demo-capture : erreur du comparateur (code $$rc) : $$out"; exit 1; fi

# M4 : la démo donne la même image dans la tolérance en Vulkan et en OpenGL, et les couches de validation Vulkan (synchronisation
# comprise) restent silencieuses (0 message). `make demo-vulkan-check FULL=1` ajoute tous les onglets de demo-capture.
demo-vulkan-check: all-debug
	python3 tools/demo_parity.py --bin $(DEMO_BIN) --out build/demo-parity $(if $(FULL),--full)

# ── BLD-02 / A76 : de bout en bout depuis le paquet (sans les sources du moteur ni vcpkg), par le chemin de l'utilisateur ──
#   make e2e-init   paquet + outil `orosmo` construits ; OROSMO_HOME isolé dans build/e2e/<date>/home ; l'outil y est copié dans
#                   bin/ (comme install.sh) et mis en tête du PATH ;
#                   `orosmo install --package <zip>` installe le moteur dans home/engines/<version>, mis ensuite en LECTURE
#                   SEULE : rien ne doit y écrire ; contenu vérifié (deps/ sans bibliothèque partagée, VERSION, LICENSE,
#                   aucun module procédural) ;
#                   `orosmo init` d'un projet neuf (sans réseau : OROSMO_DOWNLOAD_BASE = un faux dossier de release avec
#                   SHA256SUMS), `make debug` SANS ENGINE_DIR (le Makefile du projet trouve le moteur par `orosmo path`),
#                   aucun vcpkg_installed, Generated/ dans le projet ; `orosmo build --json` ; `orosmo run -- --api opengl
#                   --headless …` (capture) si E2E_RUN_GRAPHICS=1 ; exemple minimal copié hors du moteur et compilé ; CMake.
#   Chaque lancement a son dossier (rien n'est effacé ; les droits d'écriture du moteur installé sont rendus à la fin).
E2E_RUN      := build/e2e/$(shell date +%Y%m%d-%H%M%S)
E2E_PLATFORM  = $(shell $(MAKE) -s -f $(ENGINE_MK) print-platform)
E2E_EXE       = $(if $(filter Windows_NT,$(OS)),.exe)
# 1 : e2e-init lance aussi le projet créé (OpenGL 4.3 requis : Linux, ou Mesa sous xvfb). 0 : seulement compiler et lier (macOS ≤ 4.1, Windows sans GPU).
E2E_RUN_GRAPHICS ?= 1
# GAME-06 : durée maximale « .cpp du jeu modifié -> code rechargé » (5 s sur la machine de référence ; plus large en CI).
E2E_RELOAD_MAX ?= 5
E2E_HOME      = $(abspath $(E2E_RUN))/home
E2E_PKG       = $(abspath dist)/orosmo-pkg-$(E2E_PLATFORM).zip
E2E_TOOL      = $(E2E_HOME)/bin/orosmo$(E2E_EXE)
# Environnement de l'utilisateur après install.sh : OROSMO_HOME, outil dans le PATH ; faux serveur de release (aucun réseau).
# Les commandes `orosmo` partent d'un environnement de shell (sans les variables du make appelant) ; `$(MAKE)` garde son jobserver.
# bin/ entre dans le PATH par `pwd` : sous MSYS2, un chemin D:/… casserait le PATH (séparé par « : »).
E2E_VARS      = OROSMO_HOME="$(E2E_HOME)" OROSMO_DOWNLOAD_BASE="$(abspath $(E2E_RUN))/release" PATH="$$(cd "$(E2E_HOME)/bin" && pwd):$$PATH"
E2E_ENV       = env -u ENGINE_DIR $(E2E_VARS)
E2E_SH        = env -u ENGINE_DIR -u MAKEFLAGS -u MFLAGS -u MAKELEVEL $(E2E_VARS)
# Dossier du moteur installé : écrit par `orosmo path` pendant la recette (lu par le shell, pas par make).
E2E_ENGINE    = $$(cat "$(abspath $(E2E_RUN))/engine-dir")
.PHONY: e2e-init
e2e-init:
	$(MAKE) -f $(ENGINE_MK) engine-tool
	mkdir -p $(E2E_HOME)/bin $(E2E_RUN)/release $(E2E_RUN)/minimal
	cp "$(abspath dist)/orosmo-$(E2E_PLATFORM)$(E2E_EXE)" "$(E2E_TOOL)"
	cp "$(E2E_PKG)" $(E2E_RUN)/release/
	cd $(E2E_RUN)/release && { sha256sum orosmo-pkg-$(E2E_PLATFORM).zip 2>/dev/null || shasum -a 256 orosmo-pkg-$(E2E_PLATFORM).zip; } > SHA256SUMS
	$(E2E_SH) orosmo install --package "$(E2E_PKG)"
	v=$$(ls "$(E2E_HOME)/engines") && $(E2E_SH) orosmo path "$$v" > "$(abspath $(E2E_RUN))/engine-dir"
	@echo "e2e-init : moteur installé dans $(E2E_ENGINE)"
	test -x "$(E2E_ENGINE)/tools/cui$(E2E_EXE)"
	test -f "$(E2E_ENGINE)/tools/build_progress.py"   # barre de progression du projet (Makefile du modèle)
	test -s "$(E2E_ENGINE)/VERSION" && test -s "$(E2E_ENGINE)/LICENSE"
	test -d "$(E2E_ENGINE)/deps/include" && test -d "$(E2E_ENGINE)/deps/lib"
	@test -z "$$(find "$(E2E_ENGINE)/deps" \( -name '*.so' -o -name '*.so.*' -o -name '*.dylib' -o -name '*.dll' -o -name 'libvulkan*' \) -print)" \
	    || { echo "e2e-init : bibliothèque partagée ou chargeur Vulkan dans deps/"; exit 1; }
	@e="$(E2E_ENGINE)"; for f in includes/orosmo/world includes/orosmo/renderer/terrain_renderer.hpp \
	    lib/debug/libworld.a lib/release/libworld.a lib/debug/libterrain_renderer.a lib/release/libterrain_renderer.a \
	    shaders/rhi/terrain.vert shaders/rhi/terrain.frag examples/terrain_gen examples/terrain_viewer; do \
	    test ! -e "$$e/$$f" || { echo "e2e-init : module procédural dans le paquet : $$f"; exit 1; }; done
	cp "$(E2E_ENGINE)/examples/minimal/main.cpp" "$(E2E_ENGINE)/examples/minimal/Makefile" $(E2E_RUN)/minimal/
	chmod -R a-w "$(E2E_ENGINE)"
	cd $(E2E_RUN) && $(E2E_SH) orosmo init mygame
	test ! -e $(E2E_RUN)/mygame/vcpkg_installed && test ! -e $(E2E_RUN)/mygame/orosmo
	grep -q "^OROSMO_VERSION *:= *$$(cat "$(E2E_ENGINE)/VERSION")" $(E2E_RUN)/mygame/config.mk
	@# Sans ENGINE_DIR : le Makefile du projet appelle `orosmo path $(OROSMO_VERSION)` (outil trouvé dans le PATH).
	cd $(E2E_RUN)/mygame && $(E2E_ENV) $(MAKE) debug
	test -n "$$(find $(E2E_RUN)/mygame/Generated -name '*.gen.cpp')"   # sortie de cui dans le projet
	@# A79 L5 : `make debug` construit la logique de jeu (src/game/) en bibliothèque rechargeable, à côté de l'exécutable.
	test -n "$$(find $(E2E_RUN)/mygame/bin/debug \( -name 'game.so' -o -name 'game.dylib' -o -name 'game.dll' \) -print)"
	@# `make release` la lie statiquement, sans code de rechargement (GAME-07).
	cd $(E2E_RUN)/mygame && $(E2E_ENV) $(MAKE) release
	@syms=$$(nm -C $(E2E_RUN)/mygame/bin/release/mygame$(E2E_EXE)) || { echo "e2e-init : nm a échoué, contrôle GAME-07 impossible"; exit 1; }; \
	    ! printf '%s\n' "$$syms" | grep -E 'orosmo::platform::DynamicLibrary|GameModule::load\(|DynamicReloader' \
	    || { echo "e2e-init : code de rechargement dans la version finale"; exit 1; }
	@# 2e build (copy_res recopie les ressources du moteur en lecture seule : elles doivent rester inscriptibles, G220).
	cd $(E2E_RUN)/mygame && $(E2E_SH) orosmo build --json > ../build.json
	@# Motif sans guillemets doubles : sous mingw32-make (Windows), '"…"' arrive à grep avec des barres obliques inverses.
	grep -q 'event.:.done' $(E2E_RUN)/build.json || { echo "e2e-init : pas d'événement done dans build.json :"; tail -n 20 $(E2E_RUN)/build.json; exit 1; }
	@if [ "$(E2E_RUN_GRAPHICS)" = 1 ]; then \
	    echo "e2e-init : orosmo run (OpenGL, sans fenêtre)"; \
	    cd $(E2E_RUN)/mygame && $(E2E_SH) orosmo run -- --api opengl --headless \
	        --capture "$(abspath $(E2E_RUN))/capture" --capture-frames 3 \
	      && test -s "$(abspath $(E2E_RUN))/capture/scene.png" && test -s "$(abspath $(E2E_RUN))/capture/ui.png"; \
	  else \
	    echo "e2e-init : lancement graphique sauté (E2E_RUN_GRAPHICS=0 : exécuteur sans OpenGL 4.3, projet seulement compilé et lié)"; \
	  fi
	@# GAME-06 (A79 L7) : projet de même Makefile et même src/game/ que mygame, avec un hôte sans GPU
	@# (tools/e2e/reload_host.cpp). `make game` ne refait que la bibliothèque, avec ccache s'il est là (un faux suffit ici) ;
	@# puis, programme lancé, un .cpp du jeu est modifié, reconstruit par `orosmo build --game` et rechargé, temps mesuré.
	mkdir -p $(E2E_RUN)/reload/src $(E2E_RUN)/fake-ccache
	cp $(E2E_RUN)/mygame/Makefile $(E2E_RUN)/mygame/config.mk $(E2E_RUN)/reload/
	cp -r $(E2E_RUN)/mygame/src/game $(E2E_RUN)/reload/src/
	cp tools/e2e/reload_host.cpp $(E2E_RUN)/reload/src/main.cpp
	cd $(E2E_RUN)/reload && $(E2E_ENV) $(MAKE) debug
	printf '#!/bin/sh\nexec "$$@"\n' > $(E2E_RUN)/fake-ccache/ccache
	chmod +x $(E2E_RUN)/fake-ccache/ccache
	cd $(E2E_RUN)/reload && $(E2E_ENV) PATH="$$(cd ../fake-ccache && pwd):$$(cd "$(E2E_HOME)/bin" && pwd):$$PATH" \
	    $(MAKE) -n -W src/game/game.cpp game > ../game-goal.txt
	grep -q 'ccache .*src/game/game\.cpp' $(E2E_RUN)/game-goal.txt
	@! grep -E 'src/main\.cpp|-o bin/debug/main(\.exe)?( |$$)' $(E2E_RUN)/game-goal.txt \
	    || { echo "e2e-init : make game refait autre chose que la bibliothèque de jeu"; exit 1; }
	$(E2E_SH) python3 tools/e2e_reload.py --project $(E2E_RUN)/reload --tool "$(E2E_TOOL)" \
	    --exe bin/debug/main$(E2E_EXE) --max $(E2E_RELOAD_MAX) --record $(E2E_RUN)/reload.txt
	$(MAKE) -C $(E2E_RUN)/minimal ENGINE_DIR="$(E2E_ENGINE)"
	$(E2E_RUN)/minimal/bin/minimal$(E2E_EXE) > $(E2E_RUN)/minimal.txt
	@echo "e2e-init : sortie de l'exemple minimal :"; cat $(E2E_RUN)/minimal.txt
	grep -q 524800 $(E2E_RUN)/minimal.txt
	@# doctor avec le même OROSMO_HOME que install/init (G219).
	cd $(E2E_RUN)/mygame && $(E2E_SH) orosmo doctor
	cd $(E2E_RUN)/mygame && $(E2E_ENV) $(MAKE) compile-commands
	test -s $(E2E_RUN)/mygame/compile_commands.json
	@if command -v cmake >/dev/null 2>&1; then \
	    mkdir -p $(E2E_RUN)/cmake-test && \
	    printf 'cmake_minimum_required(VERSION 3.20)\nproject(orosmo_cmake_e2e LANGUAGES CXX)\nfind_package(Orosmo REQUIRED)\nadd_executable(minimal "%s")\ntarget_link_libraries(minimal PRIVATE Orosmo::Orosmo)\n' \
	        "$(abspath $(E2E_RUN)/minimal/main.cpp)" > $(E2E_RUN)/cmake-test/CMakeLists.txt && \
	    cmake -S $(E2E_RUN)/cmake-test -B $(E2E_RUN)/cmake-test/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(E2E_ENGINE)" && \
	    cmake --build $(E2E_RUN)/cmake-test/build && \
	    $(E2E_RUN)/cmake-test/build/minimal$(E2E_EXE) > $(E2E_RUN)/cmake-minimal.txt && \
	    grep -q 524800 $(E2E_RUN)/cmake-minimal.txt; \
	fi
	@# Lecture seule tenue jusqu'ici ; droits rendus pour pouvoir effacer build/.
	chmod -R u+w "$(E2E_ENGINE)"
	@echo "e2e-init : OK ($(E2E_RUN) : orosmo install --package, moteur en lecture seule dans OROSMO_HOME, orosmo init, make sans ENGINE_DIR (orosmo path), orosmo build --json, orosmo run headless si E2E_RUN_GRAPHICS=1, doctor, Make + CMake, compile_commands.json, exemple minimal)"

# ── UI-11 : tests du compilateur du langage d'interface ──
#   make cui-test   tests unitaires (cargo test) + entrées invalides de bout en bout (binaire cui : messages fichier:ligne:colonne ;
#                   une erreur du C++ généré ramenée à la ligne du .cui par #line). Les vues valides sont compilées avec `make demo-test`.
CUI_BIN := tools/cui$(if $(filter Windows_NT,$(OS)),.exe)
.PHONY: cui-test
cui-test:
	$(MAKE) -f $(ENGINE_MK) cui-tool
	cargo test --manifest-path tools/Cargo.toml -p cui -p orosmo
	python3 tools/check_cui_errors.py --cui $(CUI_BIN) --cases tests/ui/cui_errors --out build/cui-errors --cxx auto

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
