// REN-28 et REN-31 sur le GPU : dessin instancié (un draw call par (maillage, matériau)) et fondu tramé des LOD sans trou, sur
// chaque backend (FOR_EACH_BACKEND), couches de validation à 0.
#include <doctest/doctest.h>

#include "renderer/repo_paths.hpp"
#include "rhi/gpu_fixture.hpp"

#include <orosmo/renderer/camera.hpp>
#include <orosmo/renderer/instance_renderer.hpp>
#include <orosmo/renderer/light.hpp>
#include <orosmo/renderer/mesh.hpp>
#include <orosmo/renderer/object_set.hpp>
#include <orosmo/renderer/scene_renderer.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <filesystem>

namespace fs = std::filesystem;
namespace rd = orosmo::renderer;

namespace
{

fs::path shaderDir() { return tests::repoRoot(__FILE__) / "shaders" / "rhi"; }

constexpr std::uint32_t kSize = 8;

struct InstancingScene
{
    Gpu gpu;
    std::unique_ptr<rd::ShaderLibrary> library;
    std::unique_ptr<rd::PipelineCache> pipelines;
    std::unique_ptr<rd::SceneRenderer> renderer;
    std::unique_ptr<rd::InstanceRenderer> instances;
    rd::LightManager lights;
    rd::Camera camera;

    explicit InstancingScene(Backend backend) : gpu(backend)
    {
        if (!gpu.device)
        {
            return;
        }
        library = std::make_unique<rd::ShaderLibrary>(*gpu.device);
        pipelines = std::make_unique<rd::PipelineCache>(*gpu.device);
        auto created = rd::SceneRenderer::create(*gpu.device, *library, *pipelines, {.shaderDirectory = shaderDir()});
        INFO((created ? std::string() : created.error().message));
        REQUIRE(created.has_value());
        renderer = std::move(*created);
        auto inst = rd::InstanceRenderer::create(*gpu.device, *library, *pipelines, shaderDir());
        INFO((inst ? std::string() : inst.error().message));
        REQUIRE(inst.has_value());
        instances = std::move(*inst);
        auto l = rd::LightManager::create(*gpu.device);
        REQUIRE(l.has_value());
        lights = std::move(*l);
        lights.setAmbientLight({1, 1, 1}, 1.0f); // éclairage 1 : couleur du sommet × couleur de l'instance
        REQUIRE(lights.upload().has_value());
        renderer->setLights(&lights);
        renderer->setOutputResolution(kSize, kSize);
        // Orthographique de hauteur 8 : une unité du monde = un pixel, origine au centre de l'image.
        camera.setAspect(kSize, kSize);
        camera.setNearPlane(0.1f);
        camera.setFarPlane(100.0f);
        camera.setOrthographic(static_cast<float>(kSize));
    }
    ~InstancingScene()
    {
        instances.reset();
        renderer.reset();
    }

    // Dessine les lots de `batches` (et rien d'autre) ; rend les statistiques du dessin instancié.
    rd::InstanceDrawStats render(TextureHandle out, const rd::InstanceBatchBuilder &batches, bool wireframe = false)
    {
        rd::InstanceDrawStats stats;
        gpu.run(
            [&](CommandEncoder &e)
            {
                REQUIRE(renderer->beginFrame(e, {.clearColor = {0, 0, 0, 1}}).has_value());
                REQUIRE(renderer->setCamera(camera).has_value());
                auto drawn = instances->draw(*renderer, batches, wireframe);
                INFO((drawn ? std::string() : drawn.error().message));
                REQUIRE(drawn.has_value());
                stats = *drawn;
                REQUIRE(renderer->endFrame({out, Format::kRGBA8Unorm, {kSize, kSize}}).has_value());
            });
        return stats;
    }
};

// Carré de côté `side` centré à l'origine (plan z = 0, normale +Z), couleur des sommets donnée.
rd::MeshData squareData(float side, const glm::vec3 &color, const char *name)
{
    const float h = side * 0.5f;
    rd::MeshData d;
    d.vertices = {-h, -h, 0, 0, 0, 1, color.r, color.g, color.b, //
                  h,  -h, 0, 0, 0, 1, color.r, color.g, color.b, //
                  h,  h,  0, 0, 0, 1, color.r, color.g, color.b, //
                  -h, h,  0, 0, 0, 1, color.r, color.g, color.b};
    d.indices = {0, 1, 2, 0, 2, 3};
    d.attributeSizes = {3, 3, 3};
    d.name = name;
    return d;
}

template <typename R> bool failsWith(const R &result, ErrorCode code) { return !result.has_value() && result.error().code == code; }

glm::mat4 at(float x, float y, float z = -5.0f) { return glm::translate(glm::mat4(1.0f), glm::vec3(x, y, z)); }

} // namespace

