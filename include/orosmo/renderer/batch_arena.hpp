// Tampon de sommets partagé par plusieurs appels de rendu sur le même encodeur (revue CodeRabbit M5). Sur Vulkan, `updateBuffer`
// est un transfert soumis AVANT les commandes de l'encodeur : deux écritures à l'offset 0 avant `submit` donneraient à tous les
// dessins les données de la dernière. Chaque envoi reçoit donc sa propre plage, tant que l'encodeur reste le même ; un nouvel
// encodeur repart du début (l'encodeur précédent est supposé soumis avant que le suivant ne soit enregistré).
#pragma once

#include "orosmo/core/error.hpp"
#include "orosmo/renderer/gpu_buffer.hpp"

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

    // Écrit `data` dans une plage libre (alignée sur 256 octets) et la renvoie. `encoderId` est `CommandEncoder::id()` de l'encodeur
    // qui dessinera : un nouvel encodeur fait repartir l'arène du début (ses plages précédentes ont été soumises).
    core::Result<Range> upload(rhi::Device &device, std::uint64_t encoderId, std::span<const std::byte> data, const char *debugName);
    std::uint64_t capacity() const noexcept { return current_.size(); }

  private:
    GpuBuffer current_;
    std::vector<GpuBuffer> retired_; // remplacés par un tampon plus grand, encore référencés par l'encodeur en cours
    std::uint64_t encoderId_ = 0;
    std::uint64_t cursor_ = 0;
};

} // namespace orosmo::renderer
