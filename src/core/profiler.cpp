#include "Profiler.h"

#include <atomic>

namespace
{
// Fournisseur de minuteries GPU installé par le runtime graphique (voir Profiler/GpuTimer.h).
std::atomic<const IGpuTimerFactory *> gpuTimerFactory{nullptr};
} // namespace

void Profiler::SetGpuTimerFactory(const IGpuTimerFactory *factory) noexcept { gpuTimerFactory.store(factory); }

const IGpuTimerFactory *Profiler::GetGpuTimerFactory() noexcept { return gpuTimerFactory.load(); }

void Profiler::ClearGpuQueries()
{
    std::lock_guard<std::mutex> lock(getQueryMutex());
    for (auto &[name, data] : getQueryData())
    {
        data.queries = {};
    }
}
