// Échange d'instantanés entre un écrivain et un lecteur, sans verrou ni attente (M8, A79 lot L4) : le thread de simulation
// remplit `back()` puis `publish()` ; le rendu appelle `acquire()` et lit le dernier instantané publié, qui ne change pas
// jusqu'à son prochain `acquire()`. Trois emplacements (le « double tampon » publié/lu, plus celui que l'écrivain remplit) :
// aucun côté n'attend l'autre, l'écrivain ne touche jamais l'emplacement lu, un instantané publié mais pas encore lu est
// remplacé par le suivant. Les emplacements sont réutilisés (pas d'allocation une fois les tailles atteintes).
//
// Un seul écrivain (`back`, `publish`) et un seul lecteur (`acquire`, `front`, `fresh`). L'indice du milieu et le drapeau
// « nouveau » tiennent dans un atomique : `publish` y dépose son emplacement (release) et reprend l'ancien milieu ;
// `acquire` échange son emplacement contre le milieu (acquire) seulement si le drapeau est levé.
#pragma once

#include <atomic>
#include <cstdint>

namespace orosmo::sim
{

template <typename T> class SnapshotBuffer
{
  public:
    SnapshotBuffer() = default;
    SnapshotBuffer(const SnapshotBuffer &) = delete;
    SnapshotBuffer &operator=(const SnapshotBuffer &) = delete;

    // ── Écrivain ──────────────────────────────────────────────────────────────
    // Emplacement à remplir : il garde le contenu d'un instantané plus ancien (réutiliser ses tableaux).
    T &back() noexcept { return slots_[back_]; }
    // Publie `back()` ; l'écrivain reçoit un autre emplacement libre.
    void publish() noexcept { back_ = middle_.exchange(static_cast<std::uint32_t>(back_ | kFresh), std::memory_order_acq_rel) & kIndex; }

    // ── Lecteur ───────────────────────────────────────────────────────────────
    // Prend le dernier instantané publié s'il y en a un nouveau ; rend l'instantané lu (inchangé jusqu'au prochain appel).
    const T &acquire() noexcept
    {
        if ((middle_.load(std::memory_order_relaxed) & kFresh) != 0u)
            front_ = middle_.exchange(front_, std::memory_order_acq_rel) & kIndex;
        return slots_[front_];
    }
    // Dernier instantané pris par `acquire` (valeur par défaut de T avant la première publication lue).
    const T &front() const noexcept { return slots_[front_]; }
    // Vrai si un instantané a été publié depuis le dernier `acquire`.
    bool fresh() const noexcept { return (middle_.load(std::memory_order_relaxed) & kFresh) != 0u; }

  private:
    static constexpr std::uint32_t kIndex = 3u;
    static constexpr std::uint32_t kFresh = 4u;

    T slots_[3]{};
    std::uint32_t back_ = 0u;  // écrivain
    std::uint32_t front_ = 2u; // lecteur
    alignas(64) std::atomic<std::uint32_t> middle_{1u};
};

} // namespace orosmo::sim
