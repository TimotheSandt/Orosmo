# Projet minimal : un exécutable lié au moteur. Hors du dépôt : ENGINE_DIR = le paquet décompressé (ou orosmo/ d'un projet
# créé par `orosmo init`).
ENGINE_DIR   ?= $(if $(wildcard ../../make/engine.mk),../..,../../../../apps/demo/orosmo)
GAME_SRC_DIR := .
BUILD_TYPE   ?= release
include $(ENGINE_DIR)/make/engine.mk

.DEFAULT_GOAL := all
.PHONY: all
all: bin/minimal$(ENGINE_EXE_EXT)

bin/minimal$(ENGINE_EXE_EXT): obj/main.o $(ENGINE_LIBS)
	@mkdir -p bin
	$(call ors_link,LD)
	$(ORS_Q)$(CXX) $(ENGINE_CXXFLAGS) obj/main.o $(ENGINE_LINK_LIBS) $(ENGINE_LDFLAGS) -o $@
	$(ors_end)

obj/main.o: main.cpp
	@mkdir -p obj
	$(call ors_compile,CXX)
	$(ORS_Q)$(CXX) $(ENGINE_CXXFLAGS) -MMD -MP $(ENGINE_INCLUDES) -c $< -o $@
	$(ors_end)

-include obj/main.d
