// Minuterie GPU d'une portion de commandes (RHI-11) : deux horodatages dans une pool, durée lue sans bloquer quand elle est prête.
#pragma once

#include "orosmo/core/gpu_timer.hpp" // Profiler : IGpuTimer, IGpuTimerFactory
#include "orosmo/rhi/device.hpp"

#include <chrono>
#include <optional>
#include <string>

namespace orosmo::renderer
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

// Fournisseur de minuteries GPU du `Profiler` (A25) : chaque minuteur écrit deux horodatages dans l'encodeur COURANT, que l'application
// donne au début de l'image (`setEncoder`) et retire à la fin. Hors image (aucun encodeur), les minuteurs ne mesurent rien et ne sont
// jamais prêts (le Profiler les abandonne après son délai). Sans `Capabilities::timestampQueries`, `CreateTimer` rend nul : le
// Profiler mesure alors sur le CPU (une mesure par appel).
class ProfilerGpuTimerFactory final : public core::IGpuTimerFactory
{
  public:
    explicit ProfilerGpuTimerFactory(rhi::Device &device) : device_(device) {}
    void setEncoder(rhi::CommandEncoder *encoder) noexcept { encoder_ = encoder; }
    rhi::CommandEncoder *encoder() const noexcept { return encoder_; }
    std::unique_ptr<core::IGpuTimer> CreateTimer(const std::string &name) const override;

  private:
    rhi::Device &device_;
    rhi::CommandEncoder *encoder_ = nullptr;
};

} // namespace orosmo::renderer
