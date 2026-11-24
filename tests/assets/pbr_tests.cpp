// REN-10 / REN-11 : matériaux PBR (variantes, instances) et rendu d'un modèle glTF par RenderModel, sur chaque backend. Le modèle
// est un carré plein écran généré par le code ; les valeurs attendues viennent de la BRDF (pbr.glsl) calculée à la main.
#include <doctest/doctest.h>

#include "renderer/repo_paths.hpp"
#include "rhi/gpu_fixture.hpp"
#include "support/temp_dir.hpp"
#include "support/test_support.hpp"

#include <orosmo/assets/render_model.hpp>
#include <orosmo/renderer/image.hpp>
#include <orosmo/renderer/light.hpp>
#include <orosmo/renderer/pipeline_cache.hpp>

#include <algorithm>

namespace fs = std::filesystem;
namespace as = orosmo::assets;
namespace rd = orosmo::renderer;
using orosmo::core::Vec2;
using orosmo::core::Vec3;
using orosmo::core::Vec4;

namespace
{

fs::path shaderDir() { return tests::repoRoot(__FILE__) / "shaders" / "rhi"; }

struct PbrScene
{
    Gpu gpu;
    std::unique_ptr<rd::ShaderLibrary> library;
    std::unique_ptr<rd::PipelineCache> pipelines;
    std::unique_ptr<rd::SceneRenderer> renderer;
    std::unique_ptr<rd::PbrShaderCache> shaders;
    std::shared_ptr<rd::PbrDefaults> defaults;
    std::unique_ptr<as::TextureCache> textures;
    std::optional<rd::LightManager> lights;

    explicit PbrScene(Backend backend) : gpu(backend)
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
    }
    ~PbrScene() { renderer.reset(); }

    // Dessine les modèles et rend le pixel central de la sortie 8x8.
    std::array<std::uint8_t, 4> render(std::initializer_list<as::RenderModel *> models)
    {
        REQUIRE(lights->upload().has_value());
        rd::Camera camera;
        camera.setAspect(8, 8);
        camera.setFarPlane(100.0f);
        const TextureHandle out = gpu.target(Format::kRGBA8Unorm, {8, 8}, "output");
        renderer->setOutputResolution(8, 8);
        gpu.run(
            [&](CommandEncoder &e)
            {
                REQUIRE(renderer->beginFrame(e, {.clearColor = {0, 0, 0, 1}}).has_value());
                REQUIRE(renderer->setCamera(camera).has_value());
                for (as::RenderModel *m : models)
                {
                    auto drawn = m->draw(*renderer, camera);
                    INFO((drawn ? std::string() : drawn.error().message));
                    REQUIRE(drawn.has_value());
                }
                REQUIRE(renderer->endFrame({out, Format::kRGBA8Unorm, {8, 8}}).has_value());
            });
        return rgba(gpu.pixel(out, 4, 4));
    }

    as::RenderModel model(const as::Model &m, const std::string &key = "quad")
    {
        as::RenderModelOptions options;
        options.sourceKey = key;
        auto r = as::RenderModel::create(*gpu.device, m, *textures, *shaders, defaults, options);
        INFO((r ? std::string() : r.error().message));
        REQUIRE(r.has_value());
        return std::move(*r);
    }
};

// Carré plein écran à z = -1 face à la caméra (sens direct vu de la caméra), ou de dos si `flipped`.
as::Model quad(const as::MaterialData &material, bool flipped = false)
{
    as::Model m;
    as::MeshPrimitive p;
    p.positions = {{-3, -3, -1}, {3, -3, -1}, {3, 3, -1}, {-3, 3, -1}};
    p.normals.assign(4, Vec3(0, 0, 1));
    p.uv0 = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    p.indices = flipped ? std::vector<std::uint32_t>{0, 2, 1, 0, 3, 2} : std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3};
    p.material = 0;
    m.meshes.push_back({"quad", {p}});
    m.materials.push_back(material);
    m.nodes.resize(1);
    m.nodes[0].mesh = 0;
    m.scenes.push_back({"s", {0}});
    m.defaultScene = 0;
    return m;
}

