// CORE-08 : bus d'événements typé. publish<E>() n'alloue pas : il parcourt un tableau d'abonnés déjà construit.
// (subscribe() peut allouer : il se fait au chargement, pas par image.) Mono-thread : publier et s'abonner depuis
// le même thread, ou verrouiller à l'extérieur.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace engine::core
{

class EventBus;

// Abonnement : se désabonne à la destruction (RAII) ou par reset().
class Subscription
{
  public:
    Subscription() = default;
    Subscription(Subscription &&other) noexcept { *this = std::move(other); }
    Subscription &operator=(Subscription &&other) noexcept
    {
        if (this != &other)
        {
            reset();
            mUnsubscribe = std::move(other.mUnsubscribe);
            other.mUnsubscribe = nullptr;
        }
        return *this;
    }
    Subscription(const Subscription &) = delete;
    Subscription &operator=(const Subscription &) = delete;
    ~Subscription() { reset(); }
    void reset()
    {
        if (mUnsubscribe)
        {
            mUnsubscribe();
            mUnsubscribe = nullptr;
        }
    }
    bool active() const { return static_cast<bool>(mUnsubscribe); }

  private:
    friend class EventBus;
    explicit Subscription(std::function<void()> f) : mUnsubscribe(std::move(f)) {}
    std::function<void()> mUnsubscribe;
};

class EventBus
{
  public:
    EventBus() : mAlive(std::make_shared<bool>(true)) {}
    ~EventBus() { *mAlive = false; }
    EventBus(const EventBus &) = delete;
    EventBus &operator=(const EventBus &) = delete;

    template <typename E> [[nodiscard]] Subscription subscribe(std::function<void(const E &)> handler)
    {
        Channel<E> &ch = channel<E>();
        const std::uint64_t id = ++mNextId;
        ch.slots.push_back({id, std::move(handler), true});
        return Subscription([this, alive = std::weak_ptr<bool>(mAlive), id] {
            auto a = alive.lock();
            if (a && *a)
                channel<E>().remove(id);
        });
    }

    // Appelle les abonnés dans l'ordre d'abonnement. Un abonné peut s'abonner ou se désabonner pendant l'appel :
    // les nouveaux ne reçoivent pas l'événement en cours ; un désabonné ne le reçoit plus.
    template <typename E> void publish(const E &event)
    {
        auto it = mChannels.find(std::type_index(typeid(E)));
        if (it == mChannels.end())
            return;
        auto &ch = static_cast<Channel<E> &>(*it->second);
        ++ch.depth;
        const std::size_t count = ch.slots.size();
        for (std::size_t i = 0; i < count; ++i)
            if (ch.slots[i].live)
                ch.slots[i].handler(event);
        if (--ch.depth == 0)
            ch.compact();
    }

    template <typename E> std::size_t subscriberCount() const
    {
        auto it = mChannels.find(std::type_index(typeid(E)));
        if (it == mChannels.end())
            return 0;
        std::size_t n = 0;
        for (const auto &s : static_cast<const Channel<E> &>(*it->second).slots)
            n += s.live ? 1 : 0;
        return n;
    }

  private:
    struct ChannelBase
    {
        virtual ~ChannelBase() = default;
    };
    template <typename E> struct Channel final : ChannelBase
    {
        struct Slot
        {
            std::uint64_t id;
            std::function<void(const E &)> handler;
            bool live;
        };
        std::vector<Slot> slots;
        int depth = 0;
        void remove(std::uint64_t id)
        {
            for (auto &s : slots)
                if (s.id == id)
                    s.live = false;
            if (depth == 0)
                compact();
        }
        void compact()
        {
            std::erase_if(slots, [](const Slot &s) { return !s.live; });
        }
    };

    template <typename E> Channel<E> &channel()
    {
        auto &p = mChannels[std::type_index(typeid(E))];
        if (!p)
            p = std::make_unique<Channel<E>>();
        return static_cast<Channel<E> &>(*p);
    }

    std::unordered_map<std::type_index, std::unique_ptr<ChannelBase>> mChannels;
    std::shared_ptr<bool> mAlive;
    std::uint64_t mNextId = 0;
};

} // namespace engine::core
