// REN-04 (forward+ clusterisé) et REN-05 (types de lumières) : références CPU de lights.glsl, disposition std430 du tampon des
// lumières lue par le GPU, pré-passe de profondeur, remplissage des clusters en compute, budgets, et la même image en forward et en
// forward+ (tolérance 8) sur chaque backend, couches de validation à 0.
#include <doctest/doctest.h>

#include "renderer/repo_paths.hpp"
#include "rhi/gpu_fixture.hpp"

#include <orosmo/renderer/camera.hpp>
#include <orosmo/renderer/forward_plus.hpp>
#include <orosmo/renderer/image.hpp>
#include <orosmo/renderer/light.hpp>
#include <orosmo/renderer/mesh.hpp>
#include <orosmo/renderer/scene_renderer.hpp>

#include <glm/glm.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace rd = orosmo::renderer;

namespace
{

fs::path repoRoot() { return tests::repoRoot(__FILE__); }
fs::path shaderDir() { return repoRoot() / "shaders" / "rhi"; }

constexpr std::uint32_t kWidth = 96;
constexpr std::uint32_t kHeight = 64;

// Sol carré (y = 0, normale +Y, gris clair) de demi-côté `half`.
rd::MeshData groundData(float half)
{
    rd::MeshData d;
    const float c = 0.8f;
    d.vertices = {-half, 0, -half, 0, 1, 0, c, c, c, //
                  half,  0, -half, 0, 1, 0, c, c, c, //
                  half,  0, half,  0, 1, 0, c, c, c, //
                  -half, 0, half,  0, 1, 0, c, c, c};
    d.indices = {0, 1, 2, 0, 2, 3};
    d.attributeSizes = {3, 3, 3};
    d.name = "ground";
    return d;
}

// Lumières ponctuelles déterministes (aucun hasard) sur une grille au-dessus du sol, petites portées.
std::vector<rd::Light> gridLights(std::uint32_t count, float half)
{
    std::vector<rd::Light> out;
    const auto side = static_cast<std::uint32_t>(std::ceil(std::sqrt(static_cast<float>(count))));
    for (std::uint32_t i = 0; i < count; ++i)
    {
        const float u = (static_cast<float>(i % side) + 0.5f) / static_cast<float>(side);
        const float v = (static_cast<float>(i / side) + 0.5f) / static_cast<float>(side);
        const glm::vec3 color(0.3f + 0.7f * static_cast<float>((i * 7u) % 5u) / 4.0f, 0.3f + 0.7f * static_cast<float>((i * 3u) % 7u) / 6.0f,
                              0.3f + 0.7f * static_cast<float>((i * 5u) % 3u) / 2.0f);
        out.push_back(rd::pointLight({(u * 2.0f - 1.0f) * half, 0.4f, (v * 2.0f - 1.0f) * half}, color, 0.6f, 200.0f, 4.0f)); // portée ≈ 1 m
    }
    return out;
}

struct Fixture
{
    Gpu gpu;
    std::unique_ptr<rd::ShaderLibrary> library;
    std::unique_ptr<rd::PipelineCache> pipelines;
    std::unique_ptr<rd::SceneRenderer> renderer;
    std::shared_ptr<rd::ShaderProgram> forward;
    std::shared_ptr<rd::ShaderProgram> forwardPlus;
    std::optional<rd::Mesh> ground;
    std::optional<rd::LightManager> lights;
    TextureHandle output;
    rd::Camera camera;

