// ANIM-04 : foules instanciées. Un agent de la foule (skinning dans le vertex shader, palette par instance) donne la même image que
// le même modèle skinné par le compute (ANIM-01) ; mille agents, chacun à sa pose, tiennent en un draw call par lot, sans erreur de
// validation, sur chaque backend ; le coût GPU de la passe est mesuré quand les horodatages existent.
#include <doctest/doctest.h>

#include "renderer/repo_paths.hpp"
#include "rhi/gpu_fixture.hpp"

#include <orosmo/anim/crowd.hpp>
#include <orosmo/anim/skinning.hpp>
#include <orosmo/assets/render_model.hpp>
#include <orosmo/renderer/gpu_timer.hpp>
#include <orosmo/renderer/light.hpp>
#include <orosmo/renderer/pipeline_cache.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <cstdlib>

namespace fs = std::filesystem;
namespace an = orosmo::anim;
namespace as = orosmo::assets;
namespace rd = orosmo::renderer;
using orosmo::core::Mat4;
using orosmo::core::Vec3;

namespace
{

fs::path shaderDir() { return tests::repoRoot(__FILE__) / "shaders" / "rhi"; }

constexpr float kHalfPi = 1.57079632679f;

// Même bras que skinning_tests.cpp : bande de 3 segments sur Y, os 0 = épaule (nœud 1), os 1 = coude (nœud 2, y = 1,5).
as::Model makeArm()
{
    as::Model m;
    as::MeshPrimitive prim;
    for (int y = 0; y <= 3; ++y)
    {
        for (int x = 0; x <= 1; ++x)
        {
            prim.positions.emplace_back(static_cast<float>(x) * 0.5f - 0.25f, static_cast<float>(y), 0.0f);
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
    m.nodes[2].translation = Vec3(0.0f, 1.5f, 0.0f);
    as::SkinData skin;
    skin.joints = {1, 2};
    skin.inverseBindMatrices = {Mat4(1.0f), glm::translate(Mat4(1.0f), Vec3(0.0f, -1.5f, 0.0f))};
    m.skins.push_back(skin);
    m.scenes.push_back({"s", {0, 1}});
    m.defaultScene = 0;
    return m;
}

struct CrowdScene
{
    explicit CrowdScene(Backend backend) : gpu(backend)
    {
        REQUIRE(gpu.device);
        library.emplace(*gpu.device);
        pipelines.emplace(*gpu.device);
        auto r = rd::SceneRenderer::create(*gpu.device, *library, *pipelines, {.shaderDirectory = shaderDir()});
        REQUIRE(r.has_value());
        renderer = std::move(*r);
        shaders.emplace(*library, shaderDir());
        auto d = rd::PbrDefaults::create(*gpu.device);
        REQUIRE(d.has_value());
        defaults = *d;
        textures.emplace(*gpu.device);
        auto l = rd::LightManager::create(*gpu.device);
        REQUIRE(l.has_value());
        lights.emplace(std::move(*l));
        lights->setAmbientLight(Vec3(1.0f), 1.0f);
        REQUIRE(lights->upload().has_value());
        renderer->setLights(&*lights);
    }
    ~CrowdScene() { renderer.reset(); }

    as::RenderModel model(const as::Model &m, bool skinnable, const char *key)
    {
        as::RenderModelOptions options;
        options.sourceKey = key;
        options.skinnable = skinnable;
        auto r = as::RenderModel::create(*gpu.device, m, *textures, *shaders, defaults, options);
        INFO((r ? std::string() : r.error().message));
        REQUIRE(r.has_value());
        return std::move(*r);
    }

    Gpu gpu;
    std::optional<rd::ShaderLibrary> library;
    std::optional<rd::PipelineCache> pipelines;
    std::unique_ptr<rd::SceneRenderer> renderer;
    std::optional<rd::PbrShaderCache> shaders;
    std::shared_ptr<rd::PbrDefaults> defaults;
    std::optional<as::TextureCache> textures;
    std::optional<rd::LightManager> lights;
};

an::Pose bent(const as::Model &model, float angle)
{
    an::Pose pose = an::restPose(model);
    pose.nodes[2].rotation = glm::angleAxis(angle, Vec3(0, 0, 1));
    return pose;
}

} // namespace

TEST_SUITE("anim/crowd")
{
    TEST_CASE("palette of an agent; wrong sizes are refused; a model without skinned primitive is refused")
    {
        FOR_EACH_BACKEND(backend)
        {
            CrowdScene s(backend);
            const as::Model model = makeArm();
            as::RenderModel render = s.model(model, false, "arm");
            auto crowd = an::CrowdRenderer::create(*s.gpu.device, *s.library, *s.pipelines, shaderDir(), model, render);
            INFO((crowd ? std::string() : crowd.error().message));
            REQUIRE(crowd.has_value());
            CHECK((*crowd)->jointCount() == 2);
            CHECK((*crowd)->batchCount() == 1);

            const an::Pose pose = bent(model, kHalfPi);
            std::vector<Mat4> palette(2);
            REQUIRE((*crowd)->agentPalette(pose, palette).has_value());
            auto world = an::worldMatrices(an::nodeParents(model), pose);
            REQUIRE(world.has_value());
            const std::vector<Mat4> expected = an::jointPalette(model.skins[0], *world, -1);
            for (int j = 0; j < 2; ++j)
            {
                for (int c = 0; c < 4; ++c)
                {
                    for (int r = 0; r < 4; ++r)
                    {
                        CHECK(palette[static_cast<std::size_t>(j)][c][r] == doctest::Approx(expected[static_cast<std::size_t>(j)][c][r]));
                    }
                }
            }
            std::vector<Mat4> one(1);
            CHECK(((*crowd)->agentPalette(pose, one).error().code == orosmo::core::ErrorCode::kInvalidArgument));
            const std::vector<Mat4> worlds(2, Mat4(1.0f));
            CHECK(((*crowd)->setAgentPalettes(worlds, palette).error().code == orosmo::core::ErrorCode::kInvalidArgument));
            CHECK(((*crowd)->setAgents(worlds, std::vector<an::Pose>(1, pose)).error().code == orosmo::core::ErrorCode::kInvalidArgument));

            as::Model rigid = makeArm();
            rigid.nodes[0].skin = -1;
            as::RenderModel rigidRender = s.model(rigid, false, "rigid");
            auto none = an::CrowdRenderer::create(*s.gpu.device, *s.library, *s.pipelines, shaderDir(), rigid, rigidRender);
            CHECK((none.error().code == orosmo::core::ErrorCode::kInvalidArgument));
            s.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("one crowd agent draws the same image as the compute-skinned model at the same pose and place")
    {
        FOR_EACH_BACKEND(backend)
        {
            CrowdScene s(backend);
            if (!an::SkinnedModel::supportedBy(s.gpu->capabilities()))
            {
                MESSAGE("Skipped: no compute skinning on ", tests::backendName(backend));
                continue;
            }
            const as::Model model = makeArm();
            const an::Pose pose = bent(model, kHalfPi);
            const Mat4 place = glm::translate(Mat4(1.0f), Vec3(0.5f, -0.25f, 0.0f));

            rd::Camera camera;
            camera.setAspect(16, 16);
            camera.setPosition({-0.25f, 1.0f, 3.0f});
            camera.setOrientation({0.0f, 0.0f, -1.0f});
            camera.setFarPlane(100.0f);
            s.renderer->setOutputResolution(16, 16);

            // Référence : skinning en compute (ANIM-01), modèle placé par sa racine.
            as::RenderModel skinRender = s.model(model, true, "skinned");
            skinRender.setRootTransform(place);
            auto skinned = an::SkinnedModel::create(*s.gpu.device, *s.library, *s.pipelines, shaderDir(), model, skinRender);
            REQUIRE(skinned.has_value());
            REQUIRE(skinned->setPose(pose).has_value());
            const TextureHandle reference = s.gpu.target(Format::kRGBA8Unorm, {16, 16}, "reference");
            s.gpu.run(
                [&](CommandEncoder &e)
                {
                    REQUIRE(skinned->dispatch(e).has_value());
                    REQUIRE(s.renderer->beginFrame(e, {.clearColor = {0, 0, 0, 1}}).has_value());
                    REQUIRE(s.renderer->setCamera(camera).has_value());
                    REQUIRE(skinRender.draw(*s.renderer, camera).has_value());
                    REQUIRE(s.renderer->endFrame({reference, Format::kRGBA8Unorm, {16, 16}}).has_value());
                });

            // Foule d'un agent : même pose, même place.
            as::RenderModel crowdRender = s.model(model, false, "crowd");
            auto crowd = an::CrowdRenderer::create(*s.gpu.device, *s.library, *s.pipelines, shaderDir(), model, crowdRender);
            REQUIRE(crowd.has_value());
            REQUIRE((*crowd)->setAgents(std::vector<Mat4>{place}, std::vector<an::Pose>{pose}).has_value());
            const TextureHandle out = s.gpu.target(Format::kRGBA8Unorm, {16, 16}, "crowd");
            an::CrowdDrawStats stats;
            s.gpu.run(
                [&](CommandEncoder &e)
                {
                    REQUIRE(s.renderer->beginFrame(e, {.clearColor = {0, 0, 0, 1}}).has_value());
                    REQUIRE(s.renderer->setCamera(camera).has_value());
                    auto drawn = (*crowd)->draw(*s.renderer);
                    INFO((drawn ? std::string() : drawn.error().message));
                    REQUIRE(drawn.has_value());
                    stats = *drawn;
                    REQUIRE(s.renderer->endFrame({out, Format::kRGBA8Unorm, {16, 16}}).has_value());
                });
            CHECK(stats.drawCalls == 1);
            CHECK(stats.instances == 1);

            auto a = s.gpu->readTexture(reference);
            auto b = s.gpu->readTexture(out);
            REQUIRE(a.has_value());
            REQUIRE(b.has_value());
            REQUIRE(a->size() == b->size());
            int lit = 0;
            int maxDiff = 0;
            for (std::size_t i = 0; i < a->size(); i += 4)
            {
                lit += static_cast<int>((*a)[i]) > 0 ? 1 : 0;
                for (std::size_t c = 0; c < 3; ++c)
                {
                    maxDiff = std::max(maxDiff, std::abs(static_cast<int>((*a)[i + c]) - static_cast<int>((*b)[i + c])));
                }
            }
            CHECK(lit > 4);      // le bras est bien dans l'image
            CHECK(maxDiff <= 2); // arrondis du rendu seulement
            s.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("a thousand agents, each at its own pose, draw in one call per batch; GPU cost measured")
    {
        FOR_EACH_BACKEND(backend)
        {
            CrowdScene s(backend);
            const as::Model model = makeArm();
            as::RenderModel render = s.model(model, false, "arm");
            auto crowd = an::CrowdRenderer::create(*s.gpu.device, *s.library, *s.pipelines, shaderDir(), model, render);
            REQUIRE(crowd.has_value());

            constexpr int kSide = 40; // 40 × 25 = 1000 agents sur une grille
            std::vector<Mat4> worlds;
            std::vector<an::Pose> poses;
            for (int i = 0; i < 1000; ++i)
            {
                const float x = static_cast<float>(i % kSide) - kSide * 0.5f;
                const float z = -static_cast<float>(i / kSide) * 1.5f;
                worlds.push_back(glm::translate(Mat4(1.0f), Vec3(x, 0.0f, z)));
                poses.push_back(bent(model, kHalfPi * static_cast<float>(i % 7) / 6.0f));
            }
            REQUIRE((*crowd)->setAgents(worlds, poses).has_value());
            CHECK((*crowd)->agentCount() == 1000);

            rd::Camera camera;
            camera.setAspect(64, 64);
            camera.setPosition({0.0f, 12.0f, 20.0f});
            camera.setOrientation(glm::normalize(Vec3(0.0f, -0.5f, -1.0f)));
            camera.setFarPlane(200.0f);
            s.renderer->setOutputResolution(64, 64);
            const TextureHandle out = s.gpu.target(Format::kRGBA8Unorm, {64, 64}, "crowd");
            const bool timed = s.gpu->capabilities().timestampQueries;
            std::optional<rd::GpuTimer> timer;
            if (timed)
            {
                timer.emplace(*s.gpu.device, "crowd");
            }
            an::CrowdDrawStats stats;
            s.gpu.run(
                [&](CommandEncoder &e)
                {
                    if (timer)
                    {
                        timer->begin(e);
                    }
                    REQUIRE(s.renderer->beginFrame(e, {.clearColor = {0, 0, 0, 1}}).has_value());
                    REQUIRE(s.renderer->setCamera(camera).has_value());
                    auto drawn = (*crowd)->draw(*s.renderer);
                    REQUIRE(drawn.has_value());
                    stats = *drawn;
                    REQUIRE(s.renderer->endFrame({out, Format::kRGBA8Unorm, {64, 64}}).has_value());
                    if (timer)
                    {
                        timer->end(e);
                    }
                });
            CHECK(stats.drawCalls == 1);
            CHECK(stats.instances == 1000);
            CHECK(stats.paletteBytes == 1000u * 2u * sizeof(Mat4));

            auto pixels = s.gpu->readTexture(out);
            REQUIRE(pixels.has_value());
            int lit = 0;
            for (std::size_t i = 0; i < pixels->size(); i += 4)
            {
                lit += static_cast<int>((*pixels)[i]) > 0 ? 1 : 0;
            }
            CHECK(lit > 200); // la grille d'agents couvre une bonne part de l'image
            if (timer)
            {
                s.gpu->waitIdle();
                const auto elapsed = timer->elapsed();
                REQUIRE(elapsed.has_value());
                MESSAGE(std::string(tests::backendName(backend)), ": 1000 agents (", stats.drawCalls, " draw call), GPU frame ",
                        static_cast<double>(elapsed->count()) / 1e6, " ms");
            }
            s.gpu.expectNoValidationErrors();
        }
    }
}
