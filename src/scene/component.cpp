// ECS-01 (A79 lot L2) : validation des descriptions de composants et stockage contigu par type.
#include <orosmo/scene/component.hpp>
#include <orosmo/scene/component_pool.hpp>

#include <algorithm>
#include <limits>
#include <stdexcept>
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

bool sameFieldLayout(const std::vector<FieldDesc> &a, const std::vector<FieldDesc> &b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].name != b[i].name || a[i].type != b[i].type || a[i].offset != b[i].offset || a[i].size != b[i].size ||
            !sameFieldLayout(a[i].fields, b[i].fields))
            return false;
    return true;
}

bool ComponentDesc::sameLayout(const ComponentDesc &other) const
{
    return name == other.name && size == other.size && alignment == other.alignment && version == other.version &&
           sameFieldLayout(fields, other.fields);
}

Result<> validateComponentDesc(const ComponentDesc &desc)
{
    if (desc.name.empty())
        return core::makeError(ErrorCode::kInvalidArgument, "component without a name");
    if (!isPowerOfTwo(desc.alignment) || desc.alignment > ComponentDesc::kMaxAlignment)
        return core::makeError(ErrorCode::kInvalidArgument, "component " + desc.name + ": invalid alignment");
    if (desc.size > ComponentDesc::kMaxSize)
        return core::makeError(ErrorCode::kInvalidArgument, "component " + desc.name + ": size above ComponentDesc::kMaxSize");
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
    // Copie des entités avant l'allocation du bloc : si elle échoue, aucun bloc n'est perdu (le destructeur ne s'exécute
    // pas pour un objet dont la construction échoue). Ensuite, plus rien ne peut échouer.
    std::vector<Entity> entities = other.entities_;
    reserve(entities.size()); // entities_ encore vide : rien à recopier de l'ancien tableau
    if (!entities.empty())
        std::memcpy(data_, other.data_, entities.size() * desc_.size);
    entities_ = std::move(entities);
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
    // Taille bornée par kMaxSize (validateComponentDesc) : capacité bornée pour que capacité × taille ne déborde pas.
    const std::size_t maxCount = std::numeric_limits<std::size_t>::max() / 2u / std::max<std::size_t>(desc_.size, 1u);
    if (count > maxCount)
        throw std::length_error("ComponentPool: too many components");
    std::size_t capacity = std::max<std::size_t>(capacity_ <= maxCount / 2u ? capacity_ * 2u : maxCount, 16u);
    capacity = std::min(std::max(capacity, count), maxCount);
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
        // `value` peut désigner un composant de ce stockage, que `reserve` libère en agrandissant le bloc : copie d'abord.
        std::vector<std::byte> saved;
        if (value != nullptr && entities_.size() == capacity_)
        {
            saved.assign(static_cast<const std::byte *>(value), static_cast<const std::byte *>(value) + desc_.size);
            value = saved.data();
        }
        reserve(entities_.size() + 1u);
        if (entity.index >= sparse_.size())
            sparse_.resize(static_cast<std::size_t>(entity.index) + 1u, kAbsent);
        entities_.push_back(entity);
        // Indice publié après l'insertion : un échec de push_back ne laisse pas d'entrée vers un composant absent.
        sparse_[entity.index] = static_cast<std::uint32_t>(entities_.size() - 1u);
        slot = static_cast<std::byte *>(at(entities_.size() - 1u));
        if (value != nullptr)
        {
            std::memcpy(slot, value, desc_.size);
            return slot;
        }
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
