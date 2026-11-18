// RHI-02 (lot B4) : couche de scène sur le RHI : caméra, lumières, maillage, rendu de scène avec mise à l'échelle, extensions de fin
// d'image, et parité avec l'image de référence du rendu hérité (docs/BASELINE/reference/). Contexte hors écran.
#include <doctest/doctest.h>

#include "support/test_support.hpp"
#include "rhi/gpu_fixture.hpp"

#include <engine/renderer/camera.hpp>
#include <engine/renderer/image.hpp>
#include <engine/renderer/light.hpp>
#include <engine/renderer/mesh.hpp>
#include <engine/renderer/scene_renderer.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <filesystem>

namespace fs = std::filesystem;
namespace rd = engine::renderer;
namespace rhi = engine::rhi;

namespace
{

// Racine du dépôt déduite de ce fichier (les tests tournent depuis apps/demo : __FILE__ est relatif à ce dossier).
fs::path repoRoot() { return fs::path(__FILE__).parent_path().parent_path().parent_path(); }
fs::path shaderDir() { return repoRoot() / "shaders" / "rhi"; }

struct Scene
{
    Gpu gpu;
    std::unique_ptr<rd::ShaderLibrary> library;
    std::unique_ptr<rd::PipelineCache> pipelines;
    std::unique_ptr<rd::SceneRenderer> renderer;

    Scene()
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
    }
    ~Scene()
    {
        renderer.reset(); // le renderer d'abord : il référence la bibliothèque et le cache
    }

    TextureHandle output(std::uint32_t w, std::uint32_t h) { return gpu.target(Format::kRGBA8Unorm, {w, h}, "output"); }
};

// Un triangle plein écran en coordonnées d'écran (z = 0), couleur du sommet donnée, normale +Z.
rd::MeshData triangleData(const std::array<float, 3> &color)
{
    rd::MeshData d;
    // position, normale, couleur
    d.vertices = {-3, -3, -1, 0, 0, 1, color[0], color[1], color[2],  //
                  3,  -3, -1, 0, 0, 1, color[0], color[1], color[2],  //
                  0,  3,  -1, 0, 0, 1, color[0], color[1], color[2]};
    d.indices = {0, 1, 2};
    d.attributeSizes = {3, 3, 3};
    d.name = "triangle";
    return d;
}

} // namespace

