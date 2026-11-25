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
    ProfilerTimer(const ProfilerGpuTimerFactory &factory, std::unique_ptr<GpuTimer> timer) : factory_(factory), timer_(std::move(timer)) {}
    ~ProfilerTimer() override
    {
        // Réutilisable seulement si sa mesure est terminée : une minuterie abandonnée (délai, file pleine) peut encore être
        // écrite par le GPU, elle est détruite (destruction différée par le RHI).
        if (timer_->valid() && timer_->isReady())
        {
            factory_.recycle(std::move(timer_));
        }
    }
    ProfilerTimer(const ProfilerTimer &) = delete;
    ProfilerTimer &operator=(const ProfilerTimer &) = delete;

    void Begin() override
    {
        if (rhi::CommandEncoder *e = factory_.encoder(); e != nullptr)
        {
            timer_->begin(*e);
        }
    }
    void End() override
    {
        if (rhi::CommandEncoder *e = factory_.encoder(); e != nullptr)
        {
            timer_->end(*e);
        }
    }
    bool IsReady() const override { return timer_->isReady(); }
    std::chrono::nanoseconds GetElapsedTime() const override { return timer_->elapsed().value_or(std::chrono::nanoseconds(0)); }

  private:
    const ProfilerGpuTimerFactory &factory_;
    std::unique_ptr<GpuTimer> timer_;
};
} // namespace

std::unique_ptr<core::IGpuTimer> ProfilerGpuTimerFactory::CreateTimer(const std::string &name) const
{
    // Sans horodatages (capacité absente, règle 11), une minuterie GPU ne serait jamais prête : nul, et le Profiler mesure sur le CPU.
    if (!device_.capabilities().timestampQueries)
    {
        return nullptr;
    }
    std::unique_ptr<GpuTimer> timer = takeRecycled();
    if (timer == nullptr)
    {
        timer = std::make_unique<GpuTimer>(device_, name);
    }
    return std::make_unique<ProfilerTimer>(*this, std::move(timer));
}

std::unique_ptr<GpuTimer> ProfilerGpuTimerFactory::takeRecycled() const
{
    std::lock_guard<std::mutex> lock(recycledMutex_);
    if (recycled_.empty())
    {
        return nullptr;
    }
    std::unique_ptr<GpuTimer> timer = std::move(recycled_.back());
    recycled_.pop_back();
    timer->reset();
    return timer;
}

void ProfilerGpuTimerFactory::recycle(std::unique_ptr<GpuTimer> timer) const
{
    std::lock_guard<std::mutex> lock(recycledMutex_);
    if (recycled_.size() < kMaxRecycled)
    {
        recycled_.push_back(std::move(timer));
    }
}

std::size_t ProfilerGpuTimerFactory::recycledCount() const
{
    std::lock_guard<std::mutex> lock(recycledMutex_);
    return recycled_.size();
}

} // namespace orosmo::renderer
