// ECS-01 (A79 lot L2) : stockage d'un type de composant, sans type C++. Ensemble clairsemé (« sparse set ») :
//   - tableau dense d'octets, contigu et aligné (`desc().alignment`), un composant tous les `desc().size` octets ;
//   - tableau dense des entités, dans le même ordre ;
//   - tableau clairsemé indice d'entité -> position dense.
// Ajout en fin de tableau, retrait par échange avec le dernier (`memcpy`) : les composants restent contigus, l'ordre dépend
// seulement de la suite des ajouts et retraits (déterministe). Un pointeur rendu reste valide jusqu'au prochain ajout ou
// retrait dans le même stockage. Utilisé par `World` ; rien n'y est propre à un thread.
#pragma once

#include <orosmo/scene/component.hpp>
#include <orosmo/scene/entity.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace orosmo::scene
{

class ComponentPool
{
  public:
    explicit ComponentPool(ComponentDesc desc);
    ~ComponentPool();
    ComponentPool(const ComponentPool &other);
    ComponentPool &operator=(const ComponentPool &other);
    ComponentPool(ComponentPool &&other) noexcept;
    ComponentPool &operator=(ComponentPool &&other) noexcept;

    const ComponentDesc &desc() const noexcept { return desc_; }
    // Remplace la description par une autre de même disposition (nouvelle valeur par défaut, textes d'aide).
    void setDesc(ComponentDesc desc);

    std::size_t size() const noexcept { return entities_.size(); }
    bool empty() const noexcept { return entities_.empty(); }
    std::span<const Entity> entities() const noexcept { return entities_; }

    bool contains(std::uint32_t entityIndex) const noexcept
    {
        return entityIndex < sparse_.size() && sparse_[entityIndex] != kAbsent;
    }
    void *find(std::uint32_t entityIndex) noexcept
    {
        return contains(entityIndex) ? at(sparse_[entityIndex]) : nullptr;
    }
    const void *find(std::uint32_t entityIndex) const noexcept
    {
        return contains(entityIndex) ? at(sparse_[entityIndex]) : nullptr;
    }
    void *at(std::size_t denseIndex) noexcept { return data_ + denseIndex * desc_.size; }
    const void *at(std::size_t denseIndex) const noexcept { return data_ + denseIndex * desc_.size; }

    // Écrit `value` (ou la valeur par défaut si nul) pour l'entité, en l'ajoutant si elle n'en a pas. Rend l'emplacement.
    void *set(Entity entity, const void *value);
    // Faux si l'entité n'avait pas ce composant.
    bool erase(std::uint32_t entityIndex);
    void clear() noexcept;

  private:
    static constexpr std::uint32_t kAbsent = 0xFFFFFFFFu;

    void reserve(std::size_t count);
    void release() noexcept;

    ComponentDesc desc_;
    std::byte *data_ = nullptr;
    std::size_t capacity_ = 0; // en composants
    std::vector<Entity> entities_;
    std::vector<std::uint32_t> sparse_;
};

} // namespace orosmo::scene