TEST_SUITE("renderer/camera")
{
    TEST_CASE("Camera: matrices follow the legacy formulas; data() has the std140 layout; jitter shifts the projection")
    {
        rd::Camera camera;
        camera.setPosition({1.0f, 2.0f, 3.0f});
        camera.setFovDegrees(75.0f);
        camera.setNearPlane(0.1f);
        camera.setFarPlane(1000.0f);
        camera.setAspect(1280, 720);
        const glm::mat4 view = glm::lookAt(glm::vec3(1, 2, 3), glm::vec3(1, 2, 3) + glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
        const glm::mat4 proj = glm::perspective(glm::radians(75.0f), 1280.0f / 720.0f, 0.1f, 1000.0f);
        CHECK(camera.matrix() == proj * view);
        const rd::CameraData data = camera.data();
        CHECK(data.position[0] == 1.0f);
        CHECK(data.position[2] == 3.0f);
        CHECK(std::memcmp(data.matrix, glm::value_ptr(proj * view), sizeof data.matrix) == 0);

        camera.setJitter({0.01f, -0.02f});
        CHECK(camera.projectionMatrix()[2][0] == doctest::Approx(proj[2][0] + 0.01f));
        CHECK(camera.projectionMatrix()[2][1] == doctest::Approx(proj[2][1] - 0.02f));
        camera.setAspect(10, 0); // hauteur nulle : pas de division par zéro
        CHECK(camera.aspect() == 1.0f);
    }

    TEST_CASE("Camera: movement and rotation; the pitch cannot go within 10 degrees of the vertical")
    {
        rd::Camera camera;
        camera.moveForward(2.0f);
        CHECK(camera.position().z == doctest::Approx(-2.0f));
        camera.moveRight(1.0f);
        CHECK(camera.position().x == doctest::Approx(1.0f));
        camera.moveUp(0.5f);
        CHECK(camera.position().y == doctest::Approx(0.5f));
        camera.rotateYaw(90.0f);
        CHECK(camera.orientation().x == doctest::Approx(-1.0f).epsilon(1e-4));
        rd::Camera pitch;
        pitch.rotatePitch(45.0f);
        CHECK(pitch.orientation().y > 0.5f);
        const glm::vec3 before = pitch.orientation();
        pitch.rotatePitch(40.0f); // 85° : à moins de 10° de la verticale, refusé
        CHECK(pitch.orientation() == before);
        pitch.rotateRoll(30.0f);
        CHECK(pitch.up() != glm::vec3(0, 1, 0));
    }
}

TEST_SUITE("renderer/light")
{
    TEST_CASE("Light constructors, equality and the GPU layout")
    {
        const rd::Light sun = rd::directionalLight({2, -3, 0.5f}, {1, 0.5f, 0.25f}, 2.0f);
        CHECK((sun.type == rd::LightType::kDirectional));
        CHECK(sun == rd::directionalLight({2, -3, 0.5f}, {1, 0.5f, 0.25f}, 2.0f));
        CHECK_FALSE(sun == rd::directionalLight({2, -3, 0.5f}, {1, 0.5f, 0.25f}, 3.0f));
        CHECK_FALSE(sun == rd::ambientLight({1, 1, 1}, 0.2f));
        CHECK(rd::pointLight({0, 1, 0}, {1, 1, 1}, 1.0f, 0.1f, 0.2f).a == 0.1f);
        CHECK(rd::spotLight({0, 1, 0}, {0, -1, 0}, {1, 1, 1}, 1.0f, 0.9f, 0.95f, 0.1f, 0.2f).innerCone == 0.95f);
        CHECK(sizeof(rd::GpuLight) == 96);
        CHECK(sizeof(rd::GpuLightHeader) == 48);
    }

    TEST_CASE("LightManager: upload writes the std430 block; the buffer grows; remove and reset")
    {
        Gpu gpu;
        SKIP_TEST_IF_NULL(gpu.device.get(), "OpenGL 4.3 context");
        auto created = rd::LightManager::create(*gpu.device, 2);
        REQUIRE(created.has_value());
        rd::LightManager &lights = *created;
        lights.setAmbientLight({1, 0.5f, 0.25f}, 0.2f);
        lights.addLight(rd::directionalLight({2, -3, 0.5f}, {0.99f, 0.76f, 0.81f}, 1.0f));
        REQUIRE(lights.upload().has_value());
        CHECK_FALSE(lights.dirty());
        auto bytes = lights.buffer().read(0, 48 + 96);
        REQUIRE(bytes.has_value());
        std::int32_t size;
        float ambient[5];
        std::memcpy(&size, bytes->data(), 4);
        std::memcpy(ambient, bytes->data() + 16, 20);
        CHECK(size == 1);
        CHECK(ambient[0] == 1.0f);
        CHECK(ambient[1] == 0.5f);
        CHECK(ambient[2] == 0.25f);
        CHECK(ambient[4] == 0.2f); // AmbientLightStrength à l'offset 32
        std::int32_t type;
        float direction[3];
        float strength;
        std::memcpy(&type, bytes->data() + 48, 4);
        std::memcpy(direction, bytes->data() + 48 + 32, 12);
        std::memcpy(&strength, bytes->data() + 48 + 60, 4);
        CHECK(type == 2);
        CHECK(direction[1] == -3.0f);
        CHECK(strength == 1.0f);

        const rhi::BufferHandle before = lights.buffer().handle();
        for (int i = 0; i < 10; ++i)
        {
            lights.addLight(rd::ambientLight({1, 1, 1}, 0.1f * static_cast<float>(i)));
        }
        REQUIRE(lights.upload().has_value()); // 11 lumières > capacité 2 : le tampon grandit
        CHECK(lights.buffer().handle() != before);
        CHECK(lights.buffer().size() >= 48 + 11 * 96);
        lights.removeLight(0);
        lights.removeLight(rd::ambientLight({1, 1, 1}, 0.1f * 3.0f));
        CHECK(lights.lights().size() == 9);
        lights.removeLight(99); // hors limites : sans effet
        CHECK(lights.lights().size() == 9);
        lights.resetAmbientLight();
        CHECK(lights.ambient().strength == 0.0f);
        CHECK_THROWS(lights.light(50));
        gpu.expectNoValidationErrors();
    }
}

TEST_SUITE("renderer/mesh")
{
    TEST_CASE("Mesh: vertex layout from attribute sizes, instancing, transform, updates, RAII release")
    {
        Gpu gpu;
        SKIP_TEST_IF_NULL(gpu.device.get(), "OpenGL 4.3 context");
        {
            rd::MeshData d = triangleData({1, 0, 0});
            d.instances = {0, 0, 1, 1, 0, 0, 1, 2, 2, 0, 0, 1}; // 2 instances de (vec3, vec3)...
            d.instanceAttributeSizes = {3, 3};
            auto mesh = rd::Mesh::create(*gpu.device, d);
            INFO((mesh ? std::string() : mesh.error().message));
            REQUIRE(mesh.has_value());
            REQUIRE(mesh->vertexBuffers().size() == 2);
            CHECK(mesh->vertexBuffers()[0].stride == 36);
            CHECK((mesh->vertexBuffers()[1].stepRate == rhi::VertexStepRate::kInstance));
            REQUIRE(mesh->vertexAttributes().size() == 5);
            CHECK(mesh->vertexAttributes()[2].offset == 24);
            CHECK(mesh->vertexAttributes()[3].location == 3);
            CHECK(mesh->vertexAttributes()[4].offset == 12);
            CHECK(mesh->instanceCount() == 2);
            CHECK(mesh->indexCount() == 3);
            CHECK(mesh->instanced());

            mesh->setPosition({1, 2, 3});
            mesh->setRotation({90, 0, 0});
            mesh->setScale({2, 2, 2});
            glm::mat4 expected(1.0f);
            expected = glm::translate(expected, glm::vec3(1, 2, 3));
            expected = glm::rotate(expected, glm::radians(90.0f), glm::vec3(1, 0, 0));
            expected = glm::rotate(expected, 0.0f, glm::vec3(0, 1, 0));
            expected = glm::rotate(expected, 0.0f, glm::vec3(0, 0, 1));
            expected = glm::scale(expected, glm::vec3(2));
            CHECK(mesh->modelMatrix() == expected);
            REQUIRE(mesh->uploadTransform().has_value());

            const std::vector<float> patch{9, 9, 9};
            CHECK(mesh->updateVertices(patch, 3).has_value());
            CHECK_FALSE(mesh->updateVertices(patch, 1000).has_value());
            CHECK(mesh->updateInstances(patch, 0).has_value());
            rd::Mesh moved = std::move(*mesh);
            CHECK_FALSE(mesh->valid());
            CHECK(moved.valid());
            CHECK(gpu->stats().buffers == 4); // sommets, index, instances, modèle
        }
        CHECK(gpu->stats().buffers == 0);
        CHECK(gpu->stats().bindGroups == 0);

        rd::MeshData wide = triangleData({1, 0, 0});
        wide.attributeSizes = {3, 3, 5};
        CHECK((rd::Mesh::create(*gpu.device, wide).error().code == ErrorCode::kInvalidArgument));
        rd::MeshData ragged = triangleData({1, 0, 0});
        ragged.vertices.pop_back();
        CHECK((rd::Mesh::create(*gpu.device, ragged).error().code == ErrorCode::kInvalidArgument));
        CHECK_FALSE(rd::Mesh::create(*gpu.device, {}).has_value());
        gpu.expectNoValidationErrors();
    }
}

TEST_SUITE("renderer/scene")
{
    TEST_CASE("SceneRenderer: a lit triangle reaches the output; the scene target keeps the render resolution")
    {
        Scene scene;
        SKIP_TEST_IF_NULL(scene.gpu.device.get(), "OpenGL 4.3 context");
        auto program = scene.library->loadGraphics(shaderDir() / "mesh.vert", shaderDir() / "mesh.frag");
        REQUIRE(program.has_value());
        auto mesh = rd::Mesh::create(*scene.gpu.device, triangleData({0.5f, 0.25f, 1.0f}));
        REQUIRE(mesh.has_value());
        mesh->setProgram(*program);

        auto lights = rd::LightManager::create(*scene.gpu.device);
        REQUIRE(lights.has_value());
        lights->setAmbientLight({1, 1, 1}, 1.0f); // éclairage 1 : couleur du sommet inchangée
        REQUIRE(lights->upload().has_value());
        scene.renderer->setLights(&*lights);

        rd::Camera camera;
        camera.setAspect(8, 8);
        camera.setFarPlane(100.0f);
        const TextureHandle out = scene.output(8, 8);
        scene.renderer->setOutputResolution(8, 8);
        scene.gpu.run(
            [&](CommandEncoder &e)
            {
                REQUIRE(scene.renderer->beginFrame(e, {.clearColor = {0, 0, 0, 1}}).has_value());
                REQUIRE(scene.renderer->setCamera(camera).has_value());
                auto drawn = scene.renderer->drawMesh(*mesh, camera);
                INFO((drawn ? std::string() : drawn.error().message));
                REQUIRE(drawn.has_value());
                REQUIRE(scene.renderer->endFrame({out, Format::kRGBA8Unorm, {8, 8}}).has_value());
            });
        const auto px = rgba(scene.gpu.pixel(out, 4, 4));
        CHECK(near(px[0], 128));
        CHECK(near(px[1], 64));
        CHECK(near(px[2], 255));
        CHECK(scene.renderer->frameWidth() == 8);
        CHECK(scene.renderer->sceneTarget().colorCount() == 1);
        scene.gpu.expectNoValidationErrors();
    }

    TEST_CASE("SceneRenderer: render scale shrinks the scene target and upscales into the full-size output; modes and passes are extension points")
    {
        Scene scene;
        SKIP_TEST_IF_NULL(scene.gpu.device.get(), "OpenGL 4.3 context");
        auto program = scene.library->loadGraphics(shaderDir() / "mesh.vert", shaderDir() / "mesh.frag");
        REQUIRE(program.has_value());
        auto mesh = rd::Mesh::create(*scene.gpu.device, triangleData({1, 1, 1}));
        REQUIRE(mesh.has_value());
        mesh->setProgram(*program);
        auto lights = rd::LightManager::create(*scene.gpu.device);
        REQUIRE(lights.has_value());
        lights->setAmbientLight({1, 1, 1}, 1.0f);
        REQUIRE(lights->upload().has_value());
        scene.renderer->setLights(&*lights);
        rd::Camera camera;
        camera.setAspect(16, 16);
        scene.renderer->setOutputResolution(16, 16);
        scene.renderer->setRenderScale(0.5f);
        CHECK(scene.renderer->upscalingEnabled());
        scene.renderer->setRenderScale(2.0f); // refusé : hors de (0, 1]
        CHECK(scene.renderer->renderScale() == 0.5f);

        struct CountingPass final : rd::IPostProcessPass
        {
            int calls = 0;
            std::string name() const noexcept override { return "count"; }
            engine::core::Result<> process(rd::FrameContext &, rd::RenderTarget &target) override
            {
                ++calls;
                CHECK(target.width() == 8);
                return {};
            }
        };
        auto pass = std::make_unique<CountingPass>();
        CountingPass *counting = pass.get();
        scene.renderer->addPostProcessPass(std::move(pass));

        struct RecordingMode final : rd::IUpscaleMode
        {
            int calls = 0;
            std::string name() const noexcept override { return "recording"; }
            engine::core::Result<> upscale(rd::FrameContext &, const rd::RenderTarget &source, const rd::UpscaleTarget &destination) override
            {
                ++calls;
                CHECK(source.width() == 8);
                CHECK(destination.size.width == 16);
                return {};
            }
        };
        auto mode = std::make_unique<RecordingMode>();
        RecordingMode *recording = mode.get();
        scene.renderer->registerUpscaleMode(std::move(mode));
        CHECK(scene.renderer->registeredUpscaleModes().size() == 2);
        CHECK(scene.renderer->activeUpscaleMode() == "bilinear-blit");
        CHECK_FALSE(scene.renderer->setActiveUpscaleMode("unknown"));

        const TextureHandle out = scene.output(16, 16);
        auto frame = [&]()
        {
            scene.gpu.run(
                [&](CommandEncoder &e)
                {
                    REQUIRE(scene.renderer->beginFrame(e).has_value());
                    REQUIRE(scene.renderer->setCamera(camera).has_value());
                    REQUIRE(scene.renderer->drawMesh(*mesh, camera).has_value());
                    REQUIRE(scene.renderer->endFrame({out, Format::kRGBA8Unorm, {16, 16}}).has_value());
                });
        };
        frame();
        CHECK(scene.renderer->frameWidth() == 8);
        CHECK(scene.renderer->sceneTarget().width() == 8);
        CHECK(counting->calls == 1);
        CHECK(rgba(scene.gpu.pixel(out, 8, 8))[0] > 0); // la mise à l'échelle bilinéaire a rempli la sortie
        CHECK(scene.renderer->setActiveUpscaleMode("recording"));
        frame();
        CHECK(recording->calls == 1);
        CHECK(counting->calls == 2); // exécutée à chaque image
        scene.renderer->removePostProcessPass("count");
        frame(); // plus appelée : l'objet est détruit, le test ne le relit pas (aucun accès invalide ne doit être signalé par ASan)
        scene.gpu.expectNoValidationErrors();
    }

    TEST_CASE("SceneRenderer: temporal resources (Halton jitter, history, motion vectors, sampled depth) follow the active modes")
    {
        Scene scene;
        SKIP_TEST_IF_NULL(scene.gpu.device.get(), "OpenGL 4.3 context");
        struct TemporalMode final : rd::IUpscaleMode
        {
            std::string name() const noexcept override { return "temporal"; }
            rd::UpscaleRequirements requirements() const noexcept override { return {.needsJitter = true, .needsHistory = true}; }
            engine::core::Result<> upscale(rd::FrameContext &, const rd::RenderTarget &, const rd::UpscaleTarget &) override { return {}; }
        };
        scene.renderer->registerUpscaleMode(std::make_unique<TemporalMode>());
        REQUIRE(scene.renderer->setActiveUpscaleMode("temporal"));
        scene.renderer->setOutputResolution(8, 4);
        scene.renderer->setMotionVectorsEnabled(true);
        scene.renderer->setDepthAsTextureEnabled(true);
        const TextureHandle out = scene.output(8, 4);
        rd::Camera camera;
        auto frame = [&]()
        {
            scene.gpu.run(
                [&](CommandEncoder &e)
                {
                    REQUIRE(scene.renderer->beginFrame(e).has_value());
                    REQUIRE(scene.renderer->setCamera(camera).has_value());
                    REQUIRE(scene.renderer->endFrame({out, Format::kRGBA8Unorm, {8, 4}}).has_value());
                });
        };
        frame();
        // Halton(1, 2) = 0,5 -> 0 ; Halton(1, 3) = 1/3 -> -(1/3) / hauteur de rendu.
        CHECK(scene.renderer->cameraJitter().x == doctest::Approx(0.0f));
        CHECK(scene.renderer->cameraJitter().y == doctest::Approx(-(1.0f / 3.0f) / 4.0f));
        CHECK(camera.jitter() == scene.renderer->cameraJitter());
        CHECK(scene.renderer->sceneTarget().colorCount() == 2);
        CHECK((scene.renderer->sceneTarget().info().colorFormats[1] == Format::kRG16Float));
        CHECK(scene.renderer->sceneTarget().info().depthSampled);
        frame();
        CHECK(scene.renderer->cameraJitter().x != 0.0f); // image suivante : Halton(2, 2) = 0,25
        scene.renderer->setCameraJitter({0.0f, 0.0f}); // manuel
        CHECK((scene.renderer->jitterMode() == rd::SceneRenderer::JitterMode::kManual));
        frame();
        CHECK(scene.renderer->cameraJitter() == glm::vec2(0.0f));
        scene.renderer->resetTemporalHistory();
        scene.gpu.expectNoValidationErrors();
    }

    TEST_CASE("SceneRenderer: misuse is reported, a disabled upscale mode leaves the output alone, unscaled rendering still presents")
    {
        Scene scene;
        SKIP_TEST_IF_NULL(scene.gpu.device.get(), "OpenGL 4.3 context");
        rd::Camera camera;
        auto encoder = scene.gpu->createCommandEncoder();
        CHECK((scene.renderer->beginFrame(*encoder).error().code == ErrorCode::kInvalidArgument)); // pas de résolution
        scene.renderer->setOutputResolution(4, 4);
        const TextureHandle out = scene.output(4, 4);
        auto mesh = rd::Mesh::create(*scene.gpu.device, triangleData({1, 0, 0}));
        REQUIRE(mesh.has_value());
        CHECK((scene.renderer->drawMesh(*mesh, camera).error().code == ErrorCode::kInvalidArgument)); // hors image
        CHECK((scene.renderer->endFrame({out, Format::kRGBA8Unorm, {4, 4}}).error().code == ErrorCode::kInvalidArgument));
        REQUIRE(scene.renderer->beginFrame(*encoder, {.clearColor = {0, 1, 0, 1}}).has_value());
        CHECK((scene.renderer->beginFrame(*encoder).error().code == ErrorCode::kInvalidArgument)); // déjà ouverte
        CHECK((scene.renderer->drawMesh(*mesh, camera).error().code == ErrorCode::kInvalidArgument)); // pas de programme
        REQUIRE(scene.renderer->endFrame({out, Format::kRGBA8Unorm, {4, 4}}).has_value());
        REQUIRE(scene.gpu->submit(std::move(encoder)).has_value());
        CHECK(rgba(scene.gpu.pixel(out, 2, 2))[1] == 255); // sans échelle : la scène est tout de même présentée en 1:1

        REQUIRE(scene.renderer->setActiveUpscaleMode("disabled"));
        CHECK(scene.renderer->activeUpscaleMode() == "disabled");
        const TextureHandle untouched = scene.output(4, 4);
        scene.gpu.run(
            [&](CommandEncoder &e)
            {
                REQUIRE(scene.renderer->beginFrame(e).has_value());
                REQUIRE(scene.renderer->endFrame({untouched, Format::kRGBA8Unorm, {4, 4}}).has_value()); // scale 1 : copie 1:1 de repli
            });
        CHECK(rgba(scene.gpu.pixel(untouched, 2, 2))[3] == 255);
        CHECK_FALSE(scene.renderer->saveColorPng("/nonexistent-dir-xyz/forbidden/shot.png").has_value());
        scene.gpu.expectNoValidationErrors();
    }
}

