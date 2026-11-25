#include "orosmo/renderer/frame_pacing.hpp"

#include <algorithm>

namespace orosmo::renderer
{

std::string_view framePacingModeName(FramePacingMode mode) noexcept
{
    switch (mode)
    {
    case FramePacingMode::kVSync:
        return "vsync";
    case FramePacingMode::kLimited:
        return "limited";
    case FramePacingMode::kUnlimited:
        return "unlimited";
    }
    return "vsync";
}

std::string describeFramePacing(const FramePacing &pacing)
{
    switch (pacing.mode)
    {
    case FramePacingMode::kVSync:
        return "vsync";
    case FramePacingMode::kLimited:
        return "limit " + std::to_string(clampFpsLimit(pacing.fpsLimit)) + " FPS";
    case FramePacingMode::kUnlimited:
        return "unlimited";
    }
    return "vsync";
}

int clampFpsLimit(int fps) noexcept { return std::clamp(fps, kMinFpsLimit, kMaxFpsLimit); }

void FramePacer::bind(VSyncSetter setVSync, LimitSetter setLimit)
{
    setVSync_ = std::move(setVSync);
    setLimit_ = std::move(setLimit);
    appliedVSync_ = appliedLimit_ = -1;
    apply();
}

void FramePacer::setMode(FramePacing pacing)
{
    pacing.fpsLimit = clampFpsLimit(pacing.fpsLimit);
    current_ = pacing;
    apply();
}

void FramePacer::apply()
{
    const int vsync = vsyncActive() ? 1 : 0;
    const int limit = effectiveLimit();
    if (vsync != appliedVSync_ && setVSync_)
    {
        appliedVSync_ = vsync;
        setVSync_(vsync != 0);
    }
    if (limit != appliedLimit_ && setLimit_)
    {
        appliedLimit_ = limit;
        setLimit_(limit);
    }
}

} // namespace orosmo::renderer
