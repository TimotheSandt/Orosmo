// REN-13 : IBL et sondes. Références CPU (harmoniques sphériques, table BRDF, mélange des sondes) comparées à des valeurs
// analytiques ; préfiltrage en compute lu face par face sur chaque backend ; rendu d'un matériau PBR éclairé par l'environnement
// et par une sonde (valeurs attendues tirées de la table BRDF du CPU).
#include <doctest/doctest.h>

#include "renderer/counting_device.hpp"
#include "renderer/repo_paths.hpp"
#include "rhi/gpu_fixture.hpp"

#include <orosmo/assets/render_model.hpp>
#include <orosmo/renderer/ibl.hpp>
#include <orosmo/renderer/light.hpp>
#include <orosmo/renderer/pipeline_cache.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/packing.hpp>

#include <cmath>
#include <cstring>

namespace fs = std::filesystem;
namespace as = orosmo::assets;
namespace rd = orosmo::renderer;

namespace
{

fs::path shaderDir() { return tests::repoRoot(__FILE__) / "shaders" / "rhi"; }

bool close(const glm::vec3 &a, const glm::vec3 &b, float tolerance)
{
    return std::abs(a.x - b.x) <= tolerance && std::abs(a.y - b.y) <= tolerance && std::abs(a.z - b.z) <= tolerance;
}

// Hémisphère `bright` (direction de `axis` positive) à `value`, le reste à `other`.
rd::EquirectImage halfSphere(const glm::vec3 &axis, const glm::vec3 &value, const glm::vec3 &other, std::uint32_t w = 64, std::uint32_t h = 32)
{
    rd::EquirectImage image;
    image.width = w;
    image.height = h;
    image.texels.resize(std::size_t{w} * h);
    for (std::uint32_t y = 0; y < h; ++y)
    {
        for (std::uint32_t x = 0; x < w; ++x)
        {
            const glm::vec3 d = rd::equirectDirection({(x + 0.5f) / w, (y + 0.5f) / h});
            image.texels[std::size_t{y} * w + x] = glm::dot(d, axis) > 0.0f ? value : other;
        }
    }
    return image;
}

// Moyenne RGB d'une face (couche) d'un niveau du cube RGBA16F.
glm::vec3 faceMean(Device &device, TextureHandle cube, std::uint32_t face, std::uint32_t mip)
{
    // Taille explicite : une région vide lirait toutes les couches à partir de `face`.
    auto desc = device.textureDesc(cube);
    REQUIRE(desc.has_value());
    const std::uint32_t side = std::max(desc->size.width >> mip, 1u);
    auto data = device.readTexture(cube, {.mipLevel = mip, .layer = face, .size = {side, side, 1}});
    REQUIRE(data.has_value());
    const std::size_t texels = data->size() / 8;
    REQUIRE(texels > 0);
    glm::dvec3 sum(0.0);
    for (std::size_t i = 0; i < texels; ++i)
    {
        std::uint16_t h[4];
        std::memcpy(h, data->data() + i * 8, 8);
        sum += glm::dvec3(glm::unpackHalf1x16(h[0]), glm::unpackHalf1x16(h[1]), glm::unpackHalf1x16(h[2]));
    }
    return glm::vec3(sum / static_cast<double>(texels));
}

rd::IblSettings smallSettings()
{
    rd::IblSettings s;
    s.cubeSize = 16;
    s.mipLevels = 3;
    s.samples = 32;
    s.brdfLutSize = 32;
    return s;
}

struct IblScene
{
    Gpu gpu;
    std::unique_ptr<rd::ShaderLibrary> library;
    std::unique_ptr<rd::PipelineCache> pipelines;
    std::unique_ptr<rd::SceneRenderer> renderer;
    std::unique_ptr<rd::PbrShaderCache> shaders;
    std::shared_ptr<rd::PbrDefaults> defaults;
    std::unique_ptr<as::TextureCache> textures;
    std::optional<rd::LightManager> lights;
    rd::IblFeature *ibl = nullptr;

