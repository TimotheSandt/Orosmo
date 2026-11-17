#include <doctest/doctest.h>

#include <engine/core/tasks.hpp>

#include <atomic>
#include <chrono>
#include <numeric>
#include <stdexcept>
#include <vector>

using namespace engine::core;

TEST_SUITE("core/tasks")
{
    TEST_CASE("load test: 10 000 tasks all run exactly once")
    {
        TaskSystem sys(4);
        std::atomic<int> counter{0};
        std::atomic<long> sum{0};
        for (int i = 0; i < 10000; ++i)
            sys.submit([&, i] {
                counter.fetch_add(1);
                sum.fetch_add(i);
            });
        sys.waitIdle();
        CHECK(counter.load() == 10000);
        CHECK(sum.load() == 10000L * 9999 / 2);
        CHECK(sys.pendingTasks() == 0);
    }

    TEST_CASE("dependencies: a task runs after all of its dependencies")
    {
        TaskSystem sys(4);
        std::atomic<int> stage{0};
        std::atomic<bool> orderOk{true};
        TaskHandle a = sys.submit([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            stage.fetch_add(1);
        });
        TaskHandle b = sys.submit([&] { stage.fetch_add(1); });
        const TaskHandle deps[] = {a, b};
        TaskHandle c = sys.submit([&] { orderOk = (stage.load() == 2); stage.fetch_add(1); }, deps);
        sys.wait(c);
        CHECK(orderOk.load());
        CHECK(stage.load() == 3);
        CHECK(c.done());
    }

    TEST_CASE("a dependency that is already finished does not block")
    {
        TaskSystem sys(2);
        TaskHandle a = sys.submit([] {});
        sys.wait(a);
        std::atomic<bool> ran{false};
        const TaskHandle deps[] = {a, TaskHandle{}};
        sys.wait(sys.submit([&] { ran = true; }, deps));
        CHECK(ran.load());
    }

    TEST_CASE("TaskGroup waits for every task")
    {
        TaskSystem sys(3);
        std::atomic<int> n{0};
        {
            TaskGroup g(sys);
            for (int i = 0; i < 100; ++i)
                g.run([&] { n.fetch_add(1); });
            g.wait();
            CHECK(n.load() == 100);
        }
    }

    TEST_CASE("waiting inside a task does not deadlock, even with a single thread")
    {
        TaskSystem sys(1);
        std::atomic<int> result{0};
        TaskHandle outer = sys.submit([&] {
            TaskHandle inner = sys.submit([&] { result = 42; });
            sys.wait(inner);
        });
        sys.wait(outer);
        CHECK(result.load() == 42);
    }

    TEST_CASE("parallelFor visits every index once")
    {
        TaskSystem sys(4);
        std::vector<std::atomic<int>> hits(1000);
        sys.parallelFor(hits.size(), 17, [&](std::size_t i) { hits[i].fetch_add(1); });
        bool all = true;
        for (auto &h : hits)
            all = all && h.load() == 1;
        CHECK(all);
        sys.parallelFor(0, 1, [](std::size_t) { FAIL("never called"); });
    }

    TEST_CASE("clean shutdown: the destructor finishes the submitted tasks")
    {
        std::atomic<int> n{0};
        {
            TaskSystem sys(2);
            for (int i = 0; i < 500; ++i)
                sys.submit([&] {
                    std::this_thread::yield();
                    n.fetch_add(1);
                });
        }
        CHECK(n.load() == 500);
    }

    TEST_CASE("a throwing task is contained and its dependents still run")
    {
        TaskSystem sys(2);
        std::atomic<bool> ran{false};
        TaskHandle bad = sys.submit([] { throw std::runtime_error("boom"); });
        const TaskHandle deps[] = {bad};
        sys.wait(sys.submit([&] { ran = true; }, deps));
        CHECK(ran.load());
    }
}
