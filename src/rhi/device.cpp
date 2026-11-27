#include "orosmo/rhi/device.hpp"

#include "opengl/gl_device.hpp"
#include "vulkan/vk_device.hpp"

#include <atomic>

namespace orosmo::rhi
{

namespace
{
std::atomic<std::uint64_t> nextEncoderId{1};
} // namespace

CommandEncoder::CommandEncoder() noexcept : id_(nextEncoderId.fetch_add(1, std::memory_order_relaxed)) {}

bool isBackendCompiled(Backend backend) noexcept { return backend == Backend::kOpenGL || backend == Backend::kVulkan; }

Result<> prepareWindowHints(const DeviceDesc &desc)
{
    if (desc.backend == Backend::kOpenGL)
    {
        gl::applyWindowHints(desc);
        return {};
    }
    if (desc.backend == Backend::kVulkan)
    {
        vk::applyWindowHints(desc);
        return {};
    }
    return orosmo::core::makeError(orosmo::core::ErrorCode::kUnsupported,
                                   std::string("RHI backend '") + toString(desc.backend) + "' is not compiled in this build");
}

Result<std::unique_ptr<Device>> createDevice(const DeviceDesc &desc)
{
    if (desc.framesInFlight == 0 || desc.framesInFlight > kMaxFramesInFlight)
    {
        return orosmo::core::makeError(orosmo::core::ErrorCode::kInvalidArgument, "createDevice: framesInFlight must be between 1 and " +
                                                                                       std::to_string(kMaxFramesInFlight) + " (got " +
                                                                                       std::to_string(desc.framesInFlight) + ")");
    }
    switch (desc.backend)
    {
        case Backend::kOpenGL: return gl::createGlDevice(desc);
        case Backend::kVulkan: return vk::createVulkanDevice(desc);
        case Backend::kMetal: break;
    }
    return orosmo::core::makeError(orosmo::core::ErrorCode::kUnsupported,
                                   std::string("RHI backend '") + toString(desc.backend) + "' is not compiled in this build");
}

} // namespace orosmo::rhi
