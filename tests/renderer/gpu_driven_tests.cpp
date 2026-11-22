// REN-29 : rendu piloté par le GPU. Sans GPU : scène GPU persistante (seuls les objets modifiés sont renvoyés), capacités. Sur chaque
// backend (FOR_EACH_BACKEND, validation à 0) : ensemble visible et instances par clé identiques au culling CPU, même image
// qu'InstanceRenderer, occlusion Hi-Z de l'image précédente.
#include <doctest/doctest.h>

#include "renderer/repo_paths.hpp"
#include "rhi/gpu_fixture.hpp"

#include <orosmo/renderer/camera.hpp>
#include <orosmo/renderer/gpu_driven.hpp>
#include <orosmo/renderer/gpu_scene.hpp>
#include <orosmo/renderer/instance_renderer.hpp>
#include <orosmo/renderer/light.hpp>
#include <orosmo/renderer/mesh.hpp>
#include <orosmo/renderer/object_set.hpp>
#include <orosmo/renderer/scene_renderer.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <filesystem>
#include <map>
#include <set>

namespace fs = std::filesystem;
namespace rd = orosmo::renderer;

namespace
{

fs::path shaderDir() { return tests::repoRoot(__FILE__) / "shaders" / "rhi"; }

glm::mat4 at(float x, float y, float z) { return glm::translate(glm::mat4(1.0f), glm::vec3(x, y, z)); }

rd::ObjectDesc cubeDesc(const glm::mat4 &transform, std::vector<std::uint32_t> lods = {0}, std::uint32_t material = 0, std::uint32_t lodSettings = 0)
{
    rd::ObjectDesc d;
    d.lodMeshes = std::move(lods);
    d.material = material;
    d.lodSettings = lodSettings;
    d.localBounds = {glm::vec3(-0.5f), glm::vec3(0.5f)};
    d.transform = transform;
    return d;
}

// Cube unité (sommets : position, normale, couleur blanche).
rd::MeshData cubeData(const char *name)
{
    rd::MeshData d;
    const glm::vec3 n[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int f = 0; f < 6; ++f)
    {
        const glm::vec3 normal = n[f];
        const glm::vec3 u = std::abs(normal.x) > 0.5f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
        const glm::vec3 v = glm::cross(normal, u);
        const auto base = static_cast<std::uint32_t>(d.vertices.size() / 9);
        for (int c = 0; c < 4; ++c)
        {
            const float su = (c == 1 || c == 2) ? 0.5f : -0.5f;
            const float sv = (c >= 2) ? 0.5f : -0.5f;
            const glm::vec3 p = normal * 0.5f + u * su + v * sv;
            d.vertices.insert(d.vertices.end(), {p.x, p.y, p.z, normal.x, normal.y, normal.z, 1.0f, 1.0f, 1.0f});
        }
        d.indices.insert(d.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    d.attributeSizes = {3, 3, 3};
    d.name = name;
    return d;
}

struct GpuDrivenScene
{
    Gpu gpu;
    std::unique_ptr<rd::ShaderLibrary> library;
    std::unique_ptr<rd::PipelineCache> pipelines;
    std::unique_ptr<rd::SceneRenderer> renderer;
    std::unique_ptr<rd::InstanceRenderer> instances;
    rd::GpuDrivenFeature *feature = nullptr;
    rd::LightManager lights;
    rd::Camera camera;
    std::array<rd::Mesh, 2> meshes;
    std::uint32_t size;

    GpuDrivenScene(Backend backend, std::uint32_t side, std::uint32_t materials = 2) : gpu(backend, {side, side}), size(side)
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
        auto inst = rd::InstanceRenderer::create(*gpu.device, *library, *pipelines, shaderDir());
        REQUIRE(inst.has_value());
        instances = std::move(*inst);
        for (std::size_t i = 0; i < meshes.size(); ++i)
        {
            auto mesh = rd::Mesh::create(*gpu.device, cubeData(i == 0 ? "lod0" : "lod1"));
            REQUIRE(mesh.has_value());
            meshes[i] = std::move(*mesh);
            REQUIRE(instances->addMesh(meshes[i]).has_value());
        }
        for (std::uint32_t m = 0; m < materials; ++m)
        {
            instances->addMaterial({});
        }
        auto enabled = rd::enableGpuDriven(*renderer, *gpu.device, *library, *pipelines, shaderDir(), *instances);
        INFO((enabled ? std::string() : enabled.error().message));
        REQUIRE(enabled.has_value());
        feature = *enabled;
        auto l = rd::LightManager::create(*gpu.device);
        REQUIRE(l.has_value());
        lights = std::move(*l);
        lights.setAmbientLight({1, 1, 1}, 1.0f);
        REQUIRE(lights.upload().has_value());
        renderer->setLights(&lights);
        renderer->setOutputResolution(static_cast<int>(side), static_cast<int>(side));
        camera.setAspect(side, side);
        camera.setPosition({0, 0, 0});
        camera.setOrientation({0, 0, -1});
        camera.setFovDegrees(60.0f);
        camera.setNearPlane(0.1f);
        camera.setFarPlane(500.0f);
    }
    ~GpuDrivenScene()
    {
        if (renderer)
        {
            rd::disableGpuDriven(*renderer);
        }
        instances.reset();
        renderer.reset();
    }

    rd::GpuDrivenView view(float minScreenRadius = 0.0f) const
    {
        auto v = rd::GpuDrivenView::fromMatrices(camera.viewMatrix(), camera.projectionMatrix(), static_cast<float>(size));
        v.cull.minScreenRadius = minScreenRadius;
        return v;
    }

    // Une image dessinée par le GPU (gpu = vrai) ou par le culling CPU + InstanceRenderer.
    void render(TextureHandle out, rd::ObjectSet &objects, bool gpuDriven, float minScreenRadius = 0.0f, const rd::GpuDrivenOptions &options = {})
    {
        gpu.run(
            [&](CommandEncoder &e)
            {
                REQUIRE(renderer->beginFrame(e, {.clearColor = {0, 0, 0, 1}}).has_value());
                REQUIRE(renderer->setCamera(camera).has_value());
                if (gpuDriven)
                {
                    auto drawn = feature->draw(*renderer, objects, view(minScreenRadius), options);
                    INFO((drawn ? std::string() : drawn.error().message));
                    REQUIRE(drawn.has_value());
                }
                else
                {
                    rd::InstanceBatchBuilder batches;
                    objects.prepare(view(minScreenRadius).cull, batches);
                    REQUIRE(instances->draw(*renderer, batches).has_value());
                }
                auto ended = renderer->endFrame({out, Format::kRGBA8Unorm, {size, size}});
                INFO((ended ? std::string() : ended.error().message));
                REQUIRE(ended.has_value());
            });
    }
};

} // namespace

TEST_SUITE("renderer/gpu-driven")
{
    TEST_CASE("GpuScene: persistent mirror of an ObjectSet, only changed slots are uploaded (REN-29)")
    {
        rd::ObjectSet objects;
        std::vector<rd::ObjectId> ids;
        for (int i = 0; i < 100; ++i)
        {
            auto id = objects.add(cubeDesc(at(static_cast<float>(i), 0, 0), {0, 1}, static_cast<std::uint32_t>(i % 2)));
            REQUIRE(id.has_value());
            ids.push_back(*id);
        }
        rd::GpuScene scene;
        rd::GpuSceneSyncStats s = scene.collect(objects);
        CHECK(s.fullUpload);
        CHECK(s.uploadedObjects == 100);
        REQUIRE(scene.pendingRanges().size() == 1);
        CHECK(scene.liveObjects() == 100);
        CHECK(scene.keyReferences().at({0, 0}) == 50);
        CHECK(scene.keyReferences().at({1, 1}) == 50);
        CHECK(scene.records()[7].model[3].x == doctest::Approx(7.0f));
        CHECK(scene.records()[7].boundsMin.x == doctest::Approx(6.5f));
        CHECK(scene.records()[7].info[1] == 2);

        // Rien n'a changé : rien n'est relu ni envoyé.
        s = scene.collect(objects);
        CHECK(s.changedObjects == 0);
        CHECK(s.uploadedObjects == 0);
        CHECK(scene.pendingRanges().empty());

        // Deux déplacements proches (une plage fusionnée) et un lointain (une seconde plage).
        const std::uint64_t version = scene.keyVersion();
        REQUIRE(objects.setTransform(ids[3], at(0, 5, 0)).has_value());
        REQUIRE(objects.setColor(ids[5], {1, 0, 0, 1}).has_value());
        REQUIRE(objects.setTransform(ids[60], at(0, 9, 0)).has_value());
        s = scene.collect(objects);
        CHECK(s.changedObjects == 3);
        REQUIRE(scene.pendingRanges().size() == 2);
        CHECK(scene.pendingRanges()[0] == rd::GpuSceneRange{3, 3});
        CHECK(scene.pendingRanges()[1] == rd::GpuSceneRange{60, 1});
        CHECK(s.uploadedObjects == 4);
        CHECK(scene.records()[3].model[3].y == doctest::Approx(5.0f));
        CHECK(scene.records()[5].color.g == doctest::Approx(0.0f));
        CHECK(scene.keyVersion() == version); // un déplacement ne change pas les clés

        // Retrait : l'emplacement devient libre (un seul envoi), les clés et le nombre d'objets suivent ; les autres ne bougent pas.
        REQUIRE(objects.remove(ids[10]).has_value());
        s = scene.collect(objects);
        CHECK(s.changedObjects == 1);
        CHECK((scene.records()[10].info[0] & rd::kGpuSceneAlive) == 0);
        CHECK(scene.liveObjects() == 99);
        CHECK(scene.keyReferences().at({0, 0}) == 49);
        CHECK(scene.keyVersion() != version);
        CHECK(scene.records()[99].model[3].x == doctest::Approx(99.0f));

        // Plus de kGpuSceneMaxLevels niveaux : jamais envoyé au GPU (compté).
        std::vector<std::uint32_t> many(rd::kGpuSceneMaxLevels + 1, 0);
        REQUIRE(objects.add(cubeDesc(at(0, 0, 0), many)).has_value());
        s = scene.collect(objects);
        CHECK(s.skippedObjects == 1);
        CHECK(scene.liveObjects() == 99);

        // Un autre ensemble : tout est renvoyé.
        rd::ObjectSet other;
        REQUIRE(other.add(cubeDesc(at(0, 0, 0))).has_value());
        s = scene.collect(other);
        CHECK(s.fullUpload);
        CHECK(scene.liveObjects() == 1);
    }

    TEST_CASE("GpuDrivenFeature: refused without compute or indirect draws (rule 11, CPU culling kept)")
    {
        orosmo::rhi::Capabilities caps;
        caps.compute = true;
        caps.indirectDraw = true;
        caps.maxStorageBufferBindings = 8;
        caps.maxComputeWorkGroupSize = {1024, 1024, 64};
        caps.maxComputeInvocations = 1024;
        CHECK(rd::GpuDrivenFeature::supportedBy(caps));
        caps.compute = false;
        CHECK_FALSE(rd::GpuDrivenFeature::supportedBy(caps));
        caps.compute = true;
        caps.indirectDraw = false;
        CHECK_FALSE(rd::GpuDrivenFeature::supportedBy(caps));
        caps.indirectDraw = true;
        caps.maxStorageBufferBindings = 6;
        CHECK_FALSE(rd::GpuDrivenFeature::supportedBy(caps));
    }

    TEST_CASE("GpuDrivenFeature: same visible set, LOD choices and per-key instances as the CPU culling (REN-29, REN-30, REN-31)")
    {
        FOR_EACH_BACKEND(backend)
        {
            GpuDrivenScene scene(backend, 64);
            REQUIRE(scene.gpu.device);
            rd::ObjectSet objects;
            rd::LodSettings lod;
            lod.switchDistances = {20.0f};
            lod.transitionWidth = 6.0f;
            lod.cullDistance = 40.0f;
            const std::uint32_t lodId = objects.addLodSettings(lod);
            // Grille déterministe devant, derrière et autour de la caméra (hors champ, fondus, rejet au loin, petits objets).
            for (int z = 0; z < 40; ++z)
            {
                for (int x = 0; x < 50; ++x)
                {
                    const float px = (static_cast<float>(x) - 24.5f) * 2.1f;
                    const float pz = 10.0f - static_cast<float>(z) * 2.1f;
                    const float scale = 0.4f + 0.05f * static_cast<float>((x * 7 + z * 3) % 9);
                    REQUIRE(objects.add(cubeDesc(glm::scale(at(px, -1.5f + 0.1f * static_cast<float>(x % 5), pz), glm::vec3(scale)), {0, 1},
                                                 static_cast<std::uint32_t>((x + z) % 2), lodId))
                                .has_value());
                }
            }
            const float minRadius = 0.5f;
            const TextureHandle out = scene.gpu.target(Format::kRGBA8Unorm, {64, 64}, "gpu");
            scene.render(out, objects, true, minRadius);

            // Référence CPU : ensemble visible par emplacement (mêmes règles) et instances par clé.
            const rd::GpuDrivenView view = scene.view(minRadius);
            std::vector<rd::Aabb> bounds;
            for (std::uint32_t slot = 0; slot < objects.slotCount(); ++slot)
            {
                bounds.push_back(objects.objectAtSlot(slot)->worldBounds);
            }
            std::vector<std::uint32_t> cpuVisible;
            rd::cullLinear(view.cull, bounds, cpuVisible);
            rd::InstanceBatchBuilder batches;
            const rd::VisibilityStats cpu = objects.prepare(view.cull, batches);

            auto flags = scene.feature->readVisibility();
            REQUIRE(flags.has_value());
            REQUIRE(flags->size() == objects.slotCount());
            std::vector<std::uint32_t> gpuVisible;
            for (std::uint32_t slot = 0; slot < flags->size(); ++slot)
            {
                if (((*flags)[slot] & rd::kGpuVisibleFrustum) != 0)
                {
                    gpuVisible.push_back(slot);
                }
            }
            CHECK(gpuVisible == cpuVisible);
            auto stats = scene.feature->readStats();
            REQUIRE(stats.has_value());
            CHECK(stats->objects == 2000);
            CHECK(stats->frustumVisible == cpu.cull.visible);
            CHECK(stats->frustumCulled == cpu.cull.frustumCulled);
            CHECK(stats->smallCulled == cpu.cull.smallCulled);
            CHECK(stats->smallCulled > 0);
            CHECK(stats->lodCulled == cpu.lodCulled);
            CHECK(stats->lodCulled > 0);
            CHECK(stats->crossFades == cpu.crossFades);
            CHECK(stats->crossFades > 0);
            CHECK(stats->instances == cpu.instances);
            CHECK(stats->occlusionCulled == 0); // première image : pas de Hi-Z
            CHECK(stats->invalid == 0);
            CHECK(stats->overflow == 0);
            auto counts = scene.feature->readCommandCounts();
            REQUIRE(counts.has_value());
            std::map<rd::InstanceKey, std::uint32_t> gpuKeys;
            std::map<rd::InstanceKey, std::uint32_t> cpuKeys;
            for (const rd::GpuCommandCount &c : *counts)
            {
                gpuKeys[c.key] = c.instances;
            }
            for (const rd::InstanceBatch &b : batches.batches())
            {
                cpuKeys[b.key] = b.instanceCount;
            }
            CHECK(gpuKeys == cpuKeys);
            CHECK(stats->drawsWithInstances == 4); // 2 LOD × 2 matériaux
            scene.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("GpuDrivenFeature: same image as the CPU culling with InstanceRenderer; one indirect call per (material, pool) (REN-29)")
    {
        FOR_EACH_BACKEND(backend)
        {
            GpuDrivenScene scene(backend, 32);
            REQUIRE(scene.gpu.device);
            scene.feature->setOcclusion(false);
            rd::ObjectSet objects;
            const glm::vec4 colors[3] = {{1, 0, 0, 1}, {0, 1, 0, 1}, {0.2f, 0.4f, 1, 1}};
            for (int i = 0; i < 9; ++i)
            {
                rd::ObjectDesc d = cubeDesc(at(static_cast<float>(i % 3) * 2.0f - 2.0f, static_cast<float>(i / 3) * 2.0f - 2.0f, -8.0f - static_cast<float>(i)),
                                            {static_cast<std::uint32_t>(i % 2)}, static_cast<std::uint32_t>(i % 2));
                d.color = colors[i % 3];
                REQUIRE(objects.add(d).has_value());
            }
            const TextureHandle cpuOut = scene.gpu.target(Format::kRGBA8Unorm, {32, 32}, "cpu");
            const TextureHandle gpuOut = scene.gpu.target(Format::kRGBA8Unorm, {32, 32}, "gpu");
            scene.render(cpuOut, objects, false);
            rd::GpuDrawStats drawStats;
            scene.gpu.run(
                [&](CommandEncoder &e)
                {
                    REQUIRE(scene.renderer->beginFrame(e, {.clearColor = {0, 0, 0, 1}}).has_value());
                    REQUIRE(scene.renderer->setCamera(scene.camera).has_value());
                    auto drawn = scene.feature->draw(*scene.renderer, objects, scene.view());
                    REQUIRE(drawn.has_value());
                    drawStats = *drawn;
                    CHECK_FALSE(scene.feature->draw(*scene.renderer, objects, scene.view()).has_value()); // une fois par image
                    REQUIRE(scene.renderer->endFrame({gpuOut, Format::kRGBA8Unorm, {32, 32}}).has_value());
                });
            CHECK(drawStats.pools == 1);              // les deux cubes ont la même disposition : un pool
            CHECK(drawStats.commands == 4);           // 2 maillages × 2 matériaux
            CHECK(drawStats.indirectCalls == 2);      // un appel multi-draw par matériau
            auto cpuPixels = scene.gpu.device->readTexture(cpuOut);
            auto gpuPixels = scene.gpu.device->readTexture(gpuOut);
            REQUIRE(cpuPixels.has_value());
            REQUIRE(gpuPixels.has_value());
            CHECK(*cpuPixels == *gpuPixels);
            int lit = 0;
            for (std::size_t i = 0; i < gpuPixels->size(); i += 4)
            {
                lit += (*gpuPixels)[i] != std::byte{0} || (*gpuPixels)[i + 1] != std::byte{0} || (*gpuPixels)[i + 2] != std::byte{0};
            }
            CHECK(lit > 50);
            scene.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("GpuDrivenFeature: Hi-Z occlusion from the previous frame depth culls hidden objects only (REN-29)")
    {
        FOR_EACH_BACKEND(backend)
        {
            GpuDrivenScene scene(backend, 64);
            REQUIRE(scene.gpu.device);
            rd::ObjectSet objects;
            // Un mur de 2 × 2 à 5 m ; cinq cubes cachés derrière lui à 20 m ; trois cubes visibles hors de son ombre.
            REQUIRE(objects.add(cubeDesc(glm::scale(at(0, 0, -5), glm::vec3(2.0f, 2.0f, 0.2f)))).has_value());
            std::set<std::uint32_t> hidden;
            for (int i = 0; i < 5; ++i)
            {
                auto id = objects.add(cubeDesc(glm::scale(at(static_cast<float>(i) - 2.0f, 0, -20), glm::vec3(0.4f)), {0}, 1));
                REQUIRE(id.has_value());
                hidden.insert(id->index);
            }
            for (const float x : {-8.0f, 8.0f})
            {
                REQUIRE(objects.add(cubeDesc(glm::scale(at(x, 0, -20), glm::vec3(0.4f)), {0}, 1)).has_value());
            }
            REQUIRE(objects.add(cubeDesc(glm::scale(at(0, 6, -20), glm::vec3(0.4f)), {0}, 1)).has_value());

            const TextureHandle out = scene.gpu.target(Format::kRGBA8Unorm, {64, 64}, "out");
            scene.render(out, objects, true);
            CHECK(scene.feature->hizValid());
            CHECK(scene.feature->hizLevels() == 7); // 64 -> 1
            auto first = scene.feature->readStats();
            REQUIRE(first.has_value());
            CHECK(first->occlusionCulled == 0);
            CHECK(first->visible == 9);
            // Le niveau le plus grossier garde la profondeur la plus lointaine : le fond (0, reverse-Z).
            auto top = scene.feature->readHiz(scene.feature->hizLevels() - 1);
            REQUIRE(top.has_value());
            REQUIRE(top->size() == 1);
            CHECK((*top)[0] == 0.0f);

            scene.render(out, objects, true);
            auto second = scene.feature->readStats();
            REQUIRE(second.has_value());
            CHECK(second->frustumVisible == 9);
            CHECK(second->occlusionCulled == 5);
            CHECK(second->visible == 4);
            auto flags = scene.feature->readVisibility();
            REQUIRE(flags.has_value());
            for (std::uint32_t slot = 0; slot < flags->size(); ++slot)
            {
                INFO("slot ", slot);
                CHECK((((*flags)[slot] & rd::kGpuVisibleOcclusion) == 0) == hidden.contains(slot));
            }

            // Sans culling (frustumCulling faux : tout est visible), la Hi-Z, pourtant valide, ne rejette rien.
            CHECK(scene.feature->hizValid());
            scene.render(out, objects, true, 0.0f, {.frustumCulling = false});
            auto noCulling = scene.feature->readStats();
            REQUIRE(noCulling.has_value());
            CHECK(noCulling->occlusionCulled == 0);
            CHECK(noCulling->visible == 9);

            // Sans occlusion : tout redevient visible ; après une coupure de caméra (resetOcclusion), l'image suivante n'en a pas.
            scene.feature->setOcclusion(false);
            scene.render(out, objects, true);
            auto off = scene.feature->readStats();
            REQUIRE(off.has_value());
            CHECK(off->occlusionCulled == 0);
            scene.feature->setOcclusion(true);
            scene.render(out, objects, true);
            scene.feature->resetOcclusion();
            scene.render(out, objects, true);
            auto reset = scene.feature->readStats();
            REQUIRE(reset.has_value());
            CHECK(reset->occlusionCulled == 0);
            scene.render(out, objects, true);
            auto again = scene.feature->readStats();
            REQUIRE(again.has_value());
            CHECK(again->occlusionCulled == 5);
            scene.gpu.expectNoValidationErrors();
        }
    }
    TEST_CASE("GpuDrivenFeature: visibility flags are written without commands and the drawn bit means an instance was written (REN-29)")
    {
        FOR_EACH_BACKEND(backend)
        {
            // Aucun matériau : aucune commande indirecte, mais le culling écrit quand même les drapeaux de visibilité.
            {
                GpuDrivenScene scene(backend, 32, 0);
                REQUIRE(scene.gpu.device);
                rd::ObjectSet objects;
                REQUIRE(objects.add(cubeDesc(at(0, 0, -5))).has_value());
                REQUIRE(objects.add(cubeDesc(at(0, 0, 5))).has_value()); // derrière la caméra
                const TextureHandle out = scene.gpu.target(Format::kRGBA8Unorm, {32, 32}, "out");
                scene.render(out, objects, true);
                auto stats = scene.feature->readStats();
                REQUIRE(stats.has_value());
                CHECK(stats->objects == 2);
                CHECK(stats->frustumCulled == 1);
                CHECK(stats->invalid == 1);
                CHECK(stats->instances == 0);
                auto flags = scene.feature->readVisibility();
                REQUIRE(flags.has_value());
                REQUIRE(flags->size() == 2);
                CHECK((*flags)[0] == (rd::kGpuVisibleAlive | rd::kGpuVisibleFrustum | rd::kGpuVisibleOcclusion));
                CHECK((*flags)[1] == rd::kGpuVisibleAlive);
                scene.gpu.expectNoValidationErrors();
            }
            // Maillage inconnu : visible, compté invalide, mais pas « dessiné » ; un maillage connu l'est.
            {
                GpuDrivenScene scene(backend, 32);
                REQUIRE(scene.gpu.device);
                rd::ObjectSet objects;
                REQUIRE(objects.add(cubeDesc(at(-1, 0, -5), {7})).has_value());
                REQUIRE(objects.add(cubeDesc(at(1, 0, -5), {0})).has_value());
                const TextureHandle out = scene.gpu.target(Format::kRGBA8Unorm, {32, 32}, "out");
                scene.render(out, objects, true);
                auto stats = scene.feature->readStats();
                REQUIRE(stats.has_value());
                CHECK(stats->visible == 2);
                CHECK(stats->invalid == 1);
                CHECK(stats->instances == 1);
                auto flags = scene.feature->readVisibility();
                REQUIRE(flags.has_value());
                REQUIRE(flags->size() == 2);
                CHECK(((*flags)[0] & rd::kGpuVisibleOcclusion) != 0);
                CHECK(((*flags)[0] & rd::kGpuVisibleDrawn) == 0);
                CHECK(((*flags)[1] & rd::kGpuVisibleDrawn) != 0);
                scene.gpu.expectNoValidationErrors();
            }
        }
    }
}
