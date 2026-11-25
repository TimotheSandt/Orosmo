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

    TEST_CASE("flatShaded: without NORMAL every triangle gets its own vertices and face normal; other attributes follow")
    {
        const as::MeshPrimitive p = roof();
        const auto flat = as::flatShaded(p);
        REQUIRE(flat.has_value());
        REQUIRE(flat->positions.size() == 6);
        REQUIRE(flat->indices == std::vector<std::uint32_t>{0, 1, 2, 3, 4, 5});
        REQUIRE(flat->normals.size() == 6);
        CHECK(flat->tangents.empty()); // glTF : tangentes ignorées quand les normales manquent
        const Vec3 first = glm::normalize(glm::cross(p.positions[1] - p.positions[0], p.positions[2] - p.positions[0]));
        const Vec3 second = glm::normalize(glm::cross(p.positions[1] - p.positions[2], p.positions[3] - p.positions[2]));
        for (std::size_t v = 0; v < 3; ++v)
        {
            CHECK(glm::length(flat->normals[v] - first) < 1e-6f);
            CHECK(glm::length(flat->normals[3 + v] - second) < 1e-6f);
        }
        // L'arête partagée (sommets 1 et 2 du modèle) a deux normales différentes, une par face.
        CHECK(glm::length(flat->normals[1] - flat->normals[4]) > 0.1f);
        for (std::size_t v = 0; v < 6; ++v)
        {
            const std::uint32_t src = p.indices[v];
            CHECK(flat->positions[v] == p.positions[src]);
            CHECK(flat->uv0[v] == p.uv0[src]);
            CHECK(flat->joints[v] == p.joints[src]);
            CHECK(flat->weights[v] == p.weights[src]);
        }
        as::MeshPrimitive withNormals = p;
        withNormals.normals.assign(4, Vec3(0, 0, 1));
        CHECK(!as::flatShaded(withNormals).has_value()); // normales explicites gardées
    }

    TEST_CASE("RenderModel: GPU meshes of a primitive without NORMAL are flat-shaded")
    {
        FOR_EACH_BACKEND(backend)
        {
            ModelScene scene(backend);
            REQUIRE(scene.gpu.device);
            as::Model m;
            as::MeshPrimitive p = roof();
            p.joints.clear();
            p.weights.clear();
            m.meshes.push_back({"roof", {p}});
            m.nodes.resize(1);
            m.nodes[0].mesh = 0;
            auto model = scene.model(m);
            REQUIRE(model.meshCount() == 1);
            CHECK(model.mesh(0).indexCount() == 6);
            CHECK(model.mesh(0).vertexBufferSize() == 6 * rd::kPbrVertexFloats * sizeof(float));
        }
    }

    TEST_CASE("RenderModel: embedded images of two models in one cache do not collide; one image referenced twice is shared")
    {
        FOR_EACH_BACKEND(backend)
        {
            ModelScene scene(backend);
            REQUIRE(scene.gpu.device);
            tests::TempDir dir("engine_render_model");
            auto withImage = [&](std::vector<std::byte> bytes, int textureCount)
            {
                as::MaterialData mat = unlit({1, 1, 1, 1});
                mat.baseColor.texture = 0;
                mat.emissive.texture = textureCount > 1 ? 1 : -1;
                as::Model m = quad(mat);
                m.images.push_back({"img", {}, std::move(bytes), "image/png"});
                for (int t = 0; t < textureCount; ++t)
                {
                    m.textures.push_back({0, -1});
                }
                return m;
            };
            auto red = scene.model(withImage(pngBytes(dir, "red.png", 255, 0, 0), 2)); // clé de source par défaut
            CHECK(scene.textures->size() == 1);                                          // même image, même rôle : partagée
            auto green = scene.model(withImage(pngBytes(dir, "green.png", 0, 255, 0), 1));
            CHECK(scene.textures->size() == 2);
            const auto pxRed = scene.render(red);
            const auto pxGreen = scene.render(green);
            CHECK(pxRed[0] == 255);
            CHECK(pxRed[1] == 0);
            CHECK(pxGreen[0] == 0);
            CHECK(pxGreen[1] == 255);
            scene.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("RenderModel: two skinned nodes on one primitive get their own GPU mesh; static nodes share theirs, at rest")
    {
        FOR_EACH_BACKEND(backend)
        {
            ModelScene scene(backend);
            REQUIRE(scene.gpu.device);
            as::Model m;
            as::MeshPrimitive p = roof();
            for (auto &j : p.joints)
            {
                j = {0, 0, 0, 0};
            }
            p.normals.assign(4, Vec3(0, 0, 1));
            m.meshes.push_back({"skinned", {p}});
            m.nodes.resize(5);
            m.nodes[0].mesh = 0; // deux nœuds skinnés
            m.nodes[0].skin = 0;
            m.nodes[1].mesh = 0;
            m.nodes[1].skin = 0;
            m.nodes[2].mesh = 0; // deux nœuds statiques
            m.nodes[3].mesh = 0;
            m.skins.push_back({"skin", {4}, {orosmo::core::Mat4(1.0f)}, -1});

            auto skinnable = scene.model(m, "skinnable", true);
            REQUIRE(skinnable.drawCount() == 4);
            const auto &items = skinnable.items();
            CHECK(items[0].mesh != items[1].mesh); // une sortie de skinning par nœud skinné
            CHECK(items[2].mesh == items[3].mesh); // statiques : un seul jeu de tampons
            CHECK(items[0].mesh != items[2].mesh); // ... au repos : aucun nœud skinné n'écrit dans le leur
            CHECK(items[1].mesh != items[2].mesh);
            CHECK(skinnable.meshCount() == 3);
            CHECK(skinnable.meshSource(items[1].mesh).mesh == 0);
            CHECK(skinnable.meshSource(items[1].mesh).primitive == 0);

            auto plain = scene.model(m, "plain", false); // sans skinning GPU : tout est partagé
            CHECK(plain.meshCount() == 1);

            m.nodes[2].mesh = -1; // sans nœud statique, le premier nœud skinné prend le jeu de base
            m.nodes[3].mesh = -1;
            auto skinnedOnly = scene.model(m, "skinned-only", true);
            CHECK(skinnedOnly.meshCount() == 2);
        }
    }

    TEST_CASE("Mesh: each history slot keeps its own previous model matrix")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            rd::MeshData data;
            data.vertices = {0, 0, 0, 1, 0, 0, 0, 1, 0};
            data.indices = {0, 1, 2};
            data.attributeSizes = {3};
            auto mesh = rd::Mesh::create(*gpu.device, data);
            REQUIRE(mesh.has_value());
            auto previous = [&]
            {
                glm::mat4 m;
                std::memcpy(&m, mesh->modelBlock().data() + 128, sizeof m);
                return m;
            };
            const glm::mat4 a = glm::translate(glm::mat4(1.0f), {0, 0, 0});
            const glm::mat4 b = glm::translate(glm::mat4(1.0f), {10, 0, 0});
            for (std::uint64_t frame = 1; frame <= 3; ++frame)
            {
                mesh->setHistorySlot(0);
                mesh->setModelMatrix(a);
                REQUIRE(mesh->prepareTransform({}, {frame, {}}).has_value());
                CHECK(previous() == a); // immobile : jamais la matrice de l'autre nœud
                mesh->setHistorySlot(1);
                mesh->setModelMatrix(b);
                REQUIRE(mesh->prepareTransform({}, {frame, {}}).has_value());
                CHECK(previous() == b);
            }
        }
    }

    TEST_CASE("Mesh: a mirrored transform turns the front face clockwise")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            rd::MeshData data;
            data.vertices = {0, 0, 0, 1, 0, 0, 0, 1, 0};
            data.indices = {0, 1, 2};
            data.attributeSizes = {3};
            auto mesh = rd::Mesh::create(*gpu.device, data);
            REQUIRE(mesh.has_value());
            CHECK((mesh->frontFace() == FrontFace::kCounterClockwise));
            mesh->setModelMatrix(glm::scale(glm::mat4(1.0f), {-1, 1, 1}));
            REQUIRE(mesh->prepareTransform().has_value());
            CHECK((mesh->frontFace() == FrontFace::kClockwise));
            mesh->setModelMatrix(glm::scale(glm::mat4(1.0f), {-1, -1, 1})); // deux miroirs : une rotation
            REQUIRE(mesh->prepareTransform().has_value());
            CHECK((mesh->frontFace() == FrontFace::kCounterClockwise));
        }
    }

    TEST_CASE("RenderModel: a mirrored node with a single-sided material is still drawn")
    {
        FOR_EACH_BACKEND(backend)
        {
            ModelScene scene(backend);
            REQUIRE(scene.gpu.device);
            as::Model m = quad(unlit({0, 1, 0, 1}));
            m.nodes[0].scale = {-1, 1, 1};
            auto model = scene.model(m, "mirror");
            CHECK(scene.render(model)[1] == 255);
            m.nodes[0].scale = {-1, -1, 1}; // deux miroirs : une rotation, toujours visible
            auto rotated = scene.model(m, "rotated");
            CHECK(scene.render(rotated)[1] == 255);
            scene.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("RenderModel: a mirrored node keeps its normal map's green axis (lit the same from +Y)")
    {
        FOR_EACH_BACKEND(backend)
        {
            ModelScene scene(backend);
            REQUIRE(scene.gpu.device);
            tests::TempDir dir("engine_render_model");
            rd::Image tex;
            tex.width = 1;
            tex.height = 1;
            tex.rgba = {128, 204, 230, 255}; // (0 ; 0,6 ; 0,8) en espace tangent : penchée vers +V
            REQUIRE(rd::savePng(tex, dir.path / "normal.png").has_value());
            as::MaterialData mat;
            mat.name = "bumped";
            mat.baseColorFactor = {1, 1, 1, 1};
            mat.metallicFactor = 0.0f;
            mat.roughnessFactor = 1.0f;
            mat.normal.texture = 0;
            as::Model m = quad(mat);
            m.images.push_back({"normal", dir.path / "normal.png", {}, ""});
            m.textures.push_back({0, -1});
            scene.lights->addLight(rd::directionalLight({0, 1, 0}, {1, 1, 1}, 1.0f));
            auto straight = scene.model(m, "straight");
            m.nodes[0].scale = {-1, 1, 1};
            auto mirrored = scene.model(m, "mirrored");
            const auto a = scene.render(straight);
            const auto b = scene.render(mirrored);
            INFO("straight ", int(a[0]), " mirrored ", int(b[0]));
            CHECK(a[0] > 60);
            CHECK(near(b[0], a[0], 2));
            scene.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("RenderModel::rebuildMaterials: new groups reach every mesh; the variant follows shadow reception")
    {
        FOR_EACH_BACKEND(backend)
        {
            ModelScene scene(backend);
            REQUIRE(scene.gpu.device);
            as::Model m = quad(unlit({0, 0, 1, 1}));
            m.nodes.resize(2);
            m.nodes[1].mesh = 0;
            auto model = scene.model(m, "rebuild");
            const BindGroupHandle before = model.materials()[0]->group();
            REQUIRE(model.rebuildMaterials(nullptr, *scene.shaders).has_value());
            const BindGroupHandle after = model.materials()[0]->group();
            CHECK(after != before);
            CHECK(model.mesh(0).textureGroup() == after);
            CHECK(model.mesh(0).program() != nullptr);
            CHECK(scene.render(model)[2] == 255); // l'ancien handle, détruit, n'est plus lié

            // Réception des ombres ajoutée puis retirée : la variante (programme) suit le groupe.
            auto shadows = rd::ShadowFeature::create(*scene.gpu.device, *scene.library, *scene.pipelines, shaderDir());
            INFO((shadows ? std::string() : shadows.error().message));
            REQUIRE(shadows.has_value());
            REQUIRE(model.rebuildMaterials(shadows->get(), *scene.shaders).has_value());
            const std::uint32_t receiving = model.materials()[0]->features();
            CHECK((receiving & rd::kPbrShadows) != 0u);
            CHECK(model.mesh(0).textureGroup() == model.materials()[0]->group());
            CHECK(model.mesh(0).program() == *scene.shaders->program(receiving));
            REQUIRE(model.rebuildMaterials(nullptr, *scene.shaders).has_value());
            CHECK((model.materials()[0]->features() & rd::kPbrShadows) == 0u);
            CHECK(model.mesh(0).program() == *scene.shaders->program(model.materials()[0]->features()));
            scene.gpu.expectNoValidationErrors();
        }
    }
}
