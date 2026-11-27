// A79 lot L13 : façade `app::Application` (A75.1, reste d'A75.9) et composants de rendu de la scène rendus par un système (A73
// point 3). Une application sans fenêtre rend la même image que la même scène construite à la main ; Vulkan qui ne démarre pas
// passe à OpenGL ; les rappels sont appelés dans l'ordre, avec un delta borné et le pas fixe ; le mode de dessin est une propriété
// du composant (seul, instancié, foule). Tests GPU ignorés sans périphérique (`tests::backendsToRun()`).
#include <doctest/doctest.h>

#include "renderer/counting_device.hpp"
#include "renderer/repo_paths.hpp"
#include "rhi/backends.hpp"
#include "support/result_support.hpp"
#include "support/test_support.hpp"
#include "support/temp_dir.hpp"

#include <orosmo/anim/animated_model.hpp>
#include <orosmo/anim/crowd.hpp>
#include <orosmo/app/application.hpp>
#include <orosmo/app/game_context.hpp>
#include <orosmo/app/game_module.hpp>
#include <orosmo/app/render_components.hpp>
#include <orosmo/app/resource_pool.hpp>
#include <orosmo/app/scene_render_system.hpp>
#include <orosmo/assets/model.hpp>
#include <orosmo/renderer/camera.hpp>
#include <orosmo/renderer/image.hpp>
#include <orosmo/renderer/light.hpp>
#include <orosmo/renderer/mesh.hpp>
#include <orosmo/renderer/mesh_builder.hpp>
#include <orosmo/renderer/render_target.hpp>
#include <orosmo/renderer/scene_renderer.hpp>
#include <orosmo/sim/simulation.hpp>
#include <orosmo/ui/ui.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <type_traits>
#include <vector>

namespace fs = std::filesystem;
namespace app = orosmo::app;
namespace rd = orosmo::renderer;
namespace rhi = orosmo::rhi;
namespace scn = orosmo::scene;
using orosmo::core::ErrorCode;

namespace
{

constexpr int kSize = 64;

fs::path repo() { return tests::repoRoot(__FILE__); }

app::ApplicationSettings headlessSettings(rhi::Backend backend)
{
    app::ApplicationSettings s;
    s.title = "application test";
    s.width = kSize;
    s.height = kSize;
    s.backend = backend;
    s.backendFallback = false;
    s.headless = true;
    s.resourceDirectory = repo() / "res";
    s.shaderDirectory = repo() / "shaders" / "rhi";
    s.ui = false;
    s.frames = 1;
    return s;
}

std::unique_ptr<app::Application> createApp(const app::ApplicationSettings &settings)
{
    auto created = app::Application::create(settings);
    INFO((created ? std::string() : created.error().message));
    REQUIRE(created.has_value());
    return std::move(*created);
}

rd::MeshBuilder coloredCube()
{
    rd::MeshBuilder builder(rd::MeshLayout::kColored);
    builder.setName("cube").addBox({0.6f, 0.6f, 0.6f}, {0.85f, 0.45f, 0.25f, 1.0f});
    return builder;
}

// Pixels qui diffèrent de plus de `tolerance` sur un canal.
std::size_t differingPixels(const rd::Image &a, const rd::Image &b, std::uint8_t tolerance)
{
    auto diff = rd::compareImages(a, b, tolerance);
    REQUIRE(diff.has_value());
    return static_cast<std::size_t>(diff->outOfTolerance);
}

std::size_t litPixels(const rd::Image &image, const rd::Image &background)
{
    return differingPixels(image, background, 8);
}

// ── Jeu de test lié dans l'exécutable (GameSource::fromApi) ───────────────────

struct Counter
{
    std::uint32_t steps = 0;
};

} // namespace

template <> struct orosmo::core::Reflect<Counter>
{
    static constexpr auto fields() { return std::make_tuple(field("steps", &Counter::steps)); }
};

