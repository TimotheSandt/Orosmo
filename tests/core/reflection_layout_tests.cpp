// A79 lot L2 : la description de champs porte décalage, taille et sous-champs, pour stocker et lire un composant par nom
// sans connaître son type C++ (scene::World, sauvegarde L3, inspecteur L10).
#include <doctest/doctest.h>

#include <orosmo/core/reflection.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <tuple>

using namespace orosmo::core;

namespace
{
struct Inner
{
    std::uint8_t flag = 0;
    double value = 0.0;
};

struct Outer
{
    std::int16_t small = 0;
    Inner inner;
    DVec3 position{0.0};
    Quat rotation{1.f, 0.f, 0.f, 0.f};
    DQuat precise{1.0, 0.0, 0.0, 0.0};
    float weight = 1.f;
};

// Pas de constructeur par défaut : le décalage se calcule sans construire l'objet.
struct NoDefault
{
    explicit NoDefault(int v) : a(v) {}
    int a;
    std::string text;
};
} // namespace

template <> struct orosmo::core::Reflect<Inner>
{
    static constexpr auto fields() { return std::make_tuple(field("flag", &Inner::flag), field("value", &Inner::value)); }
};

template <> struct orosmo::core::Reflect<Outer>
{
    static constexpr auto fields()
    {
        return std::make_tuple(field("small", &Outer::small), field("inner", &Outer::inner), field("position", &Outer::position),
                               field("rotation", &Outer::rotation), field("precise", &Outer::precise),
                               field("weight", &Outer::weight, range(0.0, 2.0)));
    }
};

template <> struct orosmo::core::Reflect<NoDefault>
{
    static constexpr auto fields() { return std::make_tuple(field("a", &NoDefault::a), field("text", &NoDefault::text)); }
};

TEST_SUITE("core/reflection")
{
    TEST_CASE("describeFields gives offsets and sizes equal to offsetof and sizeof")
    {
        const auto d = describeFields<Outer>();
        REQUIRE(d.size() == 6u);
        CHECK(d[0].offset == offsetof(Outer, small));
        CHECK(d[0].size == sizeof(std::int16_t));
        CHECK((d[0].type == FieldType::kInt));
        CHECK(d[1].offset == offsetof(Outer, inner));
        CHECK(d[1].size == sizeof(Inner));
        CHECK((d[1].type == FieldType::kStruct));
        CHECK(d[2].offset == offsetof(Outer, position));
        CHECK(d[2].size == sizeof(DVec3));
        CHECK((d[2].type == FieldType::kDVec3));
        CHECK(d[3].offset == offsetof(Outer, rotation));
        CHECK((d[3].type == FieldType::kQuat));
        CHECK(d[4].offset == offsetof(Outer, precise));
        CHECK((d[4].type == FieldType::kDQuat));
        CHECK(d[4].size == sizeof(DQuat));
        CHECK(d[5].offset == offsetof(Outer, weight));
        CHECK(d[5].range.bounded);
        for (const auto &f : d)
            CHECK(f.offset + f.size <= sizeof(Outer));
    }

    TEST_CASE("a reflected member is described by its sub-fields, offsets relative to the sub-structure")
    {
        const auto d = describeFields<Outer>();
        const auto &sub = d[1].fields;
        REQUIRE(sub.size() == 2u);
        CHECK(sub[0].name == "flag");
        CHECK(sub[0].offset == offsetof(Inner, flag));
        CHECK(sub[0].size == 1u);
        CHECK((sub[0].type == FieldType::kUInt));
        CHECK(sub[1].offset == offsetof(Inner, value));
        CHECK((sub[1].type == FieldType::kDouble));
        CHECK(d[0].fields.empty()); // un champ simple n'a pas de sous-champ
    }

    TEST_CASE("a type without default constructor has unknown offsets (no member address taken on an unbuilt object)")
    {
        const auto d = describeFields<NoDefault>();
        REQUIRE(d.size() == 2u);
        CHECK(d[0].offset == FieldDesc::kUnknownOffset);
        CHECK(d[1].offset == FieldDesc::kUnknownOffset);
        CHECK(d[1].size == sizeof(std::string));
        CHECK(describeFields<Outer>() == describeFields<Outer>());
        auto changed = describeFields<Outer>();
        changed[1].fields[1].offset += 1;
        CHECK_FALSE(changed == describeFields<Outer>());
    }
}
