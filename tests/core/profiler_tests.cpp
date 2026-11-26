#include "support/test_support.hpp"

#include <orosmo/core/fps_counter.hpp>
#include <orosmo/core/profiler.hpp>
#include <orosmo/core/ring_buffer.hpp>

#include <algorithm>
#include <chrono>
#include <thread>

using namespace orosmo::core;

namespace tests
{

TEST_SUITE("Profiler")
{
    TEST_CASE("ring buffer enforces minimum capacity")
    {
        RingBuffer<int> values(1);
        Assert(values.GetCapacity() >= 10u, "RingBuffer should clamp to the minimum capacity");
    }

    TEST_CASE("ring buffer returns newest values first")
    {
        RingBuffer<int> values(10);
        values.Push(10);
        values.Push(20);
        values.Push(30);
        AssertEqual(values.Get(0), 30, "Newest value should be returned first");
        AssertEqual(values.Get(2), 10, "Oldest stored value should be returned last");
        AssertEqual(values.GetAverage(), 20, "Average should be computed over stored values");
    }

    TEST_CASE("ring buffer resize keeps recent values")
    {
        RingBuffer<int> values(12);
        for (int i = 1; i <= 11; ++i)
        {
            values.Push(i);
        }
        values.Resize(10);
        AssertEqual(values.GetCapacity(), static_cast<size_t>(10),
                    "Resize should update the capacity when staying above the minimum");
        AssertEqual(values.GetSize(), static_cast<size_t>(10), "Resize should keep only the most recent values when shrinking");
        AssertEqual(values.Get(0), 11, "Newest value should be preserved after resize");
        AssertEqual(values.Get(9), 2, "Oldest preserved value should remain accessible after resize");
    }

    TEST_CASE("ring buffer clear resets the content")
    {
        RingBuffer<int> values(10);
        values.Push(1);
        values.Push(2);
        values.Clear();
        AssertEqual(values.GetSize(), static_cast<size_t>(0), "Clear should reset the stored size");
        AssertEqual(values.Get(0), 0, "Reading from an empty buffer should return the default value");
    }

    TEST_CASE("ring buffer move transfers ownership")
    {
        RingBuffer<int> values(10);
        values.Push(7);
        values.Push(9);
        RingBuffer<int> moved(std::move(values));
        AssertEqual(moved.GetSize(), static_cast<size_t>(2), "Moved buffer should keep its values");
        AssertEqual(moved.Get(0), 9, "Moved buffer should keep the newest value");
        AssertEqual(moved.Get(1), 7, "Moved buffer should keep the older value");
    }

    TEST_CASE("fps counter starts at zero")
    {
        FPSCounter fpsCounter;
        AssertEqual(fpsCounter.getFrame(), 0, "FPS counter should start with frame zero");
        AssertNear(fpsCounter.getFPS(), 0.0, 1e-9, "FPS counter should start with zero FPS");
    }

    TEST_CASE("fps counter newFrame increments frame")
    {
        FPSCounter fpsCounter;
        fpsCounter.setFrameRateLimitingMode(0);
        fpsCounter.newFrame(0);
        AssertEqual(fpsCounter.getFrame(), 1, "newFrame should increment the frame count");
        Assert(fpsCounter.getFPS() >= 0.0, "FPS should stay non-negative");
        Assert(fpsCounter.getElapseTimeInNanoseconds() >= 0.0, "Elapsed time should stay non-negative");
    }

    TEST_CASE("fps counter statistics stay ordered")
    {
        FPSCounter fpsCounter;
        fpsCounter.setFrameRateLimitingMode(0);
        for (int i = 0; i < 5; ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            fpsCounter.newFrame(0);
        }
        fpsCounter.updateStat();
        Assert(fpsCounter.getMaxFPS() >= fpsCounter.getMinFPS(), "Max FPS should stay above min FPS");
        Assert(fpsCounter.getAverageFPS() >= fpsCounter.getMinFPS(), "Average FPS should stay above min FPS");
        Assert(fpsCounter.getAverageFPS() <= fpsCounter.getMaxFPS(), "Average FPS should stay below max FPS");
    }

    TEST_CASE("Names lists the known measures sorted")
    {
        orosmo::core::Profiler::Profile("zz.names.b", [] {});
        orosmo::core::Profiler::Profile("zz.names.a", [] {});
        const auto names = orosmo::core::Profiler::Names();
        const auto a = std::find(names.begin(), names.end(), "zz.names.a");
        const auto b = std::find(names.begin(), names.end(), "zz.names.b");
        REQUIRE(a != names.end());
        REQUIRE(b != names.end());
        CHECK(a < b);
        CHECK(std::is_sorted(names.begin(), names.end()));
    }
}

} // namespace tests
