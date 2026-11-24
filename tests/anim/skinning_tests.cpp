// ANIM-01 : skinning GPU. Un bras à deux os (épaule, coude) : au repos, les sommets ne bougent pas ; coude plié de 90°, le compute
// donne les mêmes sommets que la référence CPU (`anim::skinVertices`) et que le calcul à la main ; le modèle se dessine ensuite
// dans la même soumission, sans erreur de validation, sur chaque backend.
#include <doctest/doctest.h>

#include "renderer/repo_paths.hpp"
#include "rhi/gpu_fixture.hpp"

#include <orosmo/anim/clip.hpp>
#include <orosmo/anim/skinning.hpp>
#include <orosmo/assets/render_model.hpp>
#include <orosmo/renderer/light.hpp>
#include <orosmo/renderer/pipeline_cache.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <cstring>

namespace fs = std::filesystem;
namespace an = orosmo::anim;
namespace as = orosmo::assets;
namespace rd = orosmo::renderer;
using orosmo::core::Vec3;

namespace
{

fs::path shaderDir() { return tests::repoRoot(__FILE__) / "shaders" / "rhi"; }

constexpr float kHalfPi = 1.57079632679f;

// Bande de 3 segments sur Y (x = ±0,25), os 0 = épaule (nœud 1, origine), os 1 = coude (nœud 2, y = 1,5, enfant de 1). Poids :
// y ≤ 1 → épaule, y = 2 → moitié, y = 3 → coude. Nœud 0 : le maillage.
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
    m.materials.back().metallicFactor = 0.0f; // diélectrique : l'ambiante l'éclaire
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
    m.scenes.push_back({"s", {0, 1}});
    m.defaultScene = 0;
    return m;
}

std::vector<float> readVertices(Device &device, BufferHandle buffer, std::size_t floats)
{
    auto bytes = device.readBuffer(buffer, 0, floats * sizeof(float));
    REQUIRE(bytes.has_value());
    std::vector<float> out(floats);
    std::memcpy(out.data(), bytes->data(), bytes->size());
    return out;
}

} // namespace

