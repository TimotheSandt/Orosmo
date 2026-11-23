#include "orosmo/renderer/batch_arena.hpp"

#include <algorithm>

namespace orosmo::renderer
{

core::Result<BatchArena::Range> BatchArena::upload(rhi::Device &device, const void *encoder, std::span<const std::byte> data, const char *debugName)
{
    constexpr std::uint64_t kAlign = 256;
    if (encoder != encoder_)
    {
        encoder_ = encoder;
        cursor_ = 0;
        retired_.clear();
    }
    const std::uint64_t needed = data.size();
    if (!current_.valid() || cursor_ + needed > current_.size())
    {
        rhi::BufferDesc bd;
        bd.size = std::max<std::uint64_t>({needed, 64 * 1024, current_.valid() ? current_.size() * 2 : 0});
        bd.usage = rhi::BufferUsage::kVertex | rhi::BufferUsage::kCopyDst;
        bd.debugName = debugName;
        auto created = GpuBuffer::create(device, bd);
        if (!created)
        {
            return std::unexpected(created.error());
        }
        if (current_.valid() && cursor_ > 0)
        {
            retired_.push_back(std::move(current_)); // les dessins déjà enregistrés pointent encore dedans
        }
        current_ = std::move(*created);
        cursor_ = 0;
    }
    if (auto r = current_.update(cursor_, data); !r)
    {
        return std::unexpected(r.error());
    }
    const Range range{current_.handle(), cursor_};
    cursor_ += (needed + kAlign - 1) / kAlign * kAlign;
    return range;
}

} // namespace orosmo::renderer
