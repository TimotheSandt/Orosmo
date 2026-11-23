#include "orosmo/renderer/batch_arena.hpp"

#include <algorithm>

namespace orosmo::renderer
{

core::Result<BatchArena::Range> BatchArena::upload(rhi::Device &device, std::uint64_t encoderId, std::span<const std::byte> data, const char *debugName)
{
    constexpr std::uint64_t kAlign = 256;
    if (encoderId != encoderId_)
    {
        // Nouvel encodeur (nouvelle image) : zone suivante. Ses tampons remplacés ont été lus par un encodeur fini ; leur destruction
        // reste différée par le RHI.
        encoderId_ = encoderId;
        slot_ = (slot_ + 1) % static_cast<std::uint32_t>(slots_.size());
        cursor_ = 0;
        slots_[slot_].retired.clear();
    }
    Slot &slot = slots_[slot_];
    const std::uint64_t needed = data.size();
    if (!slot.current.valid() || cursor_ + needed > slot.current.size())
    {
        rhi::BufferDesc bd;
        bd.size = std::max<std::uint64_t>({needed, 64 * 1024, slot.current.valid() ? slot.current.size() * 2 : 0});
        bd.usage = rhi::BufferUsage::kVertex | rhi::BufferUsage::kCopyDst;
        bd.memory = rhi::MemoryKind::kUpload; // écrit par le CPU (OpenGL : mappé en permanence ; sinon copie ordonnée comme avant)
        bd.debugName = debugName;
        auto created = GpuBuffer::create(device, bd);
        if (!created)
        {
            return std::unexpected(created.error());
        }
        if (slot.current.valid() && cursor_ > 0)
        {
            slot.retired.push_back(std::move(slot.current)); // les dessins déjà enregistrés pointent encore dedans
        }
        slot.current = std::move(*created);
        cursor_ = 0;
    }
    if (auto r = slot.current.update(cursor_, data); !r)
    {
        return std::unexpected(r.error());
    }
    const Range range{slot.current.handle(), cursor_};
    cursor_ += (needed + kAlign - 1) / kAlign * kAlign;
    return range;
}

} // namespace orosmo::renderer
