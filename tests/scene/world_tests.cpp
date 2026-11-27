// ECS-01 (A79 lot L2) : entités, composants enregistrés par nom, stockage contigu, requêtes.
#include <doctest/doctest.h>

#include <orosmo/scene/world.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

using namespace orosmo;
using namespace orosmo::scene;

namespace
{
struct Position
{
    core::Vec3 value{0.f};
};

struct Velocity
{
    core::Vec3 value{1.f, 2.f, 3.f};
    float drag = 0.5f;
};

struct alignas(64) Wide
{
    std::uint64_t a = 7;
};

struct Tag
{
};

struct NoDefaultData
{
    explicit NoDefaultData(int v) : value(v) {}
    int value;
};

template <typename W>
concept CanViewNothing = requires(W &w) { w.view(); };
} // namespace

template <> struct orosmo::core::Reflect<Position>
{
    static constexpr auto fields() { return std::make_tuple(field("value", &Position::value)); }
};
template <> struct orosmo::core::Reflect<Velocity>
{
    static constexpr int kVersion = 3;
    static constexpr auto fields() { return std::make_tuple(field("value", &Velocity::value), field("drag", &Velocity::drag)); }
};
template <> struct orosmo::core::Reflect<Wide>
{
    static constexpr auto fields() { return std::make_tuple(field("a", &Wide::a)); }
};
template <> struct orosmo::core::Reflect<NoDefaultData>
{
    static constexpr auto fields() { return std::make_tuple(field("value", &NoDefaultData::value)); }
};
template <> struct orosmo::core::Reflect<Tag>
{
    static constexpr auto fields() { return std::tuple<>{}; }
};

