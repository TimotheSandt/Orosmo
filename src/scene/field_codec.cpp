// ECS-03 (A79 lot L3) : champs décrits <-> JSON, et hachage des champs. Voir field_codec.hpp.
#include "field_codec.hpp"

#include <orosmo/core/math.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>

namespace orosmo::scene::detail
{

using core::ErrorCode;
using core::FieldDesc;
using core::FieldType;
using core::Json;
using core::Result;

namespace
{

// ── Lecture et écriture des scalaires (petit-boutiste de la machine : les octets restent dans la mémoire du processus) ──

template <typename T> T load(const std::byte *p) noexcept
{
    T v{};
    std::memcpy(&v, p, sizeof(T));
    return v;
}

template <typename T> void store(std::byte *p, T v) noexcept { std::memcpy(p, &v, sizeof(T)); }

bool validIntSize(std::size_t size) noexcept { return size == 1 || size == 2 || size == 4 || size == 8; }

std::int64_t loadSigned(const std::byte *p, std::size_t size) noexcept
{
    switch (size)
    {
    case 1: return load<std::int8_t>(p);
    case 2: return load<std::int16_t>(p);
    case 4: return load<std::int32_t>(p);
    default: return load<std::int64_t>(p);
    }
}

std::uint64_t loadUnsigned(const std::byte *p, std::size_t size) noexcept
{
    switch (size)
    {
    case 1: return load<std::uint8_t>(p);
    case 2: return load<std::uint16_t>(p);
    case 4: return load<std::uint32_t>(p);
    default: return load<std::uint64_t>(p);
    }
}

// Écrit les `size` octets de poids faible de `bits`.
void storeBits(std::byte *p, std::size_t size, std::uint64_t bits) noexcept
{
    switch (size)
    {
    case 1: store(p, static_cast<std::uint8_t>(bits)); break;
    case 2: store(p, static_cast<std::uint16_t>(bits)); break;
    case 4: store(p, static_cast<std::uint32_t>(bits)); break;
    default: store(p, bits); break;
    }
}

// Nombre de composantes et taille d'une composante des types vecteurs ; {0, 0} si le type n'en est pas un.
std::pair<std::size_t, std::size_t> vectorShape(FieldType type) noexcept
{
    switch (type)
    {
    case FieldType::kVec2: return {2, sizeof(float)};
    case FieldType::kVec3: return {3, sizeof(float)};
    case FieldType::kVec4: return {4, sizeof(float)};
    case FieldType::kQuat: return {4, sizeof(float)};
    case FieldType::kDVec3: return {3, sizeof(double)};
    case FieldType::kDQuat: return {4, sizeof(double)};
    default: return {0, 0};
    }
}

// Ordre des composantes : x, y, z, w pour les vecteurs ; w, x, y, z pour les quaternions (ordre du constructeur de glm et
// des valeurs écrites à la main), lus par nom de membre et non par position en mémoire (indépendant de la configuration de glm).
template <typename V, typename S> std::array<S, 4> vectorComponents(const std::byte *p, FieldType type) noexcept
{
    const V v = load<V>(p);
    if constexpr (requires { v.w; v.x; v.y; v.z; } && sizeof(V) == 4 * sizeof(S))
    {
        if (type == FieldType::kQuat || type == FieldType::kDQuat)
            return {static_cast<S>(v.w), static_cast<S>(v.x), static_cast<S>(v.y), static_cast<S>(v.z)};
    }
    std::array<S, 4> out{};
    for (int i = 0; i < static_cast<int>(sizeof(V) / sizeof(S)); ++i)
        out[static_cast<std::size_t>(i)] = static_cast<S>(v[i]);
    return out;
}

template <typename V, typename S> void storeVectorComponents(std::byte *p, FieldType type, const std::array<S, 4> &c) noexcept
{
    V v{};
    if constexpr (requires { v.w; v.x; v.y; v.z; } && sizeof(V) == 4 * sizeof(S))
    {
        if (type == FieldType::kQuat || type == FieldType::kDQuat)
        {
            v.w = c[0];
            v.x = c[1];
            v.y = c[2];
            v.z = c[3];
            store(p, v);
            return;
        }
    }
    for (int i = 0; i < static_cast<int>(sizeof(V) / sizeof(S)); ++i)
        v[i] = c[static_cast<std::size_t>(i)];
    store(p, v);
}

// Réels : nombre JSON s'il est fini (écrit au plus court qui se relit à l'identique), sinon "nan", "inf" ou "-inf".
template <typename S> Json realToJson(S v)
{
    if (std::isnan(v))
        return "nan";
    if (std::isinf(v))
        return v > 0 ? "inf" : "-inf";
    return static_cast<double>(v);
}

template <typename S> Result<S> realFromJson(const Json &j, const std::string &path)
{
    if (j.is_number())
    {
        const double d = j.get<double>();
        if constexpr (std::is_same_v<S, float>)
            if (std::isfinite(d) && (d > std::numeric_limits<float>::max() || d < std::numeric_limits<float>::lowest()))
                return core::makeError(ErrorCode::kOutOfRange, path + ": value does not fit a float");
        return static_cast<S>(d);
    }
    if (j.is_string())
    {
        const auto &s = j.get_ref<const std::string &>();
        if (s == "nan")
            return std::numeric_limits<S>::quiet_NaN();
        if (s == "inf")
            return std::numeric_limits<S>::infinity();
        if (s == "-inf")
            return -std::numeric_limits<S>::infinity();
    }
    return core::makeError(ErrorCode::kParse, path + ": expected a number (or \"nan\", \"inf\", \"-inf\")");
}

constexpr char kHex[] = "0123456789abcdef";

Json opaqueToJson(const std::byte *p, std::size_t size)
{
    std::string s;
    s.reserve(size * 2);
    for (std::size_t i = 0; i < size; ++i)
    {
        const auto b = static_cast<unsigned>(p[i]);
        s.push_back(kHex[b >> 4u]);
        s.push_back(kHex[b & 15u]);
    }
    return s;
}

int hexDigit(char c) noexcept
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

Result<> opaqueFromJson(std::byte *p, std::size_t size, const Json &j, const std::string &path)
{
    if (!j.is_string() || j.get_ref<const std::string &>().size() != size * 2)
        return core::makeError(ErrorCode::kParse, path + ": expected a string of " + std::to_string(size * 2) + " hex digits");
    const auto &s = j.get_ref<const std::string &>();
    for (std::size_t i = 0; i < size; ++i)
    {
        const int hi = hexDigit(s[2 * i]);
        const int lo = hexDigit(s[2 * i + 1]);
        if (hi < 0 || lo < 0)
            return core::makeError(ErrorCode::kParse, path + ": invalid hex digit");
        p[i] = static_cast<std::byte>((hi << 4) | lo);
    }
    return {};
}

// Le type déclaré correspond-il à la taille ? Sinon le champ est traité comme opaque (octets), sans rien supposer.
bool scalarShapeOk(const FieldDesc &f) noexcept
{
    switch (f.type)
    {
    case FieldType::kBool: return f.size == 1;
    case FieldType::kInt:
    case FieldType::kUInt:
    case FieldType::kEnum: return validIntSize(f.size);
    case FieldType::kFloat: return f.size == sizeof(float);
    case FieldType::kDouble: return f.size == sizeof(double);
    case FieldType::kVec2:
    case FieldType::kVec3:
    case FieldType::kVec4:
    case FieldType::kQuat:
    case FieldType::kDVec3:
    case FieldType::kDQuat:
    {
        const auto [n, s] = vectorShape(f.type);
        return f.size == n * s;
    }
    default: return false;
    }
}

Json vectorToJson(const FieldDesc &f, const std::byte *p)
{
    Json a = Json::array();
    const auto [n, s] = vectorShape(f.type);
    switch (f.type)
    {
    case FieldType::kVec2:
    {
        const auto c = vectorComponents<core::Vec2, float>(p, f.type);
        for (std::size_t i = 0; i < n; ++i)
            a.push_back(realToJson(c[i]));
        break;
    }
    case FieldType::kVec3:
    {
        const auto c = vectorComponents<core::Vec3, float>(p, f.type);
        for (std::size_t i = 0; i < n; ++i)
            a.push_back(realToJson(c[i]));
        break;
    }
    case FieldType::kVec4:
    {
        const auto c = vectorComponents<core::Vec4, float>(p, f.type);
        for (std::size_t i = 0; i < n; ++i)
            a.push_back(realToJson(c[i]));
        break;
    }
    case FieldType::kQuat:
    {
        const auto c = vectorComponents<core::Quat, float>(p, f.type);
        for (std::size_t i = 0; i < n; ++i)
            a.push_back(realToJson(c[i]));
        break;
    }
    case FieldType::kDVec3:
    {
        const auto c = vectorComponents<core::DVec3, double>(p, f.type);
        for (std::size_t i = 0; i < n; ++i)
            a.push_back(realToJson(c[i]));
        break;
    }
    default:
    {
        const auto c = vectorComponents<core::DQuat, double>(p, f.type);
        for (std::size_t i = 0; i < n; ++i)
            a.push_back(realToJson(c[i]));
        break;
    }
    }
    (void)s;
    return a;
}

template <typename V, typename S> Result<> vectorFromJsonAs(const FieldDesc &f, std::byte *p, const Json &j, const std::string &path, std::size_t n)
{
    std::array<S, 4> c{};
    for (std::size_t i = 0; i < n; ++i)
    {
        auto v = realFromJson<S>(j[i], path + "[" + std::to_string(i) + "]");
        if (!v)
            return std::unexpected(v.error());
        c[i] = *v;
    }
    storeVectorComponents<V, S>(p, f.type, c);
    return {};
}

Result<> vectorFromJson(const FieldDesc &f, std::byte *p, const Json &j, const std::string &path)
{
    const std::size_t n = vectorShape(f.type).first;
    if (!j.is_array() || j.size() != n)
        return core::makeError(ErrorCode::kParse, path + ": expected an array of " + std::to_string(n) + " numbers");
    switch (f.type)
    {
    case FieldType::kVec2: return vectorFromJsonAs<core::Vec2, float>(f, p, j, path, n);
    case FieldType::kVec3: return vectorFromJsonAs<core::Vec3, float>(f, p, j, path, n);
    case FieldType::kVec4: return vectorFromJsonAs<core::Vec4, float>(f, p, j, path, n);
    case FieldType::kQuat: return vectorFromJsonAs<core::Quat, float>(f, p, j, path, n);
    case FieldType::kDVec3: return vectorFromJsonAs<core::DVec3, double>(f, p, j, path, n);
    default: return vectorFromJsonAs<core::DQuat, double>(f, p, j, path, n);
    }
}

Json fieldToJson(const FieldDesc &f, const std::byte *p)
{
    if (f.type == FieldType::kStruct && !f.fields.empty())
        return encodeFields(f.fields, p);
    if (!scalarShapeOk(f))
        return opaqueToJson(p, f.size);
    switch (f.type)
    {
    case FieldType::kBool: return load<std::uint8_t>(p) != 0;
    case FieldType::kInt:
    case FieldType::kEnum: return loadSigned(p, f.size); // énumération : entier signé de sa taille (bits gardés)
    case FieldType::kUInt: return loadUnsigned(p, f.size);
    case FieldType::kFloat: return realToJson(load<float>(p));
    case FieldType::kDouble: return realToJson(load<double>(p));
    default: return vectorToJson(f, p);
    }
}

Result<> checkRange(const FieldDesc &f, double v, const std::string &path)
{
    if (f.range.bounded && !f.range.contains(v))
        return core::makeError(ErrorCode::kOutOfRange,
                               path + ": outside [" + std::to_string(f.range.min) + ", " + std::to_string(f.range.max) + "]");
    return {};
}

Result<> integerFromJson(const FieldDesc &f, std::byte *p, const Json &j, const std::string &path)
{
    if (!j.is_number_integer())
        return core::makeError(ErrorCode::kParse, path + ": expected integer");
    const unsigned bits = static_cast<unsigned>(f.size * 8);
    if (j.is_number_unsigned())
    {
        const auto v = j.get<std::uint64_t>();
        // Champ signé ou énumération : borne du signé (énumération : aussi du non signé de cette taille).
        const std::uint64_t unsignedMax = bits == 64 ? ~0ull : (1ull << bits) - 1u;
        const std::uint64_t signedMax = unsignedMax >> 1u;
        const std::uint64_t max = f.type == FieldType::kInt ? signedMax : unsignedMax;
        if (v > max)
            return core::makeError(ErrorCode::kOutOfRange, path + ": integer does not fit the field");
        if (auto r = checkRange(f, static_cast<double>(v), path); !r)
            return r;
        storeBits(p, f.size, v);
        return {};
    }
    const auto v = j.get<std::int64_t>();
    if (f.type == FieldType::kUInt && v < 0)
        return core::makeError(ErrorCode::kOutOfRange, path + ": negative value for an unsigned field");
    if (bits < 64)
    {
        const std::int64_t min = -(std::int64_t{1} << (bits - 1u));
        const std::int64_t max = f.type == FieldType::kInt ? (std::int64_t{1} << (bits - 1u)) - 1 : (std::int64_t{1} << bits) - 1;
        if (v < min || v > max)
            return core::makeError(ErrorCode::kOutOfRange, path + ": integer does not fit the field");
    }
    if (auto r = checkRange(f, static_cast<double>(v), path); !r)
        return r;
    storeBits(p, f.size, static_cast<std::uint64_t>(v));
    return {};
}

Result<> fieldFromJson(const FieldDesc &f, std::byte *p, const Json &j, const std::string &path, std::vector<std::string> &warnings)
{
    if (f.type == FieldType::kStruct && !f.fields.empty())
    {
        if (!j.is_object())
            return core::makeError(ErrorCode::kParse, path + ": expected object");
        return decodeFields(f.fields, p, j, path, warnings);
    }
    if (!scalarShapeOk(f))
        return opaqueFromJson(p, f.size, j, path);
    switch (f.type)
    {
    case FieldType::kBool:
        if (!j.is_boolean())
            return core::makeError(ErrorCode::kParse, path + ": expected boolean");
        store(p, static_cast<std::uint8_t>(j.get<bool>() ? 1 : 0));
        return {};
    case FieldType::kInt:
    case FieldType::kUInt:
    case FieldType::kEnum: return integerFromJson(f, p, j, path);
    case FieldType::kFloat:
    {
        auto v = realFromJson<float>(j, path);
        if (!v)
            return std::unexpected(v.error());
        if (auto r = checkRange(f, static_cast<double>(*v), path); !r)
            return r;
        store(p, *v);
        return {};
    }
    case FieldType::kDouble:
    {
        auto v = realFromJson<double>(j, path);
        if (!v)
            return std::unexpected(v.error());
        if (auto r = checkRange(f, *v, path); !r)
            return r;
        store(p, *v);
        return {};
    }
    default: return vectorFromJson(f, p, j, path);
    }
}

void hashField(core::Fnv1a &hash, const FieldDesc &f, const std::byte *p)
{
    if (f.type == FieldType::kStruct && !f.fields.empty())
    {
        hashFields(hash, f.fields, p);
        return;
    }
    if (!scalarShapeOk(f))
    {
        for (std::size_t i = 0; i < f.size; ++i)
            hash.byte(static_cast<std::uint8_t>(p[i]));
        return;
    }
    switch (f.type)
    {
    case FieldType::kBool: hash.u64(load<std::uint8_t>(p) != 0 ? 1u : 0u); return;
    case FieldType::kInt:
    case FieldType::kEnum: hash.i64(loadSigned(p, f.size)); return;
    case FieldType::kUInt: hash.u64(loadUnsigned(p, f.size)); return;
    case FieldType::kFloat: hash.f32(load<float>(p)); return;
    case FieldType::kDouble: hash.f64(load<double>(p)); return;
    default:
    {
        // Composantes une à une, dans l'ordre de la sauvegarde (indépendant de la disposition mémoire de glm).
        const auto [n, s] = vectorShape(f.type);
        if (s == sizeof(float))
        {
            std::array<float, 4> c{};
            if (f.type == FieldType::kVec2)
                c = vectorComponents<core::Vec2, float>(p, f.type);
            else if (f.type == FieldType::kVec3)
                c = vectorComponents<core::Vec3, float>(p, f.type);
            else if (f.type == FieldType::kVec4)
                c = vectorComponents<core::Vec4, float>(p, f.type);
            else
                c = vectorComponents<core::Quat, float>(p, f.type);
            for (std::size_t i = 0; i < n; ++i)
                hash.f32(c[i]);
        }
        else
        {
            const auto c = f.type == FieldType::kDVec3 ? vectorComponents<core::DVec3, double>(p, f.type)
                                                       : vectorComponents<core::DQuat, double>(p, f.type);
            for (std::size_t i = 0; i < n; ++i)
                hash.f64(c[i]);
        }
        return;
    }
    }
}

} // namespace

Json encodeFields(const std::vector<FieldDesc> &fields, const std::byte *base)
{
    Json object = Json::object();
    for (const FieldDesc &f : fields)
        object[f.name] = fieldToJson(f, base + f.offset);
    return object;
}

Result<> decodeFields(const std::vector<FieldDesc> &fields, std::byte *base, const Json &object, const std::string &path,
                      std::vector<std::string> &warnings)
{
    if (!object.is_object())
        return core::makeError(ErrorCode::kParse, path + ": expected object");
    for (const FieldDesc &f : fields)
    {
        auto it = object.find(f.name);
        if (it == object.end())
            continue; // absent : la valeur en place (défaut) est gardée
        if (auto r = fieldFromJson(f, base + f.offset, *it, path + "." + f.name, warnings); !r)
            return r;
    }
    for (auto it = object.begin(); it != object.end(); ++it)
    {
        bool known = false;
        for (const FieldDesc &f : fields)
            known = known || f.name == it.key();
        if (!known)
            warnings.push_back(path + "." + it.key() + ": unknown field ignored");
    }
    return {};
}

void hashFields(core::Fnv1a &hash, const std::vector<FieldDesc> &fields, const std::byte *base)
{
    for (const FieldDesc &f : fields)
    {
        hash.string(f.name);
        hashField(hash, f, base + f.offset);
    }
}

} // namespace orosmo::scene::detail
