# TOOL-01 : terrain-viewer. Utilise le SDK moteur synchronisé dans apps/demo/orosmo (`make viewer` à la racine du dépôt).
# Le panneau src/TerrainPanel.cui est compilé par cui (cible orosmo/ui) puis avec le reste.
# Dans le dépôt : le SDK synchronisé (apps/demo/orosmo) ; dans le paquet du moteur (examples/<nom>/) : la racine du paquet.
ENGINE_DIR           ?= $(if $(wildcard ../../make/engine.mk),../..,../demo/orosmo)
ENGINE_EXTRA_MODULES := world terrain_renderer
GAME_SRC_DIR         := src
BUILD_TYPE   ?= release
SHELL        := sh
include $(ENGINE_DIR)/make/engine.mk

TARGET   := bin/terrain-viewer$(ENGINE_EXE_EXT)
SRCS     := $(wildcard src/*.cpp)
OBJS     := $(SRCS:src/%.cpp=obj/%.o) $(GEN_CPP_SOURCES:$(ENGINE_GEN_DIR)/%.gen.cpp=obj/gen/%.o)
# Ressources : celles du paquet (res/, shaders/) s'il y en a, sinon celles du dépôt.
ENGINE_RES     ?= $(if $(wildcard $(ENGINE_DIR)/res/fonts),$(ENGINE_DIR)/res,../../res)
ENGINE_SHADERS ?= $(if $(wildcard $(ENGINE_DIR)/shaders/rhi),$(ENGINE_DIR)/shaders,../../shaders)
RES_OUT  := bin/res
.DEFAULT_GOAL := all

.PHONY: all run resources clean
all: $(TARGET) resources

$(TARGET): $(OBJS) $(ENGINE_LIBS)
	@mkdir -p bin
	$(call ors_link,LD)
	$(ORS_Q)$(CXX) $(ENGINE_CXXFLAGS) $(OBJS) $(ENGINE_LINK_LIBS) $(ENGINE_LDFLAGS) -o $@
	$(ors_end)

obj/%.o: src/%.cpp | $(GEN_CPP_SOURCES)
	@mkdir -p "$(dir $@)"
	$(call ors_compile,CXX)
	$(ORS_Q)$(CXX) $(ENGINE_CXXFLAGS) -MMD -MP $(ENGINE_INCLUDES) -Isrc -c $< -o $@
	$(ors_end)

obj/gen/%.o: $(ENGINE_GEN_DIR)/%.gen.cpp
	@mkdir -p "$(dir $@)"
	$(call ors_compile,CXX)
	$(ORS_Q)$(CXX) $(ENGINE_CXXFLAGS) -MMD -MP $(ENGINE_INCLUDES) -Isrc -c $< -o $@
	$(ors_end)

# Ressources à côté de l'exécutable : police, thèmes (rechargés à chaud depuis bin/res/themes), shaders du RHI, préréglages.
resources:
	@mkdir -p "$(RES_OUT)/fonts" "$(RES_OUT)/themes" "$(RES_OUT)/shader/rhi" "$(RES_OUT)/presets"
	cp "$(ENGINE_RES)/fonts/Roboto-Regular.ttf" "$(RES_OUT)/fonts/"
	cp -r "$(ENGINE_RES)/themes/." "$(RES_OUT)/themes/"
	cp "$(ENGINE_SHADERS)/rhi/"* "$(RES_OUT)/shader/rhi/"  # chemins entre guillemets (espaces) ; seul * reste dehors
	cp presets/*.json "$(RES_OUT)/presets/"

run: all
	./$(TARGET) $(ARGS)

clean:
	rm -rf obj bin

-include $(wildcard obj/*.d obj/gen/*.d)
