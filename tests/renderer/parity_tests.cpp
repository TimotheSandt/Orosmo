// RHI-02 (lot B4) : parité avec le rendu hérité. La scène de terrain (graine 1) dessinée par la nouvelle couche doit donner l'image
// de référence produite par l'ancien rendu (docs/BASELINE/reference/). Fichier séparé : il inclut les en-têtes hérités (Grid, Noise),
// dont les noms globaux (`ShaderStage`, `BufferDesc`...) entrent en conflit avec `using namespace engine::rhi`.
#include <doctest/doctest.h>

#include "support/test_support.hpp"

#include <engine/renderer/image.hpp>
#include <engine/renderer/light.hpp>
#include <engine/renderer/mesh.hpp>
#include <engine/renderer/scene_renderer.hpp>

#include "Noise.h"
#include "ProceduralGeneration/Grid.h"

#include <filesystem>

namespace fs = std::filesystem;
namespace rd = engine::renderer;
namespace rhi = engine::rhi;

namespace
{
fs::path repoRoot() { return fs::path(__FILE__).parent_path().parent_path().parent_path(); }
fs::path shaderDir() { return repoRoot() / "shaders" / "rhi"; }
} // namespace

TEST_SUITE("renderer/parity")
{
    TEST_CASE("the seed-1 terrain scene rendered through the RHI matches the legacy reference image (docs/BASELINE/reference)")
    {
        auto created = rhi::createDevice({.surface = {.size = {1280, 720}}});
        if (!created)
        {
            WARN_MESSAGE(false, "Skipped (no driver): OpenGL 4.3 context");
            return;
        }
        rhi::Device &device = **created;
        rd::ShaderLibrary library(device);
        rd::PipelineCache pipelines(device);
        auto rendererCreated = rd::SceneRenderer::create(device, library, pipelines, {.shaderDirectory = shaderDir()});
        REQUIRE(rendererCreated.has_value());
        std::unique_ptr<rd::SceneRenderer> renderer = std::move(*rendererCreated);

        // Même terrain que la démo (World::Init, graine 1) : sans passer par l'ancien `Mesh`, qui exige l'ancien runtime graphique.
        Grid grid;
        grid.init(500.0f, 500.0f, 500, 500);
        Noise noise(1);
        grid.TransformPoints(
            [&](Vertex &vertex, unsigned int)
            {
                const float r = noise.FractalNoise(vertex.Position.x, vertex.Position.z, 0.01f, 10, 0.5f, 2.0f);
                vertex.Position.y = r * 50.0f;
                vertex.Color = glm::vec3(r, 0.0f, -r);
            });
        rd::MeshData data;
        data.name = "terrain";
        for (const Vertex &v : grid.GetPoints())
        {
            for (const float f : {v.Position.x, v.Position.y, v.Position.z, v.Normal.x, v.Normal.y, v.Normal.z, v.Color.x, v.Color.y, v.Color.z})
            {
                data.vertices.push_back(f);
            }
        }
        for (const auto &t : grid.GetTriangles())
        {
            data.indices.insert(data.indices.end(), t.begin(), t.end());
        }
        data.attributeSizes = {3, 3, 3};
        auto mesh = rd::Mesh::create(device, data);
        REQUIRE(mesh.has_value());
        auto program = library.loadGraphics(shaderDir() / "mesh.vert", shaderDir() / "mesh.frag");
        REQUIRE(program.has_value());
        mesh->setProgram(*program);

        auto lights = rd::LightManager::create(device);
        REQUIRE(lights.has_value());
        lights->setAmbientLight({1.0f, 1.0f, 1.0f}, 0.2f);
        lights->addLight(rd::directionalLight({2.0f, -3.0f, 0.5f}, {0.99f, 0.76f, 0.81f}, 1.0f));
        REQUIRE(lights->upload().has_value());
        renderer->setLights(&*lights);

        rd::Camera camera; // réglages de Game::init
        camera.setPosition({0.0f, 1.0f, 0.0f});
        camera.setFovDegrees(75.0f);
        camera.setNearPlane(0.1f);
        camera.setFarPlane(1000.0f);
        camera.setAspect(1280, 720);

        rhi::TextureDesc od;
        od.size = {1280, 720, 1};
        od.usage = rhi::TextureUsage::kRenderTarget | rhi::TextureUsage::kSampled | rhi::TextureUsage::kCopySrc;
        auto outCreated = device.createTexture(od);
        REQUIRE(outCreated.has_value());
        const rhi::TextureHandle out = *outCreated;
        renderer->setOutputResolution(1280, 720);
        auto encoder = device.createCommandEncoder();
        {
            rhi::CommandEncoder &e = *encoder;
            {
                REQUIRE(renderer->beginFrame(e, {.clearColor = {0.07f, 0.13f, 0.17f, 1.0f}}).has_value());
                REQUIRE(renderer->setCamera(camera).has_value());
                REQUIRE(renderer->drawMesh(*mesh, camera).has_value());
                REQUIRE(renderer->endFrame({out, rhi::Format::kRGBA8Unorm, {1280, 720}}).has_value());
            }
        }
        REQUIRE(device.submit(std::move(encoder)).has_value());

        auto image = rd::readImage(device, renderer->sceneTarget().color(0));
        REQUIRE(image.has_value());
        const fs::path reference = repoRoot() / "docs" / "BASELINE" / "reference" / "scene-opengl-seed1-1280x720.png";
        auto expected = rd::loadPng(reference);
        INFO((expected ? std::string() : expected.error().message));
        REQUIRE(expected.has_value());
        auto diff = rd::compareImages(*image, *expected, 4);
        REQUIRE(diff.has_value());
        INFO("pixels hors tolérance : ", diff->outOfTolerance, " / ", diff->pixels, ", écart max ", static_cast<int>(diff->maxDelta), ", moyen ", diff->meanDelta);
        if (diff->outOfTolerance > diff->pixels / 1000)
        {
            (void)rd::savePng(*image, fs::temp_directory_path() / "engine_scene_parity_actual.png");
        }
        // Intel Arc (référence) : 0 pixel d'écart à la tolérance 0. Un autre pilote (Mesa llvmpipe en CI, G30) peut différer de quelques
        // pixels de bord : tolérance de 4 par canal et au plus 0,1 % de pixels au-delà.
        CHECK(diff->outOfTolerance <= diff->pixels / 1000);
        CHECK(device.validationErrorCount() == 0);
    }
}
