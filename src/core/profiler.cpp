#include "orosmo/core/profiler.hpp"

#include <atomic>

namespace orosmo::core
{

namespace
{
// Fournisseur de minuteries GPU installé par le runtime graphique (voir Profiler/GpuTimer.h).
std::atomic<const IGpuTimerFactory *> gpuTimerFactory{nullptr};
std::atomic<bool> gpuTimingsEnabled{true};
} // namespace

void Profiler::SetGpuTimerFactory(const IGpuTimerFactory *factory) noexcept { gpuTimerFactory.store(factory); }

const IGpuTimerFactory *Profiler::GetGpuTimerFactory() noexcept { return gpuTimerFactory.load(); }

void Profiler::SetGpuTimingsEnabled(bool enabled) noexcept { gpuTimingsEnabled.store(enabled); }

bool Profiler::GpuTimingsEnabled() noexcept { return gpuTimingsEnabled.load(); }

void Profiler::ClearGpuQueries()
{
    std::lock_guard<std::mutex> lock(getQueryMutex());
    for (auto &[name, data] : getQueryData())
    {
        data.queries = {};
    }
}

} // namespace orosmo::core
