// M8-L11 : API de rendu simplifiée (A75.3 à A75.7, A75.15). Contexte de rendu, définitions de shader de la scène et programme
// résolu au dessin, matériau porté par le maillage, constructeur de maillages (tangentes), dessin sans caméra et fil de fer, fin
// d'image vers une texture. Chaque nouveau chemin est comparé à l'ancien, qui reste compilé ici (choix 10 d'A79).
#include <doctest/doctest.h>

#include "support/result_support.hpp"
#include "support/test_support.hpp"
#include "renderer/repo_paths.hpp"
#include "rhi/gpu_fixture.hpp"

#include <orosmo/renderer/camera.hpp>
#include <orosmo/renderer/canvas_renderer.hpp>
#include <orosmo/renderer/light.hpp>
#include <orosmo/renderer/material.hpp>
#include <orosmo/renderer/mesh.hpp>
#include <orosmo/renderer/mesh_builder.hpp>
#include <orosmo/renderer/render_context.hpp>
#include <orosmo/renderer/render_feature.hpp>
#include <orosmo/renderer/scene_renderer.hpp>
#include <orosmo/renderer/shadow_feature.hpp>
#include <orosmo/renderer/upscaling.hpp>

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace rd = orosmo::renderer;

namespace
{

fs::path shaderDir() { return tests::repoRoot(__FILE__) / "shaders" / "rhi"; }

bool hasDefine(const std::vector<rd::Define> &defines, const std::string &name)
{
    return std::ranges::any_of(defines, [&](const rd::Define &d) { return d.first == name; });
}

// Plugin sans passe qui demande une définition (comme ForwardPlusLighting et OROSMO_FORWARD_PLUS).
class DefineFeature final : public rd::RenderFeature
{
  public:
    explicit DefineFeature(std::string define) : define_(std::move(define)) {}
    std::string name() const override { return "test.define." + define_; }
    std::vector<rd::Define> shaderDefines() const override { return {{define_, "1"}}; }
    orosmo::core::Result<> declarePasses(rd::RenderGraphBuilder &) override { return {}; }

  private:
    std::string define_;
};

// Mode de mise à l'échelle avancé qui note la taille de sortie reçue (revue de la PR #19).
class RecordingAdvancedMode final : public rd::IAdvancedUpscaleMode
{
  public:
    explicit RecordingAdvancedMode(glm::vec2 *seen) : seen_(seen) { desc_.name = "test.advanced"; }
    std::string name() const noexcept override { return desc_.name; }
    const rd::UpscaleModeDesc &description() const noexcept override { return desc_; }
    bool isSupported(const orosmo::rhi::Capabilities &) const noexcept override { return true; }
    bool initialize(orosmo::rhi::Device &) override { return true; }
    void shutdown() override {}
    orosmo::core::Result<> execute(rd::FrameContext &, const rd::UpscaleInput &input, rd::UpscaleOutput &) override
    {
        *seen_ = input.outputResolution;
        return {};
    }

  private:
    rd::UpscaleModeDesc desc_;
    glm::vec2 *seen_;
};

// Scène 16x16 sur un contexte qui possède sa bibliothèque et son cache (RenderContext::create).
struct ApiScene
{
    Gpu gpu;
    std::unique_ptr<rd::RenderContext> context;
    std::unique_ptr<rd::SceneRenderer> renderer;
    std::unique_ptr<rd::LightManager> lights;
    rd::Camera camera;
    static constexpr std::uint32_t kSize = 16;

    explicit ApiScene(Backend backend) : gpu(backend)
    {
        if (!gpu.device)
        {
            return;
        }
        context = rd::RenderContext::create(*gpu.device, shaderDir());
        auto created = rd::SceneRenderer::create(*context);
        INFO((created ? std::string() : created.error().message));
        REQUIRE(created.has_value());
        renderer = std::move(*created);
        auto l = rd::LightManager::create(*gpu.device);
        REQUIRE(l.has_value());
        lights = std::make_unique<rd::LightManager>(std::move(*l));
        lights->setAmbientLight({1, 1, 1}, 1.0f);
        REQUIRE(lights->upload().has_value());
        renderer->setLights(lights.get());
        renderer->setOutputResolution(kSize, kSize);
        camera.setAspect(kSize, kSize);
        camera.setFarPlane(100.0f);
    }
    ~ApiScene()
    {
        renderer.reset(); // avant le contexte : le renderer référence sa bibliothèque et son cache
        lights.reset();
        context.reset();
    }

