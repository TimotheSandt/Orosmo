// TER-07 : rendu du terrain : couches choisies par l'altitude et la pente, lues en triplanaire, tuiles ajoutées et retirées.
#include <doctest/doctest.h>

#include "support/test_support.hpp"
#include "renderer/repo_paths.hpp"
#include "rhi/gpu_fixture.hpp"

#include <engine/renderer/light.hpp>
#include <engine/renderer/scene_renderer.hpp>
#include <engine/renderer/terrain_renderer.hpp>

namespace rd = engine::renderer;

namespace
{

std::filesystem::path shaderDir() { return tests::repoRoot(__FILE__) / "shaders" / "rhi"; }

// Quadrilatère de 40 m de côté autour de l'origine de la tuile, de normale `normal` (plan horizontal, incliné visuellement par
// la normale seule : le mélange ne dépend que de la normale et de l'altitude).
struct Quad
{
    std::vector<float> vertices;
    std::vector<std::uint32_t> indices{0, 2, 1, 1, 2, 3};
    explicit Quad(glm::vec3 normal)
    {
        normal = glm::normalize(normal);
        for (const float z : {-20.0f, 20.0f})
        {
            for (const float x : {-20.0f, 20.0f})
            {
                vertices.insert(vertices.end(), {x, 0.0f, z, normal.x, normal.y, normal.z});
            }
        }
    }
};

struct Fixture
{
    Gpu gpu;
    std::unique_ptr<rd::ShaderLibrary> library;
    std::unique_ptr<rd::PipelineCache> pipelines;
    std::unique_ptr<rd::SceneRenderer> scene;
    std::unique_ptr<rd::TerrainRenderer> terrain;
    rd::LightManager lights;
    Fixture()
    {
        if (!gpu.device)
        {
            return;
        }
        library = std::make_unique<rd::ShaderLibrary>(*gpu.device);
        pipelines = std::make_unique<rd::PipelineCache>(*gpu.device);
        scene = tests::valueOf(rd::SceneRenderer::create(*gpu.device, *library, *pipelines, {.shaderDirectory = shaderDir()}));
        terrain = tests::valueOf(rd::TerrainRenderer::create(*gpu.device, *library, shaderDir()));
        lights = tests::valueOf(rd::LightManager::create(*gpu.device));
        lights.setAmbientLight({1, 1, 1}, 1.0f);
        REQUIRE(lights.upload().has_value());
        scene->setLights(&lights);
        scene->setOutputResolution(16, 16);
    }
    ~Fixture()
    {
        terrain.reset();
        scene.reset();
    }
    // Couleur au centre de l'image d'un quadrilatère placé à l'altitude `height`, vu de biais.
    std::array<std::uint8_t, 4> look(float height, glm::vec3 normal)
    {
        Quad quad(normal);
        REQUIRE(terrain->updateTile({.id = 1, .origin = {0.0f, height, 0.0f}, .vertices = quad.vertices, .indices = quad.indices}).has_value());
        return render(height);
    }
    // Rendu seul : la tuile 1 déjà créée n'est pas recréée.
    std::array<std::uint8_t, 4> render(float height)
    {
        rd::Camera camera;
        camera.setAspect(16, 16);
        camera.setPosition({0.0f, height + 6.0f, 12.0f});
        camera.setOrientation(glm::normalize(glm::vec3(0.0f, -6.0f, -12.0f)));
        const TextureHandle out = gpu.target(Format::kRGBA8Unorm, {16, 16}, "out");
        gpu.run(
            [&](CommandEncoder &e)
            {
                REQUIRE(scene->beginFrame(e).has_value());
                REQUIRE(scene->setCamera(camera).has_value());
                REQUIRE(terrain->draw(*scene, camera).has_value());
                REQUIRE(scene->endFrame({out, Format::kRGBA8Unorm, {16, 16}}).has_value());
            });
        return rgba(gpu.pixel(out, 8, 8));
    }
};

} // namespace