    explicit Fixture(Backend backend) : gpu(backend, {kWidth, kHeight})
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
        auto f = library->loadGraphics(shaderDir() / "mesh.vert", shaderDir() / "mesh.frag");
        INFO((f ? std::string() : f.error().message));
        REQUIRE(f.has_value());
        forward = *f;
        auto fp = library->loadGraphics(shaderDir() / "mesh.vert", shaderDir() / "mesh.frag", rd::forwardPlusDefines());
        INFO((fp ? std::string() : fp.error().message));
        REQUIRE(fp.has_value());
        forwardPlus = *fp;
        auto mesh = rd::Mesh::create(*gpu.device, groundData(10.0f));
        REQUIRE(mesh.has_value());
        ground.emplace(std::move(*mesh));
        auto l = rd::LightManager::create(*gpu.device);
        REQUIRE(l.has_value());
        lights.emplace(std::move(*l));
        output = gpu.target(Format::kRGBA8Unorm, {kWidth, kHeight}, "output");
        camera.setAspect(kWidth, kHeight);
        camera.setNearPlane(0.1f);
        camera.setPosition({0.0f, 7.0f, 9.0f});
        camera.setOrientation(glm::normalize(glm::vec3(0.0f, -7.0f, -9.0f)));
        renderer->setOutputResolution(static_cast<int>(kWidth), static_cast<int>(kHeight));
    }
    ~Fixture()
    {
        renderer.reset(); // référence la bibliothèque, le cache et les lumières
    }

    // Une image : le sol avec le programme donné ; renvoie la couleur de la scène.
    rd::Image render(const std::shared_ptr<rd::ShaderProgram> &program)
    {
        ground->setProgram(program);
        REQUIRE(lights->upload().has_value());
        renderer->setLights(&*lights);
        gpu.run(
            [&](CommandEncoder &e)
            {
                REQUIRE(renderer->beginFrame(e, {.clearColor = {0, 0, 0, 1}}).has_value());
                REQUIRE(renderer->setCamera(camera).has_value());
                auto drawn = renderer->drawMesh(*ground, camera);
                INFO((drawn ? std::string() : drawn.error().message));
                REQUIRE(drawn.has_value());
                auto ended = renderer->endFrame({output, Format::kRGBA8Unorm, {kWidth, kHeight}});
                INFO((ended ? std::string() : ended.error().message));
                REQUIRE(ended.has_value());
            });
        auto image = rd::readImage(*gpu.device, renderer->sceneTarget().color(0));
        REQUIRE(image.has_value());
        return *image;
    }

    rd::ForwardPlusLighting *enable(const rd::ForwardPlusSettings &settings = {})
    {
        auto fp = rd::enableForwardPlus(*renderer, *gpu.device, *library, *pipelines, shaderDir(), settings);
        INFO((fp ? std::string() : fp.error().message));
        REQUIRE(fp.has_value());
        return *fp;
    }
};

void requireSameImage(const rd::Image &a, const rd::Image &b, std::uint8_t tolerance = 8)
{
    auto diff = rd::compareImages(a, b, tolerance);
    REQUIRE(diff.has_value());
    INFO("max delta ", int(diff->maxDelta), ", mean ", diff->meanDelta, ", out of tolerance ", diff->outOfTolerance);
    CHECK(diff->outOfTolerance == 0);
}

std::uint8_t maxChannel(const rd::Image &image, int channel)
{
    std::uint8_t m = 0;
    for (std::size_t i = static_cast<std::size_t>(channel); i < image.rgba.size(); i += 4)
    {
        m = std::max(m, image.rgba[i]);
    }
    return m;
}

// Cluster (même formule que clusterIndexAt, clusters.glsl) d'un point monde, depuis les matrices de la dernière image.
std::uint32_t clusterOf(const rd::FrameData &frame, const rd::ForwardPlusSettings &s, const glm::vec3 &world)
{
    const glm::vec4 clip = frame.viewProj * glm::vec4(world, 1.0f);
    const glm::vec2 ndc = glm::vec2(clip) / clip.w;
    const auto cell = [](float n, std::uint32_t size)
    { return static_cast<std::uint32_t>(std::clamp(std::floor((n * 0.5f + 0.5f) * static_cast<float>(size)), 0.0f, static_cast<float>(size - 1))); };
    const float depth = -(frame.view * glm::vec4(world, 1.0f)).z;
    return cell(ndc.x, s.gridX) + s.gridX * (cell(ndc.y, s.gridY) + s.gridY * rd::clusterSlice(s, depth));
}

} // namespace

