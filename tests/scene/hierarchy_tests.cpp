// ECS-02 (A79 lot L2) : hiérarchie parent/enfant, transformations en double recalculées seulement si sales.
#include <doctest/doctest.h>

#include <orosmo/scene/world.hpp>

#include <glm/gtc/quaternion.hpp>

#include <chrono>
#include <cmath>
#include <vector>

using namespace orosmo;
using namespace orosmo::scene;

namespace
{
Transform at(double x, double y, double z)
{
    Transform t;
    t.position = core::DVec3(x, y, z);
    return t;
}

// Quart de tour autour de Y, exact : (cos 45°, 0, sin 45°, 0) sans appel à sin/cos.
core::DQuat quarterTurnY()
{
    const double h = 0.70710678118654752440;
    return core::DQuat(h, 0.0, h, 0.0);
}

bool near(const core::DVec3 &a, const core::DVec3 &b, double eps = 1e-9)
{
    return std::abs(a.x - b.x) <= eps && std::abs(a.y - b.y) <= eps && std::abs(a.z - b.z) <= eps;
}
} // namespace

TEST_SUITE("scene/hierarchy")
{
    TEST_CASE("Hierarchy: children in insertion order, re-parenting, detaching, cycles refused")
    {
        World world;
        const Entity root = world.create();
        const Entity a = world.create();
        const Entity b = world.create();
        const Entity c = world.create();
        REQUIRE(world.setParent(a, root));
        REQUIRE(world.setParent(b, root));
        REQUIRE(world.setParent(c, root));
        CHECK(world.children(root) == std::vector<Entity>{a, b, c});
        CHECK(world.parent(b) == root);
        CHECK(world.firstChild(root) == a);
        CHECK(world.nextSibling(a) == b);
        CHECK(world.nextSibling(c) == kNullEntity);

        REQUIRE(world.setParent(b, a)); // retiré du milieu de la liste
        CHECK(world.children(root) == std::vector<Entity>{a, c});
        CHECK(world.children(a) == std::vector<Entity>{b});

        auto cycle = world.setParent(a, b); // a est l'ancêtre de b
        REQUIRE_FALSE(cycle);
        CHECK((cycle.error().code == core::ErrorCode::kInvalidArgument));
        CHECK_FALSE(world.setParent(a, a));
        CHECK(world.parent(a) == root);

        REQUIRE(world.setParent(c, kNullEntity));
        CHECK(world.parent(c) == kNullEntity);
        CHECK(world.children(root) == std::vector<Entity>{a});

        const Entity dead = world.create();
        world.destroy(dead);
        CHECK_FALSE(world.setParent(dead, root));
        CHECK_FALSE(world.setParent(c, dead));
    }

    TEST_CASE("Hierarchy: destroying an entity destroys its descendants and detaches it from its parent")
    {
        World world;
        const Entity root = world.create();
        const Entity a = world.create();
        const Entity a1 = world.create();
        const Entity a2 = world.create();
        const Entity b = world.create();
        REQUIRE(world.setParent(a, root));
        REQUIRE(world.setParent(b, root));
        REQUIRE(world.setParent(a1, a));
        REQUIRE(world.setParent(a2, a1));
        CHECK(world.destroy(a));
        CHECK_FALSE(world.alive(a));
        CHECK_FALSE(world.alive(a1));
        CHECK_FALSE(world.alive(a2));
        CHECK(world.alive(b));
        CHECK(world.children(root) == std::vector<Entity>{b});
        CHECK(world.entityCount() == 2u);
    }

    TEST_CASE("Transforms: propagation to the descendants, rotation and scale of the parent")
    {
        World world;
        const Entity root = world.create();
        const Entity child = world.create();
        const Entity grandChild = world.create();
        Transform rootT = at(10.0, 0.0, 0.0);
        rootT.rotation = quarterTurnY(); // +X local -> -Z monde
        rootT.scale = core::DVec3(2.0);
        REQUIRE(world.setTransform(root, rootT));
        REQUIRE(world.setTransform(child, at(1.0, 0.0, 0.0)));
        REQUIRE(world.setTransform(grandChild, at(0.0, 3.0, 0.0)));
        REQUIRE(world.setParent(child, root));
        REQUIRE(world.setParent(grandChild, child));
        CHECK(world.updateTransforms() == 3u);
        CHECK(near(world.transform(root)->worldPosition, {10.0, 0.0, 0.0}));
        CHECK(near(world.transform(child)->worldPosition, {10.0, 0.0, -2.0}));
        CHECK(near(world.transform(grandChild)->worldPosition, {10.0, 6.0, -2.0}));

        // Déplacer la racine déplace toute la descendance.
        world.editTransform(root)->position = core::DVec3(0.0, 100.0, 0.0);
        CHECK(world.updateTransforms() == 3u);
        CHECK(near(world.transform(grandChild)->worldPosition, {0.0, 106.0, -2.0}));

        // Une entité intermédiaire sans Transform transmet celle de son ancêtre.
        world.remove(child, world.transformType());
        CHECK(world.updateTransforms() == 1u);
        CHECK(near(world.transform(grandChild)->worldPosition, {0.0, 106.0, 0.0}));
    }

    TEST_CASE("Transforms: changing the parent recomputes the moved subtree only")
    {
        World world;
        const Entity left = world.create();
        const Entity right = world.create();
        const Entity node = world.create();
        const Entity leaf = world.create();
        REQUIRE(world.setTransform(left, at(-5.0, 0.0, 0.0)));
        REQUIRE(world.setTransform(right, at(5.0, 0.0, 0.0)));
        REQUIRE(world.setTransform(node, at(0.0, 1.0, 0.0)));
        REQUIRE(world.setTransform(leaf, at(0.0, 0.0, 1.0)));
        REQUIRE(world.setParent(node, left));
        REQUIRE(world.setParent(leaf, node));
        CHECK(world.updateTransforms() == 4u);
        CHECK(near(world.transform(leaf)->worldPosition, {-5.0, 1.0, 1.0}));

        REQUIRE(world.setParent(node, right));
        CHECK(world.dirtyTransformCount() == 1u);
        CHECK(world.updateTransforms() == 2u); // node et leaf, pas left ni right
        CHECK(near(world.transform(leaf)->worldPosition, {5.0, 1.0, 1.0}));
        CHECK(world.setParent(node, right)); // même parent : rien à recalculer
        CHECK(world.updateTransforms() == 0u);
    }

    TEST_CASE("Transforms: recomputed only when dirty")
    {
        World world;
        std::vector<Entity> chain;
        for (int i = 0; i < 5; ++i)
        {
            chain.push_back(world.create());
            REQUIRE(world.setTransform(chain.back(), at(1.0, 0.0, 0.0)));
            if (i > 0)
                REQUIRE(world.setParent(chain.back(), chain[static_cast<std::size_t>(i - 1)]));
        }
        const Entity other = world.create();
        REQUIRE(world.setTransform(other, at(0.0, 0.0, 0.0)));
        CHECK(world.updateTransforms() == 6u);
        CHECK(world.updateTransforms() == 0u); // rien de sale
        CHECK(near(world.transform(chain[4])->worldPosition, {5.0, 0.0, 0.0}));

        world.editTransform(chain[2])->position.x = 2.0;
        world.markTransformDirty(chain[3]); // déjà couvert par chain[2]
        CHECK(world.updateTransforms() == 3u); // chain[2], [3], [4]
        CHECK(near(world.transform(chain[4])->worldPosition, {6.0, 0.0, 0.0}));
        CHECK(near(world.transform(chain[1])->worldPosition, {2.0, 0.0, 0.0}));

        // Écriture directe par la vue : l'appelant marque l'entité.
        world.view(world.transformType()).each([&](Entity e, Transform &t) {
            if (e == other)
                t.position.y = 4.0;
        });
        world.markTransformDirty(other);
        CHECK(world.updateTransforms() == 1u);
        CHECK(near(world.transform(other)->worldPosition, {0.0, 4.0, 0.0}));

        // Une entité sale détruite avant la mise à jour est ignorée.
        world.editTransform(chain[4]);
        world.destroy(chain[4]);
        CHECK(world.updateTransforms() == 0u);
    }

    TEST_CASE("Transforms: precision at 10^7 m, error under 1 mm relative to the camera")
    {
        World world;
        const Entity planet = world.create();
        const Entity ship = world.create();
        const Entity part = world.create();
        Transform planetT = at(1.0e7, -3.0e6, 1.0e7);
        planetT.rotation = quarterTurnY();
        REQUIRE(world.setTransform(planet, planetT));
        REQUIRE(world.setTransform(ship, at(1234.5678, 9.25, -4321.125)));
        REQUIRE(world.setTransform(part, at(0.0123, 0.5, -0.75)));
        REQUIRE(world.setParent(ship, planet));
        REQUIRE(world.setParent(part, ship));
        world.updateTransforms();

        // Position exacte : rotation d'un quart de tour (x, z) -> (z, -x), puis translations.
        const core::DVec3 exact = core::DVec3(1.0e7, -3.0e6, 1.0e7) + core::DVec3(-4321.125 - 0.75, 9.25 + 0.5, -(1234.5678 + 0.0123));
        const core::WorldPosition camera(exact + core::DVec3(2.3, 1.1, -3.7));
        const core::Mat4 m = relativeMatrix(*world.transform(part), camera);
        const core::Vec3 rel(m[3]);
        const core::DVec3 expected = exact - camera.value;
        const double error = glm::length(core::DVec3(rel) - expected);
        MESSAGE("error at 10^7 m: " << error * 1000.0 << " mm");
        CHECK(error < 1.0e-3);

        // Sans origine flottante, un float à 10^7 m a un pas de 1 m : l'erreur dépasserait le millimètre.
        const float naive = static_cast<float>(exact.x) + 0.3f;
        CHECK(std::abs(static_cast<double>(naive) - (exact.x + 0.3)) > 1.0e-3);
    }

    TEST_CASE("Transforms: updating 100 000 entities in a hierarchy")
    {
        World world;
        std::vector<Entity> roots;
        for (int r = 0; r < 1000; ++r)
        {
            const Entity root = world.create();
            REQUIRE(world.setTransform(root, at(static_cast<double>(r), 0.0, 0.0)));
            roots.push_back(root);
            for (int c = 0; c < 99; ++c)
            {
                const Entity child = world.create();
                REQUIRE(world.setTransform(child, at(0.0, 1.0, 0.0)));
                REQUIRE(world.setParent(child, root));
            }
        }
        const auto t0 = std::chrono::steady_clock::now();
        const std::size_t all = world.updateTransforms();
        const auto t1 = std::chrono::steady_clock::now();
        CHECK(all == 100000u);
        world.editTransform(roots[10])->position.z = 1.0;
        const std::size_t some = world.updateTransforms();
        CHECK(some == 100u);
        CHECK(near(world.transform(world.firstChild(roots[10]))->worldPosition, {10.0, 1.0, 1.0}));
        MESSAGE("updateTransforms, 100000 entities: " << std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count()
                                                       << " us");
    }
}
