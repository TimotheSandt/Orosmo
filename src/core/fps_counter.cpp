#include "engine/core/fps_counter.hpp"
#include <algorithm>

#include <engine/core/system.hpp>

namespace engine::core
{

FPSCounter::FPSCounter()
    : lastTime(std::chrono::high_resolution_clock::now()), frameStartTime(std::chrono::high_resolution_clock::now()),
      nextFrameTime(std::chrono::high_resolution_clock::now())
{
    this->fpsBuffer.Init(this->BufferSize);
    this->elapseTimeBuffer.Init(this->BufferSize);
    this->initializePlatformTimer();
}

FPSCounter::~FPSCounter() noexcept { this->cleanupPlatformTimer(); }

FPSCounter::FPSCounter(FPSCounter &&other) noexcept { this->Swap(other); }

FPSCounter &FPSCounter::operator=(FPSCounter &&other) noexcept
{
    if (this != &other)
    {
        this->Destroy();
        this->Swap(other);
    }
    return *this;
}

void FPSCounter::Swap(FPSCounter &other) noexcept
{
    double tempFPS = this->fps.exchange(other.fps.load(std::memory_order_relaxed));
    other.fps.exchange(tempFPS, std::memory_order_relaxed);
    int tempFrame = this->frame.exchange(other.frame.load(std::memory_order_relaxed));
    other.frame.exchange(tempFrame, std::memory_order_relaxed);
    std::swap(this->elapseTime, other.elapseTime);
    std::swap(this->lastTime, other.lastTime);
    std::swap(this->frameStartTime, other.frameStartTime);
    std::swap(this->frameRateLimitingMode, other.frameRateLimitingMode);
    std::swap(this->spinThreshold, other.spinThreshold);
    std::swap(this->targetFrameTime, other.targetFrameTime);
    std::swap(this->nextFrameTime, other.nextFrameTime);
    std::swap(this->sleepOffset, other.sleepOffset);
    std::swap(this->adaptiveCounter, other.adaptiveCounter);
    std::swap(this->sleepAccuracy, other.sleepAccuracy);
    int64_t tempFramesDropped = this->framesDropped.exchange(other.framesDropped.load(std::memory_order_relaxed));
    other.framesDropped.exchange(tempFramesDropped, std::memory_order_relaxed);
    std::swap(this->avgFps, other.avgFps);
    std::swap(this->maxFps, other.maxFps);
    std::swap(this->minFps, other.minFps);
    std::swap(this->avgElapseTimens, other.avgElapseTimens);
    std::swap(this->maxElapseTimens, other.maxElapseTimens);
    std::swap(this->minElapseTimens, other.minElapseTimens);
    std::swap(this->fpsBuffer, other.fpsBuffer);
    std::swap(this->elapseTimeBuffer, other.elapseTimeBuffer);
}

void FPSCounter::Destroy() noexcept { this->cleanupPlatformTimer(); }

void FPSCounter::newFrame(unsigned int maxFPS)
{
    this->frame.fetch_add(1, std::memory_order_relaxed);
    this->frameStartTime = this->getTime();

    // Calculate elapsed time from previous frame
    this->elapseTime = this->frameStartTime - this->lastTime;

    // Frame rate limiting
    if (maxFPS > 0)
    {
        this->targetFrameTime = std::chrono::nanoseconds(1000000000ULL / maxFPS);

        switch (this->frameRateLimitingMode)
        {
            case 0: // No limiting
                break;
            case 1: // Hybrid sleep (default)
            {
                auto sleepTime = this->targetFrameTime - this->elapseTime;
                if (sleepTime.count() > 0)
                {
                    this->hybridSleep(sleepTime);
                }
            }
            break;
            case 2: // Adaptive sleep
            {
                auto sleepTime = this->targetFrameTime - this->elapseTime;
                if (sleepTime.count() > 0)
                {
                    this->adaptiveSleep(sleepTime);
                }
            }
            break;
            case 3: // VSync-like pacing
                this->vsyncLikePacing(maxFPS);
                break;
            default:
                // Fallback to hybrid
                {
                    auto sleepTime = this->targetFrameTime - this->elapseTime;
                    if (sleepTime.count() > 0)
                    {
                        this->hybridSleep(sleepTime);
                    }
                }
                break;
        }
    }

    // Calculate final timing metrics
    auto currentTime = this->getTime();
    auto totalElapsed = currentTime - this->lastTime;

    // Calculate FPS with safety check
    double newFps = (totalElapsed.count() > 0) ? 1.0e9 / static_cast<double>(totalElapsed.count()) : 0.0;
    this->fps.store(newFps, std::memory_order_relaxed);

    // Check for dropped frames
    if (maxFPS > 0 && totalElapsed > this->targetFrameTime * 1.5)
    {
        this->framesDropped.fetch_add(1, std::memory_order_relaxed);
    }

    // Update timing
    // this->elapseTime = totalElapsed;
    this->lastTime = currentTime;

    // Update statistics buffers
    this->updateBuffers();
}

void FPSCounter::hybridSleep(std::chrono::nanoseconds sleepTime)
{
    // Most accurate method: combines sleep_for with spin-wait

    if (sleepTime <= this->spinThreshold)
    {
        // For very short waits, just spin-wait with yields
        auto targetTime = this->getTime() + sleepTime;
        while (this->getTime() < targetTime)
        {
            std::this_thread::yield();
        }
        return;
    }

    // For longer waits, sleep for most of the time
    auto sleepDuration = sleepTime - this->spinThreshold;

    // Use platform-optimized sleep if available
    this->platformOptimizedSleep(sleepDuration);

    // Spin-wait for the remaining time for high precision
    auto targetTime = this->getTime() + this->spinThreshold;
    while (this->getTime() < targetTime)
    {
        std::this_thread::yield();
    }
}

void FPSCounter::adaptiveSleep(std::chrono::nanoseconds sleepTime)
{
    // Self-learning sleep that adapts to system sleep overhead

    auto requestedSleep = sleepTime - this->sleepOffset;

    if (requestedSleep.count() > 0)
    {
        auto sleepStart = this->getTime();

        if (requestedSleep <= this->spinThreshold)
        {
            // Spin-wait for short durations
            auto targetTime = sleepStart + requestedSleep;
            while (this->getTime() < targetTime)
            {
                std::this_thread::yield();
            }
        }
        else
        {
            // Use sleep_for for longer durations
            this->platformOptimizedSleep(requestedSleep - this->spinThreshold / 2);
        }

        auto sleepEnd = this->getTime();
        auto actualSleepTime = sleepEnd - sleepStart;
        auto sleepError = actualSleepTime - requestedSleep;

        // Update sleep offset with exponential moving average
        this->adaptiveCounter++;
        if (this->adaptiveCounter >= 10)
        {
            this->sleepOffset = std::chrono::duration_cast<std::chrono::nanoseconds>(this->sleepOffset * 0.9 + sleepError * 0.1);
            this->sleepAccuracy = static_cast<double>(std::abs(sleepError.count())) / static_cast<double>(sleepTime.count());
            this->adaptiveCounter = 0;
        }
    }

    // Fine-tune with spin-wait
    auto remainingTime = sleepTime - (this->getTime() - this->frameStartTime + this->elapseTime);
    if (remainingTime.count() > 0 && remainingTime < std::chrono::microseconds(200))
    {
        auto targetTime = this->getTime() + remainingTime;
        while (this->getTime() < targetTime)
        {
            std::this_thread::yield();
        }
    }
}

void FPSCounter::vsyncLikePacing(unsigned int maxFPS)
{
    // Maintains steady frame intervals, handles dropped frames gracefully

    auto frameInterval = std::chrono::nanoseconds(1000000000ULL / maxFPS);
    this->nextFrameTime += frameInterval;

    auto currentTime = this->getTime();
    auto waitTime = this->nextFrameTime - currentTime;

    if (waitTime.count() > 0)
    {
        // We're ahead of schedule, wait
        this->hybridSleep(waitTime);
    }
    else if (waitTime.count() < -frameInterval.count() * 2)
    {
        // We're running way behind (more than 2 frames), reset timing
        this->nextFrameTime = currentTime + frameInterval;
        this->framesDropped.fetch_add(1, std::memory_order_relaxed);
    }
    // If we're slightly behind (1-2 frames), just continue without reset
}

void FPSCounter::platformOptimizedSleep(std::chrono::nanoseconds sleepTime)
{
    // Attente précise propre à chaque système : src/platform/system.cpp
    engine::core::system::preciseSleep(sleepTime);
}

void FPSCounter::initializePlatformTimer()
{
    // Windows : résolution du minuteur à 1 ms pour une attente plus précise (src/platform/system.cpp) ; sans effet ailleurs.
    if (engine::core::system::beginHighResolutionTimer())
    {
        this->timerInitialized = true;
    }
}

void FPSCounter::cleanupPlatformTimer()
{
    if (this->timerInitialized)
    {
        engine::core::system::endHighResolutionTimer();
        this->timerInitialized = false;
    }
}

void FPSCounter::updateBuffers()
{
    double currentFps = this->fps.load(std::memory_order_relaxed);
    this->fpsBuffer.Push(currentFps);
    this->elapseTimeBuffer.Push(static_cast<double>(this->elapseTime.count()));
}

void FPSCounter::updateStat()
{
    this->avgFps = this->fpsBuffer.GetAverage();
    this->maxFps = this->fpsBuffer.GetMax();
    this->minFps = this->fpsBuffer.GetMin();

    this->avgElapseTimens = this->elapseTimeBuffer.GetAverage();
    this->maxElapseTimens = this->elapseTimeBuffer.GetMax();
    this->minElapseTimens = this->elapseTimeBuffer.GetMin();
}

// Configuration methods
void FPSCounter::setFrameRateLimitingMode(int mode) { this->frameRateLimitingMode = std::clamp(mode, 0, 3); }

void FPSCounter::setSpinThreshold(std::chrono::nanoseconds threshold)
{
    this->spinThreshold = std::max(threshold, std::chrono::nanoseconds(0));
}

} // namespace engine::core
