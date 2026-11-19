// CORE-12 (1/2) : réflexion des champs. Une structure se décrit en UN endroit (nom, pointeur de membre, plage) ;
// sérialisation, interface automatique (UI-12) et migration d'état en dérivent. Aucune macro, aucun code généré.
//
//   struct TerrainParams { float frequency = 1.f; int octaves = 4; };
//   template <> struct engine::core::Reflect<TerrainParams> {
//       static constexpr int kVersion = 1;
//       static constexpr auto fields() {
//           return std::make_tuple(field("frequency", &TerrainParams::frequency, range(0.0, 16.0)),
//                                  field("octaves",   &TerrainParams::octaves,   range(1, 16)));
//       }
//   };
#pragma once

#include <engine/core/math.hpp>

#include <cstdint>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

namespace engine::core
{

template <typename T> struct Reflect; // à spécialiser ; sans spécialisation, T n'est pas réfléchi

// Noms des valeurs d'une énumération continue à partir de 0 (interface automatique : liste déroulante, UI-12). À spécialiser :
//   template <> struct engine::core::EnumNames<Basis> { static constexpr std::array<const char *, 2> names{"perlin", "simplex"}; };
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
    else if constexpr (IsVector<T>::value) return FieldType::kArray;
    else return FieldType::kStruct;
}

// Description d'un champ pour l'interface automatique : nom, type, plage, texte d'aide.
struct FieldDesc
{
    std::string name;
    FieldType type;
    Range range;
    std::string description;
};

template <Reflected T> std::vector<FieldDesc> describeFields()
{
    std::vector<FieldDesc> out;
    std::apply(
        [&](const auto &...f) {
            (out.push_back(FieldDesc{f.name, fieldTypeOf<typename std::remove_cvref_t<decltype(f)>::Member>(), f.range,
                                     f.description}),
             ...);
        },
        Reflect<T>::fields());
    return out;
}

// Appelle fn(name, valeur&, plage) sur chaque champ.
template <typename T, typename Fn>
    requires Reflected<std::remove_const_t<T>>
void forEachField(T &object, Fn &&fn)
{
    std::apply([&](const auto &...f) { (fn(f.name, object.*(f.member), f.range), ...); }, Reflect<std::remove_const_t<T>>::fields());
}

template <Reflected T> constexpr int versionOf() { return Reflect<T>::kVersion; }

} // namespace engine::core