    TextureHandle output() { return gpu.target(Format::kRGBA8Unorm, {kSize, kSize}, "output"); }

    // Une image : `draw` dépose les dessins, puis fin d'image vers `out` (par l'ancienne ou la nouvelle signature).
    template <typename F> void frame(TextureHandle out, F &&draw, bool endWithTexture = false)
    {
        gpu.run(
            [&](CommandEncoder &e)
            {
                REQUIRE(renderer->beginFrame(e, {.clearColor = {0, 0, 0, 1}}).has_value());
                REQUIRE(renderer->setCamera(camera).has_value());
                draw();
                auto ended = endWithTexture ? renderer->endFrame(out) : renderer->endFrame({out, Format::kRGBA8Unorm, {kSize, kSize}});
                INFO((ended ? std::string() : ended.error().message));
                REQUIRE(ended.has_value());
            });
    }
};

std::vector<std::array<std::uint8_t, 4>> readAll(Gpu &gpu, TextureHandle out, std::uint32_t size)
{
    std::vector<std::array<std::uint8_t, 4>> pixels;
    for (std::uint32_t y = 1; y < size; y += 3)
    {
        for (std::uint32_t x = 1; x < size; x += 3)
        {
            pixels.push_back(rgba(gpu.pixel(out, static_cast<std::int32_t>(x), static_cast<std::int32_t>(y))));
        }
    }
    return pixels;
}

} // namespace

