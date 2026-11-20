#include "World.h"

#include <engine/core/log.hpp>

namespace rd = engine::renderer;

World::World() {}
World::~World() { this->Destroy(); }

engine::core::Result<> World::Init(engine::rhi::Device &device, rd::ShaderLibrary &library, const std::filesystem::path &shaderDirectory,
                                   std::uint32_t seed)
{
    auto lights = rd::LightManager::create(device);
    if (!lights)
    {
        return std::unexpected(lights.error());
    }
    this->lightManager = std::move(*lights);
    this->lightManager.setAmbientLight(glm::vec3(1.0f, 1.0f, 1.0f), 0.2f);
    this->lightManager.addLight(rd::directionalLight(glm::vec3(2.0f, -3.0f, 0.5f), glm::vec3(0.99f, 0.76f, 0.81f), 1.0f));
    if (auto uploaded = this->lightManager.upload(); !uploaded)
    {
        return uploaded;
    }

    this->terrain.init(500.0f, 500.0f, 500, 500);
    this->terrain.SetNoiseSeed(static_cast<int>(seed));
    LOG_DEBUGGING("Triangle count: ", this->terrain.GetGrid().GetTriangleCount());
    this->terrain.GenerateFractalTerrain(0.01f, 50.0f, 10, 0.5f, 2.0f);

    auto mesh = rd::Mesh::create(device, this->terrain.GetMeshData());
    if (!mesh)
    {
        return std::unexpected(mesh.error());
    }
    this->terrainMesh = std::move(*mesh);
    auto program = library.loadGraphics(shaderDirectory / "mesh.vert", shaderDirectory / "mesh.frag");
    if (!program)
    {
        return std::unexpected(program.error());
    }
    this->terrainMesh.setProgram(*program);
    LOG_INFO("World terrain ready: triangles=", this->terrain.GetGrid().GetTriangleCount());
    return {};
}

void World::Destroy()
{
    this->terrainMesh.reset();
    this->terrain.Destroy();
    this->lightManager = {};
}

void World::Update() {}

engine::core::Result<> World::Render(rd::SceneRenderer &renderer, const rd::Camera &camera)
{
    renderer.setLights(&this->lightManager);
    return renderer.drawMesh(this->terrainMesh, camera);
}
