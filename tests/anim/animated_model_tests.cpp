// A75.2 et A75.11 (M8-L12a) : API des contenus. Un modèle glTF animé se charge et se joue en cinq lignes
// (`AnimatedModel::create`, `play`, `update`, `dispatch`, `draw`) ; le repli CPU (`skinVertices`) donne les mêmes sommets que le
// skinning en compute à 1e-4 près ; le lecteur (`play`, `crossFade`) donne la même pose que `StateMachine` ; une foule se crée
// depuis le modèle (`CrowdRenderer::create(context, model)`) et joue ses clips par indice et temps, avec la même image que la foule
// construite à la main. Sans erreur de validation, sur chaque backend.
#include <doctest/doctest.h>

#include "renderer/repo_paths.hpp"
#include "rhi/gpu_fixture.hpp"
#include "support/temp_dir.hpp"

#include <orosmo/anim/animated_model.hpp>
#include <orosmo/anim/crowd.hpp>
#include <orosmo/anim/state_machine.hpp>
#include <orosmo/assets/model_loader.hpp>
#include <orosmo/renderer/light.hpp>
#include <orosmo/renderer/render_context.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace an = orosmo::anim;
namespace as = orosmo::assets;
namespace rd = orosmo::renderer;
using orosmo::core::ErrorCode;
using orosmo::core::Vec3;

namespace
{

fs::path shaderDir() { return tests::repoRoot(__FILE__) / "shaders" / "rhi"; }

constexpr float kHalfPi = 1.57079632679f;

// Clip d'une seconde qui tourne le nœud `node` de 0 à `angle` autour de `axis` (5 clés linéaires).
as::AnimationData bendClip(const char *name, int node, const Vec3 &axis, float angle)
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
        const glm::quat q = glm::angleAxis(angle * std::sin(t * kHalfPi), axis);
        ch.times.push_back(t);
        ch.values.insert(ch.values.end(), {q.x, q.y, q.z, q.w});
    }
    anim.channels.push_back(std::move(ch));
    return anim;
}

// Bras à deux os (comme tests/anim/skinning_tests.cpp) : bande de 3 segments sur Y, épaule (nœud 1) et coude (nœud 2) ; deux clips.
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
    skin.inverseBindMatrices = {orosmo::core::Mat4(1.0f), glm::translate(orosmo::core::Mat4(1.0f), Vec3(0.0f, -1.5f, 0.0f))};
    m.skins.push_back(skin);
    m.animations.push_back(bendClip("bend", 2, Vec3(0, 0, 1), kHalfPi));
    m.animations.push_back(bendClip("twist", 1, Vec3(0, 1, 0), 2.0f * kHalfPi));
    m.scenes.push_back({"s", {0, 1}});
    m.defaultScene = 0;
    return m;
}

fs::path writeArm(const fs::path &dir)
{
    const fs::path path = dir / "arm.gltf";
    REQUIRE(as::saveGltf(makeArm(), path).has_value());
    return path;
}

std::vector<float> readVertices(Device &device, BufferHandle buffer, std::size_t floats)
{
    auto bytes = device.readBuffer(buffer, 0, floats * sizeof(float));
    REQUIRE(bytes.has_value());
    std::vector<float> out(floats);
    std::memcpy(out.data(), bytes->data(), bytes->size());
    return out;
}

std::vector<std::byte> readImage(Device &device, TextureHandle texture, Extent2D size)
{
    TextureCopy c;
    c.size = {size.width, size.height, 1};
    auto bytes = device.readTexture(texture, c);
    REQUIRE(bytes.has_value());
    return *bytes;
}

int litPixels(const std::vector<std::byte> &image)
{
    int lit = 0;
    for (std::size_t i = 0; i + 3 < image.size(); i += 4)
    {
        lit += static_cast<std::uint8_t>(image[i]) > 0 ? 1 : 0;
    }
    return lit;
}

// Scène minimale : contexte (qui possède bibliothèque et cache), renderer, lumière ambiante, caméra devant le bras.
struct Stage
{
    explicit Stage(Backend backend) : gpu(backend)
    {
        REQUIRE(gpu.device);
        context = rd::RenderContext::create(*gpu.device, shaderDir());
        auto r = rd::SceneRenderer::create(*context);
        REQUIRE(r.has_value());
        renderer = std::move(*r);
        auto l = rd::LightManager::create(*gpu.device);
        REQUIRE(l.has_value());
        lights.emplace(std::move(*l));
        lights->setAmbientLight(Vec3(1.0f), 1.0f);
        REQUIRE(lights->upload().has_value());
        renderer->setLights(&*lights);
        camera.setAspect(kSize.width, kSize.height);
        camera.setPosition({0.0f, 1.5f, 6.0f});
        camera.setOrientation({0.0f, 0.0f, -1.0f});
        camera.setFarPlane(100.0f);
        renderer->setOutputResolution(static_cast<int>(kSize.width), static_cast<int>(kSize.height));
        out = gpu.target(Format::kRGBA8Unorm, kSize, "output");
    }
    ~Stage()
    {
        if (gpu.device)
        {
            gpu->waitIdle();
        }
    }