TEST_SUITE("renderer/mesh-builder")
{
    TEST_CASE("MeshBuilder (A75.5): box and plane layouts, tangents orthonormal with handedness, invalid input refused")
    {
        rd::MeshBuilder box;
        box.setName("box").addBox({1.0f, 2.0f, 3.0f}, {0.5f, 0.25f, 1.0f, 1.0f});
        CHECK(box.vertexCount() == 24);
        CHECK(box.indexCount() == 36);
        auto data = box.build();
        REQUIRE(data.has_value());
        CHECK(data->name == "box");
        CHECK(data->attributeSizes == std::vector<std::uint32_t>(rd::kPbrVertexAttributeSizes.begin(), rd::kPbrVertexAttributeSizes.end()));
        REQUIRE(data->vertices.size() == 24 * rd::kPbrVertexFloats);
        for (std::size_t v = 0; v < 24; ++v)
        {
            const float *f = data->vertices.data() + v * rd::kPbrVertexFloats;
            const glm::vec3 n{f[3], f[4], f[5]};
            const glm::vec3 t{f[8], f[9], f[10]};
            CHECK(std::fabs(glm::length(t) - 1.0f) < 1e-5f);
            CHECK(std::fabs(glm::dot(t, n)) < 1e-5f);
            CHECK(std::fabs(f[11]) == doctest::Approx(1.0f));
            CHECK(f[12] == doctest::Approx(0.5f)); // couleur rgba à la fin
            CHECK(f[15] == doctest::Approx(1.0f));
        }
        // Chaque triangle de la boîte est anti-horaire vu de l'extérieur : sa normale géométrique suit celle des sommets.
        for (std::size_t i = 0; i < box.indices().size(); i += 3)
        {
            const auto &v = box.vertices();
            const glm::vec3 a = v[box.indices()[i]].position;
            const glm::vec3 b = v[box.indices()[i + 1]].position;
            const glm::vec3 c = v[box.indices()[i + 2]].position;
            CHECK(glm::dot(glm::cross(b - a, c - a), v[box.indices()[i]].normal) > 0.0f);
        }

        // Tangente : direction des u croissants ; uv en miroir : signe de la bitangente négatif.
        const std::vector<glm::vec3> positions{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
        const std::vector<glm::vec3> normals(3, glm::vec3(0, 0, 1));
        const std::vector<std::uint32_t> tri{0, 1, 2};
        const auto straight = rd::MeshBuilder::computeTangents(positions, normals, std::vector<glm::vec2>{{0, 0}, {1, 0}, {0, 1}}, tri);
        CHECK(straight[0].x == doctest::Approx(1.0f));
        CHECK(straight[0].w == doctest::Approx(1.0f));
        const auto mirrored = rd::MeshBuilder::computeTangents(positions, normals, std::vector<glm::vec2>{{0, 0}, {1, 0}, {0, -1}}, tri);
        CHECK(mirrored[0].w == doctest::Approx(-1.0f));
        // uv dégénérés : tangente perpendiculaire quelconque, stable.
        const auto degenerate = rd::MeshBuilder::computeTangents(positions, normals, std::vector<glm::vec2>(3, glm::vec2(0.0f)), tri);
        CHECK(std::fabs(glm::dot(glm::vec3(degenerate[0]), normals[0])) < 1e-6f);
        CHECK(glm::length(glm::vec3(degenerate[0])) == doctest::Approx(1.0f));
        // Revue PR #37 : tangente de très grande norme (arête 1e10, delta uv 1e-10 : |t| ≈ 1e20, dot(t, t) déborde) : unitaire.
        const std::vector<glm::vec3> huge{{0, 0, 0}, {1e10f, 0, 0}, {0, 1e10f, 0}};
        const auto large = rd::MeshBuilder::computeTangents(huge, normals, std::vector<glm::vec2>{{0, 0}, {1e-10f, 0}, {0, 1e-10f}}, tri);
        CHECK(large[0].x == doctest::Approx(1.0f));
        CHECK(glm::length(glm::vec3(large[0])) == doctest::Approx(1.0f));

        rd::MeshBuilder colored(rd::MeshLayout::kColored);
        colored.addPlane({2.0f, 2.0f}, {1.0f, 0.0f, 0.0f, 1.0f});
        auto plane = colored.build();
        REQUIRE(plane.has_value());
        CHECK(plane->attributeSizes == std::vector<std::uint32_t>{3, 3, 3});
        CHECK(plane->vertices.size() == 4 * 9);
        CHECK(plane->indices.size() == 6);

        rd::MeshBuilder empty;
        CHECK((tests::errorOf(empty.build()) == ErrorCode::kInvalidArgument));
        rd::MeshBuilder outOfRange;
        outOfRange.addVertex({});
        outOfRange.addTriangle(0, 0, 3);
        CHECK((tests::errorOf(outOfRange.build()) == ErrorCode::kInvalidArgument));
    }

    TEST_CASE("MeshBuilder::addBox with one color per face (A79 L14): same geometry as one color, faces in the order +X -X +Y -Y +Z -Z")
    {
        const std::array<glm::vec4, 6> colors{{{1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 1, 1}, {1, 1, 0, 1}, {0, 1, 1, 1}, {1, 0, 1, 1}}};
        rd::MeshBuilder faces(rd::MeshLayout::kColored);
        faces.addBox({1.0f, 2.0f, 3.0f}, colors, {0.5f, 0.0f, 0.0f});
        rd::MeshBuilder single(rd::MeshLayout::kColored);
        single.addBox({1.0f, 2.0f, 3.0f}, glm::vec4(1.0f), {0.5f, 0.0f, 0.0f});
        REQUIRE(faces.vertexCount() == 24);
        CHECK(faces.indices() == single.indices());
        const std::array<glm::vec3, 6> normals{{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
        for (std::size_t v = 0; v < 24; ++v)
        {
            CHECK(faces.vertices()[v].position == single.vertices()[v].position);
            CHECK(faces.vertices()[v].normal == normals[v / 4]);
            CHECK(faces.vertices()[v].color == colors[v / 4]);
        }
    }
}

TEST_SUITE("renderer/render-api")
{
    TEST_CASE("RenderContext (A75.4): an owning context and a referencing one build the same factories as the old signatures")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            auto owning = rd::RenderContext::create(*gpu.device, shaderDir());
            CHECK(owning->ownsResources());
            CHECK(&owning->device() == gpu.device.get());
            CHECK(owning->shaderDirectory() == shaderDir());

            rd::ShaderLibrary library(*gpu.device);
            rd::PipelineCache pipelines(*gpu.device);
            const rd::RenderContext shared(*gpu.device, library, pipelines, shaderDir());
            CHECK_FALSE(shared.ownsResources());
            CHECK(&shared.library() == &library);
            CHECK(&shared.pipelines() == &pipelines);
            {
                for (const rd::RenderContext *context : {static_cast<const rd::RenderContext *>(owning.get()), &shared})
                {
                    auto scene = rd::SceneRenderer::create(*context);
                    INFO((scene ? std::string() : scene.error().message));
                    CHECK(scene.has_value());
                    CHECK(rd::CanvasRenderer::create(*context).has_value());
                    CHECK(rd::ShadowFeature::create(*context).has_value());
                    rd::PbrShaderCache cache(*context);
                    CHECK(cache.program(rd::kPbrBaseColorMap).has_value());
                }
                // Ancienne signature, gardée jusqu'à M10.
                CHECK(rd::SceneRenderer::create(*gpu.device, library, pipelines, {.shaderDirectory = shaderDir()}).has_value());
            }
            gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("SceneRenderer::shaderDefines and revision (A75.3): plugins first, then AOVs; the revision moves only when they change")
    {
        FOR_EACH_BACKEND(backend)
        {
            ApiScene scene(backend);
            REQUIRE(scene.gpu.device);
            rd::SceneRenderer &r = *scene.renderer;
            CHECK(r.shaderDefines().empty());
            const std::uint64_t first = r.revision();
            CHECK(r.revision() == first);

            REQUIRE(r.addFeature(std::make_unique<DefineFeature>("OROSMO_TEST_A")).has_value());
            const std::uint64_t withFeature = r.revision();
            CHECK(withFeature > first);
            REQUIRE(r.declareAov(rd::Aov::kNormals).has_value());
            const std::vector<rd::Define> defines = r.shaderDefines();
            REQUIRE(defines.size() >= 2);
            CHECK(defines.front().first == "OROSMO_TEST_A");
            CHECK(hasDefine(defines, "OROSMO_AOV_NORMALS"));
            const std::vector<rd::Define> aov = r.aovShaderDefines();
            CHECK(std::equal(aov.begin(), aov.end(), defines.end() - static_cast<std::ptrdiff_t>(aov.size())));
            const std::uint64_t withAov = r.revision();
            CHECK(withAov > withFeature);
            CHECK(r.revision() == withAov);

            r.releaseAov(rd::Aov::kNormals);
            r.removeFeature("test.define.OROSMO_TEST_A");
            CHECK(r.shaderDefines().empty());
            CHECK(r.revision() > withAov);
        }
    }

    TEST_CASE("drawMesh(mesh) (A75.3, A75.6): the default program is resolved at draw time, recompiled for new AOVs, and draws like the old path")
    {
        FOR_EACH_BACKEND(backend)
        {
            ApiScene scene(backend);
            REQUIRE(scene.gpu.device);
            rd::MeshBuilder builder(rd::MeshLayout::kColored);
            builder.setName("quad");
            const glm::vec3 n{0, 0, 1};
            const glm::vec4 color{0.5f, 0.25f, 1.0f, 1.0f};
            builder.addQuad(builder.addVertex({{-3, -3, -1}, n, {}, color}), builder.addVertex({{3, -3, -1}, n, {}, color}),
                            builder.addVertex({{3, 3, -1}, n, {}, color}), builder.addVertex({{-3, 3, -1}, n, {}, color}));
            auto resolved = builder.create(*scene.gpu.device);
            auto legacy = builder.create(*scene.gpu.device);
            REQUIRE(resolved.has_value());
            REQUIRE(legacy.has_value());
            auto program = scene.context->library().loadGraphics(shaderDir() / "mesh.vert", shaderDir() / "mesh.frag");
            REQUIRE(program.has_value());
            legacy->setProgram(*program);

            const TextureHandle oldOut = scene.output();
            const TextureHandle newOut = scene.output();
            scene.frame(oldOut, [&] { REQUIRE(scene.renderer->drawMesh(*legacy, scene.camera).has_value()); }); // ancien chemin
            scene.frame(newOut, [&] {
                auto drawn = scene.renderer->drawMesh(*resolved);
                INFO((drawn ? std::string() : drawn.error().message));
                REQUIRE(drawn.has_value());
            });
            CHECK(readAll(scene.gpu, newOut, ApiScene::kSize) == readAll(scene.gpu, oldOut, ApiScene::kSize));
            const auto px = rgba(scene.gpu.pixel(newOut, 8, 8));
            CHECK(near(px[0], 128));
            CHECK(near(px[1], 64));
            CHECK(near(px[2], 255));
            CHECK(resolved->program() == nullptr); // le maillage n'a reçu aucun programme
            // L'ancienne signature garde son contrat d'avant M8 : sans programme imposé, le dessin est refusé.
            scene.frame(oldOut, [&] { CHECK((tests::errorOf(scene.renderer->drawMesh(*resolved, scene.camera)) == ErrorCode::kInvalidArgument)); });

            const std::size_t programsBefore = scene.context->library().programCount();
            // Nouvel AOV entre deux images : la révision change, le programme est recompilé avec OROSMO_AOV_NORMALS et écrit l'AOV.
            REQUIRE(scene.renderer->declareAov(rd::Aov::kNormals).has_value());
            scene.frame(newOut, [&] { REQUIRE(scene.renderer->drawMesh(*resolved).has_value()); });
            CHECK(scene.renderer->sceneTarget().colorCount() == 2);
            CHECK(scene.context->library().programCount() == programsBefore + 1); // mesh.vert/frag recompilé avec OROSMO_AOV_NORMALS
            CHECK(near(rgba(scene.gpu.pixel(newOut, 8, 8))[0], 128));

            // Disposition inconnue sans programme ni matériau : refusée, rien n'est enregistré.
            rd::MeshData odd;
            odd.vertices = {0, 0, 0, 1, 0, 0, 0, 1, 0};
            odd.indices = {0, 1, 2};
            odd.attributeSizes = {3};
            auto oddMesh = rd::Mesh::create(*scene.gpu.device, odd);
            REQUIRE(oddMesh.has_value());
            scene.frame(newOut, [&] { CHECK((tests::errorOf(scene.renderer->drawMesh(*oddMesh)) == ErrorCode::kInvalidArgument)); });
            scene.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("setWireframe (A75.6): drawMesh(mesh) adds the line pass from the renderer flag or from the camera of setCamera")
    {
        FOR_EACH_BACKEND(backend)
        {
            ApiScene scene(backend);
            REQUIRE(scene.gpu.device);
            auto mesh = rd::MeshBuilder(rd::MeshLayout::kColored).addBox({0.5f, 0.5f, 0.5f}).create(*scene.gpu.device);
            REQUIRE(mesh.has_value());
            mesh->setPosition({0, 0, -3});
            const TextureHandle out = scene.output();
            scene.frame(out, [&] { REQUIRE(scene.renderer->drawMesh(*mesh).has_value()); });
            const std::size_t filled = scene.context->pipelines().size();
            CHECK_FALSE(scene.renderer->wireframe());
            scene.renderer->setWireframe(true);
            CHECK(scene.renderer->wireframe());
            scene.frame(out, [&] { REQUIRE(scene.renderer->drawMesh(*mesh).has_value()); });
            const std::size_t lines = scene.context->pipelines().size();
            CHECK(lines == filled + 1); // pipeline « .lines » du passage fil de fer
            scene.renderer->setWireframe(false);
            scene.camera.setWireframe(true); // comme drawMesh(mesh, camera) avec une caméra en fil de fer
            scene.frame(out, [&] { REQUIRE(scene.renderer->drawMesh(*mesh).has_value()); });
            CHECK(scene.context->pipelines().size() == lines);
            scene.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("Mesh::setMaterial (A75.5, A75.15): the mesh keeps its material, follows its group, culls back faces, and draws like the old path")
    {
        FOR_EACH_BACKEND(backend)
        {
            ApiScene scene(backend);
            REQUIRE(scene.gpu.device);
            auto defaults = rd::PbrDefaults::create(*scene.gpu.device);
            REQUIRE(defaults.has_value());
            rd::PbrMaterialDesc desc;
            desc.name = "unlit";
            desc.unlit = true;
            desc.params.baseColorFactor = {0.25f, 0.5f, 1.0f, 1.0f};
            auto material = rd::PbrMaterial::create(*scene.gpu.device, desc, *defaults);
            REQUIRE(material.has_value());

            rd::MeshBuilder builder;
            builder.addBox({1.0f, 1.0f, 1.0f});
            auto resolved = builder.create(*scene.gpu.device);
            auto legacy = builder.create(*scene.gpu.device);
            REQUIRE(resolved.has_value());
            REQUIRE(legacy.has_value());
            CHECK((resolved->cullMode() == CullMode::kNone)); // défaut d'avant M8 inchangé
            const std::weak_ptr<rd::PbrMaterial> watch = *material;
            resolved->setMaterial(*material);
            CHECK((resolved->cullMode() == CullMode::kBack));
            CHECK(resolved->textureGroup() == (*material)->group());
            CHECK((resolved->aovMaterial().blend == (*material)->blend()));
            resolved->setPosition({0, 0, -4});
            legacy->setPosition({0, 0, -4});

            // Ancien chemin : programme de la variante, groupe et faces à la main.
            rd::PbrShaderCache cache(*scene.context);
            auto program = cache.program((*material)->features());
            REQUIRE(program.has_value());
            legacy->setProgram(*program);
            legacy->setMaterialGroup((*material)->group());
            legacy->setCullMode(CullMode::kBack);
            material->reset(); // le maillage garde le matériau vivant
            CHECK_FALSE(watch.expired());

            const TextureHandle oldOut = scene.output();
            const TextureHandle newOut = scene.output();
            scene.frame(oldOut, [&] { REQUIRE(scene.renderer->drawMesh(*legacy, scene.camera).has_value()); });
            scene.frame(newOut, [&] {
                auto drawn = scene.renderer->drawMesh(*resolved);
                INFO((drawn ? std::string() : drawn.error().message));
                REQUIRE(drawn.has_value());
            });
            CHECK(readAll(scene.gpu, newOut, ApiScene::kSize) == readAll(scene.gpu, oldOut, ApiScene::kSize));
            const auto px = rgba(scene.gpu.pixel(newOut, 8, 8));
            CHECK(px[2] > px[1]);
            CHECK(px[1] > px[0]);

            // Double face : aucune élimination ; matériau retiré : le maillage le relâche.
            rd::PbrMaterialDesc twoSided = desc;
            twoSided.doubleSided = true;
            auto other = rd::PbrMaterial::create(*scene.gpu.device, twoSided, *defaults);
            REQUIRE(other.has_value());
            resolved->setMaterial(*other);
            CHECK((resolved->cullMode() == CullMode::kNone));
            CHECK(watch.expired());
            resolved->setMaterial(nullptr);
            CHECK(resolved->material() == nullptr);
            CHECK(resolved->textureGroup().isNull());

            // Revue PR #37 : un dessin enregistré garde SON matériau jusqu'à endFrame, même remplacé par setMaterial entre-temps.
            auto first = rd::PbrMaterial::create(*scene.gpu.device, desc, *defaults);
            auto second = rd::PbrMaterial::create(*scene.gpu.device, twoSided, *defaults);
            REQUIRE(first.has_value());
            REQUIRE(second.has_value());
            const std::weak_ptr<rd::PbrMaterial> watchFirst = *first;
            resolved->setMaterial(std::move(*first));
            const TextureHandle swapOut = scene.output();
            scene.frame(swapOut, [&] {
                REQUIRE(scene.renderer->drawMesh(*resolved).has_value());
                resolved->setMaterial(*second);
                CHECK_FALSE(watchFirst.expired()); // retenu par le dessin enregistré
                REQUIRE(scene.renderer->drawMesh(*resolved).has_value());
            });
            CHECK(watchFirst.expired()); // relâché avec les dessins de l'image
            scene.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("endFrame(texture) (A75.7): format and size are read from the device; same pixels as endFrame(UpscaleTarget); unknown texture refused")
    {
        FOR_EACH_BACKEND(backend)
        {
            ApiScene scene(backend);
            REQUIRE(scene.gpu.device);
            auto mesh = rd::MeshBuilder(rd::MeshLayout::kColored).addBox({1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}).create(*scene.gpu.device);
            REQUIRE(mesh.has_value());
            mesh->setPosition({0, 0, -4});
            const TextureHandle oldOut = scene.output();
            const TextureHandle newOut = scene.output();
            scene.frame(oldOut, [&] { REQUIRE(scene.renderer->drawMesh(*mesh).has_value()); });
            scene.frame(newOut, [&] { REQUIRE(scene.renderer->drawMesh(*mesh).has_value()); }, true);
            CHECK(readAll(scene.gpu, newOut, ApiScene::kSize) == readAll(scene.gpu, oldOut, ApiScene::kSize));
            CHECK(rgba(scene.gpu.pixel(newOut, 8, 8))[1] > 100);

            // Sortie deux fois plus grande que la résolution : la scène y est étirée.
            const TextureHandle big = scene.gpu.target(Format::kRGBA8Unorm, {32, 32}, "big");
            scene.frame(big, [&] { REQUIRE(scene.renderer->drawMesh(*mesh).has_value()); }, true);
            CHECK(rgba(scene.gpu.pixel(big, 16, 16))[1] > 100);

            scene.gpu.run(
                [&](CommandEncoder &e)
                {
                    REQUIRE(scene.renderer->beginFrame(e).has_value());
                    REQUIRE(scene.renderer->setCamera(scene.camera).has_value());
                    CHECK_FALSE(scene.renderer->endFrame(TextureHandle{}).has_value());
                    CHECK(scene.renderer->beginFrame(e).has_value()); // l'image refusée est fermée
                    CHECK(scene.renderer->endFrame(newOut).has_value());
                });
            scene.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("Review PR #19: a feature removed during a frame leaves at its end; a null output is refused even when scaled; advanced modes get the output texture size")
    {
        FOR_EACH_BACKEND(backend)
        {
            ApiScene scene(backend);
            REQUIRE(scene.gpu.device);
            rd::SceneRenderer &r = *scene.renderer;
            REQUIRE(r.addFeature(std::make_unique<DefineFeature>("OROSMO_TEST_B")).has_value());
            const TextureHandle out = scene.output();
            scene.frame(out, [&] {
                r.removeFeature("test.define.OROSMO_TEST_B");
                CHECK(hasDefine(r.shaderDefines(), "OROSMO_TEST_B")); // différé : les dessins de l'image gardent ses définitions
                CHECK(r.featureNames().size() == 1);
            });
            CHECK(r.featureNames().empty());
            CHECK_FALSE(hasDefine(r.shaderDefines(), "OROSMO_TEST_B"));

            // Échelle réduite sans mode : aucune passe ne lirait la sortie ; une sortie nulle est quand même refusée.
            r.setUpscalingEnabled(true);
            r.setRenderScale(0.5f);
            REQUIRE(r.setActiveUpscaleMode("disabled"));
            scene.gpu.run(
                [&](CommandEncoder &e)
                {
                    REQUIRE(r.beginFrame(e).has_value());
                    CHECK((tests::errorOf(r.endFrame(TextureHandle{})) == ErrorCode::kInvalidArgument));
                    CHECK(r.beginFrame(e).has_value()); // l'image refusée est fermée
                    // Revue PR #37 : un handle inconnu mais non nul est refusé de même.
                    CHECK((tests::errorOf(r.endFrame(TextureHandle{.index = 4242, .generation = 7})) == ErrorCode::kInvalidArgument));
                    CHECK(r.beginFrame(e).has_value());
                    // Revue PR #37 : pendant une image, ajouter un plugin ou changer de vue de debug est refusé (kBusy), sans effet.
                    CHECK((tests::errorOf(r.addFeature(std::make_unique<DefineFeature>("OROSMO_TEST_C"))) == ErrorCode::kBusy));
                    CHECK((tests::errorOf(r.setDebugView(rd::DebugView::kNormals)) == ErrorCode::kBusy));
                    CHECK((r.debugView() == rd::DebugView::kNone));
                    CHECK(r.featureNames().empty());
                    CHECK(r.endFrame(out).has_value());
                });

            // Mode avancé : la taille de sortie est celle de la texture (32x32), pas celle de setOutputResolution (16x16).
            glm::vec2 seen{0.0f};
            r.registerUpscaleMode(std::make_unique<RecordingAdvancedMode>(&seen));
            REQUIRE(r.setActiveUpscaleMode("test.advanced"));
            const TextureHandle big = scene.gpu.target(Format::kRGBA8Unorm, {32, 32}, "big");
            scene.frame(big, [] {}, true);
            CHECK(seen.x == doctest::Approx(32.0f));
            CHECK(seen.y == doctest::Approx(32.0f));
            scene.gpu.expectNoValidationErrors();
        }
    }
}
