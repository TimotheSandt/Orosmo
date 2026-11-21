#include "orosmo/renderer/gpu_buffer.hpp"

#include <algorithm>

namespace orosmo::renderer
{

core::Result<GpuBuffer> GpuBuffer::create(rhi::Device &device, const rhi::BufferDesc &desc, std::span<const std::byte> initial)
{
    auto handle = device.createBuffer(desc, initial);
    if (!handle)
    {
        return std::unexpected(handle.error());
    }
    GpuBuffer out;
    out.device_ = &device;
    out.handle_ = *handle;
    out.desc_ = desc;
    return out;
}

core::Result<> GpuBuffer::update(std::uint64_t offset, std::span<const std::byte> data)
{
    if (!valid())
    {
        return core::makeError(core::ErrorCode::kInvalidArgument, "GpuBuffer::update: empty buffer");
    }
    return device_->updateBuffer(handle_, offset, data);
}

core::Result<std::vector<std::byte>> GpuBuffer::read(std::uint64_t offset, std::uint64_t size) const
{
    if (!valid())
    {
        return core::makeError(core::ErrorCode::kInvalidArgument, "GpuBuffer::read: empty buffer");
    }
    return device_->readBuffer(handle_, offset, size);
}

core::Result<> GpuBuffer::resize(std::uint64_t newSize, bool preserveContent)
{
    if (!valid())
    {
        return core::makeError(core::ErrorCode::kInvalidArgument, "GpuBuffer::resize: empty buffer");
    }
    std::vector<std::byte> keep;
    if (preserveContent)
    {
        auto old = device_->readBuffer(handle_, 0, std::min(desc_.size, newSize));
        if (!old)
        {
            return std::unexpected(old.error());
        }
        keep = std::move(*old);
    }
    rhi::BufferDesc desc = desc_;
    desc.size = newSize;
    auto fresh = device_->createBuffer(desc, keep);
    if (!fresh)
    {
        return std::unexpected(fresh.error());
    }
    device_->destroy(handle_);
    handle_ = *fresh;
    desc_ = desc;
    return {};
}

void GpuBuffer::reset() noexcept
{
    if (device_ != nullptr && !handle_.isNull())
    {
        device_->destroy(handle_);
    }
    device_ = nullptr;
    handle_ = {};
    desc_ = {};
}

} // namespace orosmo::renderer
