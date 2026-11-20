# Projet minimal : un exécutable lié au moteur. Hors du dépôt : ENGINE_DIR = le paquet décompressé (ou engine/ d'un projet
# créé par `orosmo init`).
ENGINE_DIR   ?= $(if $(wildcard ../../make/engine.mk),../..,../demo/engine)
GAME_SRC_DIR := .
BUILD_TYPE   ?= release
include $(ENGINE_DIR)/make/engine.mk

.DEFAULT_GOAL := all
.PHONY: all
all: bin/minimal$(ENGINE_EXE_EXT)

bin/minimal$(ENGINE_EXE_EXT): obj/main.o $(ENGINE_LIBS)
	@mkdir -p bin
	$(CXX) $(ENGINE_CXXFLAGS) obj/main.o $(ENGINE_LINK_LIBS) $(ENGINE_LDFLAGS) -o $@

obj/main.o: main.cpp
	@mkdir -p obj
	$(CXX) $(ENGINE_CXXFLAGS) -MMD -MP $(ENGINE_INCLUDES) -c $< -o $@

-include obj/main.d
