# Project configuration — commit this file.

# ── Engine ────────────────────────────────────────────────────────────────────
# Version of the Orosmo engine used by this project: `orosmo install <version>` installs it, the Makefile finds it with
# `orosmo path <version>`. `orosmo update` changes it. ENGINE_DIR=/path/to/engine (command line or config.local.mk) overrides it.
OROSMO_VERSION := {{OROSMO_VERSION}}

# ── Identity ──────────────────────────────────────────────────────────────────
PROJECT_NAME        ?= {{PROJECT_NAME}}
VERSION             ?= 0.1.0
PUBLISHER           ?=
MAINTAINER          ?=
PROJECT_DESCRIPTION ?=
ICON_NAME           ?=

# ── Windows packaging ─────────────────────────────────────────────────────────
ARCHITECTURE_WINDOWS ?= x64

# ── Linux packaging (.deb) ────────────────────────────────────────────────────
PACKAGE            ?= {{PROJECT_NAME_LOWER}}
ARCHITECTURE_LINUX ?= amd64
DEPENDS            ?=
SECTION            ?= utils
PRIORITY           ?= optional

# ── Extra vcpkg dependencies ──────────────────────────────────────────────────
# Added automatically by 'make add-dep DEP=<pkg>' (optional, game libraries only). Do not edit by hand.
EXTRA_LDLIBS   ?=
EXTRA_CXXFLAGS ?=
