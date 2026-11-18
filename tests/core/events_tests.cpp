#include <doctest/doctest.h>

#include <engine/core/events.hpp>

#include <atomic>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

// Compte les allocations (toute l'exécutable de test) pour prouver que publish() n'alloue pas.
namespace
{
std::atomic<long> gAllocations{0};
}
void *operator new(std::size_t n)
{
    gAllocations.fetch_add(1, std::memory_order_relaxed);
    if (void *p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }

using namespace engine::core;

namespace
{
struct Damage
{
    int amount;
};
struct Message
{
    std::string text;
};
} // namespace

TEST_SUITE("core/events")
{
    TEST_CASE("typed delivery in subscription order")
    {
        EventBus bus;
        std::vector<int> log;
        auto s1 = bus.subscribe<Damage>([&](const Damage &d) { log.push_back(d.amount); });
        auto s2 = bus.subscribe<Damage>([&](const Damage &d) { log.push_back(d.amount * 10); });
        int messages = 0;
        auto s3 = bus.subscribe<Message>([&](const Message &) { ++messages; });
        bus.publish(Damage{3});
        CHECK(log == std::vector<int>{3, 30});
        CHECK(messages == 0);
        bus.publish(Message{"hi"});
        CHECK(messages == 1);
        bus.publish(Damage{1});
        CHECK(log.size() == 4);
    }

    TEST_CASE("RAII unsubscribe, and the bus may die before its subscriptions")
    {
        int n = 0;
        Subscription outlives;
        {
            EventBus bus;
            {
                auto s = bus.subscribe<Damage>([&](const Damage &) { ++n; });
                bus.publish(Damage{1});
                CHECK(bus.subscriberCount<Damage>() == 1);
            }
            CHECK(bus.subscriberCount<Damage>() == 0);
            bus.publish(Damage{1});
            CHECK(n == 1);
            outlives = bus.subscribe<Damage>([&](const Damage &) { ++n; });
        }
        outlives.reset(); // le bus n'existe plus : ne doit rien toucher
        CHECK(n == 1);
    }

    TEST_CASE("subscribing or unsubscribing during a publish is safe")
    {
        EventBus bus;
        int a = 0, late = 0;
        Subscription lateSub, self;
        self = bus.subscribe<Damage>([&](const Damage &) {
            ++a;
            self.reset();
            lateSub = bus.subscribe<Damage>([&](const Damage &) { ++late; });
        });
        bus.publish(Damage{1});
        CHECK(a == 1);
        CHECK(late == 0); // le nouvel abonné ne reçoit pas l'événement en cours
        bus.publish(Damage{1});
        CHECK(a == 1); // désabonné
        CHECK(late == 1);
    }

    TEST_CASE("publish does not allocate")
    {
        EventBus bus;
        long sum = 0;
        auto s = bus.subscribe<Damage>([&sum](const Damage &d) { sum += d.amount; });
        bus.publish(Damage{1}); // échauffement
        const long before = gAllocations.load();
        for (int i = 0; i < 1000; ++i)
            bus.publish(Damage{2});
        CHECK(gAllocations.load() == before);
        CHECK(sum == 2001);
    }

    TEST_CASE("a handler may subscribe many others without invalidating its own captures")
    {
        struct Ctx
        {
            EventBus bus;
            std::vector<Subscription> subs;
            int seen = 0;
        } ctx;
        Ctx *c = &ctx;
        const int probe = 42;
        // 16 octets, trivialement copiable : la capture est stockée dans le std::function lui-même, donc dans le
        // tableau des abonnés. Si ce tableau se réalloue pendant l'appel, elle serait déplacée sous les pieds du handler.
        auto first = ctx.bus.subscribe<Damage>([c, probe](const Damage &) {
            for (int i = 0; i < 200; ++i)
                c->subs.push_back(c->bus.subscribe<Damage>([](const Damage &) {}));
            c->seen = probe; // lu après les abonnements : doit toujours valoir 42
        });
        ctx.bus.publish(Damage{1});
        CHECK(ctx.seen == 42);
        CHECK(ctx.bus.subscriberCount<Damage>() == 201);
    }
}
