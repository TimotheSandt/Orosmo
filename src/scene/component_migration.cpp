// GAME-03 (A79 lot L6) : migration d'une valeur de composant d'une description à une autre, par nom de champ.
#include <orosmo/scene/component_migration.hpp>

#include "field_codec.hpp"

#include <algorithm>
#include <cstring>
#include <format>
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
        // Structure opaque (octets en hexadécimal) <-> structure réfléchie (objet) : la valeur ne se relit pas.
        if (old.fields.empty() != it->fields.empty())
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

namespace
{
bool isReflectedStruct(const core::FieldDesc &f) noexcept { return f.type == core::FieldType::kStruct && !f.fields.empty(); }

// Migre les champs de `from` (lus à `src`) vers ceux de `to` (écrits à `dst`), par nom, chaque champ indépendamment.
void migrateFields(const std::vector<core::FieldDesc> &from, const std::byte *src, const std::vector<core::FieldDesc> &to,
                   std::byte *dst, const std::string &prefix, std::vector<std::string> &warnings)
{
    for (const core::FieldDesc &old : from)
    {
        // std::format plutôt que des « + » de chaînes : GCC 16 (MinGW) voit un faux débordement dans la concaténation
        // inlinée (-Werror=restrict, -Werror=alloc-size-larger-than).
        const std::string path = std::format("{}.{}", prefix, old.name);
        const auto it = std::ranges::find(to, old.name, &core::FieldDesc::name);
        if (it == to.end())
        {
            warnOnce(warnings, std::format("{}: field removed, value dropped", path));
            continue;
        }
        const core::FieldDesc &now = *it;
        if (isReflectedStruct(old) && isReflectedStruct(now))
        {
            // Sous-champs un à un : un sous-champ incompatible ne fait pas perdre ses voisins.
            migrateFields(old.fields, src + old.offset, now.fields, dst + now.offset, path, warnings);
            continue;
        }
        if (old.type == now.type && old.size == now.size && !isReflectedStruct(old) && !isReflectedStruct(now))
        {
            // Type inchangé (kCopied) : valeur gardée à l'identique, sans revalider la plage de la nouvelle version.
            std::memcpy(dst + now.offset, src + old.offset, now.size);
            continue;
        }
        // Type changé : conversion par la sauvegarde (ancienne description) puis la relecture (nouvelle, plage comprise),
        // dans une copie du champ pour qu'un échec ne laisse pas une valeur à moitié écrite.
        core::FieldDesc oldAlone = old;
        oldAlone.offset = 0;
        core::FieldDesc nowAlone = now;
        nowAlone.offset = 0;
        const core::Json saved = detail::encodeFields({oldAlone}, src + old.offset);
        std::vector<std::byte> attempt(dst + now.offset, dst + now.offset + now.size);
        std::vector<std::string> ignored;
        if (auto decoded = detail::decodeFields({nowAlone}, attempt.data(), saved, prefix, ignored); !decoded)
        {
            warnOnce(warnings,
                     std::format("{}: type changed, value not convertible ({}), default value kept", path, decoded.error().message));
            continue;
        }
        for (std::string &message : ignored)
            warnOnce(warnings, std::move(message));
        std::memcpy(dst + now.offset, attempt.data(), now.size);
    }
}
} // namespace

void migrateComponentValue(const ComponentDesc &from, const void *src, const ComponentDesc &to, void *dst,
                           std::vector<std::string> &warnings)
{
    migrateFields(from.fields, static_cast<const std::byte *>(src), to.fields, static_cast<std::byte *>(dst), to.name, warnings);
}

} // namespace orosmo::scene
