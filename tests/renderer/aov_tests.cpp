// REN-21 (AOV à la demande), REN-41 (transparents sans AOV par défaut), REN-35 (modes de debug, gel du frustum).
#include <doctest/doctest.h>

#include "renderer/repo_paths.hpp"
#include "rhi/gpu_fixture.hpp"
#include "support/test_support.hpp"

#include <orosmo/renderer/aov.hpp>
#include <orosmo/renderer/camera.hpp>
#include <orosmo/renderer/debug_view.hpp>
#include <orosmo/renderer/forward_plus.hpp>
#include <orosmo/renderer/light.hpp>
#include <orosmo/renderer/mesh.hpp>
#include <orosmo/renderer/render_feature.hpp>
#include <orosmo/renderer/scene_renderer.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;
namespace rd = orosmo::renderer;
namespace rhi = orosmo::rhi;

namespace
{
fs::path shaderDir() { return tests::repoRoot(__FILE__) / "shaders" / "rhi"; }

float halfToFloat(std::uint16_t h)
{
    const int exponent = (h >> 10) & 31;
    const float mantissa = static_cast<float>(h & 1023);
    const float sign = (h & 0x8000) != 0 ? -1.0f : 1.0f;
    if (exponent == 0)
    {
        return sign * std::ldexp(mantissa, -24);
    }
    return sign * std::ldexp(1.0f + mantissa / 1024.0f, exponent - 15);
}

rd::MeshData triangle()
{
    rd::MeshData d;
    d.vertices = {-3, -3, -1, 0, 0, 1, 1, 1, 1, 3, -3, -1, 0, 0, 1, 1, 1, 1, 0, 3, -1, 0, 0, 1, 1, 1, 1};
    d.indices = {0, 1, 2};
    d.attributeSizes = {3, 3, 3};
    d.name = "aov.triangle";
    return d;
}

struct AovScene
{
    Gpu gpu;
    std::unique_ptr<rd::ShaderLibrary> library;
    std::unique_ptr<rd::PipelineCache> pipelines;
    std::unique_ptr<rd::SceneRenderer> renderer;
    explicit AovScene(Backend backend) : gpu(backend)
    {
        library = std::make_unique<rd::ShaderLibrary>(*gpu.device);
        pipelines = std::make_unique<rd::PipelineCache>(*gpu.device);
        auto created = rd::SceneRenderer::create(*gpu.device, *library, *pipelines, {.shaderDirectory = shaderDir()});
        REQUIRE(created.has_value());
        renderer = std::move(*created);
        renderer->setOutputResolution(8, 8);
    }
    ~AovScene() { renderer.reset(); }

    // Une image : le triangle (programme compilé pour les AOV actifs) vu par `camera`.
    void frame(rd::Mesh &mesh, rd::Camera &camera, TextureHandle out)
    {
        gpu.run(
            [&](CommandEncoder &e)
            {
                REQUIRE(renderer->beginFrame(e, {.clearColor = {0, 0, 0, 1}}).has_value());
                REQUIRE(renderer->setCamera(camera).has_value());
                auto drawn = renderer->drawMesh(mesh, camera);
                INFO((drawn ? std::string() : drawn.error().message));
                REQUIRE(drawn.has_value());
                REQUIRE(renderer->endFrame({out, Format::kRGBA8Unorm, {8, 8}}).has_value());
            });
    }
};
} // namespace

