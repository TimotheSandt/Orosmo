// GAME-03 (A79 lot L6) : migration d'une valeur de composant d'une description à une autre, par nom de champ.
#include <orosmo/scene/component_migration.hpp>

#include "field_codec.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace orosmo::scene
{

const char *migrationKindName(MigrationKind kind) noexcept
{
    switch (kind)
    {
    case MigrationKind::kCopied: return "copied";
    case MigrationKind::kRestored: return "restored";
    }
    return "?";
}

MigrationKind classifyMigration(const std::vector<core::FieldDesc> &from, const std::vector<core::FieldDesc> &to)
{
    for (const core::FieldDesc &old : from)
    {
        const auto it = std::ranges::find(to, old.name, &core::FieldDesc::name);
        if (it == to.end() || it->type != old.type)
            return MigrationKind::kRestored;
        if (old.fields.empty() && it->fields.empty())
        {
            if (it->size != old.size)
                return MigrationKind::kRestored;
        }
        else if (classifyMigration(old.fields, it->fields) != MigrationKind::kCopied)
            return MigrationKind::kRestored;
    }
    return MigrationKind::kCopied;
}

namespace
{
void warnOnce(std::vector<std::string> &warnings, std::string message)
{
    if (std::ranges::find(warnings, message) == warnings.end())
        warnings.push_back(std::move(message));
}
} // namespace

void migrateComponentValue(const ComponentDesc &from, const void *src, const ComponentDesc &to, void *dst,
                           std::vector<std::string> &warnings)
{
    // Sauvegarde : champ par champ, avec l'ancienne description (jamais les octets bruts).
    const core::Json saved = detail::encodeFields(from.fields, static_cast<const std::byte *>(src));
    auto *out = static_cast<std::byte *>(dst);
    std::vector<std::byte> attempt(to.size);
    // Restauration : un champ à la fois, dans une copie, pour qu'un champ illisible ne laisse pas une valeur à moitié écrite.
    for (auto it = saved.begin(); it != saved.end(); ++it)
    {
        const std::string path = to.name + "." + it.key();
        if (std::ranges::find(to.fields, it.key(), &core::FieldDesc::name) == to.fields.end())
        {
            warnOnce(warnings, path + ": field removed, value dropped");
            continue;
        }
        if (to.size != 0)
            std::memcpy(attempt.data(), out, to.size);
        core::Json single = core::Json::object();
        single[it.key()] = it.value();
        std::vector<std::string> ignored;
        if (auto decoded = detail::decodeFields(to.fields, attempt.data(), single, to.name, ignored); !decoded)
        {
            warnOnce(warnings, path + ": type changed, value not convertible (" + decoded.error().message +
                                   "), default value kept");
            continue;
        }
        if (to.size != 0)
            std::memcpy(out, attempt.data(), to.size);
    }
}

} // namespace orosmo::scene