TEST_SUITE("renderer/terrain")
{
    TEST_CASE("TER-07: the layer follows altitude and slope (sand low, grass, snow high, rock on slopes)")
    {
        Fixture f;
        SKIP_TEST_IF_NULL(f.gpu.device.get(), "OpenGL 4.3 context");
        const auto sand = f.look(-20.0f, {0, 1, 0});
        const auto grass = f.look(30.0f, {0, 1, 0});
        const auto snow = f.look(200.0f, {0, 1, 0});
        const auto rock = f.look(30.0f, {1, 1, 0}); // pente de 45° : 1 - n.y = 0,29 < 0,35 -> encore de l'herbe
        const auto cliff = f.look(30.0f, {1, 0.2f, 0}); // pente forte : roche
        INFO("sand ", int(sand[0]), ",", int(sand[1]), ",", int(sand[2]), " grass ", int(grass[0]), ",", int(grass[1]), ",", int(grass[2]),
             " snow ", int(snow[0]), ",", int(snow[1]), ",", int(snow[2]), " cliff ", int(cliff[0]), ",", int(cliff[1]), ",", int(cliff[2]));
        CHECK(sand[0] > sand[2] + 40);            // sable : jaune (rouge > bleu)
        CHECK(grass[1] > grass[0]);               // herbe : vert dominant
        CHECK(grass[1] > grass[2]);
        CHECK(snow[0] > 200);                     // neige : presque blanche
        CHECK(snow[2] > 200);
        CHECK(rock[1] > rock[0]);                 // 45° : sous le seuil, toujours de l'herbe
        CHECK(std::abs(int(cliff[0]) - int(cliff[2])) < 30); // roche : gris (canaux proches)
        CHECK(cliff[1] < grass[1]);
        f.gpu.expectNoValidationErrors();
    }

    TEST_CASE("TER-07: tiles are added, replaced and retained by id; layers can be replaced; invalid input is refused")
    {
        Fixture f;
        SKIP_TEST_IF_NULL(f.gpu.device.get(), "OpenGL 4.3 context");
        Quad quad({0, 1, 0});
        for (std::uint64_t id : {1u, 2u, 3u})
        {
            REQUIRE(f.terrain->updateTile({.id = id, .origin = {static_cast<float>(id) * 40.0f, 0, 0}, .vertices = quad.vertices, .indices = quad.indices}).has_value());
        }
        CHECK(f.terrain->tileCount() == 3);
        REQUIRE(f.terrain->updateTile({.id = 2, .origin = {0, 0, 0}, .vertices = quad.vertices, .indices = quad.indices}).has_value()); // remplacée
        CHECK(f.terrain->tileCount() == 3);
        const std::uint64_t keep[] = {1, 3};
        f.terrain->retainOnly(keep);
        CHECK(f.terrain->tileCount() == 2);
        CHECK_FALSE(f.terrain->hasTile(2));
        f.terrain->removeTile(1);
        CHECK(f.terrain->tileCount() == 1);
        CHECK((tests::errorOf(f.terrain->updateTile({.id = 9, .vertices = std::span(quad.vertices).first(5), .indices = quad.indices})) ==
               ErrorCode::kInvalidArgument));

        // Couches remplacées : tout en magenta ; la tuile existante suit le nouveau tableau.
        std::vector<std::vector<std::uint8_t>> magenta(4, std::vector<std::uint8_t>(8 * 8 * 4, 255));
        for (auto &layer : magenta)
        {
            for (std::size_t i = 1; i < layer.size(); i += 4)
            {
                layer[i] = 0;
            }
        }
        (void)f.look(30.0f, {0, 1, 0}); // tuile 1 créée AVANT le changement de couches
        REQUIRE(f.terrain->setLayers(8, magenta).has_value());
        const auto px = f.render(30.0f); // la même tuile, réassociée au nouveau tableau (pas recréée)
        CHECK(px[0] > 240);
        CHECK(px[1] < 15);
        CHECK(px[2] > 240);
        CHECK((tests::errorOf(f.terrain->setLayers(8, std::span(magenta).first(3))) == ErrorCode::kInvalidArgument));
        f.gpu.expectNoValidationErrors();
    }
}
