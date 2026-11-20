#pragma once

#include "engine/core/tasks.hpp"
#include "engine/renderer/camera.hpp"
#include "engine/renderer/light.hpp"
#include "engine/renderer/scene_renderer.hpp"
#include "engine/renderer/shader_library.hpp"
#include "engine/renderer/terrain_renderer.hpp"
#include "engine/rhi/device.hpp"
#include "engine/world/streaming.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <unordered_map>

// Monde de la démo : terrain `engine::world` diffusé par tuiles autour de la caméra (TileStreamer, génération en tâches de fond)
// et dessiné par le TerrainRenderer, sous une lumière ambiante et un soleil.
class World
{
  public:
    World();
    ~World();

    // Paramètres du terrain de la démo (relief, échelle) ; la hauteur ne dépend que d'eux et de la graine.
    static engine::world::TerrainParams TerrainParameters();
    // Point de départ de la caméra : au-dessus du sol en (0, 0), d'où le relief est visible.
    static glm::vec3 StartPosition(std::uint32_t seed);
    static glm::vec3 StartOrientation();

    // Prépare le rendu du terrain et les lumières. `shaderDirectory` : shaders/rhi/. `waitForTiles` (capture, mode sans fenêtre) :
    // chaque image attend que toutes les tuiles visibles soient générées, pour une image complète et reproductible.
    engine::core::Result<> Init(engine::rhi::Device &device, engine::renderer::ShaderLibrary &library, const std::filesystem::path &shaderDirectory,
                                std::uint32_t seed, bool waitForTiles = false);
    void Destroy();
    void Update();
    // Suit la caméra (sélection des tuiles, envoi de celles qui sont prêtes) puis dessine le monde ; l'échec d'un dessin (pipeline
    // qui ne se lie pas...) est renvoyé à l'appelant.
    engine::core::Result<> Render(engine::renderer::SceneRenderer &renderer, const engine::renderer::Camera &camera);

  private:
    void Stream(const glm::vec3 &cameraPosition);
    engine::core::Result<> SyncTiles(bool &complete);

    engine::world::TerrainParams params;
    std::uint32_t seed = 0;
    bool waitForTiles = false;
    std::unique_ptr<engine::core::TaskSystem> tasks;
    std::unique_ptr<engine::world::TileStreamer> streamer;
    std::unique_ptr<engine::renderer::TerrainRenderer> terrainRenderer;
    // Tuiles envoyées au TerrainRenderer : tuile résidente du streamer et couture du maillage envoyé.
    struct ShownTile
    {
        const engine::world::ResidentTile *tile = nullptr;
        engine::world::CoarserNeighbors stitch{};
    };
    std::unordered_map<std::uint64_t, ShownTile> shown;
    engine::renderer::LightManager lightManager;
};
