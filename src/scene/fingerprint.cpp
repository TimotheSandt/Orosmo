// SIM-01 (A79 lot L3) : empreinte d'un World. Voir include/orosmo/scene/fingerprint.hpp.
#include <orosmo/scene/fingerprint.hpp>

#include "field_codec.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace orosmo::scene
{

void hashWorld(core::Fnv1a &hash, const World &world)
{
    const World::EntityTable table = world.entityTable();
    hash.u64(table.generations.size());
    for (std::uint32_t g : table.generations)
        hash.u64(g);
    hash.u64(table.freeIndices.size());
    for (std::uint32_t i : table.freeIndices)
        hash.u64(i);

    const std::vector<Entity> alive = world.entities();
    for (const Entity e : alive)
    {
        const std::vector<Entity> children = world.children(e);
        hash.u64(children.size());
        for (const Entity c : children)
            hash.u64(c.index);
    }

    std::vector<ComponentId> ids = world.componentIds();
    std::sort(ids.begin(), ids.end(),
              [&world](ComponentId a, ComponentId b) { return world.componentDesc(a)->name < world.componentDesc(b)->name; });
    for (const ComponentId id : ids)
    {
        const std::span<const Entity> owners = world.componentEntities(id);
        if (owners.empty())
            continue; // un type enregistré sans donnée ne fait pas partie de l'état
        const ComponentDesc &desc = *world.componentDesc(id);
        hash.string(desc.name);
        hash.i64(desc.version);
        hash.u64(owners.size());
        for (const Entity e : owners)
        {
            hash.u64(e.index);
            detail::hashFields(hash, desc.fields, static_cast<const std::byte *>(world.get(e, id)));
        }
    }
}

std::uint64_t fingerprint(const World &world)
{
    core::Fnv1a hash;
    hashWorld(hash, world);
    return hash.value();
}

} // namespace orosmo::scene
