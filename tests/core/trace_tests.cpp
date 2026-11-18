#include <doctest/doctest.h>

#include <engine/core/tasks.hpp>
#include <engine/core/trace.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace engine::core;
using std::chrono::microseconds;
using std::chrono::milliseconds;

TEST_SUITE("core/trace")
{
    TEST_CASE("ScopedTrace measures exactly the clock advance (deterministic clock)")
    {
        ManualClock clock;
        TraceRecorder rec(clock);
        {
            ENGINE_TRACE_SCOPE(rec, "frame");
            clock.advance(milliseconds(4));
            {
                ENGINE_TRACE_SCOPE(rec, "update");
                clock.advance(milliseconds(1));
            }
        }
        auto samples = rec.samples();
        REQUIRE(samples.size() == 2);
        CHECK(samples[0].name == "update"); // la portée interne se termine d'abord
        CHECK(samples[0].duration == milliseconds(1));
        CHECK(samples[1].name == "frame");
        CHECK(samples[1].duration == milliseconds(5));
        CHECK(samples[1].start == Nanoseconds{0});
    }

    TEST_CASE("stats aggregate by name, sorted by total time")
    {
        ManualClock clock;
        TraceRecorder rec(clock);
        for (int i = 1; i <= 3; ++i)
            rec.addSample("physics", Nanoseconds{0}, milliseconds(i));
        rec.addSample("draw", Nanoseconds{0}, milliseconds(10));
        auto stats = rec.stats();
        REQUIRE(stats.size() == 2);
        CHECK(stats[0].name == "draw");
        CHECK(stats[1].name == "physics");
        CHECK(stats[1].count == 3);
        CHECK(stats[1].total == milliseconds(6));
        CHECK(stats[1].min == milliseconds(1));
        CHECK(stats[1].max == milliseconds(3));
        CHECK(stats[1].averageMs() == doctest::Approx(2.0));
    }

    TEST_CASE("disabled recorder records nothing; the cap drops new samples and counts them")
    {
        ManualClock clock;
        TraceRecorder rec(clock, 2);
        rec.setEnabled(false);
        { ENGINE_TRACE_SCOPE(rec, "off"); }
        CHECK(rec.samples().empty());
        rec.setEnabled(true);
        for (int i = 0; i < 5; ++i)
            rec.addSample("x", Nanoseconds{0}, microseconds(1));
        CHECK(rec.samples().size() == 2);
        CHECK(rec.dropped() == 3);
        rec.clear();
        CHECK(rec.dropped() == 0);
    }

    TEST_CASE("Chrome trace export is valid JSON with CPU and GPU tracks")
    {
        ManualClock clock;
        TraceRecorder rec(clock);
        rec.addSample("cpu-work", microseconds(10), microseconds(250));
        rec.addSample("gpu-pass", microseconds(100), microseconds(900), TraceTrack::kGpu);
        const auto path = std::filesystem::temp_directory_path() / "engine_trace_test.json";
        REQUIRE(rec.writeChromeTrace(path).has_value());
        std::stringstream ss;
        {
            std::ifstream in(path); // closed before remove(): Windows refuses to delete an open file
            ss << in.rdbuf();
        }
        auto j = nlohmann::json::parse(ss.str(), nullptr, false);
        REQUIRE_FALSE(j.is_discarded());
        const auto &ev = j["traceEvents"];
        REQUIRE(ev.size() == 2);
        CHECK(ev[0]["ph"] == "X");
        CHECK(ev[0]["name"] == "cpu-work");
        CHECK(ev[0]["dur"].get<double>() == doctest::Approx(250.0));
        CHECK(ev[0]["pid"] == 1);
        CHECK(ev[1]["pid"] == 2);
        CHECK(ev[1]["cat"] == "gpu");
        std::filesystem::remove(path);
        CHECK_FALSE(rec.writeChromeTrace("/nonexistent-dir-xyz/t.json").has_value());
    }

    TEST_CASE("thread safe: scopes from many tasks, distinct thread numbers")
    {
        SteadyClock clock;
        TraceRecorder rec(clock);
        {
            TaskSystem tasks(4);
            tasks.parallelFor(2000, 10, [&](std::size_t) { ENGINE_TRACE_SCOPE(rec, "work"); });
        }
        CHECK(rec.samples().size() == 2000);
        CHECK(rec.stats().front().count == 2000);
        for (const auto &s : rec.samples())
            CHECK(s.threadId >= 1);
    }
}
