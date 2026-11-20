// RHI-01 : handles opaques (indice + génération) et table de ressources qui détecte les handles périmés.
// Un handle périmé (ressource détruite, emplacement réutilisé) est refusé par `get`, jamais déréférencé.
#pragma once

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace orosmo::rhi
{

template <typename Tag> struct Handle
{
    // generation == 0 : handle nul. Les générations des emplacements vivants commencent à 1.
    std::uint32_t index = 0;
    std::uint32_t generation = 0;

    constexpr bool isNull() const noexcept { return generation == 0; }
    constexpr explicit operator bool() const noexcept { return generation != 0; }
    friend constexpr bool operator==(const Handle &, const Handle &) noexcept = default;
};

using BufferHandle = Handle<struct BufferTag>;
using TextureHandle = Handle<struct TextureTag>;
using SamplerHandle = Handle<struct SamplerTag>;
using ShaderHandle = Handle<struct ShaderTag>;
using PipelineHandle = Handle<struct PipelineTag>;
using BindGroupHandle = Handle<struct BindGroupTag>;
using QueryPoolHandle = Handle<struct QueryPoolTag>;

// Table générationnelle : ajout O(1), retrait O(1), emplacements réutilisés (liste libre).
// Destinée aux backends ; non thread-safe (le périphérique est utilisé depuis un seul thread).
template <typename Tag, typename T> class HandlePool
{
  public:
    using HandleType = Handle<Tag>;

    HandleType add(T value)
    {
        std::uint32_t index;
        if (!freeList_.empty())
        {
            index = freeList_.back();
            freeList_.pop_back();
        }
        else
        {
            index = static_cast<std::uint32_t>(slots_.size());
            slots_.emplace_back();
        }
        Slot &slot = slots_[index];
        slot.value.emplace(std::move(value));
        ++live_;
        return HandleType{index, slot.generation};
    }

    bool contains(HandleType handle) const noexcept { return find(handle) != nullptr; }

    T *get(HandleType handle) noexcept
    {
        Slot *slot = find(handle);
        return slot != nullptr ? &*slot->value : nullptr;
    }
    const T *get(HandleType handle) const noexcept
    {
        const Slot *slot = find(handle);
        return slot != nullptr ? &*slot->value : nullptr;
    }

    // Retire la ressource et invalide tous les handles qui la désignaient. Renvoie std::nullopt si le handle est périmé.
    std::optional<T> remove(HandleType handle)
    {
        Slot *slot = find(handle);
        if (slot == nullptr)
        {
            return std::nullopt;
        }
        std::optional<T> out = std::move(slot->value);
        slot->value.reset();
        // Une génération qui boucle sur 0 deviendrait un handle nul : on saute 0.
        if (++slot->generation == 0)
        {
            slot->generation = 1;
        }
        freeList_.push_back(handle.index);
        --live_;
        return out;
    }

    std::size_t size() const noexcept { return live_; }

    // Visite chaque ressource vivante (destruction à l'arrêt du périphérique).
    template <typename F> void forEach(F &&visit)
    {
        for (std::uint32_t i = 0; i < slots_.size(); ++i)
        {
            if (slots_[i].value)
            {
                visit(HandleType{i, slots_[i].generation}, *slots_[i].value);
            }
        }
    }

  private:
    struct Slot
    {
        std::optional<T> value;
        std::uint32_t generation = 1;
    };

    Slot *find(HandleType handle) noexcept
    {
        if (handle.generation == 0 || handle.index >= slots_.size())
        {
            return nullptr;
        }
        Slot &slot = slots_[handle.index];
        return (slot.value && slot.generation == handle.generation) ? &slot : nullptr;
    }
    const Slot *find(HandleType handle) const noexcept { return const_cast<HandlePool *>(this)->find(handle); }

    std::vector<Slot> slots_;
    std::vector<std::uint32_t> freeList_;
    std::size_t live_ = 0;
};

} // namespace orosmo::rhi
