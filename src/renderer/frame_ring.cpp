#include "orosmo/renderer/frame_ring.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace orosmo::renderer
{

FrameRing::FrameRing(rhi::BufferUsage usage, std::string debugName, std::uint32_t framesInFlight)
    : usage_(usage | rhi::BufferUsage::kCopyDst), debugName_(std::move(debugName)), framesInFlight_(std::max(framesInFlight, 1u))
{
}

core::Result<bool> FrameRing::reserve(rhi::Device &device, std::uint64_t bytes)
{
    // Moins profond que le périphérique : une zone serait réécrite pendant que le GPU la lit encore (revue PR #37).
    if (framesInFlight_ < device.framesInFlight())
    {
        return core::makeError(core::ErrorCode::kInvalidArgument, "FrameRing::reserve: framesInFlight (" + std::to_string(framesInFlight_) +
                                                                      ") is below the device's (" + std::to_string(device.framesInFlight()) + ")");
    }
    if (slots_.size() != framesInFlight_)
    {
        slots_.resize(framesInFlight_);
    }
    GpuBuffer &current = slots_[slot_];
    if (current.valid() && current.size() >= bytes)
    {
        return false;
    }
    rhi::BufferDesc desc;
    // Croissance géométrique : rarement recréé. 4 Kio au moins (blocs de l'image, quelques tranches).
    desc.size = std::max<std::uint64_t>({bytes, 4 * 1024, current.valid() ? current.size() * 2 : 0});
    desc.usage = usage_;
    desc.memory = rhi::MemoryKind::kUpload;
    desc.debugName = debugName_ + "[" + std::to_string(slot_) + "]";
    auto created = GpuBuffer::create(device, desc);
    if (!created)
    {
        return std::unexpected(created.error());
    }
    current = std::move(*created);
    return true;
}

core::Result<> FrameRing::write(std::uint64_t offset, std::span<const std::byte> data)
{
    if (slot_ >= slots_.size() || !slots_[slot_].valid())
    {
        return core::makeError(core::ErrorCode::kInvalidArgument, "FrameRing::write: no buffer for this frame (call reserve first)");
    }
    return slots_[slot_].update(offset, data);
}

} // namespace orosmo::renderer
