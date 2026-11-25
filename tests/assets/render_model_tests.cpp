// Revue M6 de RenderModel : indices de textures hors bornes, images incluses de modèles distincts dans un même cache, normales
// plates sans NORMAL, sortie de skinning par nœud, historique de mouvement par dessin, face avant et tangente sous une
// transformation miroir, reconstruction des groupes de matériaux.
#include <doctest/doctest.h>

#include "renderer/repo_paths.hpp"
#include "rhi/gpu_fixture.hpp"
#include "support/temp_dir.hpp"

#include <orosmo/assets/render_model.hpp>
#include <orosmo/renderer/image.hpp>
#include <orosmo/renderer/light.hpp>
#include <orosmo/renderer/pipeline_cache.hpp>
#include <orosmo/renderer/shadow_feature.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <cstring>
#include <fstream>
#include <iterator>

namespace fs = std::filesystem;
namespace as = orosmo::assets;
namespace rd = orosmo::renderer;
using orosmo::core::Vec2;
using orosmo::core::Vec3;
using orosmo::core::Vec4;

namespace render_model_test
{

inline fs::path shaderDir() { return tests::repoRoot(__FILE__) / "shaders" / "rhi"; }

struct ModelScene
{
    Gpu gpu;
    std::unique_ptr<rd::ShaderLibrary> library;
    std::unique_ptr<rd::PipelineCache> pipelines;
    std::unique_ptr<rd::SceneRenderer> renderer;
    std::unique_ptr<rd::PbrShaderCache> shaders;
    std::shared_ptr<rd::PbrDefaults> defaults;
    std::unique_ptr<as::TextureCache> textures;
    std::optional<rd::LightManager> lights;

    explicit ModelScene(Backend backend) : gpu(backend)
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
    ~ModelScene() { renderer.reset(); }

    std::array<std::uint8_t, 4> render(as::RenderModel &model)
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
                auto drawn = model.draw(*renderer, camera);
                INFO((drawn ? std::string() : drawn.error().message));
                REQUIRE(drawn.has_value());
                REQUIRE(renderer->endFrame({out, Format::kRGBA8Unorm, {8, 8}}).has_value());
            });
        return rgba(gpu.pixel(out, 4, 4));
    }

    as::RenderModel model(const as::Model &m, const std::string &key = "model", bool skinnable = false)
    {
        as::RenderModelOptions options;
        options.sourceKey = key;
        options.skinnable = skinnable;
        auto r = as::RenderModel::create(*gpu.device, m, *textures, *shaders, defaults, options);
        INFO((r ? std::string() : r.error().message));
        REQUIRE(r.has_value());
        return std::move(*r);
    }
};

// Carré plein écran à z = -1 face à la caméra (sens direct vu de la caméra).
inline as::Model quad(const as::MaterialData &material)
{
    as::Model m;
    as::MeshPrimitive p;
    p.positions = {{-3, -3, -1}, {3, -3, -1}, {3, 3, -1}, {-3, 3, -1}};
    p.normals.assign(4, Vec3(0, 0, 1));
    p.uv0 = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    p.indices = {0, 1, 2, 0, 2, 3};
    p.material = 0;
    m.meshes.push_back({"quad", {p}});
    m.materials.push_back(material);
    m.nodes.resize(1);
    m.nodes[0].mesh = 0;
    m.scenes.push_back({"s", {0}});
    m.defaultScene = 0;
    return m;
}

inline as::MaterialData unlit(Vec4 base)
{
    as::MaterialData mat;
    mat.name = "mat";
    mat.baseColorFactor = base;
    mat.unlit = true;
    return mat;
}

// PNG 1x1 encodé en mémoire (comme une image incluse d'un .glb).
inline std::vector<std::byte> pngBytes(const tests::TempDir &dir, const std::string &name, std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
    rd::Image image;
    image.width = 1;
    image.height = 1;
    image.rgba = {r, g, b, 255};
    const fs::path path = dir.path / name;
    REQUIRE(rd::savePng(image, path).has_value());
    std::ifstream in(path, std::ios::binary);
    const std::vector<char> chars{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    std::vector<std::byte> out(chars.size());
    std::memcpy(out.data(), chars.data(), chars.size());
    return out;
}

// Deux triangles non coplanaires qui partagent l'arête (1, 2) : un toit.
inline as::MeshPrimitive roof()
{
    as::MeshPrimitive p;
    p.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 1}};
    p.uv0 = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
    p.joints = {{0, 0, 0, 0}, {1, 0, 0, 0}, {2, 0, 0, 0}, {3, 0, 0, 0}};
    p.weights.assign(4, Vec4(1, 0, 0, 0));
    p.tangents.assign(4, Vec4(0, 1, 0, 1)); // ignorées sans NORMAL
    p.indices = {0, 1, 2, 2, 1, 3};
    return p;
}

} // namespace render_model_test

using namespace render_model_test;

TEST_SUITE("assets/render_model")
{
    TEST_CASE("pbrMaterialDesc: texture, image and sampler indices out of range are errors, never reads")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            as::TextureCache cache(*gpu.device);
            as::Model m;
            as::MaterialData mat;
            mat.baseColor.texture = 0; // aucune texture dans le modèle
            auto r = as::pbrMaterialDesc(m, mat, cache, "bad");
            REQUIRE(!r.has_value());
            CHECK((r.error().code == ErrorCode::kOutOfRange));

            m.textures.push_back({3, -1}); // image absente
            r = as::pbrMaterialDesc(m, mat, cache, "bad");
            REQUIRE(!r.has_value());
            CHECK((r.error().code == ErrorCode::kOutOfRange));

            m.textures[0] = {-1, 7}; // sans image : carte absente, l'échantillonneur n'est pas lu
            CHECK(as::pbrMaterialDesc(m, mat, cache, "bad").has_value());
            m.images.push_back({"img", "missing.png", {}, ""});
            m.textures[0] = {0, 7}; // échantillonneur absent
            r = as::pbrMaterialDesc(m, mat, cache, "bad");
            REQUIRE(!r.has_value());
            CHECK((r.error().code == ErrorCode::kOutOfRange));
            CHECK(cache.size() == 0);
        }
    }
}
