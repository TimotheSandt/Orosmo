#include <doctest/doctest.h>

#include <orosmo/core/time.hpp>

using namespace orosmo::core;
using std::chrono::milliseconds;

TEST_SUITE("core/time")
{
    TEST_CASE("FrameTimer: first tick is 0, then the exact delta, clamped to maxDelta")
    {
        ManualClock clock;
        FrameTimer timer(clock, milliseconds(100));
        clock.advance(milliseconds(5));
        CHECK(timer.tick() == Nanoseconds{0});
        clock.advance(milliseconds(16));
        CHECK(timer.tick() == milliseconds(16));
        CHECK(timer.deltaSeconds() == doctest::Approx(0.016));
        clock.advance(milliseconds(5000));
        CHECK(timer.tick() == milliseconds(100));
        CHECK(timer.frameIndex() == 3);
    }

    TEST_CASE("FixedStepper: same total simulation time whatever the frame deltas")
    {
        const Nanoseconds second = std::chrono::seconds(1);
        FixedStepper a(milliseconds(10));
        FixedStepper b(milliseconds(10));
        for (int i = 0; i < 100; ++i)
            a.advance(milliseconds(10)); // 100 frames de 10 ms
        for (int i = 0; i < 50; ++i)
            b.advance(milliseconds(20)); // 50 frames de 20 ms
        CHECK(a.stepCount() == 100);
        CHECK(b.stepCount() == 100);
        CHECK(a.simulationTime() == second);
        CHECK(b.simulationTime() == second);
    }

    TEST_CASE("FixedStepper: remainder carries over and alpha interpolates")
    {
        FixedStepper s(milliseconds(10));
        CHECK(s.advance(milliseconds(25)) == 2);
        CHECK(s.alpha() == doctest::Approx(0.5));
        CHECK(s.advance(milliseconds(5)) == 1);
        CHECK(s.alpha() == doctest::Approx(0.0));
    }

    TEST_CASE("FixedStepper: bounded catch-up, the surplus is dropped")
    {
        FixedStepper s(milliseconds(10), 4);
        CHECK(s.advance(std::chrono::seconds(10)) == 4);
        CHECK(s.alpha() == doctest::Approx(0.0));
        CHECK(s.advance(milliseconds(10)) == 1);
    }

    TEST_CASE("SteadyClock is monotone")
    {
        SteadyClock c;
        auto a = c.now();
        auto b = c.now();
        CHECK(b >= a);
    }
}