TEST_SUITE("renderer/instancing")
{
    TEST_CASE("InstanceRenderer: each instance has its own transform and color; one draw call per (mesh, material) batch (REN-28)")
    {
        FOR_EACH_BACKEND(backend)
        {
            InstancingScene scene(backend);
            REQUIRE(scene.gpu.device);
            auto white = rd::Mesh::create(*scene.gpu.device, squareData(4.0f, {1, 1, 1}, "white"));
            auto small = rd::Mesh::create(*scene.gpu.device, squareData(2.0f, {1, 1, 1}, "small"));
            REQUIRE(white.has_value());
            REQUIRE(small.has_value());
            auto whiteId = scene.instances->addMesh(*white);
            auto smallId = scene.instances->addMesh(*small);
            REQUIRE(whiteId.has_value());
            REQUIRE(smallId.has_value());
            const std::uint32_t material = scene.instances->addMaterial({});

            // Quatre carrés de 4 × 4 pixels, un par quart de l'image (même maillage, même matériau : UN lot), plus un petit carré
            // devant au centre (autre maillage : un second lot).
            rd::InstanceBatchBuilder batches;
            batches.add({*whiteId, material}, at(-2.0f, 2.0f), {1, 0, 0, 1});  // haut gauche : rouge
            batches.add({*whiteId, material}, at(2.0f, 2.0f), {0, 1, 0, 1});   // haut droite : vert
            batches.add({*whiteId, material}, at(-2.0f, -2.0f), {0, 0, 1, 1}); // bas gauche : bleu
            batches.add({*whiteId, material}, at(2.0f, -2.0f), {1, 1, 0, 1});  // bas droite : jaune
            batches.add({*smallId, material}, at(0.0f, 0.0f, -1.0f), {1, 1, 1, 1});
            batches.build();
            REQUIRE(batches.batches().size() == 2);

            const TextureHandle out = scene.gpu.target(Format::kRGBA8Unorm, {kSize, kSize}, "output");
            const rd::InstanceDrawStats stats = scene.render(out, batches);
            CHECK(stats.drawCalls == 2);
            CHECK(stats.instances == 5);
            CHECK(stats.pipelineChanges == 1); // même programme, même état : un seul pipeline
            CHECK(stats.uploadedBytes == 5 * sizeof(rd::GpuInstance));
            const auto expect = [&](int x, int y, int r, int g, int b)
            {
                const auto px = rgba(scene.gpu.pixel(out, x, y));
                INFO("pixel ", x, ",", y, " = ", int(px[0]), " ", int(px[1]), " ", int(px[2]));
                CHECK(near(px[0], r));
                CHECK(near(px[1], g));
                CHECK(near(px[2], b));
            };
            expect(0, 0, 255, 0, 0); // origine de lecture en haut à gauche (A26)
            expect(7, 0, 0, 255, 0);
            expect(0, 7, 0, 0, 255);
            expect(7, 7, 255, 255, 0);
            expect(3, 3, 255, 255, 255); // le petit carré, devant
            expect(4, 4, 255, 255, 255);

            // Rendu relatif à la caméra (REN-02) : caméra et instances déplacées ensemble loin de l'origine, même image (les
            // translations sont retranchées à l'envoi).
            const glm::vec3 offset(100.0f, 200.0f, 0.0f);
            rd::InstanceBatchBuilder far;
            far.add({*whiteId, material}, at(offset.x - 2.0f, offset.y + 2.0f), {1, 0, 0, 1});
            far.add({*whiteId, material}, at(offset.x + 2.0f, offset.y - 2.0f), {1, 1, 0, 1});
            far.build();
            scene.renderer->setCameraRelative(true);
            scene.camera.setPosition(offset);
            const TextureHandle moved = scene.gpu.target(Format::kRGBA8Unorm, {kSize, kSize}, "moved");
            scene.render(moved, far);
            CHECK(rgba(scene.gpu.pixel(moved, 0, 0)) == rgba(scene.gpu.pixel(out, 0, 0)));
            CHECK(rgba(scene.gpu.pixel(moved, 7, 7)) == rgba(scene.gpu.pixel(out, 7, 7)));
            scene.renderer->setCameraRelative(false);
            scene.camera.setPosition(glm::vec3(0.0f));

            // Erreurs : lot non construit, identifiant inconnu (rien n'est dessiné), maillage avec ses propres instances.
            rd::InstanceBatchBuilder unbuilt;
            unbuilt.add({*whiteId, material}, at(0, 0));
            rd::InstanceBatchBuilder unknown;
            unknown.add({42, material}, at(0, 0));
            unknown.build();
            scene.gpu.run(
                [&](CommandEncoder &e)
                {
                    CHECK_FALSE(scene.instances->draw(*scene.renderer, batches).has_value()); // aucune image ouverte
                    REQUIRE(scene.renderer->beginFrame(e).has_value());
                    REQUIRE(scene.renderer->setCamera(scene.camera).has_value());
                    CHECK(failsWith(scene.instances->draw(*scene.renderer, unbuilt), ErrorCode::kInvalidArgument));
                    CHECK(failsWith(scene.instances->draw(*scene.renderer, unknown), ErrorCode::kNotFound));
                    REQUIRE(scene.renderer->endFrame({out, Format::kRGBA8Unorm, {kSize, kSize}}).has_value());
                });
            rd::MeshData withInstances = squareData(1.0f, {1, 1, 1}, "own-instances");
            withInstances.instances = {0, 0, 0};
            withInstances.instanceAttributeSizes = {3};
            auto own = rd::Mesh::create(*scene.gpu.device, withInstances);
            REQUIRE(own.has_value());
            CHECK(failsWith(scene.instances->addMesh(*own), ErrorCode::kInvalidArgument));
            scene.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("InstanceRenderer: 10 000 culled objects in a handful of draw calls (REN-28, REN-30)")
    {
        FOR_EACH_BACKEND(backend)
        {
            InstancingScene scene(backend);
            REQUIRE(scene.gpu.device);
            auto cube = rd::Mesh::create(*scene.gpu.device, squareData(0.8f, {1, 1, 1}, "tile"));
            REQUIRE(cube.has_value());
            const std::uint32_t mesh = *scene.instances->addMesh(*cube);
            const std::uint32_t materialA = scene.instances->addMaterial({});
            const std::uint32_t materialB = scene.instances->addMaterial({});
            rd::ObjectSet objects;
            for (int i = 0; i < 10000; ++i)
            {
                rd::ObjectDesc d;
                d.lodMeshes = {mesh};
                d.material = i % 2 == 0 ? materialA : materialB;
                d.localBounds = {{-0.4f, -0.4f, 0.0f}, {0.4f, 0.4f, 0.0f}};
                // Grille 100 × 100 de pas 1, centres aux milieux des pixels : la vue (x, y dans [-4, 4]) en voit 8 × 8.
                d.transform = at(static_cast<float>(i % 100) - 49.5f, static_cast<float>(i / 100) - 49.5f);
                d.color = {0.5f, 1.0f, 0.25f, 1.0f};
                REQUIRE(objects.add(d).has_value());
            }
            rd::InstanceBatchBuilder batches;
            const rd::CullView view = rd::CullView::fromMatrices(scene.camera.viewMatrix(), scene.camera.projectionMatrix(), kSize, true);
            const rd::VisibilityStats visibility = objects.prepare(view, batches);
            CHECK(visibility.cull.visible == 64); // la vue 8 × 8 ne voit que 8 × 8 objets de la grille
            const TextureHandle out = scene.gpu.target(Format::kRGBA8Unorm, {kSize, kSize}, "output");
            const rd::InstanceDrawStats stats = scene.render(out, batches);
            CHECK(stats.drawCalls == 2); // un par matériau
            CHECK(stats.instances == 64);
            const auto px = rgba(scene.gpu.pixel(out, 4, 4));
            CHECK(near(px[0], 128));
            CHECK(near(px[1], 255));
            CHECK(near(px[2], 64));

            // Sans culling : les 10 000 instances, toujours 2 draw calls.
            rd::VisibilityOptions all;
            all.frustumCulling = false;
            objects.prepare(view, batches, all);
            const rd::InstanceDrawStats full = scene.render(out, batches, true); // + passage fil de fer
            CHECK(full.instances == 2 * 10000);
            CHECK(full.drawCalls == 4);
            scene.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("LOD cross-fade: two levels dithered with complementary thresholds cover every pixel exactly once (REN-31)")
    {
        FOR_EACH_BACKEND(backend)
        {
            InstancingScene scene(backend);
            REQUIRE(scene.gpu.device);
            auto red = rd::Mesh::create(*scene.gpu.device, squareData(16.0f, {1, 0, 0}, "lod0"));
            auto green = rd::Mesh::create(*scene.gpu.device, squareData(16.0f, {0, 1, 0}, "lod1"));
            REQUIRE(red.has_value());
            REQUIRE(green.has_value());
            const std::uint32_t lod0 = *scene.instances->addMesh(*red);
            const std::uint32_t lod1 = *scene.instances->addMesh(*green);
            const std::uint32_t material = scene.instances->addMaterial({});

            // Le niveau 1 est DEVANT : sans tramage complémentaire il masquerait tout ; avec, chaque pixel est à l'un des deux.
            for (const float fade : {0.25f, 0.5f, 0.75f})
            {
                rd::InstanceBatchBuilder batches;
                batches.add({lod0, material}, at(0, 0, -6.0f), glm::vec4(1.0f), fade);
                batches.add({lod1, material}, at(0, 0, -5.0f), glm::vec4(1.0f), -fade);
                batches.build();
                const TextureHandle out = scene.gpu.target(Format::kRGBA8Unorm, {kSize, kSize}, "fade");
                scene.render(out, batches);
                int reds = 0;
                int greens = 0;
                int holes = 0;
                for (std::uint32_t y = 0; y < kSize; ++y)
                {
                    for (std::uint32_t x = 0; x < kSize; ++x)
                    {
                        const auto px = rgba(scene.gpu.pixel(out, static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)));
                        reds += px[0] > 200 && px[1] < 50 ? 1 : 0;
                        greens += px[1] > 200 && px[0] < 50 ? 1 : 0;
                        holes += px[0] < 50 && px[1] < 50 ? 1 : 0;
                    }
                }
                INFO("fade ", fade, ": ", reds, " red, ", greens, " green, ", holes, " holes");
                CHECK(holes == 0);
                CHECK(reds + greens == 64);
                CHECK(greens == static_cast<int>(fade * 64.0f)); // couverture exacte du motif 4 × 4
            }
            scene.gpu.expectNoValidationErrors();
        }
    }
}
