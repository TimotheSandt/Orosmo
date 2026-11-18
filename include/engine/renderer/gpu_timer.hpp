// Minuterie GPU d'une portion de commandes (RHI-11) : deux horodatages dans une pool, durée lue sans bloquer quand elle est prête.
#pragma once

#include "engine/rhi/device.hpp"

#include <chrono>
#include <optional>
#include <string>

namespace engine::renderer
{

class GpuTimer
{
  public:
    GpuTimer(rhi::Device &device, const std::string &name);
    ~GpuTimer();
    GpuTimer(const GpuTimer &) = delete;
    GpuTimer &operator=(const GpuTimer &) = delete;

    bool valid() const noexcept { return !pool_.isNull(); }
    void begin(rhi::CommandEncoder &encoder);
    void end(rhi::CommandEncoder &encoder);
    // Vrai quand begin puis end ont été soumis et terminés par le GPU.
    bool isReady() const;
    // Durée de la dernière mesure terminée ; std::nullopt tant qu'elle ne l'est pas.
    std::optional<std::chrono::nanoseconds> elapsed() const;

  private:
    rhi::Device &device_;
    rhi::QueryPoolHandle pool_;
    bool begun_ = false;
    bool ended_ = false;
};

} // namespace engine::renderer
