#include <doctest/doctest.h>

#include <orosmo/core/spsc_queue.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <thread>

using orosmo::core::SpscQueue;

TEST_SUITE("core/spsc_queue")
{
    TEST_CASE("SpscQueue: capacity rounds up to a power of two; full and empty are reported")
    {
        SpscQueue<int> q(5);
        CHECK(q.capacity() == 8u);
        CHECK(q.emptyApprox());
        CHECK(q.front() == nullptr);
        int out = -1;
        CHECK_FALSE(q.tryPop(out));
        for (int i = 0; i < 8; ++i)
            CHECK(q.tryPush(i));
        CHECK_FALSE(q.tryPush(99)); // pleine
        CHECK(q.sizeApprox() == 8u);
        REQUIRE(q.front() != nullptr);
        CHECK(*q.front() == 0);
        q.pop();
        CHECK(q.tryPush(8)); // une place libérée, l'indice repasse le début du tableau
        for (int i = 1; i <= 8; ++i)
        {
            REQUIRE(q.tryPop(out));
            CHECK(out == i);
        }
        CHECK_FALSE(q.tryPop().has_value());
        CHECK(SpscQueue<int>(0).capacity() == 2u);
    }

    TEST_CASE("SpscQueue: move-only and non-trivial elements; the remaining ones are destroyed with the queue")
    {
        auto alive = std::make_shared<int>(0);
        {
            SpscQueue<std::shared_ptr<int>> q(4);
            CHECK(q.emplace(alive));
            CHECK(q.emplace(alive));
            CHECK(alive.use_count() == 3);
            auto first = q.tryPop();
            REQUIRE(first.has_value());
            CHECK(*first == alive);
        }
        CHECK(alive.use_count() == 1); // le second élément, resté dans la file, a été détruit

        SpscQueue<std::unique_ptr<std::string>> q(2);
        CHECK(q.tryPush(std::make_unique<std::string>("a")));
        std::unique_ptr<std::string> out;
        REQUIRE(q.tryPop(out));
        CHECK(*out == "a");
    }

    TEST_CASE("SpscQueue: 1e6 elements between two threads keep their order (run under TSan in CI)")
    {
        constexpr std::uint64_t kCount = 1'000'000;
        SpscQueue<std::uint64_t> q(1024);
        std::thread producer(
            [&q]
            {
                for (std::uint64_t i = 0; i < kCount;)
                {
                    if (q.tryPush(i))
                        ++i;
                    else
                        std::this_thread::yield();
                }
            });

        std::uint64_t expected = 0;
        std::uint64_t outOfOrder = 0;
        std::uint64_t sum = 0;
        while (expected < kCount)
        {
            std::uint64_t value = 0;
            if (!q.tryPop(value))
            {
                std::this_thread::yield();
                continue;
            }
            outOfOrder += value != expected ? 1u : 0u;
            sum += value;
            ++expected;
        }
        producer.join();

        CHECK(outOfOrder == 0u);
        CHECK(sum == kCount * (kCount - 1u) / 2u);
        CHECK(q.emptyApprox());
    }
}
