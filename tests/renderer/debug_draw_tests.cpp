// REN-34 : dessin de debug : enregistrement sans coût une fois désactivé, rendu dans la passe de scène avec ou sans profondeur.
#include <doctest/doctest.h>

#include "support/test_support.hpp"
#include "renderer/repo_paths.hpp"
#include "rhi/gpu_fixture.hpp"

#include <orosmo/renderer/debug_draw.hpp>
#include <orosmo/renderer/light.hpp>
#include <orosmo/renderer/mesh.hpp>
#include <orosmo/renderer/scene_renderer.hpp>

namespace rd = orosmo::renderer;

namespace
{
std::filesystem::path shaderDir() { return tests::repoRoot(__FILE__) / "shaders" / "rhi"; }
} // namespace

TEST_SUITE("renderer/debug")
{
    TEST_CASE("DebugDraw records lines, boxes, spheres and axes; disabled, it records nothing")
    {
        rd::DebugDraw debug;
        debug.setEnabled(true);
        debug.line({0, 0, 0}, {1, 0, 0}, {1, 1, 1, 1});
        CHECK(debug.lineCount() == 1);
        debug.box({0, 0, 0}, {1, 1, 1}, {1, 0, 0, 1});
        CHECK(debug.lineCount() == 13);
        debug.sphere({0, 0, 0}, 1.0f, {0, 1, 0, 1}, 8);
        CHECK(debug.lineCount() == 13 + 24);
        debug.axes(glm::mat4(1.0f), 2.0f);
        CHECK(debug.lineCount() == 13 + 24 + 3);
        CHECK(debug.vertices()[7 * 2 * 13 + 7 * 2 * 24 + 0] == 0.0f); // origine du repère
        debug.clear();
        CHECK(debug.lineCount() == 0);

        debug.setEnabled(false);
        debug.line({0, 0, 0}, {1, 0, 0}, {1, 1, 1, 1});
        debug.box({0, 0, 0}, {1, 1, 1}, {1, 0, 0, 1});
        debug.sphere({0, 0, 0}, 1.0f, {0, 1, 0, 1});
        debug.axes(glm::mat4(1.0f), 1.0f);
        CHECK(debug.lineCount() == 0);
        CHECK(debug.vertices().capacity() > 0); // rien n'a été réalloué ni écrit depuis clear()
#ifdef NDEBUG
        CHECK_FALSE(rd::DebugDraw().enabled()); // hors debug : désactivé par défaut
#else
        CHECK(rd::DebugDraw().enabled());
#endif
    }

    TEST_CASE("SceneRenderer::drawDebug draws the lines in the scene pass; the depth test can hide them")
    {
        Gpu gpu;
        SKIP_TEST_IF_NULL(gpu.device.get(), "OpenGL 4.3 context");
        rd::ShaderLibrary library(*gpu.device);
        rd::PipelineCache pipelines(*gpu.device);
        auto scene = rd::SceneRenderer::create(*gpu.device, library, pipelines, {.shaderDirectory = shaderDir()});
        REQUIRE(scene.has_value());
        auto renderer = std::move(*scene);
        renderer->setOutputResolution(16, 16);
        const TextureHandle out = gpu.target(Format::kRGBA8Unorm, {16, 16}, "out");
        rd::Camera camera; // à l'origine, regarde vers -Z
        camera.setAspect(16, 16);
        rd::DebugDraw debug;
        debug.setEnabled(true);
        debug.line({-10.0f, 0.0f, -5.0f}, {10.0f, 0.0f, -5.0f}, {1, 1, 0, 1}); // horizontale au milieu de l'image
        auto frame = [&]()
        {
            gpu.run(
                [&](CommandEncoder &e)
                {
                    REQUIRE(renderer->beginFrame(e).has_value());
                    REQUIRE(renderer->setCamera(camera).has_value());
                    REQUIRE(renderer->drawDebug(debug).has_value());
                    REQUIRE(renderer->endFrame({out, Format::kRGBA8Unorm, {16, 16}}).has_value());
                });
        };
        frame();
        bool found = false;
        for (int y = 6; y <= 9 && !found; ++y)
        {
            found = rgba(gpu.pixel(out, 4, y)) == std::array<std::uint8_t, 4>{255, 255, 0, 255};
        }
        CHECK(found);
        CHECK(rgba(gpu.pixel(out, 4, 1)) == std::array<std::uint8_t, 4>{0, 0, 0, 255}); // le reste de l'image est vide

        // Un triangle opaque devant la ligne (z = -2) : avec le test de profondeur elle est cachée, sans elle passe devant.
        auto program = library.loadGraphics(shaderDir() / "mesh.vert", shaderDir() / "mesh.frag");
        REQUIRE(program.has_value());
        rd::MeshData data;
        data.vertices = {-3, -3, -2, 0, 0, 1, 0, 0, 1, 3, -3, -2, 0, 0, 1, 0, 0, 1, 0, 3, -2, 0, 0, 1, 0, 0, 1};
        data.indices = {0, 1, 2};
        data.attributeSizes = {3, 3, 3};
        auto wall = rd::Mesh::create(*gpu.device, data);
        REQUIRE(wall.has_value());
        wall->setProgram(*program);
        auto lights = rd::LightManager::create(*gpu.device);
        REQUIRE(lights.has_value());
        lights->setAmbientLight({1, 1, 1}, 1.0f);
        REQUIRE(lights->upload().has_value());
        renderer->setLights(&*lights);
        auto occluded = [&](bool depthTest)
        {
            debug.setDepthTest(depthTest);
            gpu.run(
                [&](CommandEncoder &e)
                {
                    REQUIRE(renderer->beginFrame(e).has_value());
                    REQUIRE(renderer->setCamera(camera).has_value());
                    REQUIRE(renderer->drawMesh(*wall, camera).has_value());
                    REQUIRE(renderer->drawDebug(debug).has_value());
                    REQUIRE(renderer->endFrame({out, Format::kRGBA8Unorm, {16, 16}}).has_value());
                });
            bool yellow = false;
            for (int y = 6; y <= 9; ++y)
            {
                yellow = yellow || rgba(gpu.pixel(out, 7, y)) == std::array<std::uint8_t, 4>{255, 255, 0, 255};
            }
            return yellow;
        };
        CHECK_FALSE(occluded(true)); // derrière le triangle : cachée
        CHECK(occluded(false));      // sans test de profondeur : visible
        renderer->setLights(nullptr);

        debug.setEnabled(false); // désactivé : drawDebug ne fait rien, même avec des segments déjà enregistrés
        frame();
        CHECK(rgba(gpu.pixel(out, 4, 7)) == std::array<std::uint8_t, 4>{0, 0, 0, 255});
        CHECK(rgba(gpu.pixel(out, 4, 8)) == std::array<std::uint8_t, 4>{0, 0, 0, 255});
        gpu.expectNoValidationErrors();
    }
}
