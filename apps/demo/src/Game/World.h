#pragma once

#include "Camera.h"

#include <cstdint>
#include <optional>

#include "glm/glm.hpp"
#include "glm/gtx/hash.hpp"

#include "Renderer3D.h"
#include "ProceduralGeneration/TerrainGenerator.h"
#include "Light.h"

class World
{
  public:
    World();
    ~World();

    void Init(std::optional<std::uint32_t> seed = std::nullopt);
    void Destroy();

    void Update();

    void Render(Renderer3D &renderer3D, Camera &camera);

  private:
    TerrainGenerator terrain;
    LightManager lightManager;
};
