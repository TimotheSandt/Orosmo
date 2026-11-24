#include "orosmo/renderer/batch_arena.hpp"

#include <algorithm>

namespace orosmo::renderer
{

core::Result<BatchArena::Range> BatchArena::upload(rhi::Device &device, std::uint64_t encoderId, std::span<const std::byte> data, const char *debugName)
{
    constexpr std::uint64_t kAlign = 256;
    // Un encodeur garde sa zone (et son curseur) tant qu'aucun autre ne la reprend : E1 -> E2 -> E1 continue dans la zone de E1.
    auto owned = std::ranges::find_if(slots_, [&](const Slot &s) { return s.owner == encoderId; });
    if (owned != slots_.end())
    {
        active_ = static_cast<std::uint32_t>(owned - slots_.begin());
    }
    else if (encoderId != 0 && encoderId < newest_)
    {
        // Encodeur plus ancien que les `framesInFlight` derniers : sa zone a été reprise, ses données ont pu être réécrites.
        return core::makeError(core::ErrorCode::kBusy, "BatchArena: zone of this encoder already reused (more than framesInFlight encoders in flight)");
    }
    else
    {
        // Nouvel encodeur (nouvelle image) : zone suivante. Ses tampons remplacés ont été lus par un encodeur fini ; leur destruction
        // reste différée par le RHI.
        newest_ = encoderId;
        next_ = (next_ + 1) % static_cast<std::uint32_t>(slots_.size());
        active_ = next_;
        Slot &fresh = slots_[active_];
        fresh.owner = encoderId;
        fresh.cursor = 0;
        fresh.retired.clear();
    }
    Slot &slot = slots_[active_];
    const std::uint64_t needed = data.size();
    if (!slot.current.valid() || slot.cursor + needed > slot.current.size())
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
        if (slot.current.valid() && slot.cursor > 0)
        {
            slot.retired.push_back(std::move(slot.current)); // les dessins déjà enregistrés pointent encore dedans
        }
        slot.current = std::move(*created);
        slot.cursor = 0;
    }
    if (auto r = slot.current.update(slot.cursor, data); !r)
    {
        return std::unexpected(r.error());
    }
    const Range range{slot.current.handle(), slot.cursor};
    slot.cursor += (needed + kAlign - 1) / kAlign * kAlign;
    return range;
}

} // namespace orosmo::renderer
