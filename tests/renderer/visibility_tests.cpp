// REN-28 (regroupement), REN-30 (culling CPU) et REN-31 (LOD) : tout ce qui se décide sur le CPU, sans GPU ni fenêtre (suites
// « visibility/* », exécutées par make demo-test-unit).
#include <doctest/doctest.h>

#include <orosmo/renderer/camera.hpp>
#include <orosmo/renderer/culling.hpp>
#include <orosmo/renderer/instance_batch.hpp>
#include <orosmo/renderer/lod.hpp>
#include <orosmo/renderer/object_set.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <vector>

namespace rd = orosmo::renderer;

namespace
{

rd::Aabb unitBox(const glm::vec3 &center, float half = 0.5f) { return {center - glm::vec3(half), center + glm::vec3(half)}; }

// Caméra à l'origine regardant -Z, 90° de champ vertical, aspect 1, plan lointain infini (reverse-Z) : la pyramide est |x| <= -z,
// |y| <= -z, z <= -near.
rd::Camera testCamera(float farPlane = std::numeric_limits<float>::infinity())
{
    rd::Camera camera;
    camera.setPosition({0.0f, 0.0f, 0.0f});
    camera.setOrientation({0.0f, 0.0f, -1.0f});
    camera.setFovDegrees(90.0f);
    camera.setAspect(1.0f);
    camera.setNearPlane(0.1f);
    camera.setFarPlane(farPlane);
    return camera;
}

rd::CullView viewOf(const rd::Camera &camera, float viewportHeight = 1000.0f)
{
    return rd::CullView::fromMatrices(camera.viewMatrix(), camera.projectionMatrix(), viewportHeight);
}

// Grille déterministe de boîtes autour de la caméra (sans hasard ni horloge).
std::vector<rd::Aabb> gridBoxes(int side, float spacing)
{
    std::vector<rd::Aabb> boxes;
    for (int x = 0; x < side; ++x)
    {
        for (int y = 0; y < 4; ++y)
        {
            for (int z = 0; z < side; ++z)
            {
                const glm::vec3 c((static_cast<float>(x) - side * 0.5f) * spacing, static_cast<float>(y) * 2.0f - 3.0f,
                                  (static_cast<float>(z) - side * 0.5f) * spacing);
                boxes.push_back(unitBox(c, 0.3f + 0.1f * static_cast<float>((x + z) % 3)));
            }
        }
    }
    return boxes;
}

} // namespace

