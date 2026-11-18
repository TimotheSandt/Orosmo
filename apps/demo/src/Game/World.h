#pragma once

#include "engine/renderer/camera.hpp"
#include "engine/renderer/light.hpp"
#include "engine/renderer/mesh.hpp"
#include "engine/renderer/scene_renderer.hpp"
#include "engine/renderer/shader_library.hpp"
#include "engine/rhi/device.hpp"

#include "glm/glm.hpp"
#include "glm/gtx/hash.hpp"

#include "ProceduralGeneration/TerrainGenerator.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>

class World
{
  public:
    World();
    ~World();

    // Génère le terrain de `seed` et crée ses ressources GPU (maillage, lumières). `shaderDirectory` : shaders/rhi/.
    engine::core::Result<> Init(engine::rhi::Device &device, engine::renderer::ShaderLibrary &library, const std::filesystem::path &shaderDirectory,
                                std::uint32_t seed);
    void Destroy();
    void Update();
    void Render(engine::renderer::SceneRenderer &renderer, const engine::renderer::Camera &camera);

  private:
    TerrainGenerator terrain;
    engine::renderer::Mesh terrainMesh;
    engine::renderer::LightManager lightManager;
};
