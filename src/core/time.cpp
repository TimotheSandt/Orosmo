#include <engine/core/time.hpp>

#include <algorithm>

namespace engine::core
{

Nanoseconds SteadyClock::now() const
{
    return std::chrono::duration_cast<Nanoseconds>(std::chrono::steady_clock::now().time_since_epoch());
}

FrameTimer::FrameTimer(const IClock &clock, Nanoseconds maxDelta) : mClock(&clock), mMaxDelta(maxDelta) {}

Nanoseconds FrameTimer::tick()
{
    const Nanoseconds now = mClock->now();
    if (!mStarted)
    {
        mStarted = true;
        mDelta = Nanoseconds{0};
    }
    else
    {
        mDelta = std::min(std::max(now - mLast, Nanoseconds{0}), mMaxDelta);
    }
    mLast = now;
    ++mFrame;
    return mDelta;
}

FixedStepper::FixedStepper(Nanoseconds step, int maxSteps)
    : mStep(step.count() > 0 ? step : Nanoseconds{1}), mMaxSteps(std::max(1, maxSteps))
{
}

int FixedStepper::advance(Nanoseconds frameDelta)
{
    mAccumulator += std::max(frameDelta, Nanoseconds{0});
    int steps = 0;
    while (mAccumulator >= mStep && steps < mMaxSteps)
    {
        mAccumulator -= mStep;
        ++steps;
    }
    if (mAccumulator >= mStep) // trop de retard : on abandonne le surplus
        mAccumulator = Nanoseconds{0};
    mTotalSteps += static_cast<std::uint64_t>(steps);
    return steps;
}

double FixedStepper::alpha() const
{
    return std::chrono::duration<double>(mAccumulator).count() / std::chrono::duration<double>(mStep).count();
}

} // namespace engine::core