    explicit IblScene(Backend backend) : gpu(backend)
    {
        if (!gpu.device)
        {
            return;
        }
        library = std::make_unique<rd::ShaderLibrary>(*gpu.device);
        pipelines = std::make_unique<rd::PipelineCache>(*gpu.device);
        auto created = rd::SceneRenderer::create(*gpu.device, *library, *pipelines, {.shaderDirectory = shaderDir()});
        REQUIRE(created.has_value());
        renderer = std::move(*created);
        shaders = std::make_unique<rd::PbrShaderCache>(*library, shaderDir());
        auto d = rd::PbrDefaults::create(*gpu.device);
        REQUIRE(d.has_value());
        defaults = *d;
        textures = std::make_unique<as::TextureCache>(*gpu.device);
        auto l = rd::LightManager::create(*gpu.device);
        REQUIRE(l.has_value());
        lights.emplace(std::move(*l));
        renderer->setLights(&*lights);
        auto feature = rd::IblFeature::create(*gpu.device, *library, *pipelines, shaderDir(), smallSettings());
        INFO((feature ? std::string() : feature.error().message));
        REQUIRE(feature.has_value());
        auto added = renderer->addFeature(std::move(*feature));
        REQUIRE(added.has_value());
        ibl = static_cast<rd::IblFeature *>(*added);
    }
    ~IblScene() { renderer.reset(); }

    // Carré plein écran à z = -1 face à la caméra ; rend le pixel central de la sortie 8x8.
    std::array<std::uint8_t, 4> render(float metallic, float roughness, const std::string &key)
    {
        return render(metallic, roughness, key, glm::vec3(0.0f), {{4, 4}})[0];
    }

    // Même carré, caméra et modèle déplacés de `offset` (monde) ; rend les pixels demandés.
    std::vector<std::array<std::uint8_t, 4>> render(float metallic, float roughness, const std::string &key, const glm::vec3 &offset,
                                                    const std::vector<std::pair<int, int>> &pixels)
    {
        as::Model m;
        as::MeshPrimitive p;
        p.positions = {{-3, -3, -1}, {3, -3, -1}, {3, 3, -1}, {-3, 3, -1}};
        p.normals.assign(4, orosmo::core::Vec3(0, 0, 1));
        p.uv0 = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        p.indices = {0, 1, 2, 0, 2, 3};
        p.material = 0;
        m.meshes.push_back({"quad", {p}});
        as::MaterialData mat;
        mat.name = key;
        mat.baseColorFactor = {1, 1, 1, 1};
        mat.metallicFactor = metallic;
        mat.roughnessFactor = roughness;
        m.materials.push_back(mat);
        m.nodes.resize(1);
        m.nodes[0].mesh = 0;
        m.scenes.push_back({"s", {0}});
        m.defaultScene = 0;
        as::RenderModelOptions options;
        options.sourceKey = key;
        options.extraDefines = rd::iblDefines();
        auto model = as::RenderModel::create(*gpu.device, m, *textures, *shaders, defaults, options);
        INFO((model ? std::string() : model.error().message));
        REQUIRE(model.has_value());
        model->setRootTransform(glm::translate(glm::mat4(1.0f), offset));

        REQUIRE(lights->upload().has_value());
        rd::Camera camera;
        camera.setPosition(offset);
        camera.setAspect(8, 8);
        camera.setFarPlane(100.0f);
        const TextureHandle out = gpu.target(Format::kRGBA8Unorm, {8, 8}, "output");
        renderer->setOutputResolution(8, 8);
        gpu.run(
            [&](CommandEncoder &e)
            {
                REQUIRE(renderer->beginFrame(e, {.clearColor = {0, 0, 0, 1}}).has_value());
                REQUIRE(renderer->setCamera(camera).has_value());
                auto drawn = model->draw(*renderer, camera);
                INFO((drawn ? std::string() : drawn.error().message));
                REQUIRE(drawn.has_value());
                REQUIRE(renderer->endFrame({out, Format::kRGBA8Unorm, {8, 8}}).has_value());
            });
        std::vector<std::array<std::uint8_t, 4>> result;
        for (const auto &[x, y] : pixels)
        {
            result.push_back(rgba(gpu.pixel(out, x, y)));
        }
        return result;
    }
};

// (A + B) de la table BRDF du CPU au texel le plus proche de (n·v, rugosité).
float lutSum(const std::vector<glm::vec2> &lut, std::uint32_t size, float nDotV, float roughness)
{
    const auto index = [size](float t) { return std::min(static_cast<std::uint32_t>(t * static_cast<float>(size)), size - 1); };
    const glm::vec2 ab = lut[std::size_t{index(roughness)} * size + index(nDotV)];
    return ab.x + ab.y;
}

} // namespace

