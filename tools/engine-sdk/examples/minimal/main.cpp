// Projet minimal (docs/USING_THE_ENGINE.md) : tâches parallèles et réflexion/sérialisation de `orosmo::core`, sans fenêtre ni GPU.
#include <orosmo/core/reflection.hpp>
#include <orosmo/core/serialization.hpp>
#include <orosmo/core/tasks.hpp>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>

struct AppSummary
{
    std::string name = "minimal";
    std::uint32_t tasksCompleted = 0;
    std::uint64_t sum = 0;
};

template <> struct orosmo::core::Reflect<AppSummary>
{
    static constexpr int kVersion = 1;
    static constexpr auto fields()
    {
        return std::make_tuple(field("name", &AppSummary::name), field("tasksCompleted", &AppSummary::tasksCompleted),
                               field("sum", &AppSummary::sum));
    }
};

int main()
{
    constexpr std::size_t kCount = 1024;
    orosmo::core::TaskSystem tasks;
    std::atomic<std::uint64_t> total{0};
    tasks.parallelFor(kCount, 64, [&](std::size_t i) { total.fetch_add(static_cast<std::uint64_t>(i + 1), std::memory_order_relaxed); });

    AppSummary summary;
    summary.tasksCompleted = static_cast<std::uint32_t>(kCount);
    summary.sum = total.load(std::memory_order_relaxed);

    const std::string json = orosmo::core::dumpJson(orosmo::core::saveDocument(summary), /*indent=*/-1);
    std::printf("%s\n", json.c_str());
    return 0;
}
