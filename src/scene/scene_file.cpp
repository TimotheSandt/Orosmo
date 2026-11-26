// ECS-03 (A79 lot L3) : sauvegarde et chargement de scène. Voir include/orosmo/scene/scene_file.hpp.
#include <orosmo/scene/scene_file.hpp>

#include "field_codec.hpp"

#include <orosmo/core/log.hpp>

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

namespace orosmo::scene
{

using core::ErrorCode;
using core::Json;
using core::Result;

namespace
{

Result<int> readVersion(const Json &j, const std::string &what)
{
    if (!j.is_number_integer())
        return core::makeError(ErrorCode::kParse, what + ": version must be an integer");
    if (j.is_number_unsigned())
    {
        const auto v = j.get<std::uint64_t>();
        if (v > 0x7fffffffu)
            return core::makeError(ErrorCode::kVersionMismatch, what + ": version is newer than supported");
        return static_cast<int>(v);
    }
    const auto v = j.get<std::int64_t>();
    if (v < 0)
        return core::makeError(ErrorCode::kParse, what + ": version must not be negative");
    if (v > 0x7fffffff)
        return core::makeError(ErrorCode::kVersionMismatch, what + ": version is newer than supported");
    return static_cast<int>(v);
}

// Applique les migrations from -> to (from <= to) ; kVersionMismatch si plus récent ou s'il manque une étape.
Result<> migrate(Json &data, int from, int to, const core::MigrationTable &table, const std::string &what)
{
    if (from > to)
        return core::makeError(ErrorCode::kVersionMismatch, what + ": version " + std::to_string(from) +
                                                                " is newer than supported version " + std::to_string(to));
    for (int v = from; v < to; ++v)
    {
        auto it = table.find(v);
        if (it == table.end())
            return core::makeError(ErrorCode::kVersionMismatch, what + ": no migration from version " + std::to_string(v));
        if (auto r = it->second(data); !r)
            return r;
    }
    return {};
}

Result<std::uint32_t> readU32(const Json &j, const std::string &path)
{
    if (!j.is_number_unsigned() && !(j.is_number_integer() && j.get<std::int64_t>() >= 0))
        return core::makeError(ErrorCode::kParse, path + ": expected a non-negative integer");
    const auto v = j.get<std::uint64_t>();
    if (v > 0xffffffffu)
        return core::makeError(ErrorCode::kOutOfRange, path + ": integer does not fit 32 bits");
    return static_cast<std::uint32_t>(v);
}

Result<std::vector<std::uint32_t>> readU32Array(const Json &j, const std::string &path)
{
    if (!j.is_array())
        return core::makeError(ErrorCode::kParse, path + ": expected array");
    std::vector<std::uint32_t> out;
    out.reserve(j.size());
    for (std::size_t i = 0; i < j.size(); ++i)
    {
        auto v = readU32(j[i], path + "[" + std::to_string(i) + "]");
        if (!v)
            return std::unexpected(v.error());
        out.push_back(*v);
    }
    return out;
}

Result<> loadComponents(World &world, const std::vector<std::uint32_t> &generations, const Json &list, const SceneMigrations &migrations,
                        SceneLoadReport &report)
{
    if (!list.is_array())
        return core::makeError(ErrorCode::kParse, "components: expected array");
    std::vector<std::byte> buffer;
    for (std::size_t c = 0; c < list.size(); ++c)
    {
        const Json &entry = list[c];
        const std::string where = "components[" + std::to_string(c) + "]";
        if (!entry.is_object() || !entry.contains("name") || !entry["name"].is_string() || !entry.contains("version") ||
            !entry.contains("entities") || !entry.contains("values"))
            return core::makeError(ErrorCode::kParse, where + ": expected {name, version, entities, values}");
        const std::string &name = entry["name"].get_ref<const std::string &>();
        auto id = world.componentId(name);
        if (!id)
        {
            report.warnings.push_back("component " + name + ": type not registered, ignored");
            continue;
        }
        const ComponentDesc &desc = *world.componentDesc(*id);
        auto version = readVersion(entry["version"], "component " + name);
        if (!version)
            return std::unexpected(version.error());
        if (*version > desc.version)
            return core::makeError(ErrorCode::kVersionMismatch, "component " + name + ": version " + std::to_string(*version) +
                                                                    " is newer than registered version " + std::to_string(desc.version));
        auto entities = readU32Array(entry["entities"], where + ".entities");
        if (!entities)
            return std::unexpected(entities.error());
        const Json &values = entry["values"];
        if (!values.is_array() || values.size() != entities->size())
            return core::makeError(ErrorCode::kParse, where + ".values: expected an array as long as entities");
        static const core::MigrationTable kNoMigration;
        auto table = migrations.components.find(name);
        const core::MigrationTable &steps = table != migrations.components.end() ? table->second : kNoMigration;
        buffer.resize(desc.size);
        for (std::size_t i = 0; i < entities->size(); ++i)
        {
            const std::uint32_t index = (*entities)[i];
            const std::string path = name + "[" + std::to_string(index) + "]";
            Json value = values[i];
            if (auto r = migrate(value, *version, desc.version, steps, "component " + name); !r)
                return r;
            if (desc.defaultValue.size() == desc.size)
                std::copy(desc.defaultValue.begin(), desc.defaultValue.end(), buffer.begin());
            else
                std::fill(buffer.begin(), buffer.end(), std::byte{0});
            if (auto r = detail::decodeFields(desc.fields, buffer.data(), value, path, report.warnings); !r)
                return r;
            if (index >= generations.size())
                return core::makeError(ErrorCode::kParse, path + ": entity index outside the entity table");
            if (auto added = world.add(Entity{index, generations[index]}, *id, buffer.data()); !added)
                return core::makeError(ErrorCode::kParse, path + ": the entity is not alive");
            ++report.components;
        }
    }
    return {};
}

} // namespace

Json componentToJson(const ComponentDesc &desc, const void *data)
{
    return detail::encodeFields(desc.fields, static_cast<const std::byte *>(data));
}

Result<> componentFromJson(const ComponentDesc &desc, void *data, const Json &value, std::vector<std::string> &warnings)
{
    return detail::decodeFields(desc.fields, static_cast<std::byte *>(data), value, desc.name, warnings);
}

Json saveScene(const World &world)
{
    const World::EntityTable table = world.entityTable();
    Json data = Json::object();
    data["entities"] = Json{{"generations", table.generations}, {"free", table.freeIndices}};

    Json hierarchy = Json::array();
    for (const Entity parent : world.entities())
        for (const Entity child : world.children(parent))
            hierarchy.push_back(Json::array({child.index, parent.index}));
    data["hierarchy"] = std::move(hierarchy);

    Json components = Json::array();
    for (const ComponentId id : world.componentIds())
    {
        const ComponentDesc &desc = *world.componentDesc(id);
        const std::span<const Entity> owners = world.componentEntities(id);
        if (owners.empty())
            continue;
        Json indices = Json::array();
        Json values = Json::array();
        for (const Entity e : owners)
        {
            indices.push_back(e.index);
            values.push_back(componentToJson(desc, world.get(e, id)));
        }
        components.push_back(Json{{"name", desc.name}, {"version", desc.version}, {"entities", std::move(indices)}, {"values", std::move(values)}});
    }
    data["components"] = std::move(components);

    Json document = Json::object();
    document["version"] = kSceneFormatVersion;
    document["data"] = std::move(data);
    return document;
}

Result<SceneLoadReport> loadScene(World &world, const Json &document, const SceneMigrations &migrations)
{
    if (!document.is_object() || !document.contains("version") || !document.contains("data"))
        return core::makeError(ErrorCode::kParse, "scene document must be {\"version\": int, \"data\": ...}");
    auto version = readVersion(document["version"], "scene document");
    if (!version)
        return std::unexpected(version.error());
    Json data = document["data"];
    if (auto r = migrate(data, *version, kSceneFormatVersion, migrations.format, "scene document"); !r)
        return std::unexpected(r.error());
    if (!data.is_object() || !data.contains("entities") || !data["entities"].is_object())
        return core::makeError(ErrorCode::kParse, "scene document: missing entities");

    const Json &entities = data["entities"];
    World::EntityTable table;
    if (!entities.contains("generations") || !entities.contains("free"))
        return core::makeError(ErrorCode::kParse, "entities: expected {generations, free}");
    auto generations = readU32Array(entities["generations"], "entities.generations");
    if (!generations)
        return std::unexpected(generations.error());
    auto freeIndices = readU32Array(entities["free"], "entities.free");
    if (!freeIndices)
        return std::unexpected(freeIndices.error());
    table.generations = std::move(*generations);
    table.freeIndices = std::move(*freeIndices);

    // Tout ou rien : le chargement se fait dans une copie, échangée avec le monde seulement en cas de succès.
    World staging = world;
    if (auto r = staging.resetEntities(table); !r)
        return std::unexpected(r.error());

    SceneLoadReport report;
    report.entities = staging.entityCount();
    if (data.contains("hierarchy"))
    {
        const Json &hierarchy = data["hierarchy"];
        if (!hierarchy.is_array())
            return core::makeError(ErrorCode::kParse, "hierarchy: expected array");
        for (std::size_t i = 0; i < hierarchy.size(); ++i)
        {
            const std::string path = "hierarchy[" + std::to_string(i) + "]";
            if (!hierarchy[i].is_array() || hierarchy[i].size() != 2)
                return core::makeError(ErrorCode::kParse, path + ": expected [child, parent]");
            auto child = readU32(hierarchy[i][0], path);
            auto parent = readU32(hierarchy[i][1], path);
            if (!child || !parent)
                return std::unexpected(!child ? child.error() : parent.error());
            if (*child >= table.generations.size() || *parent >= table.generations.size())
                return core::makeError(ErrorCode::kParse, path + ": entity index outside the entity table");
            const Entity c{*child, table.generations[*child]};
            const Entity p{*parent, table.generations[*parent]};
            if (auto r = staging.setParent(c, p); !r)
                return core::makeError(ErrorCode::kParse, path + ": " + r.error().message);
        }
    }
    if (data.contains("components"))
        if (auto r = loadComponents(staging, table.generations, data["components"], migrations, report); !r)
            return std::unexpected(r.error());
    for (const std::string &warning : report.warnings)
        LOG_WARNING("loadScene: ", warning);
    staging.updateTransforms(); // cache monde de Transform, non sauvegardé
    world = std::move(staging);
    return report;
}

std::vector<std::uint8_t> saveSceneBinary(const World &world) { return core::toBinary(saveScene(world)); }

Result<SceneLoadReport> loadSceneBinary(World &world, std::span<const std::uint8_t> bytes, const SceneMigrations &migrations)
{
    auto document = core::fromBinary(bytes);
    if (!document)
        return std::unexpected(document.error());
    return loadScene(world, *document, migrations);
}

Result<> saveSceneFile(const World &world, const std::filesystem::path &path)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return core::makeError(ErrorCode::kIo, "saveSceneFile: cannot open " + path.string());
    if (path.extension() == ".json")
        out << core::dumpJson(saveScene(world));
    else
    {
        const std::vector<std::uint8_t> bytes = saveSceneBinary(world);
        out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    if (!out)
        return core::makeError(ErrorCode::kIo, "saveSceneFile: cannot write " + path.string());
    return {};
}

Result<SceneLoadReport> loadSceneFile(World &world, const std::filesystem::path &path, const SceneMigrations &migrations)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return core::makeError(ErrorCode::kIo, "loadSceneFile: cannot open " + path.string());
    const std::vector<char> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    if (path.extension() == ".json")
    {
        auto document = core::parseJson(std::string_view(bytes.data(), bytes.size()));
        if (!document)
            return std::unexpected(document.error());
        return loadScene(world, *document, migrations);
    }
    return loadSceneBinary(world, std::span(reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size()), migrations);
}

} // namespace orosmo::scene
