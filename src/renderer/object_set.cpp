#include "orosmo/renderer/object_set.hpp"

#include "orosmo/renderer/debug_view.hpp"

#include <string>

namespace orosmo::renderer
{

using core::ErrorCode;
using core::makeError;

namespace
{
constexpr std::uint32_t kFree = 0xFFFFFFFFu;
}

ObjectSet::ObjectSet() { lodSettings_.emplace_back(); }

std::uint32_t ObjectSet::addLodSettings(LodSettings settings)
{
    lodSettings_.push_back(std::move(settings));
    return static_cast<std::uint32_t>(lodSettings_.size() - 1);
}

const LodSettings &ObjectSet::lodSettings(std::uint32_t id) const noexcept { return lodSettings_[id < lodSettings_.size() ? id : 0]; }

core::Result<ObjectId> ObjectSet::add(const ObjectDesc &desc)
{
    if (desc.lodMeshes.empty() || desc.lodMeshes.size() > 255)
    {
        return makeError(ErrorCode::kInvalidArgument, "ObjectSet::add: an object needs 1 to 255 LOD meshes");
    }
    if (desc.lodSettings >= lodSettings_.size())
    {
        return makeError(ErrorCode::kNotFound, "ObjectSet::add: unknown LOD settings " + std::to_string(desc.lodSettings));
    }
    if (!desc.localBounds.valid())
    {
        return makeError(ErrorCode::kInvalidArgument, "ObjectSet::add: the local bounds are empty (min > max)");
    }
    std::uint32_t slot = 0;
    if (!freeSlots_.empty())
    {
        slot = freeSlots_.back();
        freeSlots_.pop_back();
    }
    else
    {
        slot = static_cast<std::uint32_t>(slots_.size());
        slots_.push_back({});
    }
    slots_[slot].dense = static_cast<std::uint32_t>(objects_.size());
    objects_.push_back({desc.lodMeshes, desc.material, desc.lodSettings, desc.localBounds, desc.transform, desc.color, slot});
    worldBounds_.push_back(desc.localBounds.transformed(desc.transform));
    structureDirty_ = true;
    return ObjectId{slot, slots_[slot].generation};
}

ObjectSet::Object *ObjectSet::find(ObjectId id) noexcept
{
    if (id.index >= slots_.size() || slots_[id.index].generation != id.generation || slots_[id.index].dense == kFree)
    {
        return nullptr;
    }
    return &objects_[slots_[id.index].dense];
}

bool ObjectSet::contains(ObjectId id) const noexcept
{
    return id.index < slots_.size() && slots_[id.index].generation == id.generation && slots_[id.index].dense != kFree;
}

core::Result<> ObjectSet::remove(ObjectId id)
{
    if (find(id) == nullptr)
    {
        return makeError(ErrorCode::kNotFound, "ObjectSet::remove: stale or unknown object");
    }
    // Retrait par échange avec le dernier objet dense.
    const std::uint32_t dense = slots_[id.index].dense;
    const std::uint32_t last = static_cast<std::uint32_t>(objects_.size() - 1);
    if (dense != last)
    {
        objects_[dense] = std::move(objects_[last]);
        worldBounds_[dense] = worldBounds_[last];
        slots_[objects_[dense].slot].dense = dense;
    }
    objects_.pop_back();
    worldBounds_.pop_back();
    slots_[id.index].dense = kFree;
    ++slots_[id.index].generation; // les anciens identifiants deviennent périmés
    freeSlots_.push_back(id.index);
    structureDirty_ = true;
    return {};
}

core::Result<> ObjectSet::setTransform(ObjectId id, const glm::mat4 &transform)
{
    Object *object = find(id);
    if (object == nullptr)
    {
        return makeError(ErrorCode::kNotFound, "ObjectSet::setTransform: stale or unknown object");
    }
    object->transform = transform;
    worldBounds_[slots_[id.index].dense] = object->localBounds.transformed(transform);
    boundsDirty_ = true;
    return {};
}

core::Result<> ObjectSet::setColor(ObjectId id, const glm::vec4 &color)
{
    Object *object = find(id);
    if (object == nullptr)
    {
        return makeError(ErrorCode::kNotFound, "ObjectSet::setColor: stale or unknown object");
    }
    object->color = color;
    return {};
}

void ObjectSet::clear() noexcept
{
    objects_.clear();
    worldBounds_.clear();
    for (Slot &slot : slots_)
    {
        if (slot.dense != kFree)
        {
            slot.dense = kFree;
            ++slot.generation;
        }
    }
    freeSlots_.clear();
    for (std::uint32_t i = static_cast<std::uint32_t>(slots_.size()); i-- > 0;)
    {
        freeSlots_.push_back(i);
    }
    bvh_.clear();
    structureDirty_ = true;
    boundsDirty_ = false;
}

VisibilityStats ObjectSet::prepare(const CullView &view, InstanceBatchBuilder &out, const VisibilityOptions &options)
{
    VisibilityStats stats;
    stats.objects = static_cast<std::uint32_t>(objects_.size());
    out.clear();
    visible_.clear();
    if (options.frustumCulling)
    {
        if (options.useBvh)
        {
            if (structureDirty_)
            {
                bvh_.build(worldBounds_);
                structureDirty_ = false;
                boundsDirty_ = false;
            }
            else if (boundsDirty_)
            {
                bvh_.refit(worldBounds_);
                boundsDirty_ = false;
            }
            stats.cull = bvh_.query(view, worldBounds_, visible_);
        }
        else
        {
            stats.cull = cullLinear(view, worldBounds_, visible_);
        }
    }
    else
    {
        visible_.resize(objects_.size());
        for (std::uint32_t i = 0; i < visible_.size(); ++i)
        {
            visible_[i] = i;
        }
        stats.cull.visible = static_cast<std::uint32_t>(visible_.size());
    }

    out.reserve(visible_.size());
    for (const std::uint32_t index : visible_)
    {
        const Object &object = objects_[index];
        const auto levels = static_cast<std::uint8_t>(object.lodMeshes.size());
        LodSelection selection;
        if (options.lod)
        {
            const float distance = glm::length(worldBounds_[index].center() - view.eye);
            selection = selectLod(lodSettings_[object.lodSettings], levels, distance);
        }
        else
        {
            selection.count = 1;
        }
        if (selection.count == 0)
        {
            ++stats.lodCulled;
            continue;
        }
        if (selection.count == 2)
        {
            ++stats.crossFades;
        }
        for (std::uint8_t i = 0; i < selection.count; ++i)
        {
            const LodDraw &draw = selection.draws[i];
            // REN-35 : la couleur de l'instance devient celle de son niveau de détail (mode « lod »).
            const glm::vec4 color = options.lodColors ? glm::vec4(lodDebugColor(draw.level), object.color.a) : object.color;
            out.add({object.lodMeshes[draw.level], object.material}, object.transform, color, draw.dither);
            ++stats.instances;
        }
    }
    out.build();
    return stats;
}

} // namespace orosmo::renderer
