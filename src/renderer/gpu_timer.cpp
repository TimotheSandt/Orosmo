#include "engine/renderer/gpu_timer.hpp"

namespace engine::renderer
{

GpuTimer::GpuTimer(rhi::Device &device, const std::string &name) : device_(device)
{
    if (!device.capabilities().timestampQueries)
    {
        return;
    }
    if (auto pool = device.createQueryPool(2, name.c_str()))
    {
        pool_ = *pool;
    }
}

GpuTimer::~GpuTimer() { device_.destroy(pool_); }

void GpuTimer::begin(rhi::CommandEncoder &encoder)
{
    if (valid())
    {
        encoder.writeTimestamp(pool_, 0);
        begun_ = true;
        ended_ = false;
    }
}

void GpuTimer::end(rhi::CommandEncoder &encoder)
{
    if (valid() && begun_)
    {
        encoder.writeTimestamp(pool_, 1);
        ended_ = true;
    }
}

bool GpuTimer::isReady() const
{
    if (!valid() || !begun_ || !ended_)
    {
        return false;
    }
    auto ready = device_.timestampsReady(pool_, 0, 2);
    return ready && *ready;
}

std::optional<std::chrono::nanoseconds> GpuTimer::elapsed() const
{
    if (!isReady())
    {
        return std::nullopt;
    }
    auto t = device_.readTimestamps(pool_, 0, 2);
    if (!t || (*t)[1] < (*t)[0])
    {
        return std::nullopt;
    }
    return std::chrono::nanoseconds(static_cast<std::chrono::nanoseconds::rep>((*t)[1] - (*t)[0]));
}

} // namespace engine::renderer
