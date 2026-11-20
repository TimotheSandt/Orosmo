#pragma once

#include "orosmo/core/tasks.hpp"
#include "orosmo/renderer/camera.hpp"
#include "orosmo/renderer/light.hpp"
#include "orosmo/renderer/scene_renderer.hpp"
#include "orosmo/renderer/shader_library.hpp"
#include "orosmo/renderer/terrain_renderer.hpp"
#include "orosmo/rhi/device.hpp"
#include "orosmo/world/streaming.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <unordered_map>

// Monde de la démo : terrain `orosmo::world` diffusé par tuiles autour de la caméra (TileStreamer, génération en tâches de fond)
// et dessiné par le TerrainRenderer, sous une lumière ambiante et un soleil.
class World
{
  public:
    World();
    ~World();

    // Paramètres du terrain de la démo (relief, échelle) ; la hauteur ne dépend que d'eux et de la graine.
    static orosmo::world::TerrainParams TerrainParameters();
    // Point de départ de la caméra : au-dessus du sol en (0, 0), d'où le relief est visible.
    static glm::vec3 StartPosition(std::uint32_t seed);
    static glm::vec3 StartOrientation();

    // Prépare le rendu du terrain et les lumières. `shaderDirectory` : shaders/rhi/. `waitForTiles` (capture, mode sans fenêtre) :
    // chaque image attend que toutes les tuiles visibles soient générées, pour une image complète et reproductible.
    orosmo::core::Result<> Init(orosmo::rhi::Device &device, orosmo::renderer::ShaderLibrary &library, const std::filesystem::path &shaderDirectory,
                                std::uint32_t seed, bool waitForTiles = false);
    void Destroy();
    void Update();
    // Suit la caméra (sélection des tuiles, envoi de celles qui sont prêtes) puis dessine le monde ; l'échec d'un dessin (pipeline
    // qui ne se lie pas...) est renvoyé à l'appelant.
    orosmo::core::Result<> Render(orosmo::renderer::SceneRenderer &renderer, const orosmo::renderer::Camera &camera);

  private:
    void Stream(const glm::vec3 &cameraPosition);
    orosmo::core::Result<> SyncTiles(bool &complete);

    orosmo::world::TerrainParams params;
    std::uint32_t seed = 0;
    bool waitForTiles = false;
    std::unique_ptr<orosmo::core::TaskSystem> tasks;
    std::unique_ptr<orosmo::world::TileStreamer> streamer;
    std::unique_ptr<orosmo::renderer::TerrainRenderer> terrainRenderer;
    // Tuiles envoyées au TerrainRenderer : tuile résidente du streamer et couture du maillage envoyé.
    struct ShownTile
    {
        const orosmo::world::ResidentTile *tile = nullptr;
        orosmo::world::CoarserNeighbors stitch{};
    };
    std::unordered_map<std::uint64_t, ShownTile> shown;
    orosmo::renderer::LightManager lightManager;
};
