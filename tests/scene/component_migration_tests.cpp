// GAME-03 (A79 lot L6) : migration d'un type de composant par le moteur, à partir des descriptions copiées.
#include <doctest/doctest.h>

#include <orosmo/scene/component_migration.hpp>
#include <orosmo/scene/world.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

using namespace orosmo;
using core::ErrorCode;

namespace
{
struct Inner
{
    float a = 1.f;
};
struct InnerWider
{
    float a = 1.f;
    float b = 2.f;
};
struct MoverV1
{
    std::int64_t hp = 10;
    float speed = 1.5f;
    std::int32_t ammo = 3;
    Inner inner{};
};
// Champ ajouté (au milieu : décalages différents), sous-structure élargie.
struct MoverAdded
{
    std::int64_t hp = 10;
    double armor = 0.25;
    float speed = 1.5f;
    std::int32_t ammo = 3;
    InnerWider inner{};
};
// `ammo` retiré, `hp` entier -> réel (convertible), `speed` réel -> vecteur (non convertible).
struct MoverChanged
{
    double hp = -1.0;
    core::Vec3 speed{9.f, 9.f, 9.f};
    Inner inner{};
};
} // namespace

template <> struct orosmo::core::Reflect<Inner>
{
    static constexpr auto fields() { return std::make_tuple(field("a", &Inner::a)); }
};
template <> struct orosmo::core::Reflect<InnerWider>
{
    static constexpr auto fields() { return std::make_tuple(field("a", &InnerWider::a), field("b", &InnerWider::b)); }
};
template <> struct orosmo::core::Reflect<MoverV1>
{
    static constexpr auto fields()
    {
        return std::make_tuple(field("hp", &MoverV1::hp), field("speed", &MoverV1::speed), field("ammo", &MoverV1::ammo),
                               field("inner", &MoverV1::inner));
    }
};
template <> struct orosmo::core::Reflect<MoverAdded>
{
    static constexpr int kVersion = 2;
    static constexpr auto fields()
    {
        return std::make_tuple(field("hp", &MoverAdded::hp), field("armor", &MoverAdded::armor),
                               field("speed", &MoverAdded::speed), field("ammo", &MoverAdded::ammo),
                               field("inner", &MoverAdded::inner));
    }
};
template <> struct orosmo::core::Reflect<MoverChanged>
{
    static constexpr auto fields()
    {
        return std::make_tuple(field("hp", &MoverChanged::hp), field("speed", &MoverChanged::speed),
                               field("inner", &MoverChanged::inner));
    }
};

namespace
{
// Monde avec trois entités « Mover » (v1), valeurs distinctes ; la deuxième sans composant pour vérifier l'ordre dense.
struct Fixture
{
    scene::World world;
    std::vector<scene::Entity> owners;
    scene::ComponentId oldId;

    Fixture()
    {
        auto type = world.registerComponent<MoverV1>("Mover");
        REQUIRE(type.has_value());
        oldId = type->id;
        for (int i = 0; i < 4; ++i)
        {
            const scene::Entity e = world.create();
            if (i == 1)
                continue;
            REQUIRE(world.add(e, *type, MoverV1{100 + i, 0.5f * static_cast<float>(i), 7 * i, {static_cast<float>(i)}}) != nullptr);
            owners.push_back(e);
        }
        // Ordre dense modifié (retrait puis ajout) : la migration doit le garder.
        REQUIRE(world.remove(owners.front(), *type));
        REQUIRE(world.add(owners.front(), *type, MoverV1{100, 0.f, 0, {0.f}}) != nullptr);
    }
};
} // namespace