TEST_SUITE("scene/world")
{
    TEST_CASE("World: a destroyed entity is invalidated by its generation, even when its index is reused")
    {
        World world;
        const Entity a = world.create();
        const Entity b = world.create();
        CHECK(world.alive(a));
        CHECK(world.entityCount() == 2u);
        CHECK(world.destroy(a));
        CHECK_FALSE(world.alive(a));
        CHECK_FALSE(world.destroy(a)); // deuxième destruction refusée
        const Entity c = world.create();
        CHECK(c.index == a.index); // indice réutilisé...
        CHECK(c.generation != a.generation); // ...génération différente
        CHECK_FALSE(world.alive(a));
        CHECK(world.alive(c));
        CHECK(world.alive(b));
        CHECK_FALSE(world.alive(kNullEntity));
        CHECK(world.entityCount() == 2u);
        CHECK(world.entities() == std::vector<Entity>{c, b});
    }

    TEST_CASE("World: an old entity id does not reach the components of the entity that reuses its index")
    {
        World world;
        auto pos = world.registerComponent<Position>("Position");
        REQUIRE(pos);
        const Entity a = world.create();
        REQUIRE(world.add(a, *pos, Position{{1.f, 1.f, 1.f}}) != nullptr);
        world.destroy(a);
        const Entity c = world.create();
        REQUIRE(c.index == a.index);
        CHECK_FALSE(world.has(c, *pos)); // composants retirés à la destruction
        world.add(c, *pos, Position{{5.f, 0.f, 0.f}});
        CHECK(world.get(a, *pos) == nullptr);
        CHECK_FALSE(world.has(a, *pos));
        CHECK(world.add(a, *pos) == nullptr);
        CHECK_FALSE(world.remove(a, *pos));
        CHECK(world.get(c, *pos)->value.x == 5.f);
    }

    TEST_CASE("World: components registered by name with a copied description; default value, version, fields")
    {
        World world;
        ComponentId id;
        {
            auto desc = describeComponent<Velocity>(std::string("Velocity"));
            auto r = world.registerComponent(std::move(desc));
            REQUIRE(r);
            id = *r;
        } // la description d'origine n'existe plus : le monde garde sa copie
        const ComponentDesc *d = world.componentDesc(id);
        REQUIRE(d != nullptr);
        CHECK(d->name == "Velocity");
        CHECK(d->size == sizeof(Velocity));
        CHECK(d->alignment == alignof(Velocity));
        CHECK(d->version == 3);
        REQUIRE(d->fields.size() == 2u);
        CHECK(d->fields[1].name == "drag");
        CHECK(d->fields[1].offset == offsetof(Velocity, drag));

        const Entity e = world.create();
        auto slot = world.add(e, "Velocity"); // par nom, valeur par défaut
        REQUIRE(slot);
        float drag = 0.f;
        std::memcpy(&drag, static_cast<const std::byte *>(*slot) + d->fields[1].offset, sizeof drag);
        CHECK(drag == 0.5f);

        auto typed = world.componentType<Velocity>("Velocity");
        REQUIRE(typed);
        CHECK(world.get(e, *typed)->value.y == 2.f);
        CHECK(world.componentId("Velocity")->index == id.index);
        CHECK(world.componentIds().size() == 2u); // Transform + Velocity
    }

    TEST_CASE("World: an unknown component is refused by name")
    {
        World world;
        const Entity e = world.create();
        auto missing = world.componentId("Nope");
        REQUIRE_FALSE(missing);
        CHECK((missing.error().code == core::ErrorCode::kNotFound));
        CHECK(missing.error().message.find("Nope") != std::string::npos);
        auto added = world.add(e, "Nope");
        REQUIRE_FALSE(added);
        CHECK((added.error().code == core::ErrorCode::kNotFound));
        CHECK_FALSE(world.componentType<Position>("Nope"));
        CHECK_FALSE(world.unregisterComponent("Nope"));
    }

    TEST_CASE("World: re-registering the same layout is idempotent; another layout under the same name is refused")
    {
        World world;
        auto a = world.registerComponent<Velocity>("Velocity");
        auto b = world.registerComponent<Velocity>("Velocity");
        REQUIRE(a);
        REQUIRE(b);
        CHECK(a->id == b->id);
        auto clash = world.registerComponent<Position>("Velocity");
        REQUIRE_FALSE(clash);
        CHECK((clash.error().code == core::ErrorCode::kAlreadyExists));
        // Type C++ qui ne correspond pas à la description enregistrée.
        CHECK_FALSE(world.componentType<Position>("Velocity"));
        CHECK_FALSE(world.componentType<Wide>("Velocity"));
    }

    TEST_CASE("World: invalid descriptions are refused")
    {
        World world;
        auto refused = [&](ComponentDesc d) { return !world.registerComponent(std::move(d)).has_value(); };
        ComponentDesc ok = describeComponent<Velocity>("V");
        ComponentDesc d = ok;
        d.name.clear();
        CHECK(refused(d));
        d = ok;
        d.alignment = 3;
        CHECK(refused(d));
        d = ok;
        d.size = 0;
        CHECK(refused(d));
        d = ok;
        d.size = ok.size + 1; // pas un multiple de l'alignement
        d.defaultValue.resize(d.size); // seul l'alignement est fautif dans ce cas
        CHECK(refused(d));
        d = ok;
        d.defaultValue.resize(2);
        CHECK(refused(d));
        d = ok;
        d.fields[1].offset = ok.size; // hors de la structure
        CHECK(refused(d));
        d = ok;
        d.fields[1].type = core::FieldType::kString; // non trivialement copiable
        CHECK(refused(d));
        d = ok;
        d.fields[1].name = d.fields[0].name; // deux champs de même nom au même niveau : une seule clé sauvegardée
        CHECK(refused(d));
        d = ok;
        d.version = -1; // écrite à la sauvegarde puis refusée au chargement
        CHECK(refused(d));
        CHECK_FALSE(world.componentId("V"));
        d = ok;
        d.version = 0; // 0 reste permis
        CHECK(world.registerComponent(d));
        REQUIRE(world.unregisterComponent("V"));
        CHECK(world.registerComponent(ok));
    }

    TEST_CASE("World: the same field name is allowed in two distinct structures")
    {
        World world;
        auto leaf = [](std::size_t offset) {
            core::FieldDesc f;
            f.name = "s";
            f.type = core::FieldType::kStruct;
            f.offset = offset;
            f.size = 4;
            core::FieldDesc x;
            x.name = "x";
            x.type = core::FieldType::kFloat;
            x.size = 4;
            f.fields.push_back(x);
            return f;
        };
        ComponentDesc d;
        d.name = "Pair";
        d.size = 8;
        d.alignment = 4;
        d.fields = {leaf(0), leaf(4)};
        d.fields[1].name = "t";
        CHECK(world.registerComponent(d));
        d.name = "Twice";
        d.fields[1].fields.push_back(d.fields[1].fields[0]); // t.x deux fois
        CHECK_FALSE(world.registerComponent(d));
    }

    TEST_CASE("World: unregistering removes the data and invalidates the id; Transform stays")
    {
        World world;
        auto vel = world.registerComponent<Velocity>("Velocity");
        REQUIRE(vel);
        const Entity e = world.create();
        world.add(e, *vel);
        REQUIRE(world.unregisterComponent("Velocity"));
        CHECK_FALSE(world.has(e, *vel));
        CHECK(world.componentDesc(vel->id) == nullptr);
        auto again = world.registerComponent<Position>("Position"); // emplacement réutilisé, nouvelle génération
        REQUIRE(again);
        CHECK(again->id.index == vel->id.index);
        CHECK_FALSE(again->id == vel->id);
        CHECK(world.get(e, *vel) == nullptr);
        CHECK_FALSE(world.unregisterComponent("Transform"));
    }

    TEST_CASE("World: storage is contiguous and aligned, removal keeps it dense")
    {
        World world;
        auto wide = world.registerComponent<Wide>("Wide");
        auto tag = world.registerComponent<Tag>("Tag");
        REQUIRE(wide);
        REQUIRE(tag);
        std::vector<Entity> es;
        for (int i = 0; i < 100; ++i)
        {
            es.push_back(world.create());
            Wide *w = world.add(es.back(), *wide, Wide{static_cast<std::uint64_t>(i)});
            REQUIRE(w != nullptr);
            CHECK(reinterpret_cast<std::uintptr_t>(w) % 64u == 0u);
            if (i % 2 == 0)
                world.add(es.back(), *tag);
        }
        const Wide *first = world.get(es[0], *wide);
        CHECK(world.get(es[1], *wide) == first + 1); // contigu
        for (int i = 0; i < 100; i += 3)
            CHECK(world.remove(es[static_cast<std::size_t>(i)], *wide));
        std::size_t n = 0;
        std::uint64_t sum = 0;
        world.view(*wide).each([&](Entity e, Wide &w) {
            CHECK(w.a == e.index);
            sum += w.a;
            ++n;
        });
        CHECK(n == 66u);
        std::uint64_t expected = 0;
        for (std::uint64_t i = 0; i < 100; ++i)
            if (i % 3 != 0)
                expected += i;
        CHECK(sum == expected);
        CHECK(world.view(*wide, *tag).count() == 33u);
    }

    TEST_CASE("World: untyped forEach and forEachComponent see the same data as the typed view")
    {
        World world;
        auto pos = world.registerComponent<Position>("Position");
        auto vel = world.registerComponent<Velocity>("Velocity");
        REQUIRE(pos);
        REQUIRE(vel);
        for (int i = 0; i < 10; ++i)
        {
            const Entity e = world.create();
            world.add(e, *pos, Position{{static_cast<float>(i), 0.f, 0.f}});
            if (i < 4)
                world.add(e, *vel);
        }
        const ComponentId ids[] = {vel->id, pos->id};
        int n = 0;
        world.forEach(ids, [&](Entity e, std::span<void *const> data) {
            REQUIRE(data.size() == 2u);
            CHECK(static_cast<Velocity *>(data[0])->drag == 0.5f);
            CHECK(static_cast<Position *>(data[1])->value.x == static_cast<float>(e.index));
            ++n;
        });
        CHECK(n == 4);
        std::vector<std::string> names;
        world.forEachComponent(Entity{0, 0}, [&](ComponentId id, void *) { names.push_back(world.componentDesc(id)->name); });
        CHECK(names == std::vector<std::string>{"Position", "Velocity"});
    }

    TEST_CASE("World: copy is deep")
    {
        World world;
        auto pos = world.registerComponent<Position>("Position");
        REQUIRE(pos);
        const Entity e = world.create();
        world.add(e, *pos, Position{{1.f, 2.f, 3.f}});
        World copy = world;
        world.get(e, *pos)->value.x = 10.f;
        world.destroy(world.create());
        CHECK(copy.get(e, *pos)->value.x == 1.f);
        CHECK(copy.entityCount() == 1u);
        World moved = std::move(copy);
        CHECK(moved.get(e, *pos)->value.z == 3.f);
    }

    TEST_CASE("World: iterating 100 000 entities (time measured by apps/demo/benchmark, scene-view)")
    {
        constexpr int kCount = 100000;
        World world;
        auto pos = world.registerComponent<Position>("Position");
        auto vel = world.registerComponent<Velocity>("Velocity");
        REQUIRE(pos);
        REQUIRE(vel);
        for (int i = 0; i < kCount; ++i)
        {
            const Entity e = world.create();
            world.add(e, *pos);
            if (i % 2 == 0)
                world.add(e, *vel);
        }
        CHECK(world.entityCount() == static_cast<std::size_t>(kCount));
        std::size_t n = 0;
        world.view(*pos).each([&](Entity, Position &p) {
            p.value.x += 1.f;
            ++n;
        });
        std::size_t moved = 0;
        world.view(*pos, *vel).each([&](Entity, Position &p, const Velocity &v) {
            p.value += v.value * 0.5f;
            ++moved;
        });
        CHECK(n == static_cast<std::size_t>(kCount));
        CHECK(moved == static_cast<std::size_t>(kCount / 2));
        CHECK(world.get(Entity{0, 0}, *pos)->value.x == 1.5f);
        CHECK(world.get(Entity{1, 0}, *pos)->value.x == 1.f);
    }

    TEST_CASE("World: adding a component whose value lies in the same full storage (regression: copy before growing)")
    {
        World world;
        auto pos = world.registerComponent<Position>("Position");
        REQUIRE(pos);
        std::vector<Entity> es;
        for (int i = 0; i < 16; ++i) // capacité initiale : 16
        {
            es.push_back(world.create());
            REQUIRE(world.add(es.back(), *pos, Position{{static_cast<float>(i) + 0.5f, 1.f, 2.f}}) != nullptr);
        }
        const Entity extra = world.create();
        const Position *source = world.get(es[3], *pos);
        Position *added = world.add(extra, *pos, *source); // le stockage s'agrandit : `source` est libéré pendant l'ajout
        REQUIRE(added != nullptr);
        CHECK(added->value.x == 3.5f);
        CHECK(added->value.z == 2.f);
        CHECK(world.get(es[3], *pos)->value.x == 3.5f);
    }

    TEST_CASE("World: a view keeps component ids; it survives new registrations and becomes invalid on unregistration")
    {
        World world;
        auto pos = world.registerComponent<Position>("Position");
        auto vel = world.registerComponent<Velocity>("Velocity");
        REQUIRE(pos);
        REQUIRE(vel);
        const Entity e = world.create();
        world.add(e, *pos);
        world.add(e, *vel);
        auto view = world.view(*pos, *vel);
        // Beaucoup de nouveaux types : la table des types est réallouée.
        for (int i = 0; i < 64; ++i)
            REQUIRE(world.registerComponent<Tag>("Tag" + std::to_string(i)));
        CHECK(view.valid());
        CHECK(view.count() == 1u);
        REQUIRE(world.unregisterComponent("Velocity"));
        CHECK_FALSE(view.valid());
        CHECK(view.count() == 0u);
    }

    TEST_CASE("World: no new type registered or removed during an iteration; the guard survives an exception")
    {
        World world;
        auto pos = world.registerComponent<Position>("Position");
        REQUIRE(pos);
        world.add(world.create(), *pos);
        int calls = 0;
        world.view(*pos).each([&](Entity, Position &) {
            ++calls;
            auto refused = world.registerComponent<Velocity>("Velocity");
            REQUIRE_FALSE(refused);
            CHECK((refused.error().code == core::ErrorCode::kBusy));
            CHECK(world.registerComponent<Position>("Position")); // même type, même disposition : permis
            CHECK_FALSE(world.unregisterComponent("Position"));
        });
        CHECK(calls == 1);
        CHECK(world.registerComponent<Velocity>("Velocity")); // parcours terminé
        const ComponentId ids[] = {pos->id};
        bool thrown = false;
        try
        {
            world.forEach(ids, [](Entity, std::span<void *const>) { throw std::runtime_error("callback failure"); });
        }
        catch (const std::runtime_error &)
        {
            thrown = true;
        }
        CHECK(thrown);
        CHECK(world.registerComponent<Wide>("Wide")); // le compteur de parcours est revenu à zéro
    }

    TEST_CASE("World: help texts and ranges are not part of the layout")
    {
        World world;
        REQUIRE(world.registerComponent<Velocity>("Velocity"));
        ComponentDesc d = describeComponent<Velocity>("Velocity");
        d.fields[1].description = "air drag";
        d.fields[1].range = core::range(0.0, 1.0);
        auto again = world.registerComponent(d);
        REQUIRE(again);
        CHECK(world.componentDesc(*again)->fields[1].description == "air drag");
        CHECK(world.componentType<Velocity>("Velocity")); // le type C++ reste accepté
    }

    TEST_CASE("World: oversized components and types without default constructor are refused")
    {
        World world;
        ComponentDesc huge = describeComponent<Wide>("Huge");
        huge.size = std::size_t{1} << 60;
        huge.defaultValue.clear();
        huge.fields.clear();
        CHECK_FALSE(world.registerComponent(huge));
        huge.size = ComponentDesc::kMaxSize + 64u;
        CHECK_FALSE(world.registerComponent(huge));
        // Décalages inconnus (core::FieldDesc::kUnknownOffset) : refusé.
        auto noDefault = world.registerComponent<NoDefaultData>("NoDefault");
        REQUIRE_FALSE(noDefault);
        CHECK((noDefault.error().code == core::ErrorCode::kInvalidArgument));
    }

    TEST_CASE("World: a copy made during an iteration is not busy")
    {
        World world;
        auto vel = world.registerComponent<Velocity>("Velocity");
        REQUIRE(vel);
        world.add(world.create(), *vel);
        std::unique_ptr<World> copy;
        world.view(*vel).each([&](Entity, Velocity &) { copy = std::make_unique<World>(world); });
        REQUIRE(copy);
        CHECK(copy->registerComponent<Position>("Position")); // pas kBusy : le compteur du parcours n'est pas copié
        World assigned;
        world.view(*vel).each([&](Entity, Velocity &) { assigned = world; });
        CHECK(assigned.registerComponent<Wide>("Wide"));
        CHECK(world.registerComponent<Position>("Position")); // la source est libre après le parcours
    }

    TEST_CASE("World: after an assignment, a view of a type the source does not have is refused")
    {
        struct Health
        {
            float hp = 100.f;
            float shield = 1.f;
            float regen = 0.f;
            float armor = 0.f;
        };
        World a;
        auto vel = a.registerComponent<Velocity>("Velocity");
        REQUIRE(vel);
        a.add(a.create(), *vel);
        World b;
        ComponentDesc health;
        health.name = "Health";
        health.size = sizeof(Health);
        health.alignment = alignof(Health);
        auto hp = b.registerComponent(health); // même emplacement que Velocity dans `a`
        REQUIRE(hp);
        REQUIRE(hp->index == vel->id.index);
        b.add(b.create(), *hp);
        auto view = a.view(*vel);
        REQUIRE(view.valid());
        a = b;
        CHECK_FALSE(view.valid()); // ne lit jamais un Health comme un Velocity
        CHECK(a.componentDesc(vel->id) == nullptr);
        CHECK(a.componentDesc(*hp) != nullptr); // les identifiants de la source restent valides dans sa copie
        std::size_t seen = 0;
        view.each([&](Entity, Velocity &) { ++seen; });
        CHECK(seen == 0);
        World moved;
        auto other = moved.registerComponent<Velocity>("Velocity");
        REQUIRE(other);
        auto movedView = moved.view(*other);
        moved = World{};
        CHECK_FALSE(movedView.valid());
    }

    TEST_CASE("World: restoring a copy of the same registry keeps ids and views valid")
    {
        World world;
        auto vel = world.registerComponent<Velocity>("Velocity");
        REQUIRE(vel);
        const Entity e = world.create();
        world.add(e, *vel);
        const World saved = world;
        auto view = world.view(*vel);
        REQUIRE(world.unregisterComponent("Velocity"));
        REQUIRE(world.registerComponent<Position>("Position")); // même emplacement, autre type
        CHECK_FALSE(view.valid());
        world = saved;
        CHECK(view.valid());
        CHECK(world.get(e, *vel) != nullptr);
        CHECK(view.count() == 1);
    }

    TEST_CASE("World: a view needs at least one component type")
    {
        CHECK_FALSE(CanViewNothing<World>);
    }
}
