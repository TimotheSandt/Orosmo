// CORE-03 : allocateurs. Arène linéaire (une image : on alloue, puis reset() libère tout d'un coup)
// et pool d'objets de taille fixe (liste libre, destruction à l'unité). Aucun n'est thread-safe : un par thread ou verrouillé.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace engine::core
{

class ArenaAllocator
{
  public:
    explicit ArenaAllocator(std::size_t capacityBytes)
        : mBuffer(std::make_unique<std::byte[]>(capacityBytes)), mCapacity(capacityBytes)
    {
    }

    // nullptr si l'arène est pleine. `alignment` : puissance de 2.
    void *allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) noexcept
    {
        const auto base = reinterpret_cast<std::uintptr_t>(mBuffer.get());
        const std::uintptr_t aligned = (base + mUsed + alignment - 1) & ~(static_cast<std::uintptr_t>(alignment) - 1);
        const std::size_t padding = static_cast<std::size_t>(aligned - base) - mUsed;
        // Contrôles sans addition qui puisse déborder : size est comparée à ce qu'il reste, pas ajoutée à mUsed.
        if (padding > mCapacity - mUsed || size > mCapacity - mUsed - padding)
            return nullptr;
        const std::size_t newUsed = mUsed + padding + size;
        mUsed = newUsed;
        if (mUsed > mPeak)
            mPeak = mUsed;
        return reinterpret_cast<void *>(aligned);
    }

    // Les destructeurs ne sont PAS appelés par reset() : réservé aux types trivialement destructibles.
    template <typename T, typename... Args> T *create(Args &&...args)
    {
        static_assert(std::is_trivially_destructible_v<T>, "ArenaAllocator::create requires a trivially destructible type");
        void *p = allocate(sizeof(T), alignof(T));
        return p ? new (p) T(std::forward<Args>(args)...) : nullptr;
    }

    void reset() noexcept { mUsed = 0; }
    std::size_t used() const noexcept { return mUsed; }
    std::size_t peak() const noexcept { return mPeak; }
    std::size_t capacity() const noexcept { return mCapacity; }

  private:
    std::unique_ptr<std::byte[]> mBuffer;
    std::size_t mCapacity;
    std::size_t mUsed = 0;
    std::size_t mPeak = 0;
};

// Pool d'objets T : mémoire réservée par blocs, emplacements recyclés. Le destructeur du pool libère les blocs mais
// n'appelle PAS les destructeurs des objets encore vivants : liveCount() != 0 à la destruction signale une fuite.
template <typename T> class ObjectPool
{
  public:
    explicit ObjectPool(std::size_t blockSize = 256) : mBlockSize(blockSize ? blockSize : 1) {}
    ObjectPool(const ObjectPool &) = delete;
    ObjectPool &operator=(const ObjectPool &) = delete;

    template <typename... Args> T *create(Args &&...args)
    {
        if (!mFree)
            grow();
        Slot *slot = mFree;
        Slot *const next = slot->next;
        // Le slot ne quitte la liste libre qu'après une construction réussie : si le constructeur de T lève, le pool
        // reste intact (liveCount() et liste libre inchangés).
        T *object = new (static_cast<void *>(slot->storage)) T(std::forward<Args>(args)...);
        mFree = next;
        ++mLive;
        return object;
    }

    void destroy(T *object) noexcept
    {
        if (!object)
            return;
        object->~T();
        Slot *slot = reinterpret_cast<Slot *>(reinterpret_cast<std::byte *>(object));
        slot->next = mFree;
        mFree = slot;
        --mLive;
    }

    std::size_t liveCount() const noexcept { return mLive; }
    std::size_t capacity() const noexcept { return mBlocks.size() * mBlockSize; }

  private:
    union Slot
    {
        Slot *next;
        alignas(T) std::byte storage[sizeof(T)];
    };

    void grow()
    {
        auto block = std::make_unique<Slot[]>(mBlockSize);
        Slot *const raw = block.get();
        mBlocks.push_back(std::move(block)); // d'abord le bloc : si push_back lève, la liste libre n'a pas bougé
        for (std::size_t i = 0; i < mBlockSize; ++i)
        {
            raw[i].next = mFree;
            mFree = &raw[i];
        }
    }

    std::size_t mBlockSize;
    std::size_t mLive = 0;
    Slot *mFree = nullptr;
    std::vector<std::unique_ptr<Slot[]>> mBlocks;
};

} // namespace engine::core