TEST_SUITE("scene/migration")
{
    TEST_CASE("migrateComponent: added field -> common fields copied, new ones at their default value, order kept")
    {
        Fixture f;
        const std::vector<scene::Entity> before(f.world.componentEntities(f.oldId).begin(), f.world.componentEntities(f.oldId).end());
        auto report = f.world.migrateComponent(scene::describeComponent<MoverAdded>("Mover"));
        REQUIRE_MESSAGE(report.has_value(), (report ? std::string() : report.error().describe()));
        CHECK(report->kind == scene::MigrationKind::kCopied);
        CHECK(report->values == 3);
        CHECK(report->fromVersion == 1);
        CHECK(report->toVersion == 2);
        CHECK(report->warnings.empty());

        // Ancien identifiant refusé (génération suivante), même emplacement.
        CHECK(f.world.componentDesc(f.oldId) == nullptr);
        CHECK(report->id.index == f.oldId.index);
        CHECK(report->id.generation == f.oldId.generation + 1);
        CHECK_FALSE(f.world.componentType<MoverV1>("Mover").has_value());
        auto type = f.world.componentType<MoverAdded>("Mover");
        REQUIRE(type.has_value());

        const std::vector<scene::Entity> after(f.world.componentEntities(type->id).begin(), f.world.componentEntities(type->id).end());
        CHECK(after == before);
        for (const scene::Entity e : f.owners)
        {
            const MoverAdded *m = f.world.get(e, *type);
            REQUIRE(m != nullptr);
            const int i = static_cast<int>(m->hp - 100);
            CHECK(m->armor == 0.25);
            CHECK(m->speed == doctest::Approx(i == 0 ? 0.f : 0.5f * static_cast<float>(i)));
            CHECK(m->ammo == (i == 0 ? 0 : 7 * i));
            CHECK(m->inner.a == static_cast<float>(i));
            CHECK(m->inner.b == 2.f);
        }
    }

    TEST_CASE("migrateComponent: removed field or changed type -> restored by field name, losses reported once")
    {
        Fixture f;
        auto report = f.world.migrateComponent(scene::describeComponent<MoverChanged>("Mover"));
        REQUIRE(report.has_value());
        CHECK(report->kind == scene::MigrationKind::kRestored);
        CHECK(report->values == 3);
        // Un avertissement par champ perdu, pas par entité.
        REQUIRE(report->warnings.size() == 2);
        CHECK(std::ranges::any_of(report->warnings, [](const std::string &w) { return w.starts_with("Mover.ammo: field removed"); }));
        CHECK(std::ranges::any_of(report->warnings, [](const std::string &w) { return w.starts_with("Mover.speed: type changed"); }));

        auto type = f.world.componentType<MoverChanged>("Mover");
        REQUIRE(type.has_value());
        for (const scene::Entity e : f.owners)
        {
            const MoverChanged *m = f.world.get(e, *type);
            REQUIRE(m != nullptr);
            CHECK(m->hp >= 100.0); // entier relu en réel
            CHECK(m->speed == core::Vec3{9.f, 9.f, 9.f}); // non convertible : défaut
            CHECK(m->inner.a == static_cast<float>(m->hp - 100.0));
        }
    }

    TEST_CASE("migrateComponent: a removed sub-field is reported like a removed field")
    {
        Fixture f;
        REQUIRE(f.world.migrateComponent(scene::describeComponent<MoverAdded>("Mover")).has_value());
        auto back = f.world.migrateComponent(scene::describeComponent<MoverV1>("Mover")); // `armor` et `inner.b` retirés
        REQUIRE(back.has_value());
        CHECK(back->kind == scene::MigrationKind::kRestored);
        REQUIRE(back->warnings.size() == 2); // une fois chacun, pas une fois par entité
        CHECK(std::ranges::any_of(back->warnings, [](const std::string &w) { return w.starts_with("Mover.armor: field removed"); }));
        CHECK(std::ranges::any_of(back->warnings, [](const std::string &w) { return w.starts_with("Mover.inner.b: unknown field ignored"); }));
        auto type = f.world.componentType<MoverV1>("Mover");
        REQUIRE(type.has_value());
        CHECK(f.world.get(f.owners.back(), *type)->inner.a == 3.f);
    }

    TEST_CASE("migrateComponent: same layout keeps the identifier; errors for unknown types, Transform, invalid layouts")
    {
        Fixture f;
        auto same = f.world.migrateComponent(scene::describeComponent<MoverV1>("Mover"));
        REQUIRE(same.has_value());
        CHECK(same->id == f.oldId);
        CHECK(same->warnings.empty());

        auto unknown = f.world.migrateComponent(scene::describeComponent<MoverV1>("Nope"));
        REQUIRE_FALSE(unknown.has_value());
        CHECK((unknown.error().code == ErrorCode::kNotFound));

        auto transform = f.world.migrateComponent(scene::describeComponent<MoverV1>("Transform"));
        REQUIRE_FALSE(transform.has_value());
        CHECK((transform.error().code == ErrorCode::kInvalidArgument));

        scene::ComponentDesc broken = scene::describeComponent<MoverAdded>("Mover");
        broken.size = 3;
        auto invalid = f.world.migrateComponent(std::move(broken));
        REQUIRE_FALSE(invalid.has_value());
        CHECK((invalid.error().code == ErrorCode::kInvalidArgument));
        CHECK(f.world.componentDesc(f.oldId) != nullptr); // rien n'a changé
    }

    TEST_CASE("classifyMigration: only additions (also in sub-structures) are a copy")
    {
        const auto v1 = core::describeFields<MoverV1>();
        CHECK(scene::classifyMigration(v1, core::describeFields<MoverAdded>()) == scene::MigrationKind::kCopied);
        CHECK(scene::classifyMigration(core::describeFields<MoverAdded>(), v1) == scene::MigrationKind::kRestored);
        CHECK(scene::classifyMigration(v1, core::describeFields<MoverChanged>()) == scene::MigrationKind::kRestored);
        CHECK(scene::classifyMigration(v1, v1) == scene::MigrationKind::kCopied);
    }
}
