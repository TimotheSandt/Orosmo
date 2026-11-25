// Tampon de sommets partagé par plusieurs appels de rendu sur le même encodeur (revue CodeRabbit M5). Sur Vulkan, `updateBuffer`
// est un transfert soumis AVANT les commandes de l'encodeur : deux écritures à l'offset 0 avant `submit` donneraient à tous les
// dessins les données de la dernière. Chaque envoi reçoit donc sa propre plage, tant que l'encodeur reste le même.
//
// Perf M5 : une zone (un tampon `rhi::MemoryKind::kUpload`) par image en vol. Un nouvel encodeur passe à la zone suivante et repart
// de son début : la zone qu'il réécrit a été lue par l'encodeur d'il y a `framesInFlight` encodeurs, que le GPU a fini (une image =
// un encodeur, le RHI limite l'avance du CPU). La zone de l'image précédente, que le GPU lit peut-être encore, n'est jamais réécrite.
//
// Contrat (revue CodeRabbit PR #12) : une zone appartient à son encodeur jusqu'à ce qu'un nouvel encodeur la reprenne ; un encodeur
// qui revient (E1 -> E2 -> E1) continue dans sa zone, sans réécrire ses plages. Au plus `framesInFlight` encodeurs distincts
// peuvent utiliser l'arène entre deux fins d'image (non soumis ou encore lus par le GPU) : l'arène ne voit pas les soumissions.
// Un encodeur dont la zone a déjà été reprise reçoit `kBusy` (les `kEvictedMemory` dernières reprises sont mémorisées) ; un encodeur
// jamais vu reçoit une zone, quel que soit l'ordre de ses premiers envois par rapport aux autres encodeurs.
#pragma once

#include "orosmo/core/error.hpp"
#include "orosmo/renderer/frame_ring.hpp"
#include "orosmo/renderer/gpu_buffer.hpp"

#include <algorithm>
#include <cstddef>
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
    std::uint64_t capacity() const noexcept { return slots_[active_].current.size(); }
    std::uint32_t framesInFlight() const noexcept { return static_cast<std::uint32_t>(slots_.size()); }
    std::uint32_t slot() const noexcept { return active_; }

  private:
    struct Slot
    {
        GpuBuffer current;
        std::vector<GpuBuffer> retired; // remplacés par un tampon plus grand, encore référencés par l'encodeur de la zone
        std::uint64_t owner = 0;        // CommandEncoder::id() propriétaire (0 : aucun)
        std::uint64_t cursor = 0;       // prochaine plage libre de la zone
    };
    std::vector<Slot> slots_;
    std::uint32_t next_ = 0;   // dernière zone donnée à un nouvel encodeur
    std::uint32_t active_ = 0; // zone du dernier envoi
    static constexpr std::size_t kEvictedMemory = 64;
    std::vector<std::uint64_t> evicted_; // derniers encodeurs dont la zone a été reprise (au plus kEvictedMemory)
};

} // namespace orosmo::renderer
