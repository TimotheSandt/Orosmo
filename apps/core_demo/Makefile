# Démo du noyau (M1) : utilise le SDK moteur synchronisé dans apps/demo/orosmo (voir `make core-demo` à la racine).
ENGINE_DIR   := ../demo/orosmo
GAME_SRC_DIR := src
BUILD_TYPE   ?= release
SHELL        := sh
include $(ENGINE_DIR)/make/engine.mk

TARGET := bin/core_demo$(ENGINE_EXE_EXT)
.DEFAULT_GOAL := all

.PHONY: all run clean
all: $(TARGET)

$(TARGET): obj/main.o $(ENGINE_LIBS)
	@mkdir -p bin
	$(call ors_link,LD)
	$(ORS_Q)$(CXX) $(ENGINE_CXXFLAGS) obj/main.o $(ENGINE_LINK_LIBS) $(ENGINE_LDFLAGS) -o $@
	$(ors_end)

obj/main.o: main.cpp
	@mkdir -p obj
	$(call ors_compile,CXX)
	$(ORS_Q)$(CXX) $(ENGINE_CXXFLAGS) -MMD -MP $(ENGINE_INCLUDES) -c $< -o $@
	$(ors_end)

run: $(TARGET)
	./$(TARGET) $(ARGS)

clean:
	rm -rf obj bin

-include obj/main.d
