// RHI-02 (lot B4) : couche de scène sur le RHI : caméra, lumières, maillage, rendu de scène avec mise à l'échelle, extensions de fin
// d'image, et parité avec l'image de référence du rendu hérité (docs/BASELINE/reference/). Contexte hors écran.
#include <doctest/doctest.h>

#include "support/test_support.hpp"
#include "renderer/repo_paths.hpp"
#include "rhi/gpu_fixture.hpp"
#include "support/temp_dir.hpp"

#include <engine/renderer/camera.hpp>
#include <engine/renderer/image.hpp>
#include <engine/renderer/light.hpp>
#include <engine/renderer/mesh.hpp>
#include <engine/renderer/scene_renderer.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;
namespace rd = engine::renderer;
namespace rhi = engine::rhi;

namespace
{

// Racine du dépôt déduite de ce fichier (les tests tournent depuis apps/demo : __FILE__ est relatif à ce dossier).
fs::path repoRoot() { return tests::repoRoot(__FILE__); }
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
        CHECK(lights.light(50) == nullptr); // hors limites : aucune exception (CLAUDE.md règle 10)
        CHECK((tests::errorOf(lights.setLight(50, rd::ambientLight({1, 1, 1}, 1.0f))) == ErrorCode::kOutOfRange));
        REQUIRE(lights.upload().has_value());
        CHECK_FALSE(lights.dirty());
        REQUIRE(lights.setLight(0, rd::ambientLight({1, 1, 1}, 0.5f)).has_value());
        CHECK(lights.dirty()); // la seule écriture passe par setLight, qui marque le tampon
        CHECK(lights.light(0)->strength == 0.5f);
        gpu.expectNoValidationErrors();
    }
}