TEST_SUITE("renderer/forward-plus")
{
    TEST_CASE("lightRange / lightAttenuation (REN-05): the range is where the attenuated peak reaches 1/256; nothing beyond it")
    {
        const rd::Light point = rd::pointLight({0, 0, 0}, {1.0f, 0.5f, 0.2f}, 0.5f, 20.0f, 1.0f);
        const float range = rd::lightRange(point);
        // couleur max × force × 1,5 / (a d² + b d + 1) = 1/256 à la portée
        CHECK(1.0f * 0.5f * 1.5f / (20.0f * range * range + 1.0f * range + 1.0f) == doctest::Approx(rd::kLightCutoff).epsilon(1e-4));
        CHECK(rd::lightAttenuation(range, 20.0f, 1.0f, range) == 0.0f);
        CHECK(rd::lightAttenuation(range * 2.0f, 20.0f, 1.0f, range) == 0.0f);
        CHECK(rd::lightAttenuation(0.0f, 20.0f, 1.0f, range) == doctest::Approx(1.0f));
        CHECK(rd::lightAttenuation(0.5f * range, 20.0f, 1.0f, range) < rd::lightAttenuation(0.25f * range, 20.0f, 1.0f, range));
        CHECK(rd::lightRange(rd::pointLight({}, {1, 1, 1}, 1.0f, 0.0f, 0.0f)) == rd::kLightMaxRange); // sans atténuation
        CHECK(rd::lightRange(rd::pointLight({}, {1, 1, 1}, 1.0f, 0.0f, 2.0f)) == doctest::Approx((1.5f * 256.0f - 1.0f) / 2.0f));
        CHECK(rd::lightRange(rd::pointLight({}, {0.001f, 0.001f, 0.001f}, 0.5f, 1.0f, 1.0f)) == 0.0f); // déjà sous le seuil
        // Petite portée non nulle : -b + sqrt(b² + 4ak) s'annulerait en float ; la forme stable donne ≈ k / b.
        const float small = rd::lightRange(rd::pointLight({}, {1, 1, 1}, 1.0f, 1.0f, 1.0e6f));
        CHECK(small > 0.0f);
        CHECK(small == doctest::Approx((1.5f * 256.0f - 1.0f) / 1.0e6f).epsilon(1e-3));
    }

    TEST_CASE("ForwardPlusSettings: validation, exponential slices, cluster statistics, capability check (rule 11)")
    {
        rd::ForwardPlusSettings s;
        CHECK(rd::validateSettings(s).has_value());
        CHECK(s.clusterCount() == 16u * 9u * 24u);
        CHECK(rd::clusterSlice(s, 0.0f) == 0u);
        CHECK(rd::clusterSlice(s, s.nearDepth) == 0u);
        CHECK(rd::clusterSlice(s, s.farDepth * 10.0f) == s.gridZ - 1);
        std::uint32_t previous = 0;
        for (float z = 0.05f; z < 1000.0f; z *= 1.3f)
        {
            const std::uint32_t slice = rd::clusterSlice(s, z);
            CHECK(slice >= previous); // monotone
            previous = slice;
        }
        // Tranches exponentielles : la tranche k commence à near × (far / near)^(k / Z).
        const float start5 = s.nearDepth * std::pow(s.farDepth / s.nearDepth, 5.0f / static_cast<float>(s.gridZ));
        CHECK(rd::clusterSlice(s, start5 * 1.001f) == 5u);
        CHECK(rd::clusterSlice(s, start5 * 0.999f) == 4u);

        rd::ForwardPlusSettings bad = s;
        bad.gridX = 0;
        CHECK((rd::validateSettings(bad).error().code == ErrorCode::kInvalidArgument));
        bad = s;
        bad.farDepth = bad.nearDepth;
        CHECK_FALSE(rd::validateSettings(bad).has_value());
        bad = s;
        bad.maxLightsPerCluster = 1u << 20; // tampon de listes gigantesque
        CHECK_FALSE(rd::validateSettings(bad).has_value());
        bad = s;
        bad.gridX = 2147483648u; // 2^31 * 2^31 * 4 = 2^64 : le produit en 64 bits reviendrait à zéro
        bad.gridY = 2147483648u;
        bad.gridZ = 4;
        const auto huge = rd::validateSettings(bad);
        REQUIRE_FALSE(huge.has_value());
        CHECK((huge.error().code == ErrorCode::kInvalidArgument));

        const std::vector<std::uint32_t> counts{0, 1, 3, 5};
        const rd::ClusterStats stats = rd::summarizeClusterCounts(counts, 4);
        CHECK(stats.clusters == 4u);
        CHECK(stats.emptyClusters == 1u);
        CHECK(stats.overflowingClusters == 1u);
        CHECK(stats.maxLightsInCluster == 5u);
        CHECK(stats.lightReferences == 0u + 1u + 3u + 4u);

        orosmo::rhi::Capabilities caps;
        caps.compute = true;
        caps.maxStorageBufferBindings = 8;
        caps.maxComputeInvocations = 1024;
        caps.maxComputeWorkGroupSize = {1024, 1024, 64};
        CHECK(rd::ForwardPlusLighting::supportedBy(caps));
        caps.compute = false;
        CHECK_FALSE(rd::ForwardPlusLighting::supportedBy(caps));
        caps.compute = true;
        caps.maxStorageBufferBindings = 4; // la liaison 4 du groupe 0 (emplacement plat 4) n'existe pas
        CHECK_FALSE(rd::ForwardPlusLighting::supportedBy(caps));
    }

    TEST_CASE("lights.glsl and the std430 light block read on the GPU match the CPU references and GpuLight (REN-05)")
    {
        const char *cs = R"(#version 450
#include "../include/orosmo/lights.glsl"
#include "light_block.glsl"
layout(local_size_x = 1) in;
layout(set = 0, binding = 0, std430) buffer Out { float o[]; };
void main()
{
    o[0] = lightRange(vec3(1.0, 0.5, 0.2), 0.5, 20.0, 1.0);
    o[1] = lightAttenuation(0.3, 20.0, 1.0, o[0]);
    o[2] = lightAttenuation(o[0] * 1.01, 20.0, 1.0, o[0]);
    o[3] = spotCone(vec3(0.0, -1.0, 0.0), normalize(vec3(0.3, 1.0, 0.0)), 0.9, 0.98);
    o[4] = float(lights.size);
    o[5] = lights.AmbientLightColor.g;
    o[6] = lights.AmbientLightStrength;
    o[7] = float(lights.lights[1].type);
    o[8] = lights.lights[1].position.z;
    o[9] = lights.lights[1].direction.y;
    o[10] = lights.lights[1].color.b;
    o[11] = lights.lights[1].strength;
    o[12] = lights.lights[1].outerCone;
    o[13] = lights.lights[1].innerCone;
    o[14] = lights.lights[1].a;
    o[15] = lights.lights[1].b;
    o[16] = float(lights.lights[0].type);
    o[17] = lightRange(vec3(1.0), 1.0, 1.0, 1.0e6); // b >> sqrt(a k) : forme conjuguée stable
}
)";
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            CompileOptions options;
            options.name = "lights-values";
            options.includeResolver = fileIncludeResolver({shaderDir()}); // "../include/orosmo/..." relatif à shaders/rhi/
            auto shader = createShaderFromGlsl(*gpu.device, ShaderStage::kCompute, cs, options);
            INFO((shader ? std::string() : shader.error().message));
            REQUIRE(shader.has_value());
            auto pipe = gpu->createComputePipeline({.compute = *shader});
            REQUIRE(pipe.has_value());

            auto lights = rd::LightManager::create(*gpu.device, 2);
            REQUIRE(lights.has_value());
            lights->setAmbientLight({0.1f, 0.6f, 0.3f}, 0.25f);
            lights->addLight(rd::pointLight({1, 2, 3}, {1, 1, 1}, 1.0f, 0.5f, 0.25f));
            lights->addLight(rd::spotLight({4, 5, 6.5f}, {0, -0.75f, 0}, {0.2f, 0.4f, 0.9f}, 1.75f, 0.85f, 0.95f, 0.125f, 0.375f));
            REQUIRE(lights->upload().has_value());

            const BufferHandle out = gpu.buffer(BufferUsage::kStorage | BufferUsage::kCopySrc, {}, 32 * 4);
            BindGroupDesc g;
            g.entries = {{.binding = 0, .type = BindingType::kStorageBuffer, .buffer = out},
                         {.binding = 1, .type = BindingType::kStorageBuffer, .buffer = lights->buffer().handle()}};
            auto group = gpu->createBindGroup(g);
            REQUIRE(group.has_value());
            gpu.run(
                [&](CommandEncoder &e)
                {
                    e.bindComputePipeline(*pipe);
                    e.setBindGroup(0, *group);
                    e.dispatch(1, 1, 1);
                });
            auto data = gpu->readBuffer(out, 0, 32 * 4);
            REQUIRE(data.has_value());
            float v[32];
            std::memcpy(v, data->data(), sizeof v);
            const float range = rd::lightRange(rd::pointLight({}, {1.0f, 0.5f, 0.2f}, 0.5f, 20.0f, 1.0f));
            CHECK(v[0] == doctest::Approx(range).epsilon(1e-4));
            CHECK(v[1] == doctest::Approx(rd::lightAttenuation(0.3f, 20.0f, 1.0f, range)).epsilon(1e-4));
            CHECK(v[2] == 0.0f);
            const float angle = glm::dot(glm::vec3(0, -1, 0), -glm::normalize(glm::vec3(0.3f, 1.0f, 0.0f)));
            CHECK(v[3] == doctest::Approx(std::clamp((angle - 0.9f) / (0.98f - 0.9f), 0.0f, 1.0f)).epsilon(1e-4));
            // Disposition std430 : chaque champ lu par le GPU est celui écrit par GpuLight / GpuLightHeader.
            CHECK(v[4] == 2.0f);
            CHECK(v[5] == doctest::Approx(0.6f));
            CHECK(v[6] == doctest::Approx(0.25f));
            CHECK(v[7] == 4.0f);
            CHECK(v[8] == 6.5f);
            CHECK(v[9] == -0.75f);
            CHECK(v[10] == doctest::Approx(0.9f));
            CHECK(v[11] == 1.75f);
            CHECK(v[12] == doctest::Approx(0.85f));
            CHECK(v[13] == doctest::Approx(0.95f));
            CHECK(v[14] == 0.125f);
            CHECK(v[15] == 0.375f);
            CHECK(v[16] == 3.0f);
            CHECK(v[17] > 0.0f);
            CHECK(v[17] == doctest::Approx(rd::lightRange(rd::pointLight({}, {1, 1, 1}, 1.0f, 1.0f, 1.0e6f))).epsilon(1e-3));
            gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("point and spot lights are rendered in forward (REN-05); the depth pre-pass alone keeps the image")
    {
        FOR_EACH_BACKEND(backend)
        {
            Fixture f(backend);
            REQUIRE(f.gpu.device);
            f.lights->setAmbientLight({1, 1, 1}, 0.05f);
            const rd::Image dark = f.render(f.forward);
            f.lights->addLight(rd::pointLight({-3, 0.5f, 0}, {1, 0, 0}, 0.8f, 2.0f, 0.5f));
            f.lights->addLight(rd::spotLight({3, 3, 0}, {0, -1, 0}, {0, 1, 0}, 1.0f, 0.9f, 0.95f, 0.1f, 0.1f));
            const rd::Image lit = f.render(f.forward);
            CHECK(maxChannel(dark, 0) < 20);
            CHECK(maxChannel(lit, 0) > 60); // la ponctuelle rouge éclaire le sol
            CHECK(maxChannel(lit, 1) > 100); // le projecteur vert aussi
            CHECK(maxChannel(lit, 2) < 20);  // aucune lumière bleue

            f.renderer->setDepthPrepass(true);
            const rd::Image prepassed = f.render(f.forward);
            const auto order = f.renderer->renderGraph().executionOrder();
            CHECK(std::find(order.begin(), order.end(), "depth-prepass") < std::find(order.begin(), order.end(), "scene"));
            requireSameImage(lit, prepassed, 1);
            f.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("A75.10 descriptor light factories give the same image as the old ones with the equivalent values")
    {
        FOR_EACH_BACKEND(backend)
        {
            Fixture f(backend);
            REQUIRE(f.gpu.device);
            f.lights->setAmbientLight({1, 1, 1}, 0.05f);
            const rd::Light spot = rd::spotLight({.position = {3, 3, 0},
                                                  .direction = {0, -1, 0.2f},
                                                  .color = {0, 1, 0},
                                                  .intensity = 1.0f,
                                                  .range = 100.0f, // portée = seuil de 1/256 : à 3 m, encore ~75 % de la lumière
                                                  .outerAngleDegrees = 35.0f,
                                                  .innerAngleDegrees = 20.0f});
            const rd::Light point = rd::pointLight({.position = {-3, 0.5f, 0}, .color = {1, 0, 0}, .intensity = 0.8f, .range = 4.0f});
            // Anciennes fabriques : direction vers la lumière pour le soleil, coefficients, cônes en cosinus.
            f.lights->addLight(rd::directionalLight({0.3f, 1.0f, 0.2f}, {0.4f, 0.4f, 0.5f}, 0.4f));
            f.lights->addLight(rd::pointLight({-3, 0.5f, 0}, {1, 0, 0}, 0.8f, point.a, 0.0f));
            f.lights->addLight(rd::spotLight({3, 3, 0}, {0, -1, 0.2f}, {0, 1, 0}, 1.0f, std::cos(glm::radians(35.0f)), std::cos(glm::radians(20.0f)),
                                             spot.a, 0.0f));
            const rd::Image before = f.render(f.forward);
            CHECK(maxChannel(before, 0) > 60); // la ponctuelle rouge éclaire le sol
            CHECK(maxChannel(before, 1) > 60); // le projecteur vert aussi

            const rd::Light sun = rd::directionalLight({.direction = {-0.3f, -1.0f, -0.2f}, .color = {0.4f, 0.4f, 0.5f}, .intensity = 0.4f});
            REQUIRE(f.lights->setLight(0, sun).has_value());
            REQUIRE(f.lights->setLight(1, point).has_value());
            REQUIRE(f.lights->setLight(2, spot).has_value());
            const rd::Image after = f.render(f.forward);
            requireSameImage(before, after, 0);
            f.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("forward+ gives the same image as forward (tolerance 8): perspective, orthographic, camera-relative (REN-04)")
    {
        FOR_EACH_BACKEND(backend)
        {
            Fixture f(backend);
            REQUIRE(f.gpu.device);
            f.lights->setAmbientLight({0.7f, 0.8f, 1.0f}, 0.15f);
            f.lights->addLight(rd::directionalLight({0.3f, 1.0f, 0.2f}, {1.0f, 0.95f, 0.9f}, 0.4f));
            f.lights->addLight(rd::pointLight({-3, 0.5f, 0}, {1, 0.2f, 0.1f}, 0.8f, 2.0f, 0.5f));
            f.lights->addLight(rd::pointLight({2, 0.5f, 2}, {0.1f, 0.3f, 1.0f}, 0.8f, 4.0f, 0.5f));
            f.lights->addLight(rd::spotLight({3, 3, -2}, {0, -1, 0.2f}, {0.2f, 1.0f, 0.3f}, 1.0f, 0.9f, 0.95f, 0.1f, 0.1f));

            struct Variant
            {
                const char *name;
                bool ortho;
                bool relative;
            };
            for (const Variant variant : {Variant{"perspective", false, false}, Variant{"orthographic", true, false}, Variant{"camera-relative", false, true}})
            {
                INFO(variant.name);
                if (variant.ortho)
                {
                    f.camera.setOrthographic(14.0f);
                }
                else
                {
                    f.camera.setPerspective();
                }
                f.renderer->setCameraRelative(variant.relative);
                rd::disableForwardPlus(*f.renderer);
                const rd::Image forward = f.render(f.forward);
                rd::ForwardPlusLighting *fp = f.enable();
                const rd::Image clustered = f.render(f.forwardPlus);
                requireSameImage(forward, clustered);
                CHECK(maxChannel(clustered, 0) > 60);
                // Ordre de l'image (docs/03) : pré-passe de profondeur -> clusters -> scène.
                const auto order = f.renderer->renderGraph().executionOrder();
                const auto at = [&](const char *pass) { return std::find(order.begin(), order.end(), pass) - order.begin(); };
                CHECK(at("depth-prepass") < at("forwardplus.clusters"));
                CHECK(at("forwardplus.clusters") < at("scene"));
                CHECK(static_cast<std::size_t>(at("scene")) < order.size());
                auto stats = fp->readClusterStats();
                REQUIRE(stats.has_value());
                CHECK(stats->clusters == fp->settings().clusterCount());
                CHECK(stats->overflowingClusters == 0u);
            }
            f.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("clusters filled in compute list exactly the lights that reach them; the per-cluster and total budgets are applied (REN-04)")
    {
        FOR_EACH_BACKEND(backend)
        {
            Fixture f(backend);
            REQUIRE(f.gpu.device);
            f.lights->setAmbientLight({1, 1, 1}, 0.1f);
            f.lights->addLight(rd::directionalLight({0, 1, 0}, {1, 1, 1}, 0.2f)); // 0 : tous les clusters
            const glm::vec3 redAt(-4, 0.5f, 1);
            const glm::vec3 blueAt(4, 0.5f, -1);
            f.lights->addLight(rd::pointLight(redAt, {1, 0, 0}, 0.5f, 20.0f, 1.0f));  // 1
            f.lights->addLight(rd::pointLight(blueAt, {0, 0, 1}, 0.5f, 20.0f, 1.0f)); // 2
            rd::ForwardPlusLighting *fp = f.enable();
            (void)f.render(f.forwardPlus);
            const rd::ForwardPlusSettings s = fp->settings(); // copie : setSettings la remplace plus bas
            const rd::FrameData &frame = f.renderer->frameData();

            const auto listOf = [&](const glm::vec3 &world)
            {
                auto list = fp->readClusterLights(clusterOf(frame, s, world));
                REQUIRE(list.has_value());
                return *list;
            };
            CHECK(listOf(redAt) == std::vector<std::uint32_t>{0, 1});
            CHECK(listOf(blueAt) == std::vector<std::uint32_t>{0, 2});
            CHECK(listOf({0, 0, 4}) == std::vector<std::uint32_t>{0}); // à 5 et 6,4 m des deux (portée ≈ 3 m)
            auto counts = fp->readClusterLightCounts();
            REQUIRE(counts.has_value());
            REQUIRE(counts->size() == s.clusterCount());
            CHECK(std::ranges::all_of(*counts, [](std::uint32_t c) { return c >= 1 && c <= 3; }));
            CHECK((fp->readClusterLights(s.clusterCount()).error().code == ErrorCode::kOutOfRange));

            // Budget par cluster : une seule lumière listée ; les clusters des ponctuelles débordent.
            rd::ForwardPlusSettings tight = s;
            tight.maxLightsPerCluster = 1;
            REQUIRE(fp->setSettings(tight).has_value());
            (void)f.render(f.forwardPlus);
            auto stats = fp->readClusterStats();
            REQUIRE(stats.has_value());
            CHECK(stats->overflowingClusters > 0u);
            CHECK(stats->maxLightsInCluster == 2u);
            CHECK(listOf(redAt) == std::vector<std::uint32_t>{0});

            // Budget total : seules les `maxLights` premières lumières sont examinées.
            rd::ForwardPlusSettings few = s;
            few.maxLights = 2;
            REQUIRE(fp->setSettings(few).has_value());
            (void)f.render(f.forwardPlus);
            CHECK(listOf(redAt) == std::vector<std::uint32_t>{0, 1});
            CHECK(listOf(blueAt) == std::vector<std::uint32_t>{0});
            CHECK_FALSE(fp->setSettings(rd::ForwardPlusSettings{.gridX = 0}).has_value());

            // Les lectures décrivent les tampons existants, pas les réglages en attente (tampons recréés à la prochaine image).
            rd::ForwardPlusSettings finer = s;
            finer.gridX = s.gridX * 2;
            REQUIRE(fp->setSettings(finer).has_value());
            auto before = fp->readClusterLightCounts();
            REQUIRE(before.has_value());
            CHECK(before->size() == s.clusterCount());
            CHECK(fp->readClusterLights(s.clusterCount() - 1).has_value());
            (void)f.render(f.forwardPlus);
            auto after = fp->readClusterLightCounts();
            REQUIRE(after.has_value());
            CHECK(after->size() == finer.clusterCount());
            f.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("1000 point lights: forward+ matches forward, each cluster lists a small fraction of them (REN-04)")
    {
        FOR_EACH_BACKEND(backend)
        {
            Fixture f(backend);
            REQUIRE(f.gpu.device);
            f.lights->setAmbientLight({1, 1, 1}, 0.05f);
            f.lights->addLights(gridLights(1000, 9.5f));
            const rd::Image forward = f.render(f.forward);
            rd::ForwardPlusLighting *fp = f.enable();
            const rd::Image clustered = f.render(f.forwardPlus);
            requireSameImage(forward, clustered);
            auto stats = fp->readClusterStats();
            REQUIRE(stats.has_value());
            MESSAGE("1000 lights: max ", stats->maxLightsInCluster, " per cluster, mean ", stats->averageLightsPerCluster, ", empty ",
                    stats->emptyClusters, " / ", stats->clusters);
            CHECK(stats->overflowingClusters == 0u);
            CHECK(stats->maxLightsInCluster <= fp->settings().maxLightsPerCluster);
            CHECK(stats->averageLightsPerCluster < 50.0); // sur 1000
            f.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("cluster debug variant compiles and paints the light count per cluster")
    {
        FOR_EACH_BACKEND(backend)
        {
            Fixture f(backend);
            REQUIRE(f.gpu.device);
            auto debug = f.library->loadGraphics(shaderDir() / "mesh.vert", shaderDir() / "mesh.frag", rd::forwardPlusDefines(true));
            INFO((debug ? std::string() : debug.error().message));
            REQUIRE(debug.has_value());
            f.lights->addLight(rd::directionalLight({0, 1, 0}, {1, 1, 1}, 0.2f));
            f.enable();
            const rd::Image heat = f.render(*debug);
            // Un cluster avec une lumière sur 128 : bleu (0, 0.3 -> 1, ...) ; le fond (aucun fragment) reste noir.
            CHECK(maxChannel(heat, 2) > 200);
            f.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("forward+ refuses a frame whose draws use another LightManager than the one its clusters were built for (REN-04)")
    {
        FOR_EACH_BACKEND(backend)
        {
            Fixture f(backend);
            REQUIRE(f.gpu.device);
            f.lights->addLight(rd::directionalLight({0, 1, 0}, {1, 1, 1}, 0.2f));
            REQUIRE(f.lights->upload().has_value());
            auto other = rd::LightManager::create(*f.gpu.device);
            REQUIRE(other.has_value());
            other->addLight(rd::pointLight({0, 0.5f, 0}, {1, 0, 0}, 1.0f, 2.0f, 0.5f));
            REQUIRE(other->upload().has_value());
            f.enable();
            f.ground->setProgram(f.forwardPlus);
            f.gpu.run(
                [&](CommandEncoder &e)
                {
                    REQUIRE(f.renderer->beginFrame(e, {.clearColor = {0, 0, 0, 1}}).has_value());
                    REQUIRE(f.renderer->setCamera(f.camera).has_value());
                    f.renderer->setLights(&*other);
                    REQUIRE(f.renderer->drawMesh(*f.ground, f.camera).has_value());
                    f.renderer->setLights(&*f.lights); // les clusters seront construits pour CES lumières
                    const auto ended = f.renderer->endFrame({f.output, Format::kRGBA8Unorm, {kWidth, kHeight}});
                    REQUIRE_FALSE(ended.has_value());
                    CHECK((ended.error().code == ErrorCode::kUnsupported));
                });
            // Un seul LightManager pour l'image : accepté.
            f.render(f.forwardPlus);
            f.gpu.expectNoValidationErrors();
        }
    }
}