TEST_SUITE("visibility/culling")
{
    TEST_CASE("Aabb: merge, radius, transformed box contains every transformed corner (REN-30)")
    {
        rd::Aabb box{{-1.0f, -2.0f, -0.5f}, {1.0f, 2.0f, 0.5f}};
        CHECK(box.valid());
        CHECK(box.center() == glm::vec3(0.0f));
        CHECK(box.radius() == doctest::Approx(std::sqrt(1.0f + 4.0f + 0.25f)));
        rd::Aabb merged = rd::Aabb::empty();
        CHECK_FALSE(merged.valid());
        merged.merge(box);
        merged.merge(unitBox({5.0f, 0.0f, 0.0f}));
        CHECK(merged.max.x == 5.5f);
        CHECK(merged.min.y == -2.0f);

        glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, -1.0f, 2.0f));
        m = glm::rotate(m, glm::radians(37.0f), glm::normalize(glm::vec3(1.0f, 2.0f, 0.5f)));
        m = glm::scale(m, glm::vec3(2.0f, 0.5f, 1.5f));
        const rd::Aabb t = box.transformed(m);
        for (int corner = 0; corner < 8; ++corner)
        {
            const glm::vec3 local((corner & 1) ? box.max.x : box.min.x, (corner & 2) ? box.max.y : box.min.y, (corner & 4) ? box.max.z : box.min.z);
            const glm::vec3 p = glm::vec3(m * glm::vec4(local, 1.0f));
            CHECK(p.x >= t.min.x - 1e-4f);
            CHECK(p.x <= t.max.x + 1e-4f);
            CHECK(p.y >= t.min.y - 1e-4f);
            CHECK(p.y <= t.max.y + 1e-4f);
            CHECK(p.z >= t.min.z - 1e-4f);
            CHECK(p.z <= t.max.z + 1e-4f);
        }
    }

    TEST_CASE("Frustum: planes of a reverse-Z infinite projection; inside, outside, intersecting boxes (REN-30)")
    {
        const rd::Camera camera = testCamera();
        const rd::Frustum frustum = rd::Frustum::fromMatrix(camera.matrix());
        CHECK(frustum.planes().size() == 5); // le plan lointain d'une projection infinie est omis
        for (const glm::vec4 &p : frustum.planes())
        {
            CHECK(glm::length(glm::vec3(p)) == doctest::Approx(1.0f));
        }
        CHECK(frustum.contains({0.0f, 0.0f, -5.0f}));
        CHECK(frustum.contains({0.0f, 0.0f, -1.0e6f})); // jamais rejeté au loin
        CHECK_FALSE(frustum.contains({0.0f, 0.0f, 5.0f}));   // derrière
        CHECK_FALSE(frustum.contains({0.0f, 0.0f, -0.05f})); // devant le plan proche
        CHECK_FALSE(frustum.contains({6.0f, 0.0f, -5.0f}));  // |x| > -z
        CHECK(frustum.test(unitBox({0.0f, 0.0f, -10.0f})) == rd::CullResult::kInside);
        CHECK(frustum.test(unitBox({0.0f, 0.0f, 10.0f})) == rd::CullResult::kOutside);
        CHECK(frustum.test(unitBox({10.0f, 0.0f, -10.0f})) == rd::CullResult::kIntersects); // à cheval sur le plan droit
        CHECK(frustum.test(unitBox({12.0f, 0.0f, -10.0f})) == rd::CullResult::kOutside);

        // Plan lointain fini : six plans, un objet au-delà est rejeté.
        const rd::Camera bounded = testCamera(100.0f);
        const rd::Frustum finite = rd::Frustum::fromMatrix(bounded.matrix());
        CHECK(finite.planes().size() == 6);
        CHECK(finite.test(unitBox({0.0f, 0.0f, -50.0f})) == rd::CullResult::kInside);
        CHECK(finite.test(unitBox({0.0f, 0.0f, -200.0f})) == rd::CullResult::kOutside);

        // Orthographique : boîte de vue, les côtés ne s'ouvrent pas avec la distance.
        rd::Camera ortho = testCamera(100.0f);
        ortho.setOrthographic(10.0f);
        const rd::Frustum box = rd::Frustum::fromMatrix(ortho.matrix());
        CHECK(box.contains({4.9f, 0.0f, -50.0f}));
        CHECK_FALSE(box.contains({5.1f, 0.0f, -50.0f}));
    }

    TEST_CASE("Small-object culling: projected radius in pixels, perspective and orthographic (REN-30)")
    {
        const rd::Camera camera = testCamera();
        rd::CullView view = viewOf(camera, 1000.0f);
        // fov 90° : proj[1][1] = 1 ; une sphère de rayon r à la distance d a un rayon de r / d × 500 pixels.
        const rd::Aabb box = unitBox({0.0f, 0.0f, -100.0f}, 1.0f);
        CHECK(rd::projectedRadiusPixels(box, view) == doctest::Approx(box.radius() / 100.0f * 500.0f).epsilon(1e-4));
        CHECK(std::isinf(rd::projectedRadiusPixels(unitBox({0.0f, 0.0f, 0.0f}), view))); // l'œil dans la sphère
        view.orthographic = true;
        view.projectionScale = 2.0f / 10.0f;
        CHECK(rd::projectedRadiusPixels(box, view) == doctest::Approx(box.radius() * 0.2f * 500.0f));

        // Rejet : les petites boîtes lointaines disparaissent, les proches restent.
        rd::CullView small = viewOf(camera, 1000.0f);
        small.minScreenRadius = 2.0f;
        const std::vector<rd::Aabb> boxes{unitBox({0.0f, 0.0f, -10.0f}, 0.1f), unitBox({0.0f, 0.0f, -1000.0f}, 0.1f)};
        std::vector<std::uint32_t> visible;
        const rd::CullStats stats = rd::cullLinear(small, boxes, visible);
        CHECK(visible == std::vector<std::uint32_t>{0});
        CHECK(stats.smallCulled == 1);
        CHECK(stats.visible == 1);
        // Distance maximale : mesurée au point le plus proche de la boîte.
        rd::CullView limited = viewOf(camera, 1000.0f);
        limited.maxDistance = 50.0f;
        visible.clear();
        const std::vector<rd::Aabb> far{unitBox({0.0f, 0.0f, -40.0f}), unitBox({0.0f, 0.0f, -60.0f}), {{-1.0f, -1.0f, -60.0f}, {1.0f, 1.0f, -45.0f}}};
        CHECK(rd::cullLinear(limited, far, visible).distanceCulled == 1);
        CHECK(visible == std::vector<std::uint32_t>{0, 2});
    }

    TEST_CASE("Bvh: same visible set as the linear test, fewer tests, deterministic; refit follows moved boxes (REN-30)")
    {
        std::vector<rd::Aabb> boxes = gridBoxes(50, 4.0f); // 10 000 boîtes
        REQUIRE(boxes.size() == 10000);
        rd::Bvh bvh;
        bvh.build(boxes);
        CHECK(bvh.objectCount() == boxes.size());
        CHECK(bvh.bounds().min.x <= boxes.front().min.x);

        rd::Camera camera = testCamera();
        camera.setOrientation(glm::normalize(glm::vec3(0.3f, -0.1f, -1.0f)));
        rd::CullView view = viewOf(camera);
        view.minScreenRadius = 1.0f;

        std::vector<std::uint32_t> linear;
        const rd::CullStats linearStats = rd::cullLinear(view, boxes, linear);
        std::vector<std::uint32_t> tree;
        const rd::CullStats treeStats = bvh.query(view, boxes, tree);
        std::sort(tree.begin(), tree.end());
        CHECK(tree == linear);
        CHECK(treeStats.visible == linearStats.visible);
        CHECK(treeStats.frustumCulled == linearStats.frustumCulled);
        CHECK(treeStats.smallCulled == linearStats.smallCulled);
        CHECK(treeStats.visible + treeStats.frustumCulled + treeStats.smallCulled == boxes.size());
        CHECK(linearStats.frustumCulled > boxes.size() / 2); // la majorité de la grille est hors champ
        // La hiérarchie évite la plupart des tests un par un (les nœuds entièrement dehors ou dedans les règlent en bloc).
        CHECK(treeStats.objectsTested + treeStats.nodesVisited < linearStats.objectsTested / 2);
        MESSAGE("BVH: ", treeStats.nodesVisited, " nodes + ", treeStats.objectsTested, " object tests vs ", linearStats.objectsTested,
                " linear tests; ", treeStats.visible, " visible of ", boxes.size());

        rd::Bvh again;
        again.build(boxes);
        std::vector<std::uint32_t> second;
        again.query(view, boxes, second);
        std::vector<std::uint32_t> first;
        bvh.query(view, boxes, first);
        CHECK(first == second); // même entrée, même arbre, même ordre

        // Déplacement : toutes les boîtes reculent derrière la caméra ; après refit, rien n'est visible.
        for (rd::Aabb &b : boxes)
        {
            b.min.z += 1000.0f;
            b.max.z += 1000.0f;
        }
        bvh.refit(boxes);
        tree.clear();
        CHECK(bvh.query(view, boxes, tree).visible == 0);
        CHECK(tree.empty());
        rd::Bvh empty;
        CHECK(empty.query(view, boxes, tree).nodesVisited == 0);
    }
}

