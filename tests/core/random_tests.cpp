#include <doctest/doctest.h>

#include <orosmo/core/random.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

using orosmo::core::Random;

namespace
{
constexpr std::array<std::uint32_t, 1000> kSeed42 = {
#include "random_seed42_table.inc"
};
} // namespace

TEST_SUITE("core/random")
{
    TEST_CASE("Random: the first 1000 values of seed 42 match the frozen PCG32 table")
    {
        Random rng(42);
        std::size_t mismatches = 0;
        for (std::size_t i = 0; i < kSeed42.size(); ++i)
        {
            const std::uint32_t v = rng.nextU32();
            if (v != kSeed42[i] && mismatches++ < 5)
                FAIL_CHECK("index " << i << ": " << v << " != " << kSeed42[i]);
        }
        CHECK(mismatches == 0);
    }

    TEST_CASE("Random: constexpr, same seed same sequence, other seed or stream another one")
    {
        static_assert(Random(42).nextU32() == 0xa15c02b7u, "pcg32-demo, first value");
        Random a(7), b(7), c(8), d(7, 99);
        bool differsSeed = false, differsStream = false;
        for (int i = 0; i < 64; ++i)
        {
            const std::uint32_t va = a.nextU32();
            CHECK(va == b.nextU32());
            differsSeed |= va != c.nextU32();
            differsStream |= va != d.nextU32();
        }
        CHECK(differsSeed);
        CHECK(differsStream);
        CHECK(a == b);
    }

    TEST_CASE("Random: advance(n) equals n draws; state round-trips through fromState")
    {
        Random stepped(42), jumped(42);
        for (int i = 0; i < 777; ++i)
            stepped.nextU32();
        jumped.advance(777);
        CHECK(stepped == jumped);
        CHECK(jumped.nextU32() == kSeed42[777]);

        const Random restored = Random::fromState(stepped.state(), stepped.increment());
        CHECK(restored == stepped);
    }

    TEST_CASE("Random: bounded integers stay in range and hit every value; reals in [0, 1)")
    {
        Random rng(1234);
        std::array<int, 10> hits{};
        for (int i = 0; i < 10000; ++i)
        {
            const std::uint32_t v = rng.nextBelow(10);
            REQUIRE(v < 10u);
            ++hits[v];
        }
        for (int h : hits)
            CHECK(h > 800); // 1000 attendus par valeur
        CHECK(rng.nextBelow(0) == 0u);

        bool sawLow = false, sawHigh = false;
        for (int i = 0; i < 2000; ++i)
        {
            const std::int32_t v = rng.range(-3, 3);
            REQUIRE(v >= -3);
            REQUIRE(v <= 3);
            sawLow |= v == -3;
            sawHigh |= v == 3;
        }
        CHECK(sawLow);
        CHECK(sawHigh);
        CHECK(rng.range(5, 5) == 5);
        CHECK(rng.range(3, -3) >= -3); // bornes échangées

        for (int i = 0; i < 2000; ++i)
        {
            const float f = rng.nextFloat();
            const double d = rng.nextDouble();
            REQUIRE(f >= 0.0f);
            REQUIRE(f < 1.0f);
            REQUIRE(d >= 0.0);
            REQUIRE(d < 1.0);
            const float r = rng.range(2.0f, 4.0f);
            REQUIRE(r >= 2.0f);
            REQUIRE(r < 4.0f);
        }
    }

    TEST_CASE("Random: derived values are exact functions of the frozen integers (same on every platform)")
    {
        Random rng(42);
        CHECK(rng.nextFloat() == static_cast<float>(kSeed42[0] >> 8) * 0x1.0p-24f);
        const std::uint64_t u = (std::uint64_t{kSeed42[1]} << 32) | kSeed42[2];
        CHECK(rng.nextDouble() == static_cast<double>(u >> 11) * 0x1.0p-53);
        CHECK(rng.nextU64() == ((std::uint64_t{kSeed42[3]} << 32) | kSeed42[4]));
    }
}