namespace
{

bool counterInit(app::GameContext *ctx)
{
    auto counter = ctx->registerComponent<Counter>("Counter");
    if (!counter)
        return false;
    const auto type = *counter;
    const scn::Entity e = ctx->world().create();
    if (ctx->world().add(e, type) == nullptr)
        return false;
    if (!ctx->addAction("ping", [](const orosmo::ui::Event &) {}))
        return false;
    return ctx->addSystem({"count", orosmo::sim::Phase::kUpdate, 0, [type](orosmo::sim::StepContext &step) {
                              step.world.view(type).each([](scn::Entity, Counter &c) { ++c.steps; });
                          }})
        .has_value();
}

constexpr OrosmoGameApi kCounterGame{orosmo::app::kGameAbiVersion, "counter", &counterInit, nullptr, nullptr, nullptr};

// ── Destruction : les rappels et leurs captures partent avant le jeu et le périphérique ───────────────────────────

int gAliveCaptures = 0;
int gAliveCapturesAtGameShutdown = -1;

bool lifetimeInit(app::GameContext *) { return true; }
void lifetimeShutdown(app::GameContext *) { gAliveCapturesAtGameShutdown = gAliveCaptures; }

constexpr OrosmoGameApi kLifetimeGame{orosmo::app::kGameAbiVersion, "lifetime", &lifetimeInit, nullptr, nullptr, &lifetimeShutdown};

// Ressource du périphérique tenue par la capture d'un rappel.
std::shared_ptr<rd::Mesh> capturedMesh(app::Application &a)
{
    auto mesh = coloredCube().create(a.device());
    REQUIRE(mesh.has_value());
    ++gAliveCaptures;
    return std::shared_ptr<rd::Mesh>(new rd::Mesh(std::move(*mesh)), [](rd::Mesh *m) {
        --gAliveCaptures;
        delete m;
    });
}

// Application dont le périphérique peut faire échouer beginFrame / present.
std::unique_ptr<app::Application> createSpied(rhi::Backend backend, std::uint32_t frames, tests::CountingDevice *&spy)
{
    auto settings = headlessSettings(backend);
    settings.frames = frames;
    settings.createDevice = [&spy](const rhi::DeviceDesc &desc) -> orosmo::core::Result<std::unique_ptr<rhi::Device>>
    {
        auto real = rhi::createDevice(desc);
        if (!real)
            return std::unexpected(real.error());
        auto counting = std::make_unique<tests::CountingDevice>(std::move(*real));
        spy = counting.get();
        return std::unique_ptr<rhi::Device>(std::move(counting));
    };
    auto application = createApp(settings);
    REQUIRE(spy != nullptr);
    return application;
}

// Bras à deux os et deux clips (comme tests/anim/animated_model_tests.cpp).
orosmo::assets::Model makeArm()
{
    namespace as = orosmo::assets;
    using orosmo::core::Vec3;
    auto clip = [](const char *name, int node, const Vec3 &axis, float angle)
    {
        as::AnimationData anim;
        anim.name = name;
        anim.duration = 1.0f;
        as::AnimationChannel ch;
        ch.node = node;
        ch.path = as::AnimationPath::kRotation;
        ch.interpolation = as::Interpolation::kLinear;
        ch.componentCount = 4;
        for (int i = 0; i <= 4; ++i)
        {
            const float t = static_cast<float>(i) / 4.0f;
            const glm::quat q = glm::angleAxis(angle * t, axis);
            ch.times.push_back(t);
            ch.values.insert(ch.values.end(), {q.x, q.y, q.z, q.w});
        }
        anim.channels.push_back(std::move(ch));
        return anim;
    };
    as::Model m;
    as::MeshPrimitive prim;
    for (int y = 0; y <= 3; ++y)
    {
        for (int x = 0; x <= 1; ++x)
        {
            prim.positions.emplace_back(static_cast<float>(x) * 0.5f - 0.25f, static_cast<float>(y) * 0.5f, 0.0f);
            prim.normals.emplace_back(0.0f, 0.0f, 1.0f);
            prim.uv0.emplace_back(static_cast<float>(x), static_cast<float>(y) / 3.0f);
            const float w = y <= 1 ? 0.0f : (y == 2 ? 0.5f : 1.0f);
            prim.joints.emplace_back(0, 1, 0, 0);
            prim.weights.emplace_back(1.0f - w, w, 0.0f, 0.0f);
        }
    }
    for (std::uint32_t s = 0; s < 3; ++s)
    {
        const std::uint32_t a = s * 2;
        prim.indices.insert(prim.indices.end(), {a, a + 1, a + 3, a, a + 3, a + 2});
    }
    prim.material = 0;
    m.meshes.push_back({"arm", {prim}});
    m.materials.push_back({});
    m.materials.back().doubleSided = true;
    m.materials.back().metallicFactor = 0.0f;
    m.nodes.resize(3);
    m.nodes[0].name = "mesh";
    m.nodes[0].mesh = 0;
    m.nodes[0].skin = 0;
    m.nodes[1].name = "shoulder";
    m.nodes[1].children = {2};
    m.nodes[2].name = "elbow";
    m.nodes[2].parent = 1;
    m.nodes[2].translation = Vec3(0.0f, 0.75f, 0.0f);
    as::SkinData skin;
    skin.joints = {1, 2};
    skin.inverseBindMatrices = {orosmo::core::Mat4(1.0f), glm::translate(orosmo::core::Mat4(1.0f), Vec3(0.0f, -0.75f, 0.0f))};
    m.skins.push_back(skin);
    m.animations.push_back(clip("bend", 2, Vec3(0, 0, 1), 1.5f));
    m.animations.push_back(clip("twist", 1, Vec3(0, 1, 0), 3.0f));
    m.scenes.push_back({"s", {0, 1}});
    m.defaultScene = 0;
    return m;
}

} // namespace