TEST_SUITE("renderer/ibl")
{
    TEST_CASE("equirectangular mapping: -Z at the centre, +X at three quarters, +Y on the top row; round trip")
    {
        CHECK(close(glm::vec3(rd::equirectUv({0, 0, -1}), 0), {0.5f, 0.5f, 0}, 1e-6f));
        CHECK(close(glm::vec3(rd::equirectUv({1, 0, 0}), 0), {0.75f, 0.5f, 0}, 1e-6f));
        CHECK(rd::equirectUv({0, 1, 0}).y == doctest::Approx(0.0f));
        for (const glm::vec3 d : {glm::vec3(0.3f, 0.5f, -0.8f), glm::vec3(-0.6f, -0.2f, 0.1f), glm::vec3(0.1f, 0.9f, 0.4f)})
        {
            const glm::vec3 n = glm::normalize(d);
            CHECK(close(rd::equirectDirection(rd::equirectUv(n)), n, 1e-5f));
        }
    }

    TEST_CASE("SH irradiance: a uniform environment gives its radiance; a lit hemisphere gives 1, 1/2 and 0; deterministic")
    {
        auto uniform = rd::projectEquirect(rd::uniformEquirect({0.5f, 0.25f, 2.0f}, 32, 16));
        REQUIRE(uniform.has_value());
        for (const glm::vec3 n : {glm::vec3(0, 1, 0), glm::vec3(1, 0, 0), glm::normalize(glm::vec3(-1, -1, 1))})
        {
            CHECK(close(uniform->evaluate(n), {0.5f, 0.25f, 2.0f}, 1e-4f));
        }
        CHECK(close(rd::ShIrradiance::uniform({0.5f, 0.25f, 2.0f}).evaluate({0, 0, 1}), {0.5f, 0.25f, 2.0f}, 1e-5f));

        // Ciel blanc au-dessus de l'horizon : E / π = 1 vers le haut, 1/2 à l'horizontale, 0 vers le bas (ordre 2 exact ici).
        const rd::EquirectImage sky = halfSphere({0, 1, 0}, glm::vec3(1.0f), glm::vec3(0.0f), 128, 64);
        auto sh = rd::projectEquirect(sky);
        REQUIRE(sh.has_value());
        CHECK(sh->evaluate({0, 1, 0}).x == doctest::Approx(1.0f).epsilon(0.02));
        CHECK(sh->evaluate({1, 0, 0}).x == doctest::Approx(0.5f).epsilon(0.02));
        CHECK(sh->evaluate({0, -1, 0}).x <= 0.02f);
        auto again = rd::projectEquirect(sky);
        CHECK(std::memcmp(again->coefficients.data(), sh->coefficients.data(), sizeof(sh->coefficients)) == 0);
        CHECK((rd::projectEquirect({}).error().code == ErrorCode::kInvalidArgument));
    }

    TEST_CASE("SH irradiance: a non-empty image too small for nine coefficients is rejected; the default uniform image is not")
    {
        auto tiny = rd::projectEquirect(rd::uniformEquirect({1, 1, 1}, 1, 1));
        REQUIRE(!tiny.has_value());
        CHECK((tiny.error().code == ErrorCode::kInvalidArgument));
        auto uniform = rd::projectEquirect(rd::uniformEquirect({1, 1, 1}));
        REQUIRE(uniform.has_value());
        CHECK(close(uniform->evaluate({0, 1, 0}), {1, 1, 1}, 1e-4f));
    }

    TEST_CASE("BRDF table: smooth at normal incidence reflects everything (A = 1, B = 0); rough loses energy; never above 1")
    {
        const std::uint32_t size = 32;
        const auto lut = rd::computeBrdfLut(size, 512);
        REQUIRE(lut.size() == size * size);
        const glm::vec2 smooth = lut[size - 1]; // rugosité ~0, n·v ~1
        CHECK(smooth.x == doctest::Approx(1.0f).epsilon(0.03));
        CHECK(smooth.y <= 0.01f);
        // Référence : intégration directe sur l'hémisphère (n·v = rugosité = 0,984, GGX + Smith corrélé) : A + B = 0,326.
        const float rough = lutSum(lut, size, 1.0f, 1.0f);
        CHECK(rough == doctest::Approx(0.326f).epsilon(0.03));
        for (const glm::vec2 ab : lut)
        {
            CHECK(ab.x + ab.y <= 1.02f);
            CHECK(ab.x >= 0.0f);
        }
        CHECK((rd::computeBrdfLut(size, 512) == lut)); // déterministe
    }

    TEST_CASE("light probes: outside every probe the environment; at a probe its own value; overlapping probes are averaged")
    {
        const rd::ShIrradiance env = rd::ShIrradiance::uniform({0, 0, 1});
        rd::ProbeSet probes;
        REQUIRE(probes.add({.position = {0, 0, 0}, .radius = 2.0f, .irradiance = rd::ShIrradiance::uniform({1, 0, 0})}).has_value());
        REQUIRE(probes.add({.position = {1, 0, 0}, .radius = 2.0f, .irradiance = rd::ShIrradiance::uniform({0, 1, 0})}).has_value());
        const glm::vec3 up(0, 1, 0);
        CHECK(close(probes.sample({10, 0, 0}, env).evaluate(up), {0, 0, 1}, 1e-5f));
        CHECK(close(probes.sample({0, 0, 0}, env).evaluate(up), {1.0f / 1.25f, 0.25f / 1.25f, 0}, 1e-5f)); // poids 1 et 0,25
        // Bord de la première sonde, loin de la seconde : poids 0,25, le reste vient de l'environnement.
        const glm::vec3 edge = probes.sample({-1, 0, 0}, env).evaluate(up);
        CHECK(close(edge, {0.25f, 0, 0.75f}, 1e-5f));
        CHECK(!probes.add({.radius = 0.0f}).has_value());
        for (std::uint32_t i = 2; i < rd::kMaxLightProbes; ++i)
        {
            REQUIRE(probes.add({.position = {100.0f + i, 0, 0}, .radius = 1.0f}).has_value());
        }
        CHECK((probes.add({.radius = 1.0f}).error().code == ErrorCode::kInvalidArgument));
    }

    TEST_CASE("IblFeature: probe positions are sent relative to the shader origin, computed in double")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            rd::ShaderLibrary library(*gpu.device);
            rd::PipelineCache pipelines(*gpu.device);
            auto created = rd::IblFeature::create(*gpu.device, library, pipelines, shaderDir(), smallSettings());
            REQUIRE(created.has_value());
            rd::IblFeature &ibl = **created;
            rd::ProbeSet probes;
            REQUIRE(probes.add({.position = {16777216.0f, 2.0f, -1.5f}, .radius = 0.25f}).has_value());
            ibl.setProbes(probes);
            CHECK(ibl.gpuBlock().probePositions[0] == glm::vec4(16777216.0f, 2.0f, -1.5f, 0.25f)); // avant la première image
            rd::FrameData frame;
            frame.cameraWorldHigh = {16777216.0f, 0.0f, 0.0f, 0.0f}; // rendu relatif : origine = caméra
            frame.cameraWorldLow = {0.5f, 0.0f, 0.0f, 0.0f};           // partie basse perdue par une somme en float
            REQUIRE(ibl.prepare(frame).has_value());
            CHECK(ibl.gpuBlock().probePositions[0] == glm::vec4(-0.5f, 2.0f, -1.5f, 0.25f));
            frame.cameraWorldLow = glm::vec4(0.0f);
            frame.cameraRelativeOrigin = frame.cameraWorldHigh; // sans rendu relatif : origine 0, positions monde
            REQUIRE(ibl.prepare(frame).has_value());
            CHECK(ibl.gpuBlock().probePositions[0] == glm::vec4(16777216.0f, 2.0f, -1.5f, 0.25f));
        }
    }

    TEST_CASE("IblFeature::create: a sampler that cannot be created leaves no sampler behind")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            for (int failing = 0; failing < 3; ++failing)
            {
                INFO("failing sampler ", failing);
                tests::CountingDevice counting(*gpu.device);
                counting.failSamplerAt = failing;
                rd::ShaderLibrary library(counting);
                rd::PipelineCache pipelines(counting);
                auto created = rd::IblFeature::create(counting, library, pipelines, shaderDir(), smallSettings());
                CHECK(!created.has_value());
                CHECK(counting.liveSamplers == 0);
            }
            tests::CountingDevice counting(*gpu.device);
            {
                rd::ShaderLibrary library(counting);
                rd::PipelineCache pipelines(counting);
                auto created = rd::IblFeature::create(counting, library, pipelines, shaderDir(), smallSettings());
                REQUIRE(created.has_value());
                CHECK(counting.liveSamplers == 3);
            }
            CHECK(counting.liveSamplers == 0);
        }
    }

    TEST_CASE("IblFeature: the prefiltered cube matches the environment face by face, at every roughness level")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            rd::ShaderLibrary library(*gpu.device);
            rd::PipelineCache pipelines(*gpu.device);
            auto created = rd::IblFeature::create(*gpu.device, library, pipelines, shaderDir(), smallSettings());
            INFO((created ? std::string() : created.error().message));
            REQUIRE(created.has_value());
            rd::IblFeature &ibl = **created;
            CHECK(ibl.cubeMipLevels() == 3);
            CHECK(faceMean(*gpu.device, ibl.environmentCube(), 0, 0).x == doctest::Approx(0.0f)); // noir par défaut

            REQUIRE(ibl.setEnvironment(rd::uniformEquirect({0.5f, 0.25f, 2.0f}, 32, 16)).has_value());
            for (std::uint32_t mip = 0; mip < 3; ++mip)
            {
                for (std::uint32_t face = 0; face < 6; ++face)
                {
                    INFO("mip ", mip, " face ", face);
                    CHECK(close(faceMean(*gpu.device, ibl.environmentCube(), face, mip), {0.5f, 0.25f, 2.0f}, 0.01f));
                }
            }
            CHECK(close(ibl.environmentIrradiance().evaluate({0, 1, 0}), {0.5f, 0.25f, 2.0f}, 1e-3f));

            // Orientation : ciel en haut (+Y), rouge du côté +X. Les faces +Y / -Y et +X / -X le retrouvent.
            REQUIRE(ibl.setEnvironment(halfSphere({0, 1, 0}, glm::vec3(1.0f), glm::vec3(0.0f))).has_value());
            CHECK(faceMean(*gpu.device, ibl.environmentCube(), 2, 0).x == doctest::Approx(1.0f).epsilon(0.01));
            CHECK(faceMean(*gpu.device, ibl.environmentCube(), 3, 0).x <= 0.01f);
            REQUIRE(ibl.setEnvironment(halfSphere({1, 0, 0}, {1, 0, 0}, {0, 0, 1})).has_value());
            const glm::vec3 px = faceMean(*gpu.device, ibl.environmentCube(), 0, 0);
            const glm::vec3 nx = faceMean(*gpu.device, ibl.environmentCube(), 1, 0);
            CHECK(px.x == doctest::Approx(1.0f).epsilon(0.01));
            CHECK(px.z <= 0.01f);
            CHECK(nx.z == doctest::Approx(1.0f).epsilon(0.01));
            // Le niveau le plus rugueux mélange les deux côtés : la face +X garde plus de rouge que de bleu, sans être pure.
            const glm::vec3 roughPx = faceMean(*gpu.device, ibl.environmentCube(), 0, 2);
            CHECK(roughPx.x > roughPx.z);
            CHECK(roughPx.z > 0.02f);
            CHECK(gpu.device->validationErrorCount() == 0);
        }
    }

    TEST_CASE("PBR with IBL: a uniform environment lights a rough dielectric to its radiance; metals follow the BRDF table; a probe "
              "replaces the environment")
    {
        FOR_EACH_BACKEND(backend)
        {
            IblScene scene(backend);
            REQUIRE(scene.gpu.device);
            const std::vector<glm::vec2> lut = rd::computeBrdfLut(smallSettings().brdfLutSize);
            REQUIRE(scene.ibl->setEnvironment(rd::uniformEquirect(glm::vec3(0.5f), 32, 16)).has_value());

            SUBCASE("rough white dielectric: diffuse (1 - spec) + spec = the radiance")
            {
                const auto px = scene.render(0.0f, 1.0f, "dielectric");
                INFO("pixel ", int(px[0]));
                CHECK(near(px[0], 128, 4));
            }
            SUBCASE("white metal: 0.5 × (A + B) of the BRDF table, smooth brighter than rough")
            {
                const auto smooth = scene.render(1.0f, 0.1f, "smooth");
                const auto rough = scene.render(1.0f, 1.0f, "rough");
                const int expectedSmooth = static_cast<int>(std::lround(0.5f * lutSum(lut, 32, 1.0f, 0.1f) * 255.0f));
                const int expectedRough = static_cast<int>(std::lround(0.5f * lutSum(lut, 32, 1.0f, 1.0f) * 255.0f));
                INFO("smooth ", int(smooth[0]), " expected ", expectedSmooth, "; rough ", int(rough[0]), " expected ", expectedRough);
                CHECK(near(smooth[0], expectedSmooth, 5));
                CHECK(near(rough[0], expectedRough, 5));
                CHECK(smooth[0] > rough[0]);
            }
            SUBCASE("intensity 0 turns the environment off")
            {
                scene.ibl->setIntensity(0.0f, 0.0f);
                CHECK(scene.render(0.0f, 1.0f, "off")[0] == 0);
            }
            SUBCASE("a red probe around the quad replaces the environment's diffuse")
            {
                REQUIRE(scene.ibl->setEnvironment(rd::uniformEquirect(glm::vec3(0.0f))).has_value());
                rd::ProbeSet probes;
                REQUIRE(probes.add({.position = {0, 0, -1}, .radius = 20.0f, .irradiance = rd::ShIrradiance::uniform({1, 0, 0})}).has_value());
                scene.ibl->setProbes(probes);
                const auto px = scene.render(0.0f, 1.0f, "probe");
                INFO("pixel ", int(px[0]), " ", int(px[1]));
                CHECK(px[0] > 200);
                CHECK(px[1] <= 2);
                CHECK(px[2] <= 2);
            }
            SUBCASE("far from the origin (camera-relative), a probe's radius is measured in camera-relative space")
            {
                // Caméra et sonde à X = 2^24 : une position monde en float y perd tout décalage < 1 m. Le centre de l'image est
                // dans le rayon (0,25 m), le bord droit (≈ 0,5 m) n'y est plus et ne reçoit que l'environnement (noir).
                scene.renderer->setCameraRelative(true);
                REQUIRE(scene.ibl->setEnvironment(rd::uniformEquirect(glm::vec3(0.0f))).has_value());
                const glm::vec3 far(16777216.0f, 0.0f, 0.0f);
                rd::ProbeSet probes;
                REQUIRE(probes.add({.position = far + glm::vec3(0, 0, -1), .radius = 0.25f, .irradiance = rd::ShIrradiance::uniform({1, 0, 0})})
                            .has_value());
                scene.ibl->setProbes(probes);
                const auto px = scene.render(0.0f, 1.0f, "far", far, {{4, 4}, {7, 4}});
                INFO("centre ", int(px[0][0]), " edge ", int(px[1][0]));
                CHECK(px[0][0] > 20);
                CHECK(px[1][0] == 0);
            }
            CHECK(scene.gpu.device->validationErrorCount() == 0);
        }
    }
}
