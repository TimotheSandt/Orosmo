// UI-04 : historique des temps d'image (anneau de taille fixe) et images par seconde qui en découlent. Repris du panneau de
// performances de la démo (A79 lot L10 : le profileur passe dans le moteur) ; lu par le profileur des outils de debug
// (`debug::DebugTools`) et par le panneau de la démo. Données seulement : aucun widget, aucune horloge (l'application pousse ses
// mesures).
#pragma once

#include <array>
#include <cstddef>
#include <vector>

namespace orosmo::ui
{

// Images par seconde d'après les intervalles entre débuts d'image : moyenne, 1 % bas (99e centile de l'intervalle), plus lente,
// plus rapide. Zéros sans intervalle.
struct FpsStats
{
    double average = 0.0;
    double low1 = 0.0;
    double min = 0.0;
    double max = 0.0;
};

class FrameHistory
{
  public:
    static constexpr std::size_t kSize = 240;

    // Une image : temps CPU de travail (attente exclue), temps GPU (0 : inconnu), intervalle depuis le début de la précédente
    // (0 pour la première : ignoré par les FPS) et attente (GPU, vsync), en ms.
    void push(float cpuMs, float gpuMs, float intervalMs, float waitMs = 0.0f) noexcept;
    std::size_t size() const noexcept { return count_; }
    // De la plus ancienne à la plus récente.
    std::vector<float> cpu() const { return ordered(cpu_); }
    std::vector<float> gpu() const { return ordered(gpu_); }
    std::vector<float> intervals() const { return ordered(interval_); }
    FpsStats fps() const;
    double averageCpuMs() const;
    double averageWaitMs() const;

  private:
    std::vector<float> ordered(const std::array<float, kSize> &ring) const;
    std::array<float, kSize> cpu_{};
    std::array<float, kSize> gpu_{};
    std::array<float, kSize> interval_{};
    std::array<float, kSize> wait_{};
    std::size_t next_ = 0;
    std::size_t count_ = 0;
};

} // namespace orosmo::ui
