// M1 bis : le Profiler (couche core) ne connaît plus le RHI. Il reçoit un fournisseur de minuteries GPU (IGpuTimerFactory)
// que le runtime graphique enregistre quand on lie un backend. Ces tests n'utilisent aucun GPU.
#include <doctest/doctest.h>

#include <orosmo/core/gpu_timer.hpp>
#include <orosmo/core/profiler.hpp>

#include <chrono>
#include <memory>

using namespace std::chrono_literals;
using namespace orosmo::core;

namespace
{
struct FakeTimer final : IGpuTimer
{
    int *begins;
    int *ends;
    FakeTimer(int *b, int *e) : begins(b), ends(e) {}
    void Begin() override { ++*begins; }
    void End() override { ++*ends; }
    bool IsReady() const override { return true; }
    std::chrono::nanoseconds GetElapsedTime() const override { return 5ms; }
};

struct FakeFactory final : IGpuTimerFactory
{
    mutable int begins = 0;
    mutable int ends = 0;
    mutable int created = 0;
    bool returnNull = false;
    std::unique_ptr<IGpuTimer> CreateTimer(const std::string &) const override
    {
        ++created;
        if (returnNull)
            return nullptr;
        return std::make_unique<FakeTimer>(&begins, &ends);
    }
};

// Remet le fournisseur précédent à la sortie du test (le runtime graphique en installe un à la liaison).
struct FactoryGuard
{
    const IGpuTimerFactory *previous = Profiler::GetGpuTimerFactory();
    ~FactoryGuard() { Profiler::SetGpuTimerFactory(previous); }
};
} // namespace

TEST_SUITE("ProfilerGpuTimer")
{
    TEST_CASE("ProfileGPU measures with the registered timer provider")
    {
        FactoryGuard guard;
        FakeFactory factory;
        Profiler::SetGpuTimerFactory(&factory);

        bool ran = false;
        Profiler::ProfileGPU("m1bis-fake-void", [&] { ran = true; });
        CHECK(ran);
        CHECK(factory.created == 1);
        CHECK(factory.begins == 1);
        CHECK(factory.ends == 1);
        CHECK(Profiler::GetLastTime("m1bis-fake-void") == 5ms); // le temps vient du minuteur, pas de l'horloge CPU

        const int value = Profiler::ProfileGPU("m1bis-fake-value", [](int a, int b) { return a + b; }, 20, 22);
        CHECK(value == 42);
        CHECK(factory.begins == 2);
        CHECK(Profiler::GetLastTime("m1bis-fake-value") == 5ms);
    }

    TEST_CASE("without a provider, or when it gives no timer, ProfileGPU falls back to the CPU clock")
    {
        FactoryGuard guard;

        Profiler::SetGpuTimerFactory(nullptr);
        int calls = 0;
        Profiler::ProfileGPU("m1bis-cpu-none", [&] { ++calls; });
        CHECK(calls == 1);
        CHECK(Profiler::ProfileGPU("m1bis-cpu-none-value", [](int a) { return a * 2; }, 21) == 42);

        FakeFactory factory;
        factory.returnNull = true;
        Profiler::SetGpuTimerFactory(&factory);
        Profiler::ProfileGPU("m1bis-cpu-null", [&] { ++calls; });
        CHECK(calls == 2);
        CHECK(factory.created == 1);
        CHECK(factory.begins == 0); // aucun minuteur GPU n'a été utilisé
    }
}
