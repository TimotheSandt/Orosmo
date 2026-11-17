#include <doctest/doctest.h>

#include <engine/core/memory.hpp>

#include <set>

using namespace engine::core;

namespace
{
struct Tracked
{
    static inline int alive = 0;
    int value;
    explicit Tracked(int v) : value(v) { ++alive; }
    ~Tracked() { --alive; }
};
struct alignas(32) Wide
{
    float f[8];
};
} // namespace

TEST_SUITE("core/memory")
{
    TEST_CASE("ArenaAllocator: aligned, bounded, reset reuses memory")
    {
        ArenaAllocator arena(1024);
        void *a = arena.allocate(3, 1);
        void *b = arena.allocate(8, 64);
        REQUIRE(a != nullptr);
        REQUIRE(b != nullptr);
        CHECK(reinterpret_cast<std::uintptr_t>(b) % 64 == 0);
        CHECK(arena.allocate(2000) == nullptr);
        CHECK(arena.used() > 0);
        const auto peak = arena.peak();
        arena.reset();
        CHECK(arena.used() == 0);
        CHECK(arena.peak() == peak);
        Wide *w = arena.create<Wide>();
        REQUIRE(w != nullptr);
        CHECK(reinterpret_cast<std::uintptr_t>(w) % 32 == 0);
    }

    TEST_CASE("ArenaAllocator: exact fit succeeds, one more byte fails")
    {
        ArenaAllocator arena(64);
        CHECK(arena.allocate(64, 1) != nullptr);
        CHECK(arena.allocate(1, 1) == nullptr);
    }

    TEST_CASE("ObjectPool: no leak, slots are recycled")
    {
        Tracked::alive = 0;
        {
            ObjectPool<Tracked> pool(4);
            std::set<Tracked *> seen;
            std::vector<Tracked *> objs;
            for (int i = 0; i < 10; ++i)
                objs.push_back(pool.create(i));
            CHECK(pool.liveCount() == 10);
            CHECK(Tracked::alive == 10);
            CHECK(pool.capacity() == 12);
            for (Tracked *t : objs)
            {
                seen.insert(t);
                pool.destroy(t);
            }
            CHECK(pool.liveCount() == 0);
            CHECK(Tracked::alive == 0);
            // les emplacements rendus sont réutilisés : pas de nouveau bloc
            for (int i = 0; i < 10; ++i)
                CHECK(seen.count(pool.create(i)) == 1);
            CHECK(pool.capacity() == 12);
            CHECK(pool.liveCount() == 10);
        }
    }

    TEST_CASE("ObjectPool: destroy(nullptr) is a no-op and values survive")
    {
        ObjectPool<Tracked> pool;
        pool.destroy(nullptr);
        Tracked *t = pool.create(7);
        CHECK(t->value == 7);
        pool.destroy(t);
        CHECK(pool.liveCount() == 0);
    }
}