    // `before` : hors passe (skinning en compute), puis `draw` dans la passe de scène.
    template <typename B, typename F> std::vector<std::byte> frame(B &&before, F &&draw)
    {
        gpu.run(
            [&](CommandEncoder &e)
            {
                before(e);
                REQUIRE(renderer->beginFrame(e, {.clearColor = {0, 0, 0, 1}}).has_value());
                REQUIRE(renderer->setCamera(camera).has_value());
                draw();
                REQUIRE(renderer->endFrame({out, Format::kRGBA8Unorm, kSize}).has_value());
            });
        return readImage(*gpu.device, out, kSize);
    }

    static constexpr Extent2D kSize{32, 32};
    Gpu gpu;
    std::unique_ptr<rd::RenderContext> context;
    std::unique_ptr<rd::SceneRenderer> renderer;
    std::optional<rd::LightManager> lights;
    rd::Camera camera;
    TextureHandle out;
};

} // namespace

TEST_SUITE("anim/animated-model")
{
    TEST_CASE("ModelLoader: a missing file is an error naming it; a loaded model becomes a RenderModel")
    {
        tests::TempDir dir("animated_model");
        // Un seul backend suffit (logique CPU, le périphérique ne sert qu'aux ressources) ; sans périphérique, le cas est sauté
        // comme ceux de FOR_EACH_BACKEND (CI sans affichage).
        const std::vector<Backend> backends = tests::backendsToRun();
        if (backends.empty())
        {
            MESSAGE("no GPU device: skipped");
            return;
        }
        Gpu gpu(backends.front());
        REQUIRE(gpu.device);
        auto context = rd::RenderContext::create(*gpu.device, shaderDir());
        auto loader = as::ModelLoader::create(*context);
        REQUIRE(loader.has_value());
        auto missing = loader->load(dir.path / "absent.gltf");
        REQUIRE_FALSE(missing.has_value());
        CHECK(missing.error().message.find("absent.gltf") != std::string::npos);
        auto model = loader->load(writeArm(dir.path));
        REQUIRE(model.has_value());
        auto render = loader->createRenderModel(*model);
        INFO((render ? std::string() : render.error().message));
        REQUIRE(render.has_value());
        CHECK(render->drawCount() == 1);
        CHECK(loader->reloadChanged() == 0);
    }

    TEST_CASE("an animated glTF model is loaded and played in five lines")
    {
        FOR_EACH_BACKEND(backend)
        {
            tests::TempDir dir("animated_model");
            const fs::path path = writeArm(dir.path);
            Stage s(backend);

            // Les cinq lignes d'A75.2 (le contexte et la scène existent déjà).
            auto arm = an::AnimatedModel::create(*s.context, path);
            REQUIRE(arm.has_value());
            REQUIRE((*arm)->play("bend").has_value());
            REQUIRE((*arm)->update(0.5f).has_value());
            const auto image = s.frame([&](CommandEncoder &e) { REQUIRE((*arm)->dispatch(e).has_value()); },
                                       [&] { REQUIRE((*arm)->draw(*s.renderer, s.camera).has_value()); });

            CHECK((*arm)->current() == "bend");
            CHECK((*arm)->currentTime() == doctest::Approx(0.5f));
            CHECK((*arm)->clips().size() == 2);
            CHECK((*arm)->skinnedVertexCount() == 8);
            CHECK((*arm)->skinningPath() == (an::SkinnedModel::supportedBy(s.gpu->capabilities()) ? an::SkinningPath::kGpu : an::SkinningPath::kCpu));
            CHECK_FALSE(((*arm)->pose().nodes[2].rotation == an::restPose((*arm)->model()).nodes[2].rotation));
            CHECK(litPixels(image) > 0);
            CHECK((((*arm)->play("absent").error().code) == ErrorCode::kNotFound));
            CHECK((((*arm)->crossFade("absent", 0.2f).error().code) == ErrorCode::kNotFound));
            CHECK(an::AnimatedModel::create(*s.context, dir.path / "absent.gltf").error().message.find("absent.gltf") != std::string::npos);
            CHECK(s.gpu->validationErrorCount() == 0);
        }
    }

    TEST_CASE("the CPU fallback deforms the vertices like the compute skinning, to 1e-4")
    {
        FOR_EACH_BACKEND(backend)
        {
            tests::TempDir dir("animated_model");
            const fs::path path = writeArm(dir.path);
            Stage s(backend);
            auto gpuArm = an::AnimatedModel::create(*s.context, path);
            REQUIRE(gpuArm.has_value());
            if ((*gpuArm)->skinningPath() != an::SkinningPath::kGpu)
            {
                MESSAGE("no compute skinning on this device: GPU/CPU comparison skipped");
                continue;
            }
            auto cpuArm = an::AnimatedModel::create(*s.context, (*gpuArm)->loader(), path, {.forceCpuSkinning = true});
            REQUIRE(cpuArm.has_value());
            CHECK((*cpuArm)->skinningPath() == an::SkinningPath::kCpu);
            for (an::AnimatedModel *arm : {gpuArm->get(), cpuArm->get()})
            {
                REQUIRE(arm->play("bend").has_value());
                REQUIRE(arm->update(0.3f).has_value());
                REQUIRE(arm->crossFade("twist", 0.5f).has_value());
                REQUIRE(arm->update(0.2f).has_value()); // au milieu du fondu : deux clips mélangés
                REQUIRE(arm->inTransition());
            }
            s.gpu.run(
                [&](CommandEncoder &e)
                {
                    REQUIRE((*gpuArm)->dispatch(e).has_value());
                    REQUIRE((*cpuArm)->dispatch(e).has_value()); // rien à enregistrer en repli CPU
                });
            const std::size_t floats = 8 * rd::kPbrVertexFloats;
            const std::vector<float> gpu = readVertices(*s.gpu.device, (*gpuArm)->renderModel().mesh(0).vertexBuffer(), floats);
            const std::vector<float> cpu = readVertices(*s.gpu.device, (*cpuArm)->renderModel().mesh(0).vertexBuffer(), floats);
            const std::vector<float> rest = as::pbrVertices(makeArm().meshes[0].primitives[0]);
            bool moved = false;
            for (std::size_t i = 0; i < floats; ++i)
            {
                INFO("float ", i);
                CHECK(std::abs(cpu[i] - gpu[i]) <= 1e-4f);
                moved = moved || std::abs(cpu[i] - rest[i]) > 1e-3f;
            }
            CHECK(moved);
            CHECK(s.gpu->validationErrorCount() == 0);
        }
    }

    TEST_CASE("play and crossFade give the same poses as a StateMachine with the same transitions")
    {
        tests::TempDir dir("animated_model");
        const fs::path path = writeArm(dir.path);
        // Un seul backend suffit (logique CPU, le périphérique ne sert qu'aux ressources) ; sans périphérique, le cas est sauté
        // comme ceux de FOR_EACH_BACKEND (CI sans affichage).
        const std::vector<Backend> backends = tests::backendsToRun();
        if (backends.empty())
        {
            MESSAGE("no GPU device: skipped");
            return;
        }
        Gpu gpu(backends.front());
        REQUIRE(gpu.device);
        auto context = rd::RenderContext::create(*gpu.device, shaderDir());
        auto arm = an::AnimatedModel::create(*context, path);
        REQUIRE(arm.has_value());
        CHECK((*arm)->current().empty());
        REQUIRE((*arm)->update(0.1f).has_value()); // sans clip : pose de repos

        const as::Model &model = (*arm)->model();
        const auto &clips = (*arm)->clips();
        an::StateMachine machine(an::restPose(model));
        REQUIRE(machine.addState({"bend", &clips[0], an::LoopMode::kLoop, 1.0f}).has_value());
        REQUIRE(machine.addState({"twist", &clips[1], an::LoopMode::kLoop, 1.5f}).has_value());
        REQUIRE(machine.addTransition({.from = "bend", .to = "twist", .duration = 0.4f, .condition = "go"}).has_value());
        REQUIRE(machine.start("bend").has_value());
        REQUIRE((*arm)->play("bend").has_value());
        // La machine part au `update` qui suit le déclencheur, après avoir avancé : le lecteur fait `update` puis `crossFade`.
        for (int frame = 0; frame < 60; ++frame)
        {
            const float dt = 1.0f / 30.0f;
            if (frame == 20)
            {
                machine.setTrigger("go");
            }
            REQUIRE(machine.update(dt).has_value());
            REQUIRE((*arm)->update(dt).has_value());
            if (frame == 20)
            {
                REQUIRE((*arm)->crossFade("twist", 0.4f, {.speed = 1.5f}).has_value());
            }
            INFO("frame ", frame);
            CHECK((*arm)->current() == machine.current());
            CHECK((*arm)->inTransition() == machine.inTransition());
            CHECK((*arm)->blendWeight() == doctest::Approx(machine.blendWeight()));
            for (std::size_t n = 0; n < model.nodes.size(); ++n)
            {
                const glm::quat a = (*arm)->pose().nodes[n].rotation;
                const glm::quat b = machine.pose().nodes[n].rotation;
                CHECK(a.x == b.x);
                CHECK(a.y == b.y);
                CHECK(a.z == b.z);
                CHECK(a.w == b.w);
            }
        }
        // setPose impose une pose et arrête le lecteur.
        an::Pose pose = an::restPose(model);
        pose.nodes[2].rotation = glm::angleAxis(0.3f, Vec3(0, 0, 1));
        REQUIRE((*arm)->setPose(pose).has_value());
        CHECK((*arm)->current().empty());
        REQUIRE((*arm)->update(1.0f).has_value());
        CHECK(((*arm)->pose().nodes[2].rotation == pose.nodes[2].rotation));
        CHECK((((*arm)->setPose(an::Pose{}).error().code) == ErrorCode::kInvalidArgument));
    }

    TEST_CASE("a crowd created from an animated model plays its clips and draws like a hand-built crowd")
    {
        FOR_EACH_BACKEND(backend)
        {
            tests::TempDir dir("animated_model");
            const fs::path path = writeArm(dir.path);
            Stage s(backend);
            if (!an::CrowdRenderer::supportedBy(s.gpu->capabilities()))
            {
                MESSAGE("no crowd on this device");
                continue;
            }
            auto arm = an::AnimatedModel::create(*s.context, path);
            REQUIRE(arm.has_value());
            auto crowd = an::CrowdRenderer::create(*s.context, **arm);
            INFO((crowd ? std::string() : crowd.error().message));
            REQUIRE(crowd.has_value());
            CHECK((*crowd)->batchCount() == 1);

            // Foule de référence, construite à la main : second RenderModel, poses échantillonnées ici.
            as::RenderModelOptions referenceOptions;
            referenceOptions.sourceKey = "reference";
            auto render = (*arm)->loader().createRenderModel((*arm)->model(), referenceOptions);
            REQUIRE(render.has_value());
            auto reference = an::CrowdRenderer::create(*s.context, (*arm)->model(), *render);
            REQUIRE(reference.has_value());

            const std::vector<glm::mat4> worlds{glm::translate(glm::mat4(1.0f), Vec3(-1.2f, 0.0f, 0.0f)), glm::translate(glm::mat4(1.0f), Vec3(1.2f, 0.0f, 0.0f)),
                                                glm::translate(glm::mat4(1.0f), Vec3(0.0f, -1.0f, -2.0f))};
            const std::vector<std::uint32_t> clips{0, 1, 0};
            const std::vector<float> times{0.25f, 0.6f, 1.75f}; // 1,75 s bouclé sur un clip d'1 s : 0,75 s
            std::vector<an::Pose> poses;
            for (std::size_t a = 0; a < worlds.size(); ++a)
            {
                poses.push_back(an::restPose((*arm)->model()));
                REQUIRE((*arm)->clips()[clips[a]].sample(std::fmod(times[a], 1.0f), poses.back()).has_value());
            }
            REQUIRE((*crowd)->setAgents(worlds, clips, times).has_value());
            REQUIRE((*reference)->setAgents(worlds, poses).has_value());
            CHECK((*crowd)->agentCount() == 3);

            const auto fromModel = s.frame([](CommandEncoder &) {}, [&] { REQUIRE((*crowd)->draw(*s.renderer).has_value()); });
            const auto byHand = s.frame([](CommandEncoder &) {}, [&] { REQUIRE((*reference)->draw(*s.renderer).has_value()); });
            CHECK(litPixels(fromModel) > 0);
            CHECK((fromModel == byHand));

            CHECK((((*crowd)->setAgents(worlds, std::vector<std::uint32_t>{0, 2, 0}, times).error().code) == ErrorCode::kOutOfRange));
            CHECK((((*crowd)->setAgents(worlds, clips, std::vector<float>{0.0f}).error().code) == ErrorCode::kInvalidArgument));
            CHECK((((*reference)->setAgents(worlds, clips, times).error().code) == ErrorCode::kInvalidArgument)); // pas de clips
            CHECK(s.gpu->validationErrorCount() == 0);
        }
    }
}
