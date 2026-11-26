// File bornée à un producteur et un consommateur, sans verrou (M8, A79 lot L1). Sert aux échanges entre le thread de simulation
// et le rendu (SIM-01 bis, `docs/05` M8) : un seul thread appelle `tryPush` / `emplace`, un seul autre appelle `tryPop` / `front`.
// Les deux indices sont des compteurs atomiques (acquire/release) sur des lignes de cache séparées ; chaque côté garde une copie
// de l'indice de l'autre pour ne relire l'atomique que lorsque la file lui paraît pleine ou vide. Aucune allocation après la
// construction. À ne pas confondre avec `core::RingBuffer` (tampon de mesures sans synchronisation).
#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>

namespace orosmo::core
{

template <typename T> class SpscQueue
{
    static_assert(std::is_nothrow_destructible_v<T>, "SpscQueue: T must be nothrow destructible");

  public:
    // Capacité utile : `capacity` arrondie à la puissance de deux supérieure (au moins 2).
    explicit SpscQueue(std::size_t capacity) : capacity_(roundUp(capacity)), mask_(capacity_ - 1u)
    {
        slots_ = std::make_unique<Slot[]>(capacity_);
    }

    SpscQueue(const SpscQueue &) = delete;
    SpscQueue &operator=(const SpscQueue &) = delete;

    ~SpscQueue()
    {
        std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        for (; head != tail; ++head)
            slotAt(head)->~T();
    }

    // Producteur. Faux si la file est pleine (rien n'est construit).
    template <typename... Args> bool emplace(Args &&...args) noexcept(std::is_nothrow_constructible_v<T, Args...>)
    {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail - headCache_ == capacity_)
        {
            headCache_ = head_.load(std::memory_order_acquire);
            if (tail - headCache_ == capacity_)
                return false;
        }
        ::new (static_cast<void *>(slots_[tail & mask_].bytes)) T(std::forward<Args>(args)...);
        tail_.store(tail + 1u, std::memory_order_release);
        return true;
    }

    bool tryPush(const T &value) noexcept(std::is_nothrow_copy_constructible_v<T>) { return emplace(value); }
    bool tryPush(T &&value) noexcept(std::is_nothrow_move_constructible_v<T>) { return emplace(std::move(value)); }

    // Consommateur : premier élément, ou nullptr si la file est vide. Valide jusqu'au prochain `pop` / `tryPop`.
    T *front() noexcept
    {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        if (head == tailCache_)
        {
            tailCache_ = tail_.load(std::memory_order_acquire);
            if (head == tailCache_)
                return nullptr;
        }
        return slotAt(head);
    }

    // Consommateur : retire le premier élément (la file ne doit pas être vide : appeler après un `front()` non nul).
    void pop() noexcept
    {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        slotAt(head)->~T();
        head_.store(head + 1u, std::memory_order_release);
    }

    // Consommateur : déplace le premier élément dans `out`. Faux si la file est vide.
    bool tryPop(T &out) noexcept(std::is_nothrow_move_assignable_v<T>)
    {
        T *value = front();
        if (value == nullptr)
            return false;
        out = std::move(*value);
        pop();
        return true;
    }

    std::optional<T> tryPop() noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        T *value = front();
        if (value == nullptr)
            return std::nullopt;
        std::optional<T> out(std::move(*value));
        pop();
        return out;
    }

    // Approximations (exactes seulement quand l'autre côté ne travaille pas) : diagnostic et tests.
    std::size_t sizeApprox() const noexcept { return tail_.load(std::memory_order_acquire) - head_.load(std::memory_order_acquire); }
    bool emptyApprox() const noexcept { return sizeApprox() == 0u; }
    std::size_t capacity() const noexcept { return capacity_; }

  private:
    struct Slot
    {
        alignas(T) unsigned char bytes[sizeof(T)];
    };

    static std::size_t roundUp(std::size_t n) noexcept
    {
        std::size_t c = 2u;
        while (c < n)
            c <<= 1u;
        return c;
    }

    T *slotAt(std::size_t index) noexcept { return std::launder(reinterpret_cast<T *>(slots_[index & mask_].bytes)); }

    // 64 octets : ligne de cache des processeurs visés (x86-64, ARM64 ; Apple M a 128, sans effet sur la justesse).
    // `std::hardware_destructive_interference_size` déclenche -Winterference-size avec GCC et varie selon -mtune.
    static constexpr std::size_t kCacheLine = 64u;

    const std::size_t capacity_;
    const std::size_t mask_;
    std::unique_ptr<Slot[]> slots_;

    alignas(kCacheLine) std::atomic<std::size_t> head_{0u}; // écrit par le consommateur
    alignas(kCacheLine) std::size_t tailCache_ = 0u;        // copie du consommateur
    alignas(kCacheLine) std::atomic<std::size_t> tail_{0u}; // écrit par le producteur
    alignas(kCacheLine) std::size_t headCache_ = 0u;        // copie du producteur
};

} // namespace orosmo::core
