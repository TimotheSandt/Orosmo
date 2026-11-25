// --scene anim de la démo (M6, lot H) : le personnage généré passe par l'aller-retour glTF (saveGltf puis loadGltf), est joué par
// la machine à états et déformé en compute ; mille agents tiennent en un draw call par primitive skinnée ; réécrire la texture sur
// le disque la recharge (reloadChanged == 1). Sans erreur de validation, sur chaque backend.
// Mesure (ignorée par défaut, `--no-skip`) : coût GPU du skinning et de la foule pour un modèle d'environ 10 000 sommets.
#include <doctest/doctest.h>

#include "renderer/repo_paths.hpp"
#include "rhi/gpu_fixture.hpp"
#include "support/temp_dir.hpp"

#include "Scene/anim_scene.hpp"

#include <orosmo/renderer/gpu_timer.hpp>
#include <orosmo/renderer/light.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
namespace an = orosmo::anim;
namespace as = orosmo::assets;
namespace rd = orosmo::renderer;

namespace
{

fs::path shaderDir() { return tests::repoRoot(__FILE__) / "shaders" / "rhi"; }

void bumpTime(const fs::path &file, int seconds) { fs::last_write_time(file, fs::last_write_time(file) + std::chrono::seconds(seconds)); }

// Primitives des maillages portés par un nœud skinné : les lots de la foule.
std::size_t skinnedPrimitives(const as::Model &model)
{
    std::size_t count = 0;
    for (const as::NodeData &node : model.nodes)
    {
        if (node.mesh >= 0 && node.skin >= 0)
        {
            count += model.meshes[static_cast<std::size_t>(node.mesh)].primitives.size();
        }
    }
    return count;
}

struct AnimFixture
{
    explicit AnimFixture(Backend backend) : gpu(backend)
    {
        REQUIRE(gpu.device);
        library.emplace(*gpu.device);
        pipelines.emplace(*gpu.device);
        auto r = rd::SceneRenderer::create(*gpu.device, *library, *pipelines, {.shaderDirectory = shaderDir()});
        REQUIRE(r.has_value());
        renderer = std::move(*r);
        auto l = rd::LightManager::create(*gpu.device);
        REQUIRE(l.has_value());
        lights.emplace(std::move(*l));
        lights->setAmbientLight(glm::vec3(1.0f), 0.6f);
        REQUIRE(lights->upload().has_value());
        renderer->setLights(&*lights);
        camera.setPosition({0.0f, 6.0f, 9.0f});
        camera.setOrientation(glm::normalize(glm::vec3(0.0f, -0.5f, -1.0f)));
        camera.setFarPlane(100.0f);
    }
    ~AnimFixture()
    {
        if (gpu.device)
        {
            gpu->waitIdle();
        }
        scene.destroy();
        renderer.reset();
    }

    // Une image : skinning (compute), puis personnage et foule dans la passe de scène.
    void frame(Extent2D size, TextureHandle out, rd::GpuTimer *skinTimer = nullptr, rd::GpuTimer *frameTimer = nullptr)
    {
        camera.setAspect(size.width, size.height);
        renderer->setOutputResolution(static_cast<int>(size.width), static_cast<int>(size.height));
        gpu.run(
            [&](CommandEncoder &e)
            {
                if (frameTimer != nullptr)
                {
                    frameTimer->begin(e);
                }
                if (skinTimer != nullptr)
                {
                    skinTimer->begin(e);
                }
                auto dispatched = scene.dispatch(e);
                INFO((dispatched ? std::string() : dispatched.error().message));
                REQUIRE(dispatched.has_value());
                if (skinTimer != nullptr)
                {
                    skinTimer->end(e);
                }
                REQUIRE(renderer->beginFrame(e, {.clearColor = {0, 0, 0, 1}}).has_value());
                REQUIRE(renderer->setCamera(camera).has_value());
                auto drawn = scene.render(*renderer, camera);
                INFO((drawn ? std::string() : drawn.error().message));
                REQUIRE(drawn.has_value());
                REQUIRE(renderer->endFrame({out, Format::kRGBA8Unorm, size}).has_value());
                if (frameTimer != nullptr)
                {
                    frameTimer->end(e);
                }
            });
    }

    Gpu gpu;
    std::optional<rd::ShaderLibrary> library;
    std::optional<rd::PipelineCache> pipelines;
    std::unique_ptr<rd::SceneRenderer> renderer;
    std::optional<rd::LightManager> lights;
    rd::Camera camera;
    demo::AnimScene scene;
};

demo::AnimSceneOptions sceneOptions(const fs::path &dir, std::uint32_t agents = 1000)
{
    demo::AnimSceneOptions o;
    o.workDirectory = dir;
    o.agents = agents;
    o.heroPosition = {0.0f, 0.0f, 3.0f};
    o.crowdCenter = {0.0f, 0.0f, -2.0f};
    return o;
}

} // namespace

