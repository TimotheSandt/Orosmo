// Anneau de tampons par image en vol (perf M5, option B) : données réécrites à chaque image (blocs uniformes, tranches de modèle,
// sommets) dans de la mémoire écrite directement par le CPU (`rhi::MemoryKind::kUpload`), un tampon par image en vol. L'image `n`
// écrit la zone `n % framesInFlight` ; la zone qu'elle réécrit a été lue par l'image `n - framesInFlight`, que le périphérique a
// terminée (Device::beginFrame / present limitent l'avance du CPU à kFramesInFlight images, docs/03 §3). Plus aucune écriture ne
// vise une plage que le GPU lit encore, et une seule écriture (`updateBuffer`) suffit par image et par anneau.
#pragma once

#include "orosmo/core/error.hpp"
#include "orosmo/renderer/gpu_buffer.hpp"
#include "orosmo/rhi/device.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace orosmo::renderer
{

// Images en vol par défaut : celles du RHI (OpenGL et Vulkan : 2). Un anneau plus profond reste juste ; moins profond, non.
inline constexpr std::uint32_t kDefaultFramesInFlight = 2;

class FrameRing
{
  public:
    FrameRing() = default;
    // `usage` : usages des tampons (kCopyDst est ajouté : sur un backend sans mappage, `updateBuffer` est une copie).
    FrameRing(rhi::BufferUsage usage, std::string debugName, std::uint32_t framesInFlight = kDefaultFramesInFlight);

    // Choisit la zone de l'image `frame` (frame % framesInFlight).
    void beginFrame(std::uint64_t frame) noexcept { slot_ = framesInFlight_ == 0 ? 0 : static_cast<std::uint32_t>(frame % framesInFlight_); }
    // Capacité d'au moins `bytes` pour la zone courante. Vrai si son tampon a été (re)créé : nouveau handle, les groupes de liaison
    // qui le visent sont à refaire. L'ancien tampon est détruit (destruction différée par le RHI : l'image qui le lit le garde).
    core::Result<bool> reserve(rhi::Device &device, std::uint64_t bytes);
    // Écrit `data` à `offset` dans la zone courante (un `updateBuffer`).
    core::Result<> write(std::uint64_t offset, std::span<const std::byte> data);

    rhi::BufferHandle buffer() const noexcept { return slot_ < slots_.size() ? slots_[slot_].handle() : rhi::BufferHandle{}; }
    rhi::BufferHandle buffer(std::uint32_t slot) const noexcept { return slot < slots_.size() ? slots_[slot].handle() : rhi::BufferHandle{}; }
    std::uint64_t capacity() const noexcept { return slot_ < slots_.size() ? slots_[slot_].size() : 0; }
    std::uint32_t slot() const noexcept { return slot_; }
    std::uint32_t framesInFlight() const noexcept { return framesInFlight_; }

  private:
    rhi::BufferUsage usage_ = rhi::BufferUsage::kNone;
    std::string debugName_;
    std::uint32_t framesInFlight_ = kDefaultFramesInFlight;
    std::uint32_t slot_ = 0;
    std::vector<GpuBuffer> slots_;
};

// Arrondi au multiple supérieur de `alignment` (> 0).
constexpr std::uint64_t alignUp(std::uint64_t value, std::uint64_t alignment) noexcept
{
    return (value + alignment - 1) / alignment * alignment;
}

} // namespace orosmo::renderer