as::MaterialData material(Vec4 base, float metallic, float roughness)
{
    as::MaterialData mat;
    mat.name = "mat";
    mat.baseColorFactor = base;
    mat.metallicFactor = metallic;
    mat.roughnessFactor = roughness;
    return mat;
}

} // namespace

TEST_SUITE("assets/pbr")
{
    TEST_CASE("PBR variants: features follow the maps and modes; defines are stable; glTF alpha modes map to blend modes")
    {
        rd::PbrMaterialDesc desc;
        CHECK(desc.features() == 0u);
        desc.blend = rd::BlendMode::kMask;
        desc.doubleSided = true;
        CHECK(desc.features() == (rd::kPbrAlphaMask | rd::kPbrDoubleSided));
        desc.blend = rd::BlendMode::kBlend;
        desc.unlit = true;
        CHECK(desc.features() == (rd::kPbrAlphaBlend | rd::kPbrDoubleSided | rd::kPbrUnlit));
        const auto defines = rd::pbrDefines(rd::kPbrNormalMap | rd::kPbrShadows | rd::kPbrBaseColorMap);
        REQUIRE(defines.size() == 3);
        CHECK(defines[0].first == "OROSMO_PBR_BASE_COLOR_MAP");
        CHECK(defines[1].first == "OROSMO_PBR_NORMAL_MAP");
        CHECK(defines[2].first == "OROSMO_SHADOWS");
        CHECK((as::blendModeForAlpha(as::AlphaMode::kOpaque) == rd::BlendMode::kOpaque));
        CHECK((as::blendModeForAlpha(as::AlphaMode::kMask) == rd::BlendMode::kMask));
        CHECK((as::blendModeForAlpha(as::AlphaMode::kBlend) == rd::BlendMode::kBlend));
    }

    TEST_CASE("pbrVertices: 16 floats per vertex; missing normals, tangents and colours are generated")
    {
        as::MeshPrimitive p;
        p.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
        p.uv0 = {{0, 0}, {1, 0}, {0, 1}};
        p.indices = {0, 1, 2};
        const auto v = as::pbrVertices(p);
        REQUIRE(v.size() == 3 * rd::kPbrVertexFloats);
        CHECK(v[3 + 2] == doctest::Approx(1.0f));  // normale +Z (sens direct)
        CHECK(v[8] == doctest::Approx(1.0f));      // tangente = +X (u croît vers +X)
        CHECK(v[11] == doctest::Approx(1.0f));     // signe : bitangente +Y = cross(n, t)
        CHECK(v[12] == 1.0f);                      // couleur blanche par défaut
        CHECK(v[15] == 1.0f);
        p.uv0 = {{0, 0}, {1, 0}, {0, -1}}; // v inversé : bitangente opposée
        CHECK(as::pbrVertices(p)[11] == doctest::Approx(-1.0f));
    }

    TEST_CASE("RenderModel: unlit, lit dielectric, ambient-only metal, sRGB base colour map, culling, mask, instance")
    {
        FOR_EACH_BACKEND(backend)
        {
            PbrScene scene(backend);
            REQUIRE(scene.gpu.device);

            SUBCASE("unlit: the base colour is written as is")
            {
                auto mat = material({0.5f, 0.25f, 1.0f, 1.0f}, 0.0f, 1.0f);
                mat.unlit = true;
                auto model = scene.model(quad(mat));
                const auto px = scene.render({&model});
                CHECK(near(px[0], 128, 2));
                CHECK(near(px[1], 64, 2));
                CHECK(near(px[2], 255, 2));
            }
            SUBCASE("lit: a rough white dielectric facing a unit directional light gives (1 - F0) + specular = 0.97")
            {
                scene.lights->addLight(rd::directionalLight({0, 0, 1}, {1, 1, 1}, 1.0f));
                auto model = scene.model(quad(material({1, 1, 1, 1}, 0.0f, 1.0f)));
                const auto px = scene.render({&model});
                // Incidence normale, rugosité 1 : diffus (1 - 0,04) + spéculaire 0,04 × D(1/π) × V(1/4) × π = 0,96 + 0,01.
                INFO("pixel ", int(px[0]));
                CHECK(near(px[0], 247, 3));
                CHECK(near(px[1], 247, 3));
            }
            SUBCASE("ambient light: a dielectric reflects it, a pure metal does not")
            {
                scene.lights->setAmbientLight({1, 1, 1}, 0.5f);
                auto dielectric = scene.model(quad(material({1, 0.5f, 0, 1}, 0.0f, 1.0f)));
                auto px = scene.render({&dielectric});
                CHECK(near(px[0], 128, 2));
                CHECK(near(px[1], 64, 2));
                auto metal = scene.model(quad(material({1, 1, 1, 1}, 1.0f, 0.5f)), "metal");
                px = scene.render({&metal});
                CHECK(px[0] <= 1);
            }
            SUBCASE("base colour map: an sRGB texel 188 reads 0.5 in linear light")
            {
                tests::TempDir dir("engine_pbr");
                rd::Image tex;
                tex.width = 1;
                tex.height = 1;
                tex.rgba = {188, 188, 188, 255};
                REQUIRE(rd::savePng(tex, dir.path / "albedo.png").has_value());
                auto mat = material({1, 1, 1, 1}, 0.0f, 1.0f);
                mat.unlit = true;
                mat.baseColor.texture = 0;
                as::Model m = quad(mat);
                m.images.push_back({"albedo", dir.path / "albedo.png", {}, ""});
                m.textures.push_back({0, -1});
                auto model = scene.model(m);
                REQUIRE(scene.textures->size() == 1);
                CHECK((scene.textures->load(dir.path / "albedo.png")->get()->format() == Format::kRGBA8Srgb));
                const auto px = scene.render({&model});
                CHECK(near(px[0], 128, 3));
            }
            SUBCASE("culling: a single-sided quad seen from the back is not drawn; a double-sided one is")
            {
                auto mat = material({0, 1, 0, 1}, 0.0f, 1.0f);
                mat.unlit = true;
                auto back = scene.model(quad(mat, true), "back");
                CHECK(scene.render({&back})[1] == 0);
                mat.doubleSided = true;
                auto both = scene.model(quad(mat, true), "both");
                CHECK(near(scene.render({&both})[1], 255, 1));
            }
            SUBCASE("alpha mask: a texel under the cutoff is discarded")
            {
                auto mat = material({1, 0, 0, 0.2f}, 0.0f, 1.0f);
                mat.unlit = true;
                mat.alphaMode = as::AlphaMode::kMask;
                mat.alphaCutoff = 0.5f;
                auto masked = scene.model(quad(mat));
                CHECK(scene.render({&masked})[0] == 0);
            }
            SUBCASE("REN-11 instance: same textures and variant, its own parameters; one program for both")
            {
                auto mat = material({1, 0, 0, 1}, 0.0f, 1.0f);
                mat.unlit = true;
                auto model = scene.model(quad(mat));
                const std::size_t programs = scene.shaders->size();
                auto base = model.materials()[0];
                rd::PbrParams params = base->params();
                params.baseColorFactor = {0, 0, 1, 1};
                auto instance = base->instantiate(params, "blue");
                REQUIRE(instance.has_value());
                CHECK((*instance)->features() == base->features());
                model.mesh(0).setMaterialGroup((*instance)->group());
                auto px = scene.render({&model});
                CHECK(px[0] == 0);
                CHECK(near(px[2], 255, 1));
                // Paramètres changés en place : la couleur suit, sans nouveau programme.
                params.baseColorFactor = {0, 1, 0, 1};
                REQUIRE((*instance)->setParams(params).has_value());
                px = scene.render({&model});
                CHECK(near(px[1], 255, 1));
                CHECK(scene.shaders->size() == programs);
            }
            scene.gpu.expectNoValidationErrors();
        }
    }
}
