// Tampon de sommets partagé par plusieurs appels de rendu sur le même encodeur (revue CodeRabbit M5). Sur Vulkan, `updateBuffer`
// est un transfert soumis AVANT les commandes de l'encodeur : deux écritures à l'offset 0 avant `submit` donneraient à tous les
// dessins les données de la dernière. Chaque envoi reçoit donc sa propre plage, tant que l'encodeur reste le même.
//
// Perf M5 : une zone (un tampon `rhi::MemoryKind::kUpload`) par image en vol. Un nouvel encodeur passe à la zone suivante et repart
// de son début : la zone qu'il réécrit a été lue par l'encodeur d'il y a `framesInFlight` encodeurs, que le GPU a fini (une image =
// un encodeur, le RHI limite l'avance du CPU). La zone de l'image précédente, que le GPU lit peut-être encore, n'est jamais réécrite.
#pragma once

#include "orosmo/core/error.hpp"
#include "orosmo/renderer/frame_ring.hpp"
#include "orosmo/renderer/gpu_buffer.hpp"

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

namespace orosmo::renderer
{

class BatchArena
{
  public:
    struct Range
    {
        rhi::BufferHandle buffer;
        std::uint64_t offset = 0;
    };

    explicit BatchArena(std::uint32_t framesInFlight = kDefaultFramesInFlight) : slots_(std::max(framesInFlight, 1u)) {}

    // Écrit `data` dans une plage libre (alignée sur 256 octets) de la zone de l'encodeur et la renvoie. `encoderId` est
    // `CommandEncoder::id()` de l'encodeur qui dessinera : un nouvel encodeur passe à la zone suivante et repart de son début.
    core::Result<Range> upload(rhi::Device &device, std::uint64_t encoderId, std::span<const std::byte> data, const char *debugName);
    // Capacité de la zone courante.
    std::uint64_t capacity() const noexcept { return slots_[slot_].current.size(); }
    std::uint32_t framesInFlight() const noexcept { return static_cast<std::uint32_t>(slots_.size()); }
    std::uint32_t slot() const noexcept { return slot_; }

  private:
    struct Slot
    {
        GpuBuffer current;
        std::vector<GpuBuffer> retired; // remplacés par un tampon plus grand, encore référencés par l'encodeur de la zone
    };
    std::vector<Slot> slots_;
    std::uint32_t slot_ = 0;
    std::uint64_t encoderId_ = 0;
    std::uint64_t cursor_ = 0;
};

} // namespace orosmo::renderer
