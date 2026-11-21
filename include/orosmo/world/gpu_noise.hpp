// TER-01 (GPU) : le même bruit que `noise.hpp`, calculé en compute par le RHI. Même algorithme (hachage entier identique, mêmes
// constantes, mêmes positions d'échantillons calculées en double depuis les indices entiers) ; les opérations flottantes du GPU
// (division, racine) ne sont pas arrondies comme celles du processeur, d'où une TOLÉRANCE fixée et vérifiée par les tests :
// |GPU - CPU| <= kGpuTolerance * max(1, heightScale).
#pragma once

#include "orosmo/core/error.hpp"
#include "orosmo/rhi/device.hpp"
#include "orosmo/world/terrain.hpp"

#include <memory>
#include <vector>

namespace orosmo::world
{

inline constexpr float kGpuTolerance = 1e-4f;

class GpuNoise
{
  public:
    // Exige le calcul (Capabilities::compute) et des flottants 64 bits dans les shaders (Capabilities::shaderFloat64) : sinon
    // kUnsupported.
    static core::Result<std::unique_ptr<GpuNoise>> create(rhi::Device &device);
    ~GpuNoise();
    GpuNoise(const GpuNoise &) = delete;
    GpuNoise &operator=(const GpuNoise &) = delete;

    // Hauteurs de la tuile, même grille et même ordre que `generateTile`.
    core::Result<std::vector<float>> generateTile(const TerrainParams &params, std::uint32_t seed, const TileKey &key);

  private:
    explicit GpuNoise(rhi::Device &device) : device_(device) {}
    rhi::Device &device_;
    rhi::ShaderHandle shader_;
    rhi::PipelineHandle pipeline_;
    rhi::BufferHandle buffer_;     // résultats, réutilisé entre les appels
    rhi::BindGroupHandle group_;
    std::uint64_t capacity_ = 0;   // octets de buffer_
};

} // namespace orosmo::world
