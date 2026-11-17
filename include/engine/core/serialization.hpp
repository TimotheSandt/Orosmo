// CORE-12 (2/2) : sérialisation JSON lisible et binaire compact (MessagePack), versionnée, dérivée de la réflexion.
// Document : {"version": N, "data": {...}}. Champ absent à la lecture : la valeur par défaut est gardée (compatibilité
// ascendante). Champ inconnu : ignoré. Type incompatible ou hors plage : Error (kParse / kOutOfRange), aucune exception.
#pragma once

#include <engine/core/error.hpp>
#include <engine/core/reflection.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace engine::core
{

using Json = nlohmann::json;

// ── Valeurs ────────────────────────────────────────────────────────────────────
template <typename T> Json toJsonValue(const T &value);
template <typename T> Result<> fromJsonValue(T &out, const Json &j, const std::string &path);

namespace detail
{
inline Error parseError(const std::string &path, const char *expected)
{
    return Error{ErrorCode::kParse, path + ": expected " + expected, std::source_location::current()};
}

template <typename V, int N> Result<> readVec(V &out, const Json &j, const std::string &path)
{
    if (!j.is_array() || j.size() != static_cast<std::size_t>(N))
        return std::unexpected(parseError(path, "array of N numbers"));
    for (int i = 0; i < N; ++i)
    {
        if (!j[static_cast<std::size_t>(i)].is_number())
            return std::unexpected(parseError(path, "number"));
        out[i] = j[static_cast<std::size_t>(i)].template get<float>();
    }
    return {};
}
} // namespace detail

template <typename T> Json toJsonValue(const T &value)
{
    if constexpr (Reflected<T>)
    {
        Json j = Json::object();
        forEachField(value, [&](const char *name, const auto &member, const Range &) { j[name] = toJsonValue(member); });
        return j;
    }
    else if constexpr (std::is_enum_v<T>)
        return static_cast<std::underlying_type_t<T>>(value);
    else if constexpr (std::is_same_v<T, Vec2>)
        return Json::array({value.x, value.y});
    else if constexpr (std::is_same_v<T, Vec3>)
        return Json::array({value.x, value.y, value.z});
    else if constexpr (std::is_same_v<T, Vec4>)
        return Json::array({value.x, value.y, value.z, value.w});
    else if constexpr (IsVector<T>::value)
    {
        Json a = Json::array();
        for (const auto &e : value)
            a.push_back(toJsonValue(e));
        return a;
    }
    else
        return Json(value);
}

template <typename T> Result<> fromJsonValue(T &out, const Json &j, const std::string &path)
{
    if constexpr (Reflected<T>)
    {
        if (!j.is_object())
            return std::unexpected(detail::parseError(path, "object"));
        Result<> status;
        forEachField(out, [&](const char *name, auto &member, const Range &r) {
            if (!status)
                return;
            auto it = j.find(name);
            if (it == j.end())
                return; // absent : valeur par défaut gardée
            const std::string childPath = path.empty() ? name : path + "." + name;
            status = fromJsonValue(member, *it, childPath);
            if (status && r.bounded)
            {
                if constexpr (std::is_arithmetic_v<std::remove_cvref_t<decltype(member)>>)
                    if (!r.contains(static_cast<double>(member)))
                        status = makeError(ErrorCode::kOutOfRange, childPath + ": outside [" + std::to_string(r.min) + ", " +
                                                                       std::to_string(r.max) + "]");
            }
        });
        return status;
    }
    else if constexpr (std::is_same_v<T, bool>)
    {
        if (!j.is_boolean())
            return std::unexpected(detail::parseError(path, "boolean"));
        out = j.get<bool>();
    }
    else if constexpr (std::is_enum_v<T>)
    {
        if (!j.is_number_integer())
            return std::unexpected(detail::parseError(path, "integer (enum)"));
        out = static_cast<T>(j.get<std::underlying_type_t<T>>());
    }
    else if constexpr (std::is_integral_v<T>)
    {
        if (!j.is_number_integer())
            return std::unexpected(detail::parseError(path, "integer"));
        if constexpr (std::is_signed_v<T>)
        {
            const auto v = j.get<std::int64_t>();
            if (v < static_cast<std::int64_t>(std::numeric_limits<T>::min()) || v > static_cast<std::int64_t>(std::numeric_limits<T>::max()))
                return makeError(ErrorCode::kOutOfRange, path + ": integer does not fit the type");
            out = static_cast<T>(v);
        }
        else
        {
            if (j.is_number_integer() && !j.is_number_unsigned())
                return makeError(ErrorCode::kOutOfRange, path + ": negative value for an unsigned field");
            const auto v = j.get<std::uint64_t>();
            if (v > static_cast<std::uint64_t>(std::numeric_limits<T>::max()))
                return makeError(ErrorCode::kOutOfRange, path + ": integer does not fit the type");
            out = static_cast<T>(v);
        }
    }
    else if constexpr (std::is_floating_point_v<T>)
    {
        if (!j.is_number())
            return std::unexpected(detail::parseError(path, "number"));
        out = j.get<T>();
    }
    else if constexpr (std::is_same_v<T, std::string>)
    {
        if (!j.is_string())
            return std::unexpected(detail::parseError(path, "string"));
        out = j.get<std::string>();
    }
    else if constexpr (std::is_same_v<T, Vec2>)
        return detail::readVec<Vec2, 2>(out, j, path);
    else if constexpr (std::is_same_v<T, Vec3>)
        return detail::readVec<Vec3, 3>(out, j, path);
    else if constexpr (std::is_same_v<T, Vec4>)
        return detail::readVec<Vec4, 4>(out, j, path);
    else if constexpr (IsVector<T>::value)
    {
        if (!j.is_array())
            return std::unexpected(detail::parseError(path, "array"));
        T result;
        result.reserve(j.size());
        for (std::size_t i = 0; i < j.size(); ++i)
        {
            typename T::value_type element{};
            if (auto r = fromJsonValue(element, j[i], path + "[" + std::to_string(i) + "]"); !r)
                return r;
            result.push_back(std::move(element));
        }
        out = std::move(result);
    }
    else
        static_assert(sizeof(T) == 0, "type not serializable: reflect it with Reflect<T> or use a supported type");
    return {};
}

// ── Documents versionnés ──────────────────────────────────────────────────────
// Migration : transforme `data` de la version n vers n+1 (clé = n).
using MigrationTable = std::map<int, std::function<Result<>(Json &data)>>;

template <Reflected T> Json saveDocument(const T &object)
{
    Json doc = Json::object();
    doc["version"] = versionOf<T>();
    doc["data"] = toJsonValue(object);
    return doc;
}

template <Reflected T> Result<> loadDocument(T &out, Json document, const MigrationTable &migrations = {})
{
    if (!document.is_object() || !document.contains("version") || !document["version"].is_number_integer() ||
        !document.contains("data"))
        return makeError(ErrorCode::kParse, "document must be {\"version\": int, \"data\": ...}");
    int version = document["version"].get<int>();
    const int current = versionOf<T>();
    if (version > current)
        return makeError(ErrorCode::kVersionMismatch, "document version " + std::to_string(version) +
                                                         " is newer than supported version " + std::to_string(current));
    Json data = std::move(document["data"]);
    for (; version < current; ++version)
    {
        auto it = migrations.find(version);
        if (it == migrations.end())
            return makeError(ErrorCode::kVersionMismatch, "no migration from version " + std::to_string(version));
        if (auto r = it->second(data); !r)
            return r;
    }
    return fromJsonValue(out, data, "");
}

// ── Texte et binaire ──────────────────────────────────────────────────────────
inline std::string dumpJson(const Json &j, int indent = 2) { return j.dump(indent); }

inline Result<Json> parseJson(std::string_view text)
{
    Json j = Json::parse(text.begin(), text.end(), nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded())
        return makeError(ErrorCode::kParse, "invalid JSON");
    return j;
}

inline std::vector<std::uint8_t> toBinary(const Json &j) { return Json::to_msgpack(j); }

inline Result<Json> fromBinary(std::span<const std::uint8_t> bytes)
{
    Json j = Json::from_msgpack(bytes.begin(), bytes.end(), /*strict=*/true, /*allow_exceptions=*/false);
    if (j.is_discarded())
        return makeError(ErrorCode::kParse, "invalid MessagePack");
    return j;
}

} // namespace engine::core
