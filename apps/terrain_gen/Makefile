# TOOL-02 : terrain-gen (sans fenêtre ni GPU). Utilise le SDK moteur synchronisé dans apps/demo/orosmo (`make terrain-gen` à la racine).
# Dans le dépôt : le SDK synchronisé (apps/demo/orosmo) ; dans le paquet du moteur (examples/<nom>/) : la racine du paquet.
ENGINE_DIR   ?= $(if $(wildcard ../../make/engine.mk),../..,../demo/orosmo)
GAME_SRC_DIR := .
BUILD_TYPE   ?= release
SHELL        := sh
include $(ENGINE_DIR)/make/engine.mk

TARGET := bin/terrain-gen$(ENGINE_EXE_EXT)
.DEFAULT_GOAL := all

.PHONY: all run clean
all: $(TARGET)

$(TARGET): obj/main.o $(ENGINE_LIBS)
	@mkdir -p bin
	$(CXX) $(ENGINE_CXXFLAGS) obj/main.o $(ENGINE_LINK_LIBS) $(ENGINE_LDFLAGS) -o $@

obj/main.o: main.cpp
	@mkdir -p obj
	$(CXX) $(ENGINE_CXXFLAGS) -MMD -MP $(ENGINE_INCLUDES) -c $< -o $@

run: $(TARGET)
	./$(TARGET) $(ARGS)

clean:
	rm -rf obj bin

-include obj/main.d