TEST_SUITE("renderer/mesh")
{
    TEST_CASE("Mesh: an index past the last vertex and an update offset that would overflow are refused (G99)")
    {
        Gpu gpu;
        SKIP_TEST_IF_NULL(gpu.device.get(), "OpenGL 4.3 context");
        rd::MeshData bad = triangleData({1, 0, 0});
        bad.indices = {0, 1, 3}; // 3 sommets : l'index 3 n'existe pas
        auto refused = rd::Mesh::create(*gpu.device, bad);
        REQUIRE_FALSE(refused.has_value());
        CHECK((tests::errorOf(refused) == ErrorCode::kOutOfRange));
        auto mesh = rd::Mesh::create(*gpu.device, triangleData({1, 0, 0}));
        REQUIRE(mesh.has_value());
        const std::vector<float> patch{1, 2, 3};
        auto wrapped = mesh->updateVertices(patch, std::numeric_limits<std::size_t>::max() / sizeof(float) + 1); // * 4 reboucle à 0
        REQUIRE_FALSE(wrapped.has_value());
        CHECK((tests::errorOf(wrapped) == ErrorCode::kOutOfRange));
        gpu.expectNoValidationErrors();
    }

    TEST_CASE("Mesh: normals follow the model rotation (normal matrix computed per object) (G99)")
    {
        Scene scene;
        SKIP_TEST_IF_NULL(scene.gpu.device.get(), "OpenGL 4.3 context");
        auto program = scene.library->loadGraphics(shaderDir() / "mesh.vert", shaderDir() / "mesh.frag");
        REQUIRE(program.has_value());
        // Triangle plein écran de normale +X, tourné de 90° autour de Z : la normale devient +Y, alignée sur la direction de la
        // lumière directionnelle (+Y, convention de mesh.frag : diffuse = dot(normale, direction)). Sans rotation de la normale, la
        // diffuse serait nulle (normale perpendiculaire).
        rd::MeshData data = triangleData({1, 1, 1});
        for (std::size_t v = 0; v < 3; ++v)
        {
            data.vertices[v * 9 + 3] = 1.0f;
            data.vertices[v * 9 + 4] = 0.0f;
            data.vertices[v * 9 + 5] = 0.0f;
        }
        auto mesh = rd::Mesh::create(*scene.gpu.device, data);
        REQUIRE(mesh.has_value());
        mesh->setProgram(*program);
        auto lights = rd::LightManager::create(*scene.gpu.device);
        REQUIRE(lights.has_value());
        lights->addLight(rd::directionalLight({0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, 1.0f));
        REQUIRE(lights->upload().has_value());
        scene.renderer->setLights(&*lights);
        rd::Camera camera;
        camera.setAspect(8, 8);
        camera.setFarPlane(100.0f);
        const TextureHandle out = scene.output(8, 8);
        scene.renderer->setOutputResolution(8, 8);
        auto brightness = [&]()
        {
            scene.gpu.run(
                [&](CommandEncoder &e)
                {
                    REQUIRE(scene.renderer->beginFrame(e).has_value());
                    REQUIRE(scene.renderer->setCamera(camera).has_value());
                    REQUIRE(scene.renderer->drawMesh(*mesh, camera).has_value());
                    REQUIRE(scene.renderer->endFrame({out, Format::kRGBA8Unorm, {8, 8}}).has_value());
                });
            return static_cast<int>(rgba(scene.gpu.pixel(out, 4, 4))[0]);
        };
        const int sideways = brightness();
        mesh->setRotation({0.0f, 0.0f, 90.0f}); // le triangle reste dans le plan z = -1 vu de face ; seule la normale tourne
        const int facing = brightness();
        CHECK(facing > sideways + 20);
        scene.gpu.expectNoValidationErrors();
    }

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
        CHECK((tests::errorOf(rd::Mesh::create(*gpu.device, wide)) == ErrorCode::kInvalidArgument));
        rd::MeshData ragged = triangleData({1, 0, 0});
        ragged.vertices.pop_back();
        CHECK((tests::errorOf(rd::Mesh::create(*gpu.device, ragged)) == ErrorCode::kInvalidArgument));
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
        scene.renderer->setUpscalingEnabled(true); // un mode n'est exécuté (et ne reçoit le jitter) que si la mise à l'échelle est active
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
        CHECK((tests::errorOf(scene.renderer->beginFrame(*encoder)) == ErrorCode::kInvalidArgument)); // pas de résolution
        scene.renderer->setOutputResolution(4, 4);
        const TextureHandle out = scene.output(4, 4);
        auto mesh = rd::Mesh::create(*scene.gpu.device, triangleData({1, 0, 0}));
        REQUIRE(mesh.has_value());
        CHECK((tests::errorOf(scene.renderer->drawMesh(*mesh, camera)) == ErrorCode::kInvalidArgument)); // hors image
        CHECK((tests::errorOf(scene.renderer->endFrame({out, Format::kRGBA8Unorm, {4, 4}})) == ErrorCode::kInvalidArgument));
        REQUIRE(scene.renderer->beginFrame(*encoder, {.clearColor = {0, 1, 0, 1}}).has_value());
        CHECK((tests::errorOf(scene.renderer->beginFrame(*encoder)) == ErrorCode::kInvalidArgument)); // déjà ouverte
        CHECK((tests::errorOf(scene.renderer->drawMesh(*mesh, camera)) == ErrorCode::kInvalidArgument)); // pas de programme
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
        // Chemin impossible quels que soient les droits du processus : un composant parent est un FICHIER ordinaire.
        tests::TempDir dir("engine_scene_png");
        dir.write("not-a-directory", "x");
        CHECK_FALSE(scene.renderer->saveColorPng(dir.path / "not-a-directory" / "shot.png").has_value());
        scene.gpu.expectNoValidationErrors();
    }

    TEST_CASE("SceneRenderer: upscale modes are initialized before activation and shut down when replaced; a refusal keeps the active mode; a mode does not run while upscaling is disabled (G99)")
    {
        Scene scene;
        SKIP_TEST_IF_NULL(scene.gpu.device.get(), "OpenGL 4.3 context");
        struct Counting final : rd::IUpscaleMode
        {
            std::string id;
            bool accept = true;
            int inits = 0, shutdowns = 0, runs = 0;
            std::string name() const noexcept override { return id; }
            bool initialize(rhi::Device &) override
            {
                ++inits;
                return accept;
            }
            void shutdown() override { ++shutdowns; }
            engine::core::Result<> upscale(rd::FrameContext &, const rd::RenderTarget &, const rd::UpscaleTarget &) override
            {
                ++runs;
                return {};
            }
        };
        auto first = std::make_unique<Counting>();
        first->id = "first";
        auto broken = std::make_unique<Counting>();
        broken->id = "broken";
        broken->accept = false;
        Counting *a = first.get();
        Counting *b = broken.get();
        scene.renderer->registerUpscaleMode(std::move(first));
        scene.renderer->registerUpscaleMode(std::move(broken));
        REQUIRE(scene.renderer->setActiveUpscaleMode("first"));
        CHECK(a->inits == 1);
        CHECK_FALSE(scene.renderer->setActiveUpscaleMode("broken")); // initialize échoue
        CHECK(b->inits == 1);
        CHECK(scene.renderer->activeUpscaleMode() == "first"); // l'ancien reste actif, pas arrêté
        CHECK(a->shutdowns == 0);

        scene.renderer->setOutputResolution(4, 4);
        const TextureHandle out = scene.output(4, 4);
        auto frame = [&]()
        {
            scene.gpu.run(
                [&](CommandEncoder &e)
                {
                    REQUIRE(scene.renderer->beginFrame(e, {.clearColor = {0, 0, 1, 1}}).has_value());
                    REQUIRE(scene.renderer->endFrame({out, Format::kRGBA8Unorm, {4, 4}}).has_value());
                });
        };
        frame(); // mise à l'échelle désactivée : le mode ne tourne pas, la scène est copiée 1:1
        CHECK(a->runs == 0);
        CHECK(rgba(scene.gpu.pixel(out, 1, 1)) == std::array<std::uint8_t, 4>{0, 0, 255, 255});
        scene.renderer->setUpscalingEnabled(true);
        frame();
        CHECK(a->runs == 1);

        REQUIRE(scene.renderer->setActiveUpscaleMode("bilinear-blit"));
        CHECK(a->shutdowns == 1); // remplacé : arrêté
        REQUIRE(scene.renderer->setActiveUpscaleMode("first"));
        CHECK(a->inits == 2);
        REQUIRE(scene.renderer->setActiveUpscaleMode("disabled"));
        CHECK(a->shutdowns == 2);
        scene.gpu.expectNoValidationErrors();
    }

    TEST_CASE("SceneRenderer: frame generation reads the PREVIOUS image as history, resets it on the first frame, exposes its output; an unsupported mode is refused (G99)")
    {
        Scene scene;
        SKIP_TEST_IF_NULL(scene.gpu.device.get(), "OpenGL 4.3 context");
        struct Recording final : rd::IFrameGenerationMode
        {
            rd::FrameGenerationModeDesc desc{.name = "recording"};
            bool supported = true;
            bool hadPrevious = false;
            bool reset = false;
            std::array<std::uint8_t, 4> previous{};
            rhi::TextureHandle produce;
            int shutdowns = 0;
            const rd::FrameGenerationModeDesc &description() const noexcept override { return desc; }
            bool isSupported(const rhi::Capabilities &) const noexcept override { return supported; }
            bool initialize(rhi::Device &) override { return true; }
            void shutdown() override { ++shutdowns; }
            engine::core::Result<> generateFrame(rd::FrameContext &context, const rd::FrameGenerationInput &input, rd::FrameGenerationOutput &output) override
            {
                hadPrevious = !input.previousColor.isNull();
                reset = input.resetHistory;
                if (hadPrevious)
                {
                    auto px = context.device.readTexture(input.previousColor, {.offset = {1, 1, 0}, .size = {1, 1, 1}});
                    REQUIRE(px.has_value());
                    previous = rgba(*px);
                }
                output.generatedFrame = produce;
                return {};
            }
        };
        auto unsupported = std::make_unique<Recording>();
        unsupported->supported = false;
        auto refused = scene.renderer->setFrameGenerationMode(std::move(unsupported));
        REQUIRE_FALSE(refused.has_value());
        CHECK((tests::errorOf(refused) == ErrorCode::kUnsupported));

        auto mode = std::make_unique<Recording>();
        Recording *rec = mode.get();
        rec->produce = scene.output(4, 4);
        REQUIRE(scene.renderer->setFrameGenerationMode(std::move(mode)).has_value());
        auto other = std::make_unique<Recording>();
        other->supported = false;
        CHECK_FALSE(scene.renderer->setFrameGenerationMode(std::move(other)).has_value());
        CHECK(rec->shutdowns == 0); // le mode actif n'a pas été arrêté par le refus

        scene.renderer->setOutputResolution(4, 4);
        const TextureHandle out = scene.output(4, 4);
        auto frame = [&](std::array<float, 4> clear)
        {
            scene.gpu.run(
                [&](CommandEncoder &e)
                {
                    REQUIRE(scene.renderer->beginFrame(e, {.clearColor = clear}).has_value());
                    REQUIRE(scene.renderer->endFrame({out, Format::kRGBA8Unorm, {4, 4}}).has_value());
                });
        };
        frame({1, 0, 0, 1}); // rouge
        CHECK_FALSE(rec->hadPrevious);
        CHECK(rec->reset);
        CHECK(scene.renderer->generatedFrame() == rec->produce);
        frame({0, 1, 0, 1}); // vert : le mode doit voir le rouge de l'image précédente
        REQUIRE(rec->hadPrevious);
        CHECK_FALSE(rec->reset);
        CHECK(rec->previous == std::array<std::uint8_t, 4>{255, 0, 0, 255});
        REQUIRE(scene.renderer->setFrameGenerationMode(nullptr).has_value());
        CHECK(scene.renderer->generatedFrame().isNull());
        scene.gpu.expectNoValidationErrors();
    }

    TEST_CASE("SceneRenderer: lights selected after beginFrame light the first draw (G99)")
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
        rd::Camera camera;
        camera.setAspect(4, 4);
        camera.setFarPlane(100.0f);
        const TextureHandle out = scene.output(4, 4);
        scene.renderer->setOutputResolution(4, 4);
        scene.gpu.run(
            [&](CommandEncoder &e)
            {
                REQUIRE(scene.renderer->beginFrame(e).has_value());
                scene.renderer->setLights(&*lights); // comme World::Render : après beginFrame
                REQUIRE(scene.renderer->setCamera(camera).has_value());
                REQUIRE(scene.renderer->drawMesh(*mesh, camera).has_value());
                REQUIRE(scene.renderer->endFrame({out, Format::kRGBA8Unorm, {4, 4}}).has_value());
            });
        CHECK(rgba(scene.gpu.pixel(out, 2, 2))[0] > 200); // éclairé dès la première image (l'ambiante nulle donnerait du noir)
        scene.gpu.expectNoValidationErrors();
    }

    TEST_CASE("SceneRenderer sends the push constants of the revision that built the pipeline it draws with (fallback after a failed link)")
    {
        Scene scene;
        SKIP_TEST_IF_NULL(scene.gpu.device.get(), "OpenGL 4.3 context");
        tests::TempDir dir("engine_scene_push");
        // Même interface que mesh.vert ; le fragment lit sa couleur dans les push constants (premier entier : fil de fer).
        std::ifstream meshVs(shaderDir() / "mesh.vert");
        std::stringstream vs;
        vs << meshVs.rdbuf();
        dir.write("m.vert", vs.str());
        dir.write("m.frag", "#version 450\nlayout(location = 0) in vec3 normal;\nlayout(location = 1) in vec3 crntPos;\nlayout(location = 2) in vec3 color;\n"
                            "layout(push_constant) uniform Push { int wire; int pad0; int pad1; int pad2; vec4 tint; } pc;\n"
                            "layout(location = 0) out vec4 FragColor;\nvoid main() { FragColor = pc.tint; }\n");
        auto program = scene.library->loadGraphics(dir.path / "m.vert", dir.path / "m.frag");
        REQUIRE(program.has_value());
        auto mesh = rd::Mesh::create(*scene.gpu.device, triangleData({1, 1, 1}));
        REQUIRE(mesh.has_value());
        mesh->setProgram(*program);
        rd::Camera camera;
        camera.setAspect(4, 4);
        camera.setFarPlane(100.0f);
        const TextureHandle out = scene.output(4, 4);
        scene.renderer->setOutputResolution(4, 4);
        auto drawWithTint = [&](const glm::vec4 &tint)
        {
            std::array<std::byte, 32> push{};
            std::memcpy(push.data() + 16, &tint, 16);
            mesh->setPushConstants(push);
            scene.gpu.run(
                [&](CommandEncoder &e)
                {
                    REQUIRE(scene.renderer->beginFrame(e).has_value());
                    REQUIRE(scene.renderer->setCamera(camera).has_value());
                    REQUIRE(scene.renderer->drawMesh(*mesh, camera).has_value());
                    REQUIRE(scene.renderer->endFrame({out, Format::kRGBA8Unorm, {4, 4}}).has_value());
                });
            return rgba(scene.gpu.pixel(out, 2, 2));
        };
        CHECK(drawWithTint({1, 0, 0, 1}) == std::array<std::uint8_t, 4>{255, 0, 0, 255});

        // Nouvelle révision : plus de push constants et une interface qui ne se lie plus (vec4 au lieu de vec3).
        dir.write("m.frag", "#version 450\nlayout(location = 0) in vec4 normal;\nlayout(location = 0) out vec4 FragColor;\nvoid main() { FragColor = normal; }\n");
        REQUIRE(scene.library->pollHotReload() == 1);
        CHECK((*program)->reflection().pushConstantSize == 0u);
        // L'ancien pipeline continue ; ses constantes doivent être celles de CE dessin (vert), pas celles du précédent (rouge).
        CHECK(drawWithTint({0, 1, 0, 1}) == std::array<std::uint8_t, 4>{0, 255, 0, 255});
        CHECK(scene.pipelines->lastError().has_value());
    }

    TEST_CASE("SceneRenderer: the first SUPPORTED mode of a name is chosen; a failing end of frame leaves no stale generated image nor history")
    {
        Scene scene;
        SKIP_TEST_IF_NULL(scene.gpu.device.get(), "OpenGL 4.3 context");
        struct Named final : rd::IUpscaleMode
        {
            bool supported = true;
            int inits = 0;
            std::string name() const noexcept override { return "dup"; }
            bool supports(const rhi::Capabilities &) const noexcept override { return supported; }
            bool initialize(rhi::Device &) override
            {
                ++inits;
                return true;
            }
            engine::core::Result<> upscale(rd::FrameContext &, const rd::RenderTarget &, const rd::UpscaleTarget &) override { return {}; }
        };
        auto unsupported = std::make_unique<Named>();
        unsupported->supported = false;
        auto supported = std::make_unique<Named>();
        Named *chosen = supported.get();
        scene.renderer->registerUpscaleMode(std::move(unsupported));
        scene.renderer->registerUpscaleMode(std::move(supported));
        REQUIRE(scene.renderer->setActiveUpscaleMode("dup"));
        CHECK(chosen->inits == 1);

        struct Flaky final : rd::IFrameGenerationMode
        {
            rd::FrameGenerationModeDesc desc{.name = "flaky"};
            bool fail = false;
            bool reset = false;
            bool hadPrevious = false;
            rhi::TextureHandle produce;
            const rd::FrameGenerationModeDesc &description() const noexcept override { return desc; }
            bool isSupported(const rhi::Capabilities &) const noexcept override { return true; }
            bool initialize(rhi::Device &) override { return true; }
            void shutdown() override {}
            engine::core::Result<> generateFrame(rd::FrameContext &, const rd::FrameGenerationInput &input, rd::FrameGenerationOutput &output) override
            {
                reset = input.resetHistory;
                hadPrevious = !input.previousColor.isNull();
                if (fail)
                {
                    return engine::core::makeError(ErrorCode::kUnknown, "flaky");
                }
                output.generatedFrame = produce;
                return {};
            }
        };
        auto mode = std::make_unique<Flaky>();
        Flaky *flaky = mode.get();
        flaky->produce = scene.output(4, 4);
        REQUIRE(scene.renderer->setFrameGenerationMode(std::move(mode)).has_value());
        scene.renderer->setOutputResolution(4, 4);
        const TextureHandle out = scene.output(4, 4);
        auto frame = [&]()
        {
            auto e = scene.gpu->createCommandEncoder();
            REQUIRE(scene.renderer->beginFrame(*e).has_value());
            auto ended = scene.renderer->endFrame({out, Format::kRGBA8Unorm, {4, 4}});
            REQUIRE(scene.gpu->submit(std::move(e)).has_value());
            return ended.has_value();
        };
        REQUIRE(frame());
        CHECK_FALSE(scene.renderer->generatedFrame().isNull());
        REQUIRE(frame());
        CHECK(flaky->hadPrevious); // historique écrit à l'image 1
        flaky->fail = true;
        CHECK_FALSE(frame()); // l'image 3 échoue avant d'écrire l'historique
        CHECK(scene.renderer->generatedFrame().isNull()); // pas le handle d'une image précédente
        flaky->fail = false;
        REQUIRE(frame());
        CHECK_FALSE(flaky->hadPrevious); // l'historique n'a pas été écrit à l'image 3 : pas d'image précédente valide...
        CHECK(flaky->reset);              // ...et une réinitialisation est signalée
        scene.gpu.expectNoValidationErrors();
    }
}