TEST_SUITE("app/application")
{
    TEST_CASE("ResourcePool: handles with generation, a removed handle never reaches the object that reuses its slot")
    {
        app::ResourcePool<int> pool;
        CHECK(pool.empty());
        CHECK_FALSE(pool.add(nullptr).valid());
        const auto a = pool.add(std::make_unique<int>(1));
        const auto b = pool.add(std::make_unique<int>(2));
        REQUIRE(a.valid());
        REQUIRE(b.valid());
        CHECK(*pool.get(a) == 1);
        CHECK(*pool.get(b) == 2);
        CHECK(pool.size() == 2);
        CHECK(pool.remove(a));
        CHECK_FALSE(pool.remove(a));
        CHECK(pool.get(a) == nullptr);
        const auto c = pool.add(std::make_unique<int>(3)); // réutilise l'emplacement de `a`
        CHECK(c.index == a.index);
        CHECK(c.generation != a.generation);
        CHECK(pool.get(a) == nullptr);
        CHECK(*pool.get(c) == 3);
        CHECK(pool.get(orosmo::core::Handle<int>{}) == nullptr);
        CHECK(pool.get(orosmo::core::Handle<int>{42, 1}) == nullptr);
        int sum = 0;
        pool.forEach([&](orosmo::core::Handle<int>, int &v) { sum += v; });
        CHECK(sum == 5);
        pool.clear();
        CHECK(pool.empty());
        CHECK(pool.get(b) == nullptr);
    }

    TEST_CASE("parseArguments: API, strict backend, headless, size, frames, capture (made absolute), help, errors")
    {
        app::ApplicationSettings s;
        const char *args[] = {"game", "--api", "opengl", "--strict-backend", "--headless", "--size", "320x200", "--frames", "5",
                              "--capture", "out", "--capture-frames", "2"};
        auto parsed = app::parseArguments(s, static_cast<int>(std::size(args)), args);
        REQUIRE(parsed.has_value());
        CHECK(*parsed);
        CHECK((s.backend == rhi::Backend::kOpenGL));
        CHECK_FALSE(s.backendFallback);
        CHECK(s.headless);
        CHECK(s.width == 320);
        CHECK(s.height == 200);
        CHECK(s.frames == 5);
        CHECK(s.captureFrames == 2);
        CHECK(s.captureDirectory.is_absolute());
        CHECK(s.captureDirectory.filename() == "out");

        app::ApplicationSettings d;
        const char *help[] = {"game", "--help", "--unknown"};
        auto h = app::parseArguments(d, 3, help);
        REQUIRE(h.has_value());
        CHECK_FALSE(*h);
        CHECK((d.backend == rhi::Backend::kVulkan)); // défaut : Vulkan avec repli
        CHECK(d.backendFallback);

        const auto refused = [](std::vector<const char *> argv, std::string_view what)
        {
            app::ApplicationSettings x;
            auto r = app::parseArguments(x, static_cast<int>(argv.size()), argv.data());
            REQUIRE_FALSE(r.has_value());
            CHECK((r.error().code == ErrorCode::kInvalidArgument));
            CHECK(r.error().message.find(what) != std::string::npos);
        };
        refused({"g", "--bogus"}, "--bogus");
        refused({"g", "--api"}, "--api");
        refused({"g", "--api", "dx12"}, "dx12");
        refused({"g", "--size", "12"}, "--size");
        refused({"g", "--size", "0x10"}, "--size");
        refused({"g", "--frames", "0"}, "--frames");
        refused({"g", "--capture-frames", "x"}, "--capture-frames");
        CHECK(app::usage("game").find("--headless") != std::string::npos);
    }

    TEST_CASE("Render components: engine-defined data, registered by name, saved and restored with the simulation state")
    {
        static_assert(std::is_trivially_copyable_v<app::MeshRenderer> && std::is_trivially_copyable_v<app::LightComponent> &&
                      std::is_trivially_copyable_v<app::CameraComponent> && std::is_trivially_copyable_v<app::AnimatedModelComponent>);
        orosmo::sim::Simulation sim;
        auto types = app::registerRenderComponents(sim.world());
        REQUIRE(types.has_value());
        auto again = app::registerRenderComponents(sim.world()); // même disposition : mêmes identifiants
        REQUIRE(again.has_value());
        CHECK(again->mesh.id == types->mesh.id);
        CHECK(again->animatedModel.id == types->animatedModel.id);
        CHECK(sim.world().componentType<app::CameraComponent>(app::kCameraComponentName).has_value());
        CHECK(sim.world().componentType<app::LightComponent>(app::kLightComponentName).has_value());

        scn::World &w = sim.world();
        const scn::Entity e = w.create();
        REQUIRE(w.setTransform(e, {.position = {1.0, 2.0, 3.0}}).has_value());
        REQUIRE(w.add(e, types->mesh, {.mesh = {3, 7}, .mode = app::DrawMode::kInstanced}) != nullptr);
        REQUIRE(w.add(e, types->light, {.type = rd::LightType::kSpot, .intensity = 2.5f}) != nullptr);
        REQUIRE(w.add(e, types->camera, {.fovDegrees = 45.0f}) != nullptr);
        REQUIRE(w.add(e, types->animatedModel, {.mode = app::DrawMode::kCrowd, .clip = 1, .time = 0.5f}) != nullptr);

        const auto state = sim.saveState();
        orosmo::sim::Simulation copy;
        auto copyTypes = app::registerRenderComponents(copy.world());
        REQUIRE(copyTypes.has_value());
        auto loaded = copy.loadState(state);
        INFO((loaded ? std::string() : loaded.error().message));
        REQUIRE(loaded.has_value());
        CHECK(copy.fingerprint() == sim.fingerprint());
        const auto *mesh = copy.world().get(e, copyTypes->mesh);
        REQUIRE(mesh != nullptr);
        CHECK((mesh->mesh == orosmo::core::Handle<rd::Mesh>{3, 7}));
        CHECK((mesh->mode == app::DrawMode::kInstanced));
        CHECK((copy.world().get(e, copyTypes->light)->type == rd::LightType::kSpot));
        CHECK(copy.world().get(e, copyTypes->camera)->fovDegrees == doctest::Approx(45.0f));
        CHECK(copy.world().get(e, copyTypes->animatedModel)->clip == 1);
    }
}

