// Tampon GPU à durée de vie liée à un objet C++ (RAII) au-dessus du RHI. Le périphérique doit survivre au tampon.
#pragma once

#include "orosmo/core/error.hpp"
#include "orosmo/rhi/device.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace orosmo::renderer
{

class GpuBuffer
{
  public:
    GpuBuffer() = default;
    ~GpuBuffer() { reset(); }
    GpuBuffer(const GpuBuffer &) = delete;
    GpuBuffer &operator=(const GpuBuffer &) = delete;
    GpuBuffer(GpuBuffer &&other) noexcept { swap(other); }
    GpuBuffer &operator=(GpuBuffer &&other) noexcept
    {
        if (this != &other)
        {
            reset();
            swap(other);
        }
        return *this;
    }

    static core::Result<GpuBuffer> create(rhi::Device &device, const rhi::BufferDesc &desc, std::span<const std::byte> initial = {});

    core::Result<> update(std::uint64_t offset, std::span<const std::byte> data);
    core::Result<std::vector<std::byte>> read(std::uint64_t offset, std::uint64_t size) const;
    // Nouvelle taille ; `preserveContent` recopie les min(ancienne, nouvelle) premiers octets. Le handle change.
    core::Result<> resize(std::uint64_t newSize, bool preserveContent);

    void reset() noexcept;
    bool valid() const noexcept { return device_ != nullptr && handle_.isNull() == false; }
    rhi::BufferHandle handle() const noexcept { return handle_; }
    std::uint64_t size() const noexcept { return desc_.size; }
    const rhi::BufferDesc &desc() const noexcept { return desc_; }

  private:
    void swap(GpuBuffer &other) noexcept
    {
        std::swap(device_, other.device_);
        std::swap(handle_, other.handle_);
        std::swap(desc_, other.desc_);
    }

    rhi::Device *device_ = nullptr;
    rhi::BufferHandle handle_;
    rhi::BufferDesc desc_;
};

} // namespace orosmo::renderer