TEST_SUITE("anim/skinning")
{
    TEST_CASE("CPU reference: rest pose leaves vertices in place; a bent elbow rotates the tip around it")
    {
        const as::Model model = makeArm();
        const as::MeshPrimitive &prim = model.meshes[0].primitives[0];
        const std::vector<float> rest = as::pbrVertices(prim);
        an::Pose pose = an::restPose(model);
        auto world = an::worldMatrices(an::nodeParents(model), pose);
        REQUIRE(world.has_value());
        auto still = an::skinVertices(rest, prim.joints, prim.weights, an::jointPalette(model.skins[0], *world, 0));
        REQUIRE(still.has_value());
        for (std::size_t i = 0; i < rest.size(); ++i)
        {
            CHECK((*still)[i] == doctest::Approx(rest[i]).epsilon(1e-5));
        }
        pose.nodes[2].rotation = glm::angleAxis(kHalfPi, Vec3(0, 0, 1));
        world = an::worldMatrices(an::nodeParents(model), pose);
        REQUIRE(world.has_value());
        auto bent = an::skinVertices(rest, prim.joints, prim.weights, an::jointPalette(model.skins[0], *world, 0));
        REQUIRE(bent.has_value());
        // Sommet 6 (-0,25 ; 3) tout sur le coude : (x ; 1,5) tourné de 90° autour de (0 ; 1,5) → (-1,5 ; 1,25). Normale inchangée (Z).
        const float *v6 = bent->data() + 6 * rd::kPbrVertexFloats;
        CHECK(v6[0] == doctest::Approx(-1.5f));
        CHECK(v6[1] == doctest::Approx(1.25f));
        CHECK(v6[5] == doctest::Approx(1.0f));
        // Sommet 0 tout sur l'épaule : immobile.
        CHECK((*bent)[0] == doctest::Approx(-0.25f));
        CHECK((*bent)[1] == doctest::Approx(0.0f));
        // Un os hors palette est refusé.
        std::vector<glm::u16vec4> badJoints = prim.joints;
        badJoints[0].x = 9;
        CHECK((an::skinVertices(rest, badJoints, prim.weights, an::jointPalette(model.skins[0], *world, 0)).error().code ==
               orosmo::core::ErrorCode::kInvalidArgument));
    }

    TEST_CASE("GPU skinning matches the CPU reference and the skinned model draws in the same submission")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            rd::ShaderLibrary library(*gpu.device);
            rd::PipelineCache pipelines(*gpu.device);
            auto renderer = rd::SceneRenderer::create(*gpu.device, library, pipelines, {.shaderDirectory = shaderDir()});
            REQUIRE(renderer.has_value());
            rd::PbrShaderCache shaders(library, shaderDir());
            auto defaults = rd::PbrDefaults::create(*gpu.device);
            REQUIRE(defaults.has_value());
            as::TextureCache textures(*gpu.device);
            auto lights = rd::LightManager::create(*gpu.device);
            REQUIRE(lights.has_value());
            lights->setAmbientLight(Vec3(1.0f), 1.0f);
            REQUIRE(lights->upload().has_value());
            (*renderer)->setLights(&*lights);

            const as::Model model = makeArm();
            as::RenderModelOptions options;
            options.sourceKey = "arm";
            options.skinnable = true;
            auto render = as::RenderModel::create(*gpu.device, model, textures, shaders, *defaults, options);
            INFO((render ? std::string() : render.error().message));
            REQUIRE(render.has_value());
            auto skinned = an::SkinnedModel::create(*gpu.device, library, pipelines, shaderDir(), model, *render);
            INFO((skinned ? std::string() : skinned.error().message));
            REQUIRE(skinned.has_value());
            CHECK(skinned->skinnedMeshCount() == 1);
            CHECK(skinned->skinnedVertexCount() == 8);

            an::Pose pose = an::restPose(model);
            pose.nodes[2].rotation = glm::angleAxis(kHalfPi, Vec3(0, 0, 1));
            REQUIRE(skinned->setPose(pose).has_value());

            rd::Camera camera;
            camera.setAspect(8, 8);
            camera.setPosition({-0.5f, 1.0f, 3.0f});
            camera.setOrientation({0.0f, 0.0f, -1.0f});
            camera.setFarPlane(100.0f);
            const TextureHandle out = gpu.target(Format::kRGBA8Unorm, {8, 8}, "output");
            (*renderer)->setOutputResolution(8, 8);
            gpu.run(
                [&](CommandEncoder &e)
                {
                    REQUIRE(skinned->dispatch(e).has_value());
                    REQUIRE((*renderer)->beginFrame(e, {.clearColor = {0, 0, 0, 1}}).has_value());
                    REQUIRE((*renderer)->setCamera(camera).has_value());
                    REQUIRE(render->draw(**renderer, camera).has_value());
                    REQUIRE((*renderer)->endFrame({out, Format::kRGBA8Unorm, {8, 8}}).has_value());
                });

            const as::MeshPrimitive &prim = model.meshes[0].primitives[0];
            auto world = an::worldMatrices(an::nodeParents(model), pose);
            REQUIRE(world.has_value());
            auto expected = an::skinVertices(as::pbrVertices(prim), prim.joints, prim.weights, an::jointPalette(model.skins[0], *world, 0));
            REQUIRE(expected.has_value());
            const std::vector<float> got = readVertices(*gpu.device, render->mesh(0).vertexBuffer(), expected->size());
            for (std::size_t i = 0; i < got.size(); ++i)
            {
                INFO("float ", i);
                CHECK(got[i] == doctest::Approx((*expected)[i]).epsilon(1e-4));
            }
            CHECK(got[6 * rd::kPbrVertexFloats] == doctest::Approx(-1.5f));
            int lit = 0;
            std::string rows;
            for (std::int32_t y = 0; y < 8; ++y)
            {
                for (std::int32_t x = 0; x < 8; ++x)
                {
                    const bool on = rgba(gpu.pixel(out, x, y))[0] > 0;
                    lit += on ? 1 : 0;
                    rows += on ? '#' : '.';
                }
                rows += '\n';
            }
            INFO("image:\n", rows);
            CHECK(lit > 0); // le bras éclairé est dessiné
            CHECK(gpu.device->validationErrorCount() == 0);
        }
    }
}
