#include "orosmo/renderer/gpu_timer.hpp"

namespace orosmo::renderer
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

namespace
{
class ProfilerTimer final : public core::IGpuTimer
{
  public:
    ProfilerTimer(const ProfilerGpuTimerFactory &factory, rhi::Device &device, const std::string &name) : factory_(factory), timer_(device, name) {}
    void Begin() override
    {
        if (rhi::CommandEncoder *e = factory_.encoder(); e != nullptr)
        {
            timer_.begin(*e);
        }
    }
    void End() override
    {
        if (rhi::CommandEncoder *e = factory_.encoder(); e != nullptr)
        {
            timer_.end(*e);
        }
    }
    bool IsReady() const override { return timer_.isReady(); }
    std::chrono::nanoseconds GetElapsedTime() const override { return timer_.elapsed().value_or(std::chrono::nanoseconds(0)); }

  private:
    const ProfilerGpuTimerFactory &factory_;
    GpuTimer timer_;
};
} // namespace

std::unique_ptr<core::IGpuTimer> ProfilerGpuTimerFactory::CreateTimer(const std::string &name) const
{
    auto timer = std::make_unique<ProfilerTimer>(*this, device_, name);
    return timer;
}

} // namespace orosmo::renderer
