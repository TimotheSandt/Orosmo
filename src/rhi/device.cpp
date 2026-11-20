#include "orosmo/rhi/device.hpp"

#include "opengl/gl_device.hpp"

namespace orosmo::rhi
{

bool isBackendCompiled(Backend backend) noexcept { return backend == Backend::kOpenGL; }

Result<> prepareWindowHints(const DeviceDesc &desc)
{
    if (desc.backend == Backend::kOpenGL)
    {
        gl::applyWindowHints(desc);
        return {};
    }
    return orosmo::core::makeError(orosmo::core::ErrorCode::kUnsupported,
                                   std::string("RHI backend '") + toString(desc.backend) + "' is not compiled in this build");
}

Result<std::unique_ptr<Device>> createDevice(const DeviceDesc &desc)
{
    switch (desc.backend)
    {
        case Backend::kOpenGL: return gl::createGlDevice(desc);
        case Backend::kVulkan:
        case Backend::kMetal: break;
    }
    return orosmo::core::makeError(orosmo::core::ErrorCode::kUnsupported,
                                   std::string("RHI backend '") + toString(desc.backend) + "' is not compiled in this build");
}

} // namespace orosmo::rhi