TEST_SUITE("apps/demo-anim")
{
    TEST_CASE("the generated figure is valid, skinned, animated, and survives the glTF round trip")
    {
        tests::TempDir dir("engine_anim_figure");
        const demo::AnimFigureDesc desc;
        const as::Model model = demo::makeAnimatedFigure(desc, dir.path / "albedo.png");
        REQUIRE(model.validate().has_value());
        CHECK(model.stats().vertices == demo::animatedFigureVertexCount(desc));
        CHECK(model.skins.size() == 1);
        CHECK(model.skins[0].joints.size() == desc.joints);
        CHECK(model.animations.size() == 2);
        CHECK(skinnedPrimitives(model) == 2);

        REQUIRE(as::saveGltf(model, dir.path / "figure.gltf").has_value());
        auto loaded = as::loadGltf(dir.path / "figure.gltf");
        INFO((loaded ? std::string() : loaded.error().message));
        REQUIRE(loaded.has_value());
        CHECK(loaded->stats().vertices == model.stats().vertices);
        CHECK(loaded->skins.size() == 1);
        CHECK(loaded->animations.size() == 2);
        REQUIRE(loaded->images.size() == 1);
        CHECK(fs::equivalent(loaded->images[0].path.parent_path(), dir.path)); // image externe, chemin rendu absolu
        CHECK(loaded->animations[0].duration == doctest::Approx(model.animations[0].duration));

        // Une machine à états sur les clips relus change bien la pose.
        auto clip = an::Clip::fromModel(*loaded, 0);
        REQUIRE(clip.has_value());
        an::Pose pose = an::restPose(*loaded);
        REQUIRE(clip->sample(0.25f, pose).has_value());
        CHECK_FALSE((pose.nodes[2].rotation == an::restPose(*loaded).nodes[2].rotation));
    }

    TEST_CASE("anim scene: model imported from glTF, 1000 agents in one draw call per skinned primitive, texture reloaded live")
    {
        FOR_EACH_BACKEND(backend)
        {
            AnimFixture f(backend);
            if (!an::CrowdRenderer::supportedBy(f.gpu->capabilities()))
            {
                MESSAGE("Skipped: no crowd on ", tests::backendName(backend));
                continue;
            }
            tests::TempDir dir("engine_anim_scene");
            auto ready = f.scene.init(*f.gpu.device, *f.library, *f.pipelines, shaderDir(), sceneOptions(dir.path));
            INFO((ready ? std::string() : ready.error().message));
            REQUIRE(ready.has_value());

            // Le modèle dessiné est celui relu du glTF écrit dans le dossier de travail.
            CHECK(fs::exists(f.scene.modelPath()));
            CHECK(f.scene.modelPath().extension() == ".gltf");
            CHECK(fs::exists(f.scene.texturePath()));
            CHECK(f.scene.model().skins.size() == 1);
            CHECK(f.scene.model().animations.size() == 2);
            CHECK(f.scene.heroSkinned() == an::SkinnedModel::supportedBy(f.gpu->capabilities()));
            REQUIRE(f.scene.stateMachine() != nullptr);
            CHECK(f.scene.stateMachine()->current() == "sway");

            const std::size_t batches = skinnedPrimitives(f.scene.model());
            CHECK(f.scene.agentCount() == 1000);
            CHECK(f.scene.crowdBatchCount() == batches);

            constexpr Extent2D kSize{96, 64};
            const TextureHandle out = f.gpu.target(Format::kRGBA8Unorm, kSize, "anim");
            REQUIRE(f.scene.update(1.0f / 60.0f).has_value());
            f.frame(kSize, out);
            CHECK(f.scene.crowdStats().drawCalls == batches);
            CHECK(f.scene.crowdStats().instances == 1000 * batches);
            CHECK(f.scene.heroDraws() == 2);

            auto before = f.gpu->readTexture(out);
            REQUIRE(before.has_value());
            int lit = 0;
            for (std::size_t i = 0; i < before->size(); i += 4)
            {
                lit += static_cast<int>((*before)[i]) + static_cast<int>((*before)[i + 1]) + static_cast<int>((*before)[i + 2]) > 0 ? 1 : 0;
            }
            CHECK(lit > 200); // la foule et le personnage couvrent une bonne part de l'image

            // Après 3 s, la machine passe à « wave » (fondu de 0,5 s).
            for (int i = 0; i < 4 * 60; ++i)
            {
                REQUIRE(f.scene.update(1.0f / 60.0f).has_value());
            }
            CHECK(f.scene.stateMachine()->current() == "wave");

            // Rechargement à chaud : rien n'a changé, puis la texture est réécrite (même taille, autres couleurs).
            CHECK(f.scene.reloadChanged() == 0);
            REQUIRE(demo::writeFigureTexture(f.scene.texturePath(), 1).has_value());
            bumpTime(f.scene.texturePath(), 2);
            CHECK(f.scene.reloadChanged() == 1);
            CHECK(f.scene.reloadChanged() == 0);
            f.frame(kSize, out);
            auto after = f.gpu->readTexture(out);
            REQUIRE(after.has_value());
            CHECK((*after != *before)); // nouvelles couleurs (et nouvelle pose)
            CHECK(f.scene.crowdStats().drawCalls == batches);
            f.gpu.expectNoValidationErrors();
        }
    }

    // Mesure manuelle (docs/PERF_OPENGL_VS_VULKAN.md, « M6 : skinning et foules ») : `bin/tests/tests -ts=apps/demo-anim --no-skip`.
    TEST_CASE("anim scene GPU cost: 10k-vertex figure, compute skinning and 1000-agent crowd" * doctest::skip())
    {
        FOR_EACH_BACKEND(backend)
        {
            AnimFixture f(backend);
            if (!f.gpu->capabilities().timestampQueries || !an::CrowdRenderer::supportedBy(f.gpu->capabilities()) ||
                !an::SkinnedModel::supportedBy(f.gpu->capabilities()))
            {
                MESSAGE("Skipped: no timestamps, compute skinning or crowd on ", tests::backendName(backend));
                continue;
            }
            tests::TempDir dir("engine_anim_perf");
            demo::AnimSceneOptions options = sceneOptions(dir.path);
            options.figure.sides = 49;  // 50 sommets par anneau (couture)
            options.figure.rings = 199; // 200 anneaux : 10 000 sommets de tronc, + la coiffe
            options.figure.joints = 8;
            const std::size_t vertices = demo::animatedFigureVertexCount(options.figure);

            constexpr Extent2D kSize{1280, 720};
            const TextureHandle out = f.gpu.target(Format::kRGBA8Unorm, kSize, "perf");
            constexpr int kRuns = 30;
            const auto median = [](std::vector<double> v)
            {
                std::sort(v.begin(), v.end());
                return v[v.size() / 2];
            };
            // Médianes (skinning en compute, image entière) pour `agents` agents.
            const auto measure = [&](std::uint32_t agents)
            {
                f.scene.destroy();
                options.agents = agents;
                REQUIRE(f.scene.init(*f.gpu.device, *f.library, *f.pipelines, shaderDir(), options).has_value());
                std::vector<double> skin;
                std::vector<double> frame;
                rd::GpuTimer skinTimer(*f.gpu.device, "skin");
                rd::GpuTimer frameTimer(*f.gpu.device, "frame");
                for (int i = 0; i < kRuns + 3; ++i)
                {
                    REQUIRE(f.scene.update(1.0f / 60.0f).has_value());
                    skinTimer.reset();
                    frameTimer.reset();
                    f.frame(kSize, out, &skinTimer, &frameTimer);
                    f.gpu->waitIdle();
                    const auto s = skinTimer.elapsed();
                    const auto fr = frameTimer.elapsed();
                    REQUIRE(s.has_value());
                    REQUIRE(fr.has_value());
                    if (i >= 3) // 3 images de chauffe (pipelines, premiers envois)
                    {
                        skin.push_back(static_cast<double>(s->count()) / 1e6);
                        frame.push_back(static_cast<double>(fr->count()) / 1e6);
                    }
                }
                return std::pair{median(skin), median(frame)};
            };
            const auto [skinMs, heroFrameMs] = measure(0);
            const auto [skinMs2, crowdFrameMs] = measure(1000);
            MESSAGE(std::string(tests::backendName(backend)), ": figure ", vertices, " vertices, ", options.figure.joints, " joints ; compute skinning (1 figure) ",
                    std::min(skinMs, skinMs2), " ms ; frame hero only ", heroFrameMs, " ms ; frame hero + 1000 agents (", f.scene.crowdStats().instances,
                    " instances, ", f.scene.crowdStats().drawCalls, " draw calls, ", vertices * 1000, " vertices skinned in the vertex shader) ", crowdFrameMs,
                    " ms ; GPU at 1280x720, median of ", kRuns);
            f.gpu.expectNoValidationErrors();
        }
    }
}
