// CORE-12 (1/2) : réflexion des champs. Une structure se décrit en UN endroit (nom, pointeur de membre, plage) ;
// sérialisation, interface automatique (UI-12) et migration d'état en dérivent. Aucune macro, aucun code généré.
//
//   struct TerrainParams { float frequency = 1.f; int octaves = 4; };
//   template <> struct orosmo::core::Reflect<TerrainParams> {
//       static constexpr int kVersion = 1;
//       static constexpr auto fields() {
//           return std::make_tuple(field("frequency", &TerrainParams::frequency, range(0.0, 16.0)),
//                                  field("octaves",   &TerrainParams::octaves,   range(1, 16)));
//       }
//   };
#pragma once

#include <orosmo/core/math.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

namespace orosmo::core
{

template <typename T> struct Reflect; // à spécialiser ; sans spécialisation, T n'est pas réfléchi

// Noms des valeurs d'une énumération continue à partir de 0 (interface automatique : liste déroulante, UI-12). À spécialiser :
//   template <> struct orosmo::core::EnumNames<Basis> { static constexpr std::array<const char *, 2> names{"perlin", "simplex"}; };
// La sérialisation garde la valeur numérique (format inchangé).
template <typename E> struct EnumNames;
template <typename E>
concept NamedEnum = std::is_enum_v<E> && requires { EnumNames<E>::names; };

template <typename T>
concept Reflected = requires { Reflect<T>::fields(); };

struct Range
{
    double min = 0.0;
    double max = 0.0;
    bool bounded = false;
    bool contains(double v) const { return !bounded || (v >= min && v <= max); }
    friend bool operator==(const Range &, const Range &) = default;
};
constexpr Range range(double lo, double hi) { return Range{lo, hi, true}; }

template <typename C, typename M> struct Field
{
    const char *name;
    M C::*member;
    Range range;
    const char *description;
    using Class = C;
    using Member = M;
};

template <typename C, typename M>
constexpr Field<C, M> field(const char *name, M C::*member, Range r = {}, const char *description = "")
{
    return Field<C, M>{name, member, r, description};
}

enum class FieldType : std::uint8_t
{
    kBool,
    kInt,
    kUInt,
    kFloat,
    kDouble,
    kString,
    kVec2,
    kVec3,
    kVec4,
    kDVec3, // DVec3 : positions en double (ECS-02)
    kQuat,
    kDQuat,
    kArray,
    kStruct,
    kEnum,
};

template <typename T> struct IsVector : std::false_type {};
template <typename T, typename A> struct IsVector<std::vector<T, A>> : std::true_type {};

template <typename T> constexpr FieldType fieldTypeOf()
{
    if constexpr (std::is_same_v<T, bool>) return FieldType::kBool;
    else if constexpr (std::is_enum_v<T>) return FieldType::kEnum;
    else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) return FieldType::kInt;
    else if constexpr (std::is_integral_v<T>) return FieldType::kUInt;
    else if constexpr (std::is_same_v<T, float>) return FieldType::kFloat;
    else if constexpr (std::is_same_v<T, double>) return FieldType::kDouble;
    else if constexpr (std::is_same_v<T, std::string>) return FieldType::kString;
    else if constexpr (std::is_same_v<T, Vec2>) return FieldType::kVec2;
    else if constexpr (std::is_same_v<T, Vec3>) return FieldType::kVec3;
    else if constexpr (std::is_same_v<T, Vec4>) return FieldType::kVec4;
    else if constexpr (std::is_same_v<T, DVec3>) return FieldType::kDVec3;
    else if constexpr (std::is_same_v<T, Quat>) return FieldType::kQuat;
    else if constexpr (std::is_same_v<T, DQuat>) return FieldType::kDQuat;
    else if constexpr (IsVector<T>::value) return FieldType::kArray;
    else return FieldType::kStruct;
}

// Description d'un champ, copiable et autonome (chaînes possédées, aucun pointeur vers le type C++) : interface automatique
// (UI-12), et depuis M8 (A79 lot L2) stockage des composants par nom (`scene::ComponentDesc`), sauvegarde et inspecteur à
// l'exécution. `offset` et `size` : position et taille du champ dans la structure décrite ; `fields` : sous-champs d'une
// structure réfléchie (type kStruct), décalages relatifs à cette sous-structure. Une structure non réfléchie (kStruct sans
// sous-champ) est opaque : seuls `offset` et `size` la décrivent. Un type sans constructeur par défaut a `offset ==
// kUnknownOffset` : le décalage se mesure sur un objet construit (prendre l'adresse d'un membre d'un objet non construit est
// un comportement indéfini), et `scene` refuse une telle description.
struct FieldDesc
{
    static constexpr std::size_t kUnknownOffset = static_cast<std::size_t>(-1);

    std::string name;
    FieldType type = FieldType::kStruct;
    Range range;
    std::string description;
    std::size_t offset = 0;
    std::size_t size = 0;
    std::vector<FieldDesc> fields;

    friend bool operator==(const FieldDesc &, const FieldDesc &) = default;
};

template <Reflected T> std::vector<FieldDesc> describeFields();

namespace detail
{
// Décalage d'un membre, mesuré sur un objet vivant (`offsetof` ne prend pas de pointeur de membre).
template <typename T, typename M, typename C> std::size_t memberOffset(const T *object, M C::*member) noexcept
{
    if (object == nullptr)
        return FieldDesc::kUnknownOffset;
    const auto *base = reinterpret_cast<const unsigned char *>(std::addressof(*object));
    const auto *at = reinterpret_cast<const unsigned char *>(std::addressof(object->*member));
    return static_cast<std::size_t>(at - base);
}

template <typename T, typename F> FieldDesc describeField(const T *object, const F &f)
{
    using M = typename F::Member;
    FieldDesc d{f.name, fieldTypeOf<M>(), f.range, f.description, memberOffset(object, f.member), sizeof(M), {}};
    if constexpr (Reflected<M>)
        d.fields = describeFields<M>();
    return d;
}

template <typename T> std::vector<FieldDesc> describeFieldsOf(const T *object)
{
    std::vector<FieldDesc> out;
    std::apply([&](const auto &...f) { (out.push_back(describeField(object, f)), ...); }, Reflect<T>::fields());
    return out;
}
} // namespace detail

// Construit un T (valeur par défaut) pour mesurer les décalages ; sans constructeur par défaut : décalages inconnus.
template <Reflected T> std::vector<FieldDesc> describeFields()
{
    if constexpr (std::is_default_constructible_v<T>)
    {
        const T object{};
        return detail::describeFieldsOf<T>(&object);
    }
    else
        return detail::describeFieldsOf<T>(nullptr);
}

// Appelle fn(name, valeur&, plage) sur chaque champ.
template <typename T, typename Fn>
    requires Reflected<std::remove_const_t<T>>
void forEachField(T &object, Fn &&fn)
{
    std::apply([&](const auto &...f) { (fn(f.name, object.*(f.member), f.range), ...); }, Reflect<std::remove_const_t<T>>::fields());
}

template <Reflected T> constexpr int versionOf() { return Reflect<T>::kVersion; }

} // namespace orosmo::core