TEST_SUITE("renderer/aov")
{
    TEST_CASE("AOV set, layout, resolve rules and shader macros (CPU)")
    {
        const rd::AovSet set{rd::Aov::kObjectId, rd::Aov::kNormals};
        CHECK(set.count() == 2);
        const rd::AovLayout layout(set);
        CHECK(layout.attachmentCount() == 3);
        CHECK(*layout.index(rd::Aov::kNormals) == 1); // l'ordre est celui de l'énumération, pas de la demande
        CHECK(*layout.index(rd::Aov::kObjectId) == 2);
        CHECK_FALSE(layout.index(rd::Aov::kMotion).has_value());
        const auto formats = layout.colorFormats(Format::kRGBA8Unorm);
        REQUIRE(formats.size() == 3);
        CHECK((formats[1] == Format::kRG16Float));
        CHECK((formats[2] == Format::kR32Uint));
        CHECK(rd::aovShaderDefines({}).empty());
        const auto defines = rd::aovShaderDefines(set);
        REQUIRE(defines.size() == 2);
        CHECK(defines[0].first == "OROSMO_AOV_NORMALS");
        CHECK(defines[0].second == "1");
        // Identifiants jamais moyennés ; normales renormalisées ; profondeur : échantillon le plus proche.
        CHECK((rd::aovResolveRule(rd::Aov::kObjectId) == rd::AovResolve::kNearestNoBlend));
        CHECK((rd::aovResolveRule(rd::Aov::kNormals) == rd::AovResolve::kAverageRenormalized));
        CHECK((rd::aovResolveRule(rd::Aov::kLinearDepth) == rd::AovResolve::kNearestSample));
        CHECK(rd::aovFromName("object-id") == rd::Aov::kObjectId);
        CHECK_FALSE(rd::aovFromName("nope").has_value());
    }

    TEST_CASE("REN-41: a transparent material writes no AOV unless it asks for it")
    {
        const rd::AovSet active{rd::Aov::kMotion, rd::Aov::kNormals, rd::Aov::kObjectId};
        CHECK(rd::aovWrites({rd::BlendMode::kOpaque, {}}, active) == active);
        CHECK(rd::aovWrites({rd::BlendMode::kMask, {}}, active) == active);
        CHECK(rd::aovWrites({rd::BlendMode::kBlend, {}}, active).empty());
        CHECK(rd::aovWrites({rd::BlendMode::kAdditive, {}}, active).empty());
        CHECK(rd::aovWrites({rd::BlendMode::kTransmission, {rd::Aov::kNormals}}, active) == rd::AovSet{rd::Aov::kNormals});
        // Un AOV non actif n'est jamais écrit, même demandé.
        CHECK(rd::aovWrites({rd::BlendMode::kBlend, {rd::Aov::kEmission}}, active).empty());
        // Masques d'écriture du pipeline.
        std::vector<rhi::ColorTargetState> targets;
        rd::aovColorTargets(targets, rd::AovLayout(active), Format::kRGBA8Unorm, {}, 4);
        REQUIRE(targets.size() == 4);
        CHECK((targets[0].writeMask == rhi::ColorWriteMask::kAll));
        for (std::size_t i = 1; i < 4; ++i)
        {
            CHECK((targets[i].writeMask == rhi::ColorWriteMask::kNone));
        }
        // Un programme à une seule sortie n'écrit que la couleur.
        rd::aovColorTargets(targets, rd::AovLayout(active), Format::kRGBA8Unorm, active, 1);
        CHECK((targets[1].writeMask == rhi::ColorWriteMask::kNone));
    }

    TEST_CASE("REN-35: debug view names, LOD colors, frustum freeze (CPU)")
    {
        for (const rd::DebugView v : {rd::DebugView::kNone, rd::DebugView::kNormals, rd::DebugView::kAlbedo, rd::DebugView::kOverdraw,
                                      rd::DebugView::kLod, rd::DebugView::kWireframe, rd::DebugView::kMotion})
        {
            CHECK(rd::debugViewFromName(rd::debugViewName(v)) == v);
        }
        CHECK_FALSE(rd::debugViewFromName("x").has_value());
        CHECK(rd::lodDebugColor(0) != rd::lodDebugColor(1));
        CHECK(rd::lodDebugColor(0) == rd::lodDebugColor(7));
        const auto viewA = rd::CullView::fromMatrices(glm::mat4(1.0f), glm::mat4(1.0f), 100.0f);
        auto viewB = viewA;
        viewB.eye = {5, 5, 5};
        rd::FrustumFreeze freeze;
        CHECK(freeze.apply(viewB).eye == viewB.eye);
        freeze.freeze(viewA, glm::mat4(1.0f));
        CHECK(freeze.apply(viewB).eye == viewA.eye);
        freeze.unfreeze();
        CHECK(freeze.apply(viewB).eye == viewB.eye);
    }

    TEST_CASE("REN-21: with no AOV requested, the scene target has the color only (nothing extra is allocated)")
    {
        FOR_EACH_BACKEND(backend)
        {
            AovScene scene(backend);
            REQUIRE(scene.gpu.device);
            auto program = scene.library->loadGraphics(shaderDir() / "mesh.vert", shaderDir() / "mesh.frag", scene.renderer->aovShaderDefines());
            REQUIRE(program.has_value());
            auto mesh = rd::Mesh::create(*scene.gpu.device, triangle());
            REQUIRE(mesh.has_value());
            mesh->setProgram(*program);
            rd::Camera camera;
            camera.setAspect(8, 8);
            const TextureHandle out = scene.gpu.target(Format::kRGBA8Unorm, {8, 8}, "out");
            scene.frame(*mesh, camera, out);
            CHECK(scene.renderer->sceneTarget().colorCount() == 1);
            CHECK(scene.renderer->activeAovs().empty());
            CHECK(scene.renderer->aovLayout().attachmentCount() == 1);
            CHECK(scene.renderer->aovShaderDefines().empty());
            CHECK_FALSE(scene.renderer->renderGraph().findTexture("scene.motion").valid());
            CHECK_FALSE(scene.renderer->renderGraph().findTexture("scene.aov.normals").valid());
            scene.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("REN-21: normals, object id, linear depth and motion are written on demand; transparents write none (REN-41)")
    {
        FOR_EACH_BACKEND(backend)
        {
            AovScene scene(backend);
            REQUIRE(scene.gpu.device);
            REQUIRE(scene.renderer->declareAov(rd::Aov::kNormals).has_value());
            REQUIRE(scene.renderer->declareAov(rd::Aov::kObjectId).has_value());
            REQUIRE(scene.renderer->declareAov(rd::Aov::kLinearDepth).has_value());
            REQUIRE(scene.renderer->declareAov(rd::Aov::kMotion).has_value());
            const rd::AovLayout layout(scene.renderer->activeAovs());
            auto program = scene.library->loadGraphics(shaderDir() / "mesh.vert", shaderDir() / "mesh.frag", scene.renderer->aovShaderDefines());
            REQUIRE(program.has_value());
            auto mesh = rd::Mesh::create(*scene.gpu.device, triangle());
            REQUIRE(mesh.has_value());
            mesh->setProgram(*program);
            mesh->setObjectId(7);
            rd::Camera camera;
            camera.setAspect(8, 8);
            const TextureHandle out = scene.gpu.target(Format::kRGBA8Unorm, {8, 8}, "out");

            scene.frame(*mesh, camera, out);
            const rd::RenderTarget &target = scene.renderer->sceneTarget();
            REQUIRE(target.colorCount() == 5);
            const auto id = target.readPixelUInt(*layout.index(rd::Aov::kObjectId), 4, 4);
            REQUIRE(id.has_value());
            CHECK(*id == 7);
            const auto depth = scene.gpu.pixel(target.color(*layout.index(rd::Aov::kLinearDepth)), 4, 4);
            float linear = 0.0f;
            std::memcpy(&linear, depth.data(), 4);
            CHECK(linear == doctest::Approx(1.0f).epsilon(0.01));
            const auto normal = scene.gpu.pixel(target.color(*layout.index(rd::Aov::kNormals)), 4, 4);
            std::uint16_t nx = 0, ny = 0;
            std::memcpy(&nx, normal.data(), 2);
            std::memcpy(&ny, normal.data() + 2, 2);
            CHECK(halfToFloat(nx) == doctest::Approx(0.5f).epsilon(0.01)); // +Z : octEncode = (0.5, 0.5)
            CHECK(halfToFloat(ny) == doctest::Approx(0.5f).epsilon(0.01));
            auto motion = [&]()
            {
                const auto m = scene.gpu.pixel(target.color(*layout.index(rd::Aov::kMotion)), 4, 4);
                std::uint16_t mx = 0;
                std::memcpy(&mx, m.data(), 2);
                return halfToFloat(mx);
            };
            CHECK(std::abs(motion()) < 1e-4f); // première image : aucun mouvement
            scene.frame(*mesh, camera, out);
            CHECK(std::abs(motion()) < 1e-4f); // caméra et objet immobiles
            camera.setPosition({0.5f, 0.0f, 0.0f});               // la caméra se décale vers +X : la géométrie glisse vers -X à l'écran
            scene.frame(*mesh, camera, out);
            CHECK(motion() < -0.01f);
            // Hors géométrie : valeurs d'effacement (identifiant 0).
            rd::MeshData small = triangle();
            for (std::size_t v = 0; v < 3; ++v)
            {
                small.vertices[v * 9] *= 0.05f;
                small.vertices[v * 9 + 1] *= 0.05f;
            }
            auto tiny = rd::Mesh::create(*scene.gpu.device, small);
            REQUIRE(tiny.has_value());
            tiny->setProgram(*program);
            tiny->setObjectId(9);
            tiny->setAovMaterial({rd::BlendMode::kBlend, {}});
            camera.setPosition({0.0f, 0.0f, 0.0f});
            scene.frame(*tiny, camera, out);
            const auto none = scene.renderer->sceneTarget().readPixelUInt(*layout.index(rd::Aov::kObjectId), 4, 4);
            REQUIRE(none.has_value());
            CHECK(*none == 0); // transparent : n'écrit pas son identifiant
            tiny->setAovMaterial({rd::BlendMode::kBlend, {rd::Aov::kObjectId}});
            scene.frame(*tiny, camera, out);
            const auto asked = scene.renderer->sceneTarget().readPixelUInt(*layout.index(rd::Aov::kObjectId), 4, 4);
            REQUIRE(asked.has_value());
            CHECK(*asked == 9); // demandé explicitement
            scene.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("REN-21: a feature that requires an AOV adds it to the scene pass")
    {
        FOR_EACH_BACKEND(backend)
        {
            AovScene scene(backend);
            REQUIRE(scene.gpu.device);
            struct Wants final : rd::RenderFeature
            {
                std::string name() const override { return "wants.normals"; }
                rd::AovSet requiredAovs() const override { return {rd::Aov::kNormals}; }
                orosmo::core::Result<> declarePasses(rd::RenderGraphBuilder &builder) override
                {
                    seen = builder.aov(rd::Aov::kNormals).valid();
                    return {};
                }
                bool seen = false;
            };
            auto added = scene.renderer->addFeature(std::make_unique<Wants>());
            REQUIRE(added.has_value());
            auto program = scene.library->loadGraphics(shaderDir() / "mesh.vert", shaderDir() / "mesh.frag", scene.renderer->aovShaderDefines());
            REQUIRE(program.has_value());
            auto mesh = rd::Mesh::create(*scene.gpu.device, triangle());
            REQUIRE(mesh.has_value());
            mesh->setProgram(*program);
            rd::Camera camera;
            camera.setAspect(8, 8);
            scene.frame(*mesh, camera, scene.gpu.target(Format::kRGBA8Unorm, {8, 8}, "out"));
            CHECK(static_cast<Wants *>(*added)->seen);
            CHECK(scene.renderer->sceneTarget().colorCount() == 2);
            scene.renderer->removeFeature("wants.normals");
            scene.frame(*mesh, camera, scene.gpu.target(Format::kRGBA8Unorm, {8, 8}, "out2"));
            CHECK(scene.renderer->sceneTarget().colorCount() == 1); // le plugin parti, plus rien n'est alloué
            scene.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("REN-35: albedo, overdraw, wireframe and normals views render")
    {
        FOR_EACH_BACKEND(backend)
        {
            AovScene scene(backend);
            REQUIRE(scene.gpu.device);
            rd::Camera camera;
            camera.setAspect(8, 8);
            const TextureHandle out = scene.gpu.target(Format::kRGBA8Unorm, {8, 8}, "out");
            for (const rd::DebugView view : {rd::DebugView::kAlbedo, rd::DebugView::kOverdraw, rd::DebugView::kWireframe, rd::DebugView::kNormals})
            {
                INFO(rd::debugViewName(view));
                REQUIRE(scene.renderer->setDebugView(view).has_value());
                auto program = scene.library->loadGraphics(shaderDir() / "mesh.vert", shaderDir() / "mesh.frag", scene.renderer->aovShaderDefines());
                REQUIRE(program.has_value());
                rd::MeshData data = triangle();
                for (std::size_t v = 0; v < 3; ++v)
                {
                    data.vertices[v * 9 + 6] = 0.2f;
                    data.vertices[v * 9 + 7] = 0.4f;
                    data.vertices[v * 9 + 8] = 0.8f;
                }
                auto mesh = rd::Mesh::create(*scene.gpu.device, data);
                REQUIRE(mesh.has_value());
                mesh->setProgram(*program);
                scene.frame(*mesh, camera, out);
                const auto px = rgba(scene.gpu.pixel(out, 4, 4));
                if (view == rd::DebugView::kAlbedo)
                {
                    CHECK(near(px[0], 51, 2));
                    CHECK(near(px[2], 204, 2));
                }
                else if (view == rd::DebugView::kOverdraw)
                {
                    CHECK(near(px[0], 32, 2)); // une couche = 1/8
                }
                else if (view == rd::DebugView::kNormals)
                {
                    CHECK(near(px[2], 255, 2)); // +Z : bleu
                }
            }
            REQUIRE(scene.renderer->setDebugView(rd::DebugView::kNone).has_value());
            CHECK(scene.renderer->activeAovs().empty()); // sorti du mode : plus d'AOV
            scene.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("REN-21: AOVs stay correct with an HDR (RGBA16F) scene target")
    {
        FOR_EACH_BACKEND(backend)
        {
            AovScene scene(backend);
            REQUIRE(scene.gpu.device);
            REQUIRE(scene.renderer->setSceneColorFormat(Format::kRGBA16Float).has_value());
            REQUIRE(scene.renderer->declareAov(rd::Aov::kObjectId).has_value());
            const rd::AovLayout layout(scene.renderer->activeAovs());
            auto program = scene.library->loadGraphics(shaderDir() / "mesh.vert", shaderDir() / "mesh.frag", scene.renderer->aovShaderDefines());
            REQUIRE(program.has_value());
            auto mesh = rd::Mesh::create(*scene.gpu.device, triangle());
            REQUIRE(mesh.has_value());
            mesh->setProgram(*program);
            mesh->setObjectId(5);
            rd::Camera camera;
            camera.setAspect(8, 8);
            scene.frame(*mesh, camera, scene.gpu.target(Format::kRGBA8Unorm, {8, 8}, "out"));
            const rd::RenderTarget &target = scene.renderer->sceneTarget();
            CHECK((target.info().colorFormats[0] == Format::kRGBA16Float));
            const auto id = target.readPixelUInt(*layout.index(rd::Aov::kObjectId), 4, 4);
            REQUIRE(id.has_value());
            CHECK(*id == 5);
            scene.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("REN-21 + REN-04: with forward+ (depth prepass) the AOVs are written once, by the scene pass only")
    {
        FOR_EACH_BACKEND(backend)
        {
            AovScene scene(backend);
            REQUIRE(scene.gpu.device);
            auto fp = rd::enableForwardPlus(*scene.renderer, *scene.gpu.device, *scene.library, *scene.pipelines, shaderDir());
            if (!fp)
            {
                MESSAGE("forward+ unavailable on this backend: " << fp.error().message);
                continue;
            }
            REQUIRE(scene.renderer->declareAov(rd::Aov::kObjectId).has_value());
            REQUIRE(scene.renderer->declareAov(rd::Aov::kLinearDepth).has_value());
            const rd::AovLayout layout(scene.renderer->activeAovs());
            auto defines = rd::forwardPlusDefines();
            for (const auto &d : scene.renderer->aovShaderDefines())
            {
                defines.push_back(d);
            }
            auto program = scene.library->loadGraphics(shaderDir() / "mesh.vert", shaderDir() / "mesh.frag", defines);
            REQUIRE(program.has_value());
            auto mesh = rd::Mesh::create(*scene.gpu.device, triangle());
            REQUIRE(mesh.has_value());
            mesh->setProgram(*program);
            mesh->setObjectId(3);
            auto lights = rd::LightManager::create(*scene.gpu.device);
            REQUIRE(lights.has_value());
            lights->setAmbientLight({1, 1, 1}, 1.0f);
            REQUIRE(lights->upload().has_value());
            scene.renderer->setLights(&*lights);
            rd::Camera camera;
            camera.setAspect(8, 8);
            scene.frame(*mesh, camera, scene.gpu.target(Format::kRGBA8Unorm, {8, 8}, "out"));
            const auto order = scene.renderer->renderGraph().executionOrder();
            CHECK(std::find(order.begin(), order.end(), "depth-prepass") != order.end());
            const rd::RenderTarget &target = scene.renderer->sceneTarget();
            const auto id = target.readPixelUInt(*layout.index(rd::Aov::kObjectId), 4, 4);
            REQUIRE(id.has_value());
            CHECK(*id == 3);
            const auto depth = scene.gpu.pixel(target.color(*layout.index(rd::Aov::kLinearDepth)), 4, 4);
            float linear = 0.0f;
            std::memcpy(&linear, depth.data(), 4);
            CHECK(linear == doctest::Approx(1.0f).epsilon(0.01));
            scene.gpu.expectNoValidationErrors();
        }
    }
}
