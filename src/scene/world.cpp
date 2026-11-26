// ECS-01, ECS-02 (A79 lot L2) : monde d'entités, registre des composants par nom, hiérarchie et transformations.
#include <orosmo/scene/world.hpp>

#include <orosmo/core/error.hpp>

#include <string>
#include <utility>

namespace orosmo::scene
{

using core::ErrorCode;
using core::Result;

namespace detail
{
Result<> checkComponentType(const ComponentDesc &desc, std::size_t size, std::size_t alignment, const std::vector<core::FieldDesc> *fields)
{
    if (desc.size != size || desc.alignment != alignment)
        return core::makeError(ErrorCode::kInvalidArgument,
                               "component " + desc.name + ": the C++ type does not match the registered size or alignment");
    if (fields != nullptr && *fields != desc.fields)
        return core::makeError(ErrorCode::kInvalidArgument, "component " + desc.name + ": the C++ type does not match the registered fields");
    return {};
}
} // namespace detail

World::World()
{
    auto id = registerComponent(describeComponent<Transform>(std::string(kTransformName)));
    ENGINE_ASSERT_ALWAYS(id.has_value(), "the Transform component description is valid");
    transformType_ = ComponentType<Transform>{*id};
}

void World::assertNotIterating() const noexcept
{
    ENGINE_ASSERT(iterating_ == 0, "World: structural change during a view or forEach iteration");
}

// ── Entités ───────────────────────────────────────────────────────────────────

Entity World::entityAt(std::uint32_t index) const noexcept
{
    if (index == kNone)
        return kNullEntity;
    return Entity{index, records_[index].generation};
}

Entity World::create()
{
    assertNotIterating();
    std::uint32_t index = 0;
    if (!freeIndices_.empty())
    {
        index = freeIndices_.back();
        freeIndices_.pop_back();
    }
    else
    {
        index = static_cast<std::uint32_t>(records_.size());
        records_.emplace_back();
    }
    EntityRecord &r = records_[index];
    const std::uint32_t generation = r.generation;
    r = EntityRecord{};
    r.generation = generation;
    r.alive = true;
    ++liveCount_;
    return Entity{index, generation};
}

bool World::alive(Entity entity) const noexcept
{
    return entity.index < records_.size() && records_[entity.index].alive && records_[entity.index].generation == entity.generation;
}

std::vector<Entity> World::entities() const
{
    std::vector<Entity> out;
    out.reserve(liveCount_);
    for (std::uint32_t i = 0; i < records_.size(); ++i)
        if (records_[i].alive)
            out.push_back(Entity{i, records_[i].generation});
    return out;
}

bool World::destroy(Entity entity)
{
    assertNotIterating();
    if (!alive(entity))
        return false;
    detach(entity.index);
    // Descendants en largeur, dans l'ordre des enfants : ordre de libération des indices déterministe.
    std::vector<std::uint32_t> doomed{entity.index};
    for (std::size_t i = 0; i < doomed.size(); ++i)
        for (std::uint32_t c = records_[doomed[i]].firstChild; c != kNone; c = records_[c].nextSibling)
            doomed.push_back(c);
    for (std::uint32_t index : doomed)
    {
        for (Slot &slot : slots_)
            if (slot.used)
                slot.pool.erase(index);
        EntityRecord &r = records_[index];
        r.alive = false;
        r.dirty = false;
        r.parent = r.firstChild = r.lastChild = r.nextSibling = r.prevSibling = kNone;
        ++r.generation;
        freeIndices_.push_back(index);
        --liveCount_;
    }
    return true;
}

// ── Types de composants ───────────────────────────────────────────────────────

ComponentPool *World::pool(ComponentId id) noexcept
{
    if (id.index >= slots_.size() || !slots_[id.index].used || slots_[id.index].generation != id.generation)
        return nullptr;
    return &slots_[id.index].pool;
}

const ComponentPool *World::pool(ComponentId id) const noexcept
{
    return const_cast<World *>(this)->pool(id);
}

Result<ComponentId> World::registerComponent(ComponentDesc desc)
{
    if (auto ok = validateComponentDesc(desc); !ok)
        return std::unexpected(ok.error());
    if (auto existing = componentId(desc.name))
    {
        ComponentPool &p = slots_[existing->index].pool;
        if (!p.desc().sameLayout(desc))
            return core::makeError(ErrorCode::kAlreadyExists,
                                   "component " + desc.name + " is already registered with another layout or version");
        p.setDesc(std::move(desc));
        return *existing;
    }
    std::uint32_t index = 0;
    while (index < slots_.size() && slots_[index].used)
        ++index;
    if (index == slots_.size())
        slots_.emplace_back();
    Slot &slot = slots_[index];
    slot.used = true;
    slot.pool = ComponentPool(std::move(desc));
    return ComponentId{index, slot.generation};
}

Result<> World::unregisterComponent(std::string_view name)
{
    assertNotIterating();
    if (name == kTransformName)
        return core::makeError(ErrorCode::kInvalidArgument, "the engine component Transform cannot be unregistered");
    auto id = componentId(name);
    if (!id)
        return std::unexpected(id.error());
    Slot &slot = slots_[id->index];
    slot.used = false;
    ++slot.generation;
    slot.pool = ComponentPool(ComponentDesc{});
    return {};
}

Result<ComponentId> World::componentId(std::string_view name) const
{
    for (std::uint32_t i = 0; i < slots_.size(); ++i)
        if (slots_[i].used && slots_[i].pool.desc().name == name)
            return ComponentId{i, slots_[i].generation};
    return core::makeError(ErrorCode::kNotFound, "unknown component: " + std::string(name));
}

const ComponentDesc *World::componentDesc(ComponentId id) const noexcept
{
    const ComponentPool *p = pool(id);
    return p != nullptr ? &p->desc() : nullptr;
}

std::vector<ComponentId> World::componentIds() const
{
    std::vector<ComponentId> out;
    for (std::uint32_t i = 0; i < slots_.size(); ++i)
        if (slots_[i].used)
            out.push_back(ComponentId{i, slots_[i].generation});
    return out;
}

// ── Données ───────────────────────────────────────────────────────────────────

Result<void *> World::add(Entity entity, ComponentId id, const void *value)
{
    assertNotIterating();
    if (!alive(entity))
        return core::makeError(ErrorCode::kNotFound, "add: the entity does not exist");
    ComponentPool *p = pool(id);
    if (p == nullptr)
        return core::makeError(ErrorCode::kNotFound, "add: unknown or unregistered component id");
    void *slot = p->set(entity, value);
    if (id == transformType_.id)
        markDirty(entity.index);
    return slot;
}

Result<void *> World::add(Entity entity, std::string_view componentName, const void *value)
{
    auto id = componentId(componentName);
    if (!id)
        return std::unexpected(id.error());
    return add(entity, *id, value);
}

bool World::remove(Entity entity, ComponentId id)
{
    assertNotIterating();
    ComponentPool *p = pool(id);
    if (p == nullptr || !alive(entity) || !p->erase(entity.index))
        return false;
    if (id == transformType_.id)
        markDirty(entity.index); // les enfants reprennent la transformation d'un ancêtre plus haut
    return true;
}

bool World::has(Entity entity, ComponentId id) const noexcept
{
    const ComponentPool *p = pool(id);
    return p != nullptr && alive(entity) && p->contains(entity.index);
}

void *World::get(Entity entity, ComponentId id) noexcept
{
    ComponentPool *p = pool(id);
    return p != nullptr && alive(entity) ? p->find(entity.index) : nullptr;
}

const void *World::get(Entity entity, ComponentId id) const noexcept
{
    const ComponentPool *p = pool(id);
    return p != nullptr && alive(entity) ? p->find(entity.index) : nullptr;
}

void World::forEachComponent(Entity entity, const std::function<void(ComponentId, void *)> &fn)
{
    if (!alive(entity))
        return;
    ++iterating_;
    for (std::uint32_t i = 0; i < slots_.size(); ++i)
        if (slots_[i].used)
            if (void *data = slots_[i].pool.find(entity.index))
                fn(ComponentId{i, slots_[i].generation}, data);
    --iterating_;
}

void World::forEach(std::span<const ComponentId> ids, const std::function<void(Entity, std::span<void *const>)> &fn)
{
    if (ids.empty())
        return;
    std::vector<ComponentPool *> pools;
    pools.reserve(ids.size());
    std::size_t lead = 0;
    for (ComponentId id : ids)
    {
        ComponentPool *p = pool(id);
        if (p == nullptr)
            return;
        if (!pools.empty() && p->size() < pools[lead]->size())
            lead = pools.size();
        pools.push_back(p);
    }
    std::vector<void *> data(pools.size(), nullptr);
    ++iterating_;
    const ComponentPool &leader = *pools[lead];
    const std::span<const Entity> entities = leader.entities();
    for (std::size_t i = 0; i < entities.size(); ++i)
    {
        bool all = true;
        for (std::size_t k = 0; k < pools.size() && all; ++k)
        {
            data[k] = pools[k]->find(entities[i].index);
            all = data[k] != nullptr;
        }
        if (all)
            fn(entities[i], std::span<void *const>(data));
    }
    --iterating_;
}

// ── Hiérarchie ────────────────────────────────────────────────────────────────

void World::detach(std::uint32_t index) noexcept
{
    EntityRecord &r = records_[index];
    if (r.parent == kNone)
        return;
    EntityRecord &p = records_[r.parent];
    if (r.prevSibling != kNone)
        records_[r.prevSibling].nextSibling = r.nextSibling;
    else
        p.firstChild = r.nextSibling;
    if (r.nextSibling != kNone)
        records_[r.nextSibling].prevSibling = r.prevSibling;
    else
        p.lastChild = r.prevSibling;
    r.parent = r.nextSibling = r.prevSibling = kNone;
}

Result<> World::setParent(Entity child, Entity parent)
{
    assertNotIterating();
    if (!alive(child))
        return core::makeError(ErrorCode::kNotFound, "setParent: the child entity does not exist");
    if (!parent.isNull() && !alive(parent))
        return core::makeError(ErrorCode::kNotFound, "setParent: the parent entity does not exist");
    if (!parent.isNull())
        for (std::uint32_t a = parent.index; a != kNone; a = records_[a].parent)
            if (a == child.index)
                return core::makeError(ErrorCode::kInvalidArgument, "setParent: the parent is the child itself or one of its descendants");
    if (records_[child.index].parent == (parent.isNull() ? kNone : parent.index))
        return {};
    detach(child.index);
    if (!parent.isNull())
    {
        EntityRecord &p = records_[parent.index];
        EntityRecord &c = records_[child.index];
        c.parent = parent.index;
        c.prevSibling = p.lastChild;
        if (p.lastChild != kNone)
            records_[p.lastChild].nextSibling = child.index;
        else
            p.firstChild = child.index;
        p.lastChild = child.index;
    }
    markDirty(child.index);
    return {};
}

Entity World::parent(Entity entity) const noexcept
{
    return alive(entity) ? entityAt(records_[entity.index].parent) : kNullEntity;
}

Entity World::firstChild(Entity entity) const noexcept
{
    return alive(entity) ? entityAt(records_[entity.index].firstChild) : kNullEntity;
}

Entity World::nextSibling(Entity entity) const noexcept
{
    return alive(entity) ? entityAt(records_[entity.index].nextSibling) : kNullEntity;
}

std::vector<Entity> World::children(Entity entity) const
{
    std::vector<Entity> out;
    if (!alive(entity))
        return out;
    for (std::uint32_t c = records_[entity.index].firstChild; c != kNone; c = records_[c].nextSibling)
        out.push_back(entityAt(c));
    return out;
}

// ── Transformations ───────────────────────────────────────────────────────────

void World::markDirty(std::uint32_t index)
{
    EntityRecord &r = records_[index];
    if (r.dirty)
        return;
    r.dirty = true;
    dirty_.push_back(index);
}

void World::markTransformDirty(Entity entity)
{
    if (alive(entity))
        markDirty(entity.index);
}

Result<> World::setTransform(Entity entity, const Transform &local)
{
    Transform *t = editTransform(entity);
    if (t == nullptr)
    {
        auto added = add(entity, transformType_, local);
        if (added == nullptr)
            return core::makeError(ErrorCode::kNotFound, "setTransform: the entity does not exist");
        return {};
    }
    t->position = local.position;
    t->rotation = local.rotation;
    t->scale = local.scale;
    return {};
}

Transform *World::editTransform(Entity entity)
{
    Transform *t = get(entity, transformType_);
    if (t != nullptr)
        markDirty(entity.index);
    return t;
}

const Transform *World::transform(Entity entity) const noexcept { return get(entity, transformType_); }

void World::recomputeSubtree(std::uint32_t root, const Transform *parentWorld, std::size_t &count)
{
    ComponentPool &transforms = slots_[transformType_.id.index].pool;
    stack_.clear();
    stack_.emplace_back(root, parentWorld);
    while (!stack_.empty())
    {
        const auto [index, above] = stack_.back();
        stack_.pop_back();
        EntityRecord &r = records_[index];
        r.dirty = false;
        const Transform *passed = above;
        if (void *data = transforms.find(index))
        {
            Transform *t = detail::componentCast<Transform>(data);
            composeWorld(*t, above);
            ++count;
            passed = t;
        }
        // Pile : enfants empilés à l'envers pour les traiter dans l'ordre. Les pointeurs restent valides : aucun ajout ici.
        for (std::uint32_t c = r.lastChild; c != kNone; c = records_[c].prevSibling)
            stack_.emplace_back(c, passed);
    }
}

std::size_t World::updateTransforms()
{
    assertNotIterating();
    std::size_t count = 0;
    ComponentPool &transforms = slots_[transformType_.id.index].pool;
    // dirty_ peut contenir des entités détruites depuis (r.dirty remis à faux) : ignorées.
    for (std::size_t i = 0; i < dirty_.size(); ++i)
    {
        const std::uint32_t index = dirty_[i];
        if (!records_[index].alive || !records_[index].dirty)
            continue;
        // Plus haut ancêtre sale : son sous-arbre couvre celui-ci. Au-dessus, tout est à jour.
        std::uint32_t top = index;
        for (std::uint32_t a = records_[index].parent; a != kNone; a = records_[a].parent)
            if (records_[a].dirty)
                top = a;
        const Transform *above = nullptr;
        for (std::uint32_t a = records_[top].parent; a != kNone && above == nullptr; a = records_[a].parent)
            if (const void *data = transforms.find(a))
                above = detail::componentCast<Transform>(data);
        recomputeSubtree(top, above, count);
    }
    dirty_.clear();
    return count;
}

} // namespace orosmo::scene