TEST_SUITE("app/application-gpu")
{
    TEST_CASE("Application without a window renders the same image as the same scene built by hand")
    {
        for (const rhi::Backend backend : tests::backendsToRun())
        {
            CAPTURE(tests::backendName(backend));
            auto application = createApp(headlessSettings(backend));
            app::Application &a = *application;
            CHECK_FALSE(a.fellBack());
            CHECK((a.backend() == backend));
            CHECK(a.window() == nullptr);
            CHECK_FALSE(a.hasUi());

            auto cube = a.addMesh(coloredCube());
            REQUIRE(cube.has_value());
            const scn::Transform cubeAt{.position = {0.3, 0.0, 0.0}, .rotation = glm::dquat(glm::dvec3(0.3, 0.7, 0.0))};
            REQUIRE_FALSE(a.spawn(app::MeshRenderer{.mesh = *cube}, cubeAt).isNull());
            REQUIRE_FALSE(a.spawn(app::LightComponent{.type = rd::LightType::kAmbient, .color = {1, 1, 1}, .intensity = 0.3f}).isNull());
            REQUIRE_FALSE(
                a.spawn(app::LightComponent{.color = {1.0f, 0.95f, 0.9f}, .intensity = 0.9f, .direction = {-0.6f, -0.8f, -0.5f}}).isNull());
            REQUIRE_FALSE(a.spawn(app::CameraComponent{.fovDegrees = 50.0f, .forward = {0.0f, -0.3f, -1.0f}}, {.position = {0.0, 1.2, 3.5}})
                              .isNull());
            REQUIRE(a.run());
            CHECK(a.frameIndex() == 1);
            CHECK(a.outputSize().width == static_cast<std::uint32_t>(kSize));
            const auto &stats = a.sceneSystem().stats();
            CHECK(stats.meshDraws == 1);
            CHECK(stats.lights == 1);
            auto fromApp = a.readOutput();
            REQUIRE(fromApp.has_value());

            // La même scène avec l'API bas niveau, sur le même périphérique.
            rhi::Device &device = a.device();
            auto renderer = rd::SceneRenderer::create(a.renderContext());
            REQUIRE(renderer.has_value());
            auto lights = rd::LightManager::create(device);
            REQUIRE(lights.has_value());
            lights->setAmbientLight({1, 1, 1}, 0.3f);
            lights->addLight(rd::directionalLight({.direction = glm::normalize(glm::vec3(-0.6f, -0.8f, -0.5f)), .color = {1.0f, 0.95f, 0.9f}, .intensity = 0.9f}));
            REQUIRE(lights->upload().has_value());
            (*renderer)->setLights(&*lights);
            auto mesh = coloredCube().create(device);
            REQUIRE(mesh.has_value());
            mesh->setModelMatrix(glm::translate(glm::mat4(1.0f), glm::vec3(0.3f, 0.0f, 0.0f)) *
                                 glm::mat4_cast(glm::quat(glm::dquat(glm::dvec3(0.3, 0.7, 0.0)))));
            rd::Camera camera;
            camera.setPosition({0.0f, 1.2f, 3.5f});
            camera.setForward({0.0f, -0.3f, -1.0f});
            camera.setFovDegrees(50.0f);
            camera.setNearPlane(0.1f);
            camera.setFarPlane(1000.0f);
            camera.setAspect(kSize, kSize);
            auto target = rd::RenderTarget::create(device, {.size = {kSize, kSize}, .depth = false, .debugName = "hand"});
            REQUIRE(target.has_value());
            (*renderer)->setOutputResolution(kSize, kSize);
            auto encoder = device.createCommandEncoder();
            REQUIRE((*renderer)->beginFrame(*encoder, {.clearColor = a.settings().clearColor}).has_value());
            REQUIRE((*renderer)->setCamera(camera).has_value());
            REQUIRE((*renderer)->drawMesh(*mesh).has_value());
            REQUIRE((*renderer)->endFrame({target->color(0), rhi::Format::kRGBA8Unorm, {kSize, kSize}}).has_value());
            REQUIRE(device.submit(std::move(encoder)).has_value());
            auto byHand = rd::readImage(device, target->color(0));
            REQUIRE(byHand.has_value());

            CHECK(differingPixels(*fromApp, *byHand, 1) == 0);
            // Le cube est bien dessiné (pas deux images vides).
            rd::Image background = *byHand;
            for (std::size_t i = 0; i < background.rgba.size(); i += 4)
            {
                background.rgba[i] = static_cast<std::uint8_t>(std::lround(a.settings().clearColor[0] * 255.0f));
                background.rgba[i + 1] = static_cast<std::uint8_t>(std::lround(a.settings().clearColor[1] * 255.0f));
                background.rgba[i + 2] = static_cast<std::uint8_t>(std::lround(a.settings().clearColor[2] * 255.0f));
                background.rgba[i + 3] = 255;
            }
            CHECK(litPixels(*fromApp, background) > 200);
            CHECK(device.validationErrorCount() == 0);
        }
    }

    TEST_CASE("Backend fallback: Vulkan that cannot start falls back to OpenGL; refused without fallback or from OpenGL")
    {
        const auto available = tests::backendsToRun();
        if (std::find(available.begin(), available.end(), rhi::Backend::kOpenGL) == available.end())
            return;
        auto settings = headlessSettings(rhi::Backend::kVulkan);
        int attempts = 0;
        settings.createDevice = [&attempts](const rhi::DeviceDesc &desc) -> orosmo::core::Result<std::unique_ptr<rhi::Device>>
        {
            ++attempts;
            if (desc.backend == rhi::Backend::kVulkan)
                return orosmo::core::makeError(ErrorCode::kUnsupported, "test: no Vulkan");
            return rhi::createDevice(desc);
        };
        settings.backendFallback = true;
        {
            auto application = createApp(settings);
            CHECK(application->fellBack());
            CHECK((application->backend() == rhi::Backend::kOpenGL));
            CHECK(attempts == 2);
            CHECK(application->run());
        }

        settings.backendFallback = false; // --strict-backend
        attempts = 0;
        auto strict = app::Application::create(settings);
        REQUIRE_FALSE(strict.has_value());
        CHECK((strict.error().code == ErrorCode::kUnsupported));
        CHECK(strict.error().message.find("test: no Vulkan") != std::string::npos);
        CHECK(attempts == 1);

        auto gl = settings;
        gl.backend = rhi::Backend::kOpenGL;
        gl.backendFallback = true;
        gl.createDevice = [&attempts](const rhi::DeviceDesc &) -> orosmo::core::Result<std::unique_ptr<rhi::Device>>
        {
            ++attempts;
            return orosmo::core::makeError(ErrorCode::kUnsupported, "test: no OpenGL");
        };
        attempts = 0;
        auto noGl = app::Application::create(gl);
        REQUIRE_FALSE(noGl.has_value());
        CHECK(attempts == 1); // pas de repli depuis OpenGL
    }

    TEST_CASE("Callbacks: update, then UI, then render, each frame; the game module runs on the fixed step")
    {
        for (const rhi::Backend backend : tests::backendsToRun())
        {
            CAPTURE(tests::backendName(backend));
            auto settings = headlessSettings(backend);
            settings.ui = true; // police et thème du dépôt
            settings.frames = 3;
            settings.game = app::GameSource::fromApi(&kCounterGame);
            settings.game->required = true;
            auto application = createApp(settings);
            app::Application &a = *application;
            REQUIRE(a.hasUi());
            REQUIRE(a.game() != nullptr);
            CHECK(a.game()->systems() == std::vector<std::string>{"count"});
            CHECK(a.ui().actions().contains("ping"));

            std::vector<std::string> calls;
            std::vector<app::FrameTime> times;
            a.onUpdate([&](app::Application &, const app::FrameTime &t) {
                calls.push_back("update");
                times.push_back(t);
            });
            a.onUi([&](app::Application &self, const app::FrameTime &) {
                calls.push_back("ui");
                CHECK(&self == &a);
            });
            a.onRender([&](app::Application &, app::RenderFrame &frame) -> orosmo::core::Result<> {
                calls.push_back("render");
                CHECK(frame.size.width == static_cast<std::uint32_t>(kSize));
                return {};
            });
            REQUIRE(a.run());
            CHECK(calls == std::vector<std::string>{"update", "ui", "render", "update", "ui", "render", "update", "ui", "render"});
            REQUIRE(times.size() == 3);
            const double step = a.simulation().stepSeconds();
            for (std::size_t i = 0; i < times.size(); ++i)
            {
                CHECK(times[i].frame == i);
                CHECK(times[i].dt == doctest::Approx(step)); // sans fenêtre : un pas fixe par image
                CHECK(times[i].fixedStep == doctest::Approx(step));
                CHECK(times[i].steps == 1);
                CHECK(times[i].simulationStep == i + 1);
            }
            auto counter = a.world().componentType<Counter>("Counter");
            REQUIRE(counter.has_value());
            std::uint32_t steps = 0;
            a.world().view(*counter).each([&](scn::Entity, Counter &c) { steps = c.steps; });
            CHECK(steps == 3);

            // Pause : les rappels continuent, la simulation n'avance plus.
            a.pauseSimulation(true);
            auto more = a.frame();
            REQUIRE(more.has_value());
            CHECK(times.back().steps == 0);
            CHECK(a.simulation().stepIndex() == 3);

            // Un rendu qui échoue arrête la boucle avec une erreur.
            a.onRender([](app::Application &, app::RenderFrame &) -> orosmo::core::Result<> {
                return orosmo::core::makeError(ErrorCode::kInvalidArgument, "test render failure");
            });
            auto failed = a.frame();
            REQUIRE_FALSE(failed.has_value());
            CHECK(failed.error().message.find("test render failure") != std::string::npos);
            CHECK(a.device().validationErrorCount() == 0);
        }
    }

    TEST_CASE("Time (A75.9): the frame delta is the clock's, bounded by maxFrameDelta; the simulation keeps its fixed step")
    {
        for (const rhi::Backend backend : tests::backendsToRun())
        {
            CAPTURE(tests::backendName(backend));
            orosmo::core::ManualClock clock;
            auto settings = headlessSettings(backend);
            settings.clock = &clock;
            settings.maxFrameDelta = std::chrono::milliseconds(250);
            settings.frames = 0;
            auto application = createApp(settings);
            std::vector<app::FrameTime> times;
            application->onUpdate([&](app::Application &, const app::FrameTime &t) { times.push_back(t); });
            REQUIRE(application->frame().has_value()); // premier tick : 0
            clock.advance(std::chrono::milliseconds(40));
            REQUIRE(application->frame().has_value());
            clock.advance(std::chrono::seconds(1));
            REQUIRE(application->frame().has_value());
            REQUIRE(times.size() == 3);
            CHECK(times[0].dt == doctest::Approx(0.0));
            CHECK(times[0].steps == 0);
            CHECK(times[1].dt == doctest::Approx(0.040));
            CHECK(times[1].steps == 2); // 40 ms / 16,67 ms
            CHECK(times[2].dt == doctest::Approx(0.250)); // borné
            CHECK(times[2].steps == application->simulation().settings().maxStepsPerFrame);
            CHECK(times[2].alpha >= 0.0);
            CHECK(times[2].alpha < 1.0);
        }
    }

    TEST_CASE("Draw modes (A73 point 3): single, instanced and crowd are properties of the components; stale handles are skipped")
    {
        for (const rhi::Backend backend : tests::backendsToRun())
        {
            CAPTURE(tests::backendName(backend));
            auto settings = headlessSettings(backend);
            settings.frames = 0;
            auto application = createApp(settings);
            app::Application &a = *application;
            auto cube = a.addMesh(coloredCube());
            auto other = a.addMesh(coloredCube());
            REQUIRE(cube.has_value());
            REQUIRE(other.has_value());
            REQUIRE_FALSE(a.spawn(app::LightComponent{.direction = {-0.5f, -1.0f, -0.3f}}).isNull());
            std::vector<scn::Entity> cubes;
            for (int i = 0; i < 3; ++i)
                cubes.push_back(a.spawn(app::MeshRenderer{.mesh = *cube, .mode = app::DrawMode::kSingle}, {.position = {-1.5 + 1.5 * i, 0.0, 0.0}}));
            a.camera().setPosition({0.0f, 0.5f, 5.0f});
            a.camera().lookAt({0.0f, 0.0f, 0.0f});

            REQUIRE(a.frame().has_value());
            CHECK(a.sceneSystem().stats().meshDraws == 3);
            CHECK(a.sceneSystem().stats().instancedBatches == 0);
            auto single = a.readOutput();
            REQUIRE(single.has_value());

            for (const scn::Entity e : cubes)
                a.world().get(e, a.components().mesh)->mode = app::DrawMode::kInstanced;
            REQUIRE(a.frame().has_value());
            CHECK(a.sceneSystem().stats().meshDraws == 0);
            CHECK(a.sceneSystem().stats().instancedBatches == 1);
            CHECK(a.sceneSystem().stats().instances == 3);
            auto instanced = a.readOutput();
            REQUIRE(instanced.has_value());
            CHECK(differingPixels(*single, *instanced, 2) == 0); // même image, un seul dessin

            // Poignée périmée : ignorée et comptée ; entité invisible : ni dessinée ni comptée.
            REQUIRE(a.resources().meshes.remove(*other));
            REQUIRE_FALSE(a.spawn(app::MeshRenderer{.mesh = *other}).isNull());
            a.world().get(cubes[0], a.components().mesh)->visible = false;
            REQUIRE(a.frame().has_value());
            CHECK(a.sceneSystem().stats().skipped == 1);
            CHECK(a.sceneSystem().stats().instances == 2);

            // Modèle animé : seul (lecteur du modèle) et foule (clip et temps par entité).
            auto arm = a.addAnimatedModel(makeArm());
            INFO((arm ? std::string() : arm.error().message));
            REQUIRE(arm.has_value());
            REQUIRE(a.animatedModel(*arm)->play("bend").has_value());
            REQUIRE_FALSE(a.spawn(app::AnimatedModelComponent{.model = *arm}, {.position = {0.0, -1.0, 0.0}}).isNull());
            for (std::uint32_t i = 0; i < 2; ++i)
                REQUIRE_FALSE(a.spawn(app::AnimatedModelComponent{.model = *arm, .mode = app::DrawMode::kCrowd, .clip = i, .time = 0.25f},
                                      {.position = {-1.0 + 2.0 * i, -1.0, -1.0}})
                                  .isNull());
            REQUIRE_FALSE(a.spawn(app::AnimatedModelComponent{.model = *arm, .mode = app::DrawMode::kCrowd, .clip = 9}).isNull()); // clip absent
            const float before = a.animatedModel(*arm)->currentTime();
            REQUIRE(a.frame().has_value());
            CHECK(a.animatedModel(*arm)->currentTime() > before); // lecteur avancé une fois par image
            const auto &stats = a.sceneSystem().stats();
            CHECK(stats.animatedDraws == 1);
            if (orosmo::anim::CrowdRenderer::supportedBy(a.device().capabilities()))
            {
                CHECK(stats.crowds == 1);
                CHECK(stats.crowdAgents == 2);
                CHECK(stats.skipped == 2); // maillage retiré + clip absent
            }
            else
            {
                CHECK(stats.crowds == 0);
                CHECK(stats.skipped == 4);
            }
            CHECK(a.device().validationErrorCount() == 0);
        }
    }

    TEST_CASE("Shadows and HDR options: casters registered by the components, lights read by the shadow plugin")
    {
        for (const rhi::Backend backend : tests::backendsToRun())
        {
            CAPTURE(tests::backendName(backend));
            auto settings = headlessSettings(backend);
            settings.shadows = true;
            settings.hdr = true;
            settings.frames = 2;
            auto application = createApp(settings);
            app::Application &a = *application;
            REQUIRE(a.shadows() != nullptr);
            auto cube = a.addMesh(coloredCube());
            REQUIRE(cube.has_value());
            auto ground = a.addMesh(rd::MeshBuilder(rd::MeshLayout::kColored).addPlane({8.0f, 8.0f}, {0.6f, 0.6f, 0.6f, 1.0f}));
            REQUIRE(ground.has_value());
            REQUIRE_FALSE(a.spawn(app::MeshRenderer{.mesh = *cube, .castShadows = true}, {.position = {0.0, 0.8, 0.0}}).isNull());
            REQUIRE_FALSE(a.spawn(app::MeshRenderer{.mesh = *ground}).isNull());
            REQUIRE_FALSE(a.spawn(app::LightComponent{.intensity = 2.0f, .direction = {-0.4f, -1.0f, -0.2f}, .castShadows = true}).isNull());
            REQUIRE(a.run());
            CHECK(a.sceneSystem().stats().shadowCasters == 1);
            CHECK(a.lights().lights().size() == 1);
            CHECK((a.lights().lights()[0].shadowMode == rd::ShadowMode::kDynamic));

            // Maillage retiré : son caster est oublié à l'image suivante.
            REQUIRE(a.resources().meshes.remove(*cube));
            REQUIRE(a.frame().has_value());
            CHECK(a.sceneSystem().stats().shadowCasters == 0);
            CHECK(a.device().validationErrorCount() == 0);
        }
    }

    TEST_CASE("Destruction: callbacks (and the device resources they capture) are released before the game and the device")
    {
        for (const rhi::Backend backend : tests::backendsToRun())
        {
            CAPTURE(tests::backendName(backend));
            gAliveCaptures = 0;
            gAliveCapturesAtGameShutdown = -1;
            {
                auto settings = headlessSettings(backend);
                settings.game = app::GameSource::fromApi(&kLifetimeGame);
                settings.game->required = true;
                auto application = createApp(settings);
                app::Application &a = *application;
                a.onUpdate([mesh = capturedMesh(a)](app::Application &, const app::FrameTime &) {});
                a.onUi([mesh = capturedMesh(a)](app::Application &, const app::FrameTime &) {});
                a.onRender([mesh = capturedMesh(a)](app::Application &, app::RenderFrame &) -> orosmo::core::Result<> { return {}; });
                REQUIRE(a.run());
                CHECK(gAliveCaptures == 3);
            }
            CHECK(gAliveCaptures == 0);
            CHECK(gAliveCapturesAtGameShutdown == 0);
        }
    }

    TEST_CASE("beginFrame: only kBusy skips the frame; any other error is propagated and stops run")
    {
        for (const rhi::Backend backend : tests::backendsToRun())
        {
            CAPTURE(tests::backendName(backend));
            tests::CountingDevice *spy = nullptr;
            auto application = createSpied(backend, 1, spy);
            app::Application &a = *application;

            spy->beginFrameError = ErrorCode::kBusy;
            auto skipped = a.frame();
            REQUIRE(skipped.has_value());
            CHECK(*skipped);
            CHECK(a.frameIndex() == 0);

            spy->beginFrameError = ErrorCode::kDeviceLost;
            auto lost = a.frame();
            REQUIRE_FALSE(lost.has_value());
            CHECK((lost.error().code == ErrorCode::kDeviceLost));
            CHECK_FALSE(a.run());
        }
    }

    TEST_CASE("present: an error is propagated (also on the last frame); kBusy is not an error")
    {
        for (const rhi::Backend backend : tests::backendsToRun())
        {
            CAPTURE(tests::backendName(backend));
            tests::CountingDevice *spy = nullptr;
            auto application = createSpied(backend, 0, spy);
            app::Application &a = *application;

            spy->presentError = ErrorCode::kBusy;
            auto busy = a.frame();
            REQUIRE(busy.has_value());
            CHECK(*busy);
            CHECK(a.frameIndex() == 1);

            spy->presentError = ErrorCode::kDeviceLost;
            auto lost = a.frame();
            REQUIRE_FALSE(lost.has_value());
            CHECK((lost.error().code == ErrorCode::kDeviceLost));

            tests::CountingDevice *lastSpy = nullptr;
            auto last = createSpied(backend, 1, lastSpy);
            lastSpy->presentError = ErrorCode::kDeviceLost;
            CHECK_FALSE(last->run());
        }
    }

    TEST_CASE("Capture without a window: the implicit frame limit does not cut it; an explicit limit that does is a failure")
    {
        for (const rhi::Backend backend : tests::backendsToRun())
        {
            CAPTURE(tests::backendName(backend));
            tests::TempDir dir("orosmo_app_capture_limit");
            auto settings = headlessSettings(backend);
            settings.frames = 0;
            settings.captureDirectory = dir.path / "late";
            settings.captureFrames = app::ApplicationSettings::kDefaultHeadlessFrames + 2;
            {
                auto application = createApp(settings);
                REQUIRE(application->run());
                CHECK(application->frameIndex() == settings.captureFrames);
                CHECK(fs::exists(dir.path / "late" / "scene.png"));
                CHECK(fs::exists(dir.path / "late" / "ui.png"));
            }

            settings.captureDirectory = dir.path / "never";
            settings.frames = 2;
            auto application = createApp(settings);
            CHECK_FALSE(application->run());
            CHECK(application->frameIndex() == 2);
            CHECK_FALSE(fs::exists(dir.path / "never" / "scene.png"));
        }
    }

    TEST_CASE("Shadow casters: removed when the entity is destroyed, hidden, stops casting or leaves kSingle")
    {
        for (const rhi::Backend backend : tests::backendsToRun())
        {
            CAPTURE(tests::backendName(backend));
            auto settings = headlessSettings(backend);
            settings.shadows = true;
            settings.frames = 0;
            auto application = createApp(settings);
            app::Application &a = *application;
            auto cube = a.addMesh(coloredCube());
            REQUIRE(cube.has_value());
            REQUIRE_FALSE(a.spawn(app::LightComponent{.direction = {-0.4f, -1.0f, -0.2f}, .castShadows = true}).isNull());
            const scn::Entity e = a.spawn(app::MeshRenderer{.mesh = *cube, .castShadows = true});
            REQUIRE_FALSE(e.isNull());
            const auto casters = [&a]
            {
                REQUIRE(a.frame().has_value());
                return a.sceneSystem().stats().shadowCasters;
            };
            const auto component = [&a, e] { return a.world().get(e, a.components().mesh); };
            CHECK(casters() == 1);
            component()->castShadows = false;
            CHECK(casters() == 0);
            component()->castShadows = true;
            CHECK(casters() == 1);
            component()->visible = false;
            CHECK(casters() == 0);
            component()->visible = true;
            CHECK(casters() == 1);
            REQUIRE(a.world().destroy(e));
            CHECK(casters() == 0);

            auto arm = a.addAnimatedModel(makeArm());
            REQUIRE(arm.has_value());
            const scn::Entity m = a.spawn(app::AnimatedModelComponent{.model = *arm, .castShadows = true});
            REQUIRE_FALSE(m.isNull());
            const auto model = [&a, m] { return a.world().get(m, a.components().animatedModel); };
            CHECK(casters() == 1);
            model()->mode = app::DrawMode::kCrowd; // les foules ne projettent pas (G353)
            CHECK(casters() == 0);
            model()->mode = app::DrawMode::kSingle;
            CHECK(casters() == 1);
            REQUIRE(a.world().destroy(m));
            CHECK(casters() == 0);
            CHECK(a.device().validationErrorCount() == 0);
        }
    }

    TEST_CASE("Shadow casters: an instanced mesh that casts follows the instance transform")
    {
        for (const rhi::Backend backend : tests::backendsToRun())
        {
            CAPTURE(tests::backendName(backend));
            auto settings = headlessSettings(backend);
            settings.shadows = true;
            settings.frames = 0;
            auto application = createApp(settings);
            app::Application &a = *application;
            auto cube = a.addMesh(coloredCube());
            REQUIRE(cube.has_value());
            const scn::Entity e = a.spawn(app::MeshRenderer{.mesh = *cube, .mode = app::DrawMode::kInstanced, .castShadows = true},
                                          {.position = {1.0, 2.0, 3.0}});
            REQUIRE_FALSE(e.isNull());
            REQUIRE(a.frame().has_value());
            REQUIRE(a.sceneSystem().stats().instances == 1);
            CHECK(a.mesh(*cube)->modelMatrix()[3].x == doctest::Approx(1.0f));
            CHECK(a.mesh(*cube)->modelMatrix()[3].y == doctest::Approx(2.0f));
            REQUIRE(a.world().setTransform(e, {.position = {-2.0, 0.5, 0.0}}).has_value());
            REQUIRE(a.frame().has_value());
            CHECK(a.mesh(*cube)->modelMatrix()[3].x == doctest::Approx(-2.0f));
            CHECK(a.mesh(*cube)->modelMatrix()[3].y == doctest::Approx(0.5f));
            CHECK(a.device().validationErrorCount() == 0);
        }
    }

    TEST_CASE("Capture: after captureFrames frames, scene.png and ui.png are written and run stops")
    {
        for (const rhi::Backend backend : tests::backendsToRun())
        {
            CAPTURE(tests::backendName(backend));
            tests::TempDir dir("orosmo_app_capture");
            auto settings = headlessSettings(backend);
            settings.ui = true;
            settings.frames = 0;
            settings.captureDirectory = dir.path / "shots";
            settings.captureFrames = 2;
            auto application = createApp(settings);
            REQUIRE(application->run());
            CHECK(application->frameIndex() == 2);
            CHECK(fs::exists(dir.path / "shots" / "scene.png"));
            CHECK(fs::exists(dir.path / "shots" / "ui.png"));
        }
    }
}