TEST_SUITE("visibility/lod")
{
    TEST_CASE("selectLod: level by distance, bias, last level kept beyond the known levels (REN-31)")
    {
        rd::LodSettings s;
        s.switchDistances = {10.0f, 30.0f};
        const auto level = [&](float d, std::uint8_t levels = 3)
        {
            const rd::LodSelection sel = rd::selectLod(s, levels, d);
            REQUIRE(sel.count == 1);
            CHECK(sel.draws[0].dither == 0.0f);
            return sel.draws[0].level;
        };
        CHECK(level(0.0f) == 0);
        CHECK(level(9.9f) == 0);
        CHECK(level(10.0f) == 1);
        CHECK(level(29.0f) == 1);
        CHECK(level(1000.0f) == 2);
        CHECK(level(1000.0f, 2) == 1); // deux niveaux seulement : la seconde bascule est ignorée
        CHECK(level(1000.0f, 1) == 0);
        CHECK(level(-5.0f) == 0);
        CHECK(level(std::nanf("")) == 0);
        s.bias = 2.0f;
        CHECK(level(6.0f) == 1); // 6 × 2 = 12 > 10
        rd::LodSettings none;
        CHECK(rd::selectLod(none, 4, 1.0e9f).draws[0].level == 0);
    }

    TEST_CASE("selectLod: the cross-fade band covers every pixel exactly once, no hole, no double coverage (REN-31)")
    {
        rd::LodSettings s;
        s.switchDistances = {10.0f, 30.0f};
        s.transitionWidth = 4.0f;
        s.cullDistance = 60.0f;
        // Les 16 valeurs du motif de Bayer 4×4 du shader : n = k / 16.
        for (float d = 0.0f; d < 70.0f; d += 0.05f)
        {
            const rd::LodSelection sel = rd::selectLod(s, 3, d);
            if (d >= 62.0f)
            {
                CHECK(sel.count == 0);
                continue;
            }
            REQUIRE(sel.count >= 1);
            float coverage = 0.0f;
            for (std::uint8_t i = 0; i < sel.count; ++i)
            {
                coverage += rd::ditherCoverage(sel.draws[i].dither);
            }
            if (d < 58.0f)
            {
                CHECK(coverage == doctest::Approx(1.0f)); // hors de la bande de rejet, couverture totale
                for (int k = 0; k < 16; ++k)
                {
                    const float n = static_cast<float>(k) / 16.0f;
                    int drawn = 0;
                    for (std::uint8_t i = 0; i < sel.count; ++i)
                    {
                        drawn += rd::ditherKeeps(sel.draws[i].dither, n) ? 1 : 0;
                    }
                    INFO("distance ", d, " bayer ", n);
                    CHECK(drawn == 1);
                }
            }
            if (sel.count == 2)
            {
                CHECK(sel.draws[1].level == sel.draws[0].level + 1);
                CHECK(sel.draws[0].dither > 0.0f); // le niveau détaillé s'efface
                CHECK(sel.draws[1].dither == -sel.draws[0].dither);
                const bool inBand = (d > 8.0f && d < 12.0f) || (d > 28.0f && d < 32.0f);
                CHECK(inBand);
            }
        }
        // Le fondu progresse avec la distance : au milieu de la bande, moitié-moitié.
        const rd::LodSelection mid = rd::selectLod(s, 3, 10.0f);
        REQUIRE(mid.count == 2);
        CHECK(mid.draws[0].dither == doctest::Approx(0.5f));
        // Bande de rejet : le dernier niveau s'efface progressivement.
        const rd::LodSelection fading = rd::selectLod(s, 3, 61.0f);
        REQUIRE(fading.count == 1);
        CHECK(fading.draws[0].level == 2);
        CHECK(rd::ditherCoverage(fading.draws[0].dither) == doctest::Approx(0.25f));
    }
}

