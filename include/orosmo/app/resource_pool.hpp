// Réserve de ressources désignées par `core::Handle<T>` (A79 lot L13) : les composants de rendu de la scène
// (`render_components.hpp`) ne tiennent que des poignées, jamais de pointeur ni d'objet du renderer (validation d'A79, choix 2).
//
// Une poignée = emplacement + génération : retirer une ressource change la génération de son emplacement, et l'ancienne poignée
// ne retombe jamais sur la ressource qui le réutilise (`get` rend nul). La réserve possède les objets (`std::unique_ptr<T>`) ;
// leur adresse ne change pas tant qu'ils y sont. Non thread-safe (un propriétaire : l'application).
#pragma once

#include <orosmo/core/resources.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace orosmo::app
{

template <typename T> class ResourcePool
{
  public:
    ResourcePool() = default;
    ResourcePool(const ResourcePool &) = delete;
    ResourcePool &operator=(const ResourcePool &) = delete;
    ResourcePool(ResourcePool &&) noexcept = default;
    ResourcePool &operator=(ResourcePool &&) noexcept = default;
    ~ResourcePool() = default;

    // Prend l'objet ; poignée vide (`valid()` faux) si `object` est nul.
    core::Handle<T> add(std::unique_ptr<T> object)
    {
        if (!object)
            return {};
        std::uint32_t index = 0;
        if (!free_.empty())
        {
            index = free_.back();
            free_.pop_back();
        }
        else
        {
            index = static_cast<std::uint32_t>(slots_.size());
            slots_.emplace_back();
        }
        Slot &slot = slots_[index];
        slot.object = std::move(object);
        ++count_;
        return core::Handle<T>{index, slot.generation};
    }

    // Nul si la poignée est vide, périmée ou d'une autre réserve de même taille.
    T *get(core::Handle<T> handle) const noexcept
    {
        if (!handle.valid() || handle.index >= slots_.size())
            return nullptr;
        const Slot &slot = slots_[handle.index];
        return slot.generation == handle.generation ? slot.object.get() : nullptr;
    }

    bool contains(core::Handle<T> handle) const noexcept { return get(handle) != nullptr; }

    // Détruit l'objet ; faux si la poignée n'est pas (ou plus) valide.
    bool remove(core::Handle<T> handle)
    {
        if (!contains(handle))
            return false;
        Slot &slot = slots_[handle.index];
        slot.object.reset();
        ++slot.generation;
        if (slot.generation == 0)
            slot.generation = 1; // 0 = poignée vide
        free_.push_back(handle.index);
        --count_;
        return true;
    }

    std::size_t size() const noexcept { return count_; }
    bool empty() const noexcept { return count_ == 0; }

    // fn(poignée, objet) pour chaque objet, par emplacement croissant.
    template <typename Fn> void forEach(Fn &&fn) const
    {
        for (std::uint32_t i = 0; i < slots_.size(); ++i)
            if (slots_[i].object)
                fn(core::Handle<T>{i, slots_[i].generation}, *slots_[i].object);
    }

    // Détruit tous les objets (par emplacement décroissant) ; les poignées données avant deviennent périmées.
    void clear()
    {
        for (std::uint32_t i = static_cast<std::uint32_t>(slots_.size()); i-- > 0;)
            if (slots_[i].object)
                (void)remove(core::Handle<T>{i, slots_[i].generation});
    }

  private:
    struct Slot
    {
        std::unique_ptr<T> object;
        std::uint32_t generation = 1;
    };
    std::vector<Slot> slots_;
    std::vector<std::uint32_t> free_;
    std::size_t count_ = 0;
};

} // namespace orosmo::app
