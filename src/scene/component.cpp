// ECS-01 (A79 lot L2) : validation des descriptions de composants et stockage contigu par type.
#include <orosmo/scene/component.hpp>
#include <orosmo/scene/component_pool.hpp>

#include <algorithm>
#include <cstring>
#include <new>
#include <utility>

namespace orosmo::scene
{

using core::ErrorCode;
using core::FieldDesc;
using core::FieldType;
using core::Result;

namespace
{
bool isPowerOfTwo(std::size_t v) { return v != 0 && (v & (v - 1u)) == 0; }

Result<> validateFields(const std::vector<FieldDesc> &fields, std::size_t enclosingSize, const std::string &path)
{
    for (const FieldDesc &f : fields)
    {
        const std::string name = path + "." + f.name;
        if (f.name.empty())
            return core::makeError(ErrorCode::kInvalidArgument, "component field without a name in " + path);
        if (f.type == FieldType::kString || f.type == FieldType::kArray)
            return core::makeError(ErrorCode::kInvalidArgument,
                                   "component field " + name + " is a string or a dynamic array: components must be trivially copyable");
        if (f.size == 0 || f.offset > enclosingSize || f.size > enclosingSize - f.offset)
            return core::makeError(ErrorCode::kInvalidArgument, "component field " + name + " lies outside its structure");
        if (auto ok = validateFields(f.fields, f.size, name); !ok)
            return ok;
    }
    return {};
}
} // namespace

bool ComponentDesc::sameLayout(const ComponentDesc &other) const
{
    return name == other.name && size == other.size && alignment == other.alignment && version == other.version &&
           fields == other.fields;
}

Result<> validateComponentDesc(const ComponentDesc &desc)
{
    if (desc.name.empty())
        return core::makeError(ErrorCode::kInvalidArgument, "component without a name");
    if (!isPowerOfTwo(desc.alignment) || desc.alignment > ComponentDesc::kMaxAlignment)
        return core::makeError(ErrorCode::kInvalidArgument, "component " + desc.name + ": invalid alignment");
    if (desc.size == 0 || desc.size % desc.alignment != 0)
        return core::makeError(ErrorCode::kInvalidArgument, "component " + desc.name + ": size must be a non-zero multiple of the alignment");
    if (!desc.defaultValue.empty() && desc.defaultValue.size() != desc.size)
        return core::makeError(ErrorCode::kInvalidArgument, "component " + desc.name + ": default value of the wrong size");
    return validateFields(desc.fields, desc.size, desc.name);
}

// ── ComponentPool ─────────────────────────────────────────────────────────────

ComponentPool::ComponentPool(ComponentDesc desc) : desc_(std::move(desc)) {}

ComponentPool::~ComponentPool() { release(); }

ComponentPool::ComponentPool(const ComponentPool &other)
    : desc_(other.desc_), sparse_(other.sparse_)
{
    reserve(other.entities_.size()); // entities_ encore vide : rien à recopier de l'ancien tableau
    if (!other.entities_.empty())
        std::memcpy(data_, other.data_, other.entities_.size() * desc_.size);
    entities_ = other.entities_;
}

ComponentPool &ComponentPool::operator=(const ComponentPool &other)
{
    if (this != &other)
    {
        ComponentPool copy(other);
        *this = std::move(copy);
    }
    return *this;
}

ComponentPool::ComponentPool(ComponentPool &&other) noexcept
    : desc_(std::move(other.desc_)), data_(std::exchange(other.data_, nullptr)), capacity_(std::exchange(other.capacity_, 0)),
      entities_(std::move(other.entities_)), sparse_(std::move(other.sparse_))
{
    other.entities_.clear();
    other.sparse_.clear();
}

ComponentPool &ComponentPool::operator=(ComponentPool &&other) noexcept
{
    if (this != &other)
    {
        release();
        // L'alignement de la mémoire libérée par release() est celui de l'ancienne description : la description change après.
        desc_ = std::move(other.desc_);
        data_ = std::exchange(other.data_, nullptr);
        capacity_ = std::exchange(other.capacity_, 0);
        entities_ = std::move(other.entities_);
        sparse_ = std::move(other.sparse_);
        other.entities_.clear();
        other.sparse_.clear();
    }
    return *this;
}

void ComponentPool::setDesc(ComponentDesc desc)
{
    // Même taille et même alignement (vérifié par World) : les données en place restent valides.
    desc_ = std::move(desc);
}

void ComponentPool::release() noexcept
{
    if (data_ != nullptr)
        ::operator delete(data_, std::align_val_t(desc_.alignment));
    data_ = nullptr;
    capacity_ = 0;
}

void ComponentPool::reserve(std::size_t count)
{
    if (count <= capacity_)
        return;
    std::size_t capacity = std::max<std::size_t>(capacity_ * 2u, 16u);
    capacity = std::max(capacity, count);
    auto *data = static_cast<std::byte *>(::operator new(capacity * desc_.size, std::align_val_t(desc_.alignment)));
    if (!entities_.empty())
        std::memcpy(data, data_, entities_.size() * desc_.size);
    release();
    data_ = data;
    capacity_ = capacity;
}

void *ComponentPool::set(Entity entity, const void *value)
{
    std::byte *slot = nullptr;
    if (contains(entity.index))
    {
        slot = static_cast<std::byte *>(at(sparse_[entity.index]));
        entities_[sparse_[entity.index]] = entity;
    }
    else
    {
        reserve(entities_.size() + 1u);
        if (entity.index >= sparse_.size())
            sparse_.resize(static_cast<std::size_t>(entity.index) + 1u, kAbsent);
        sparse_[entity.index] = static_cast<std::uint32_t>(entities_.size());
        entities_.push_back(entity);
        slot = static_cast<std::byte *>(at(entities_.size() - 1u));
    }
    if (value != nullptr)
        std::memmove(slot, value, desc_.size); // memmove : `value` peut être l'emplacement lui-même
    else if (!desc_.defaultValue.empty())
        std::memcpy(slot, desc_.defaultValue.data(), desc_.size);
    else
        std::memset(slot, 0, desc_.size);
    return slot;
}

bool ComponentPool::erase(std::uint32_t entityIndex)
{
    if (!contains(entityIndex))
        return false;
    const std::uint32_t dense = sparse_[entityIndex];
    const std::uint32_t last = static_cast<std::uint32_t>(entities_.size() - 1u);
    if (dense != last)
    {
        std::memcpy(at(dense), at(last), desc_.size);
        entities_[dense] = entities_[last];
        sparse_[entities_[dense].index] = dense;
    }
    entities_.pop_back();
    sparse_[entityIndex] = kAbsent;
    return true;
}

void ComponentPool::clear() noexcept
{
    entities_.clear();
    sparse_.clear();
}

} // namespace orosmo::scene