TEST_SUITE("visibility/batching")
{
    TEST_CASE("InstanceBatchBuilder: one batch per (mesh, material), stable order, normal matrix and dither packed (REN-28)")
    {
        rd::InstanceBatchBuilder builder;
        const rd::InstanceKey a{1, 0};
        const rd::InstanceKey b{0, 2};
        const rd::InstanceKey c{1, 1};
        for (int i = 0; i < 100; ++i)
        {
            const rd::InstanceKey key = i % 3 == 0 ? a : (i % 3 == 1 ? b : c);
            builder.add(key, glm::translate(glm::mat4(1.0f), glm::vec3(static_cast<float>(i), 0.0f, 0.0f)));
        }
        CHECK_FALSE(builder.built());
        builder.build();
        REQUIRE(builder.batches().size() == 3);
        CHECK(builder.batches()[0].key == b); // trié par (maillage, matériau)
        CHECK(builder.batches()[1].key == a);
        CHECK(builder.batches()[2].key == c);
        std::uint32_t expectedFirst = 0;
        for (const rd::InstanceBatch &batch : builder.batches())
        {
            CHECK(batch.firstInstance == expectedFirst);
            expectedFirst += batch.instanceCount;
        }
        CHECK(expectedFirst == 100);
        CHECK(builder.batches()[1].instanceCount == 34);
        // Ordre stable : dans le lot `a`, les instances 0, 3, 6...
        const auto &first = builder.instances()[builder.batches()[1].firstInstance];
        const auto &second = builder.instances()[builder.batches()[1].firstInstance + 1];
        CHECK(first.model[3].x == 0.0f);
        CHECK(second.model[3].x == 3.0f);

        const glm::mat4 scaled = glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 4.0f, 1.0f));
        const rd::GpuInstance gi = rd::makeGpuInstance(scaled, glm::vec4(0.5f), -0.25f);
        CHECK(gi.normal[0].x == doctest::Approx(0.5f)); // inverse transposée d'une échelle
        CHECK(gi.normal[1].y == doctest::Approx(0.25f));
        CHECK(gi.normal[0].w == -0.25f);
        CHECK(gi.color.a == 0.5f);
        const rd::GpuInstance flat = rd::makeGpuInstance(glm::scale(glm::mat4(1.0f), glm::vec3(0.0f)));
        CHECK(flat.normal[2].z == 1.0f); // non inversible : identité

        builder.clear();
        builder.build();
        CHECK(builder.batches().empty());
    }
}

