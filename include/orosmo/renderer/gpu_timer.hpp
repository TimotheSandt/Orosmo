// Minuterie GPU d'une portion de commandes (RHI-11) : deux horodatages dans une pool, durée lue sans bloquer quand elle est prête.
#pragma once

#include "orosmo/core/gpu_timer.hpp" // Profiler : IGpuTimer, IGpuTimerFactory
#include "orosmo/rhi/device.hpp"

#include <chrono>
#include <memory>
#include <mutex>
#include <vector>
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
    // Oublie la mesure précédente (minuterie réutilisée) : plus prête tant que begin puis end n'ont pas été refaits.
    void reset() noexcept { begun_ = ended_ = false; }
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

    // Minuteries terminées gardées pour être réutilisées : le Profiler crée une minuterie par mesure et par image ; sans ce
    // recyclage, chaque mesure créait puis détruisait une pool de requêtes (coût du pilote et des couches de validation).
    static constexpr std::size_t kMaxRecycled = 64;
    std::unique_ptr<GpuTimer> takeRecycled() const;
    void recycle(std::unique_ptr<GpuTimer> timer) const;
    std::size_t recycledCount() const;

  private:
    rhi::Device &device_;
    rhi::CommandEncoder *encoder_ = nullptr;
    mutable std::mutex recycledMutex_;
    mutable std::vector<std::unique_ptr<GpuTimer>> recycled_;
};

} // namespace orosmo::renderer