TEST_SUITE("visibility/objects")
{
    TEST_CASE("ObjectSet: generic objects culled, LOD-selected and grouped into a few batches (REN-28, REN-30, REN-31)")
    {
        rd::ObjectSet set;
        rd::LodSettings lod;
        lod.switchDistances = {20.0f};
        lod.transitionWidth = 2.0f;
        const std::uint32_t lodId = set.addLodSettings(lod);
        CHECK(set.lodSettings(lodId).switchDistances.size() == 1);
        CHECK(set.lodSettings(999).switchDistances.empty()); // inconnu : réglage par défaut

        // 10 000 objets sur une grille, deux matériaux, deux niveaux (maillages 0 et 1).
        std::vector<rd::ObjectId> ids;
        for (int x = 0; x < 100; ++x)
        {
            for (int z = 0; z < 100; ++z)
            {
                rd::ObjectDesc d;
                d.lodMeshes = {0, 1};
                d.material = static_cast<std::uint32_t>((x + z) % 2);
                d.lodSettings = lodId;
                d.localBounds = unitBox(glm::vec3(0.0f));
                d.transform = glm::translate(glm::mat4(1.0f), glm::vec3(static_cast<float>(x - 50) * 3.0f, 0.0f, static_cast<float>(z - 50) * 3.0f));
                auto id = set.add(d);
                REQUIRE(id.has_value());
                ids.push_back(*id);
            }
        }
        CHECK(set.size() == 10000);
        rd::Camera camera = testCamera();
        camera.setPosition({0.0f, 5.0f, 0.0f});
        const rd::CullView view = viewOf(camera);
        rd::InstanceBatchBuilder out;
        const rd::VisibilityStats stats = set.prepare(view, out);
        CHECK(stats.objects == 10000);
        CHECK(stats.cull.visible < 10000 / 2); // la moitié derrière la caméra au moins
        CHECK(stats.cull.visible > 0);
        CHECK(out.batches().size() <= 4); // 2 maillages × 2 matériaux
        CHECK(stats.instances == out.size());
        CHECK(stats.instances == stats.cull.visible + stats.crossFades);
        CHECK(stats.crossFades > 0);
        MESSAGE("ObjectSet: ", stats.cull.visible, " visible of 10000, ", out.batches().size(), " batches, ", stats.crossFades, " cross-fades");

        // Sans culling ni LOD : tout est produit, au niveau 0, en deux lots (un par matériau).
        rd::InstanceBatchBuilder all;
        rd::VisibilityOptions noCulling;
        noCulling.frustumCulling = false;
        noCulling.lod = false;
        const rd::VisibilityStats everything = set.prepare(view, all, noCulling);
        CHECK(everything.instances == 10000);
        CHECK(all.batches().size() == 2);
        // Test linéaire : même visibilité que la BVH.
        rd::InstanceBatchBuilder linear;
        rd::VisibilityOptions linearOptions;
        linearOptions.useBvh = false;
        CHECK(set.prepare(view, linear, linearOptions).cull.visible == stats.cull.visible);

        // Déplacement (refit), changement de couleur, retrait : identifiants périmés refusés.
        REQUIRE(set.setTransform(ids[0], glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 5.0f, -5.0f))).has_value());
        REQUIRE(set.setColor(ids[0], glm::vec4(1.0f, 0.0f, 0.0f, 1.0f)).has_value());
        rd::InstanceBatchBuilder moved;
        set.prepare(view, moved);
        const bool redFound = std::ranges::any_of(moved.instances(), [](const rd::GpuInstance &g) { return g.color.g == 0.0f && g.model[3].z == -5.0f; });
        CHECK(redFound);
        REQUIRE(set.remove(ids[0]).has_value());
        CHECK_FALSE(set.contains(ids[0]));
        using orosmo::core::ErrorCode;
        const auto failsWith = [](const auto &result, ErrorCode code) { return !result.has_value() && result.error().code == code; };
        CHECK(failsWith(set.remove(ids[0]), ErrorCode::kNotFound));
        CHECK(failsWith(set.setTransform(ids[0], glm::mat4(1.0f)), ErrorCode::kNotFound));
        CHECK(failsWith(set.setColor(ids[0], glm::vec4(1.0f)), ErrorCode::kNotFound));
        CHECK(set.contains(ids[1]));
        CHECK(set.size() == 9999);
        rd::ObjectDesc single;
        single.lodMeshes = {0};
        single.localBounds = unitBox(glm::vec3(0.0f));
        auto reused = set.add(single);
        REQUIRE(reused.has_value());
        CHECK(reused->index == ids[0].index);
        CHECK_FALSE(*reused == ids[0]); // nouvelle génération

        CHECK(failsWith(set.add(rd::ObjectDesc{}), ErrorCode::kInvalidArgument)); // aucun maillage
        rd::ObjectDesc badLod = single;
        badLod.lodSettings = 42;
        CHECK(failsWith(set.add(badLod), ErrorCode::kNotFound));
        rd::ObjectDesc badBounds = single;
        badBounds.localBounds = rd::Aabb::empty();
        CHECK(failsWith(set.add(badBounds), ErrorCode::kInvalidArgument));
        set.clear();
        CHECK(set.size() == 0);
        CHECK_FALSE(set.contains(ids[1]));
        rd::InstanceBatchBuilder none;
        CHECK(set.prepare(view, none).instances == 0);
    }
}
