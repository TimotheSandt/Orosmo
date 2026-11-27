// CORE-07 : horloge, delta variable pour le rendu, pas fixe pour la simulation.
// La simulation ne lit jamais l'horloge murale : elle reçoit des deltas, donc un test peut la rejouer à l'identique.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

namespace orosmo::core
{

using Nanoseconds = std::chrono::nanoseconds;

class IClock
{
  public:
    virtual ~IClock() = default;
    virtual Nanoseconds now() const = 0; // monotone
};

class SteadyClock final : public IClock
{
  public:
    Nanoseconds now() const override;
};

// Horloge pilotée à la main : tests déterministes. Lisible depuis un autre thread que celui qui l'avance (thread de
// simulation, M8 lot L4) : valeur atomique, `advance` publie (release) ce que `now` lit (acquire).
class ManualClock final : public IClock
{
  public:
    Nanoseconds now() const override { return Nanoseconds(mNow.load(std::memory_order_acquire)); }
    void advance(Nanoseconds d) { mNow.fetch_add(d.count(), std::memory_order_acq_rel); }

  private:
    std::atomic<Nanoseconds::rep> mNow{0};
};

// Delta variable (rendu). Le premier tick renvoie 0 ; un delta plus grand que maxDelta (pause du débogueur,
// fenêtre déplacée) est ramené à maxDelta.
class FrameTimer
{
  public:
    explicit FrameTimer(const IClock &clock, Nanoseconds maxDelta = std::chrono::milliseconds(250));
    Nanoseconds tick();
    double deltaSeconds() const { return std::chrono::duration<double>(mDelta).count(); }
    std::uint64_t frameIndex() const { return mFrame; }

  private:
    const IClock *mClock;
    Nanoseconds mMaxDelta;
    Nanoseconds mLast{0};
    Nanoseconds mDelta{0};
    std::uint64_t mFrame = 0;
    bool mStarted = false;
};

// Pas fixe (simulation) : accumule les deltas et dit combien de pas de `step` exécuter.
// Au plus `maxSteps` par appel (anti « spirale de la mort ») ; le temps en trop est abandonné.
class FixedStepper
{
  public:
    explicit FixedStepper(Nanoseconds step, int maxSteps = 8);
    // Ajoute `frameDelta` et renvoie le nombre de pas à jouer.
    int advance(Nanoseconds frameDelta);
    // Fraction [0,1) du prochain pas déjà écoulée : interpolation du rendu entre deux états de simulation.
    double alpha() const;
    Nanoseconds step() const { return mStep; }
    std::uint64_t stepCount() const { return mTotalSteps; }
    // Temps de simulation écoulé : stepCount() * step(), exact, indépendant des deltas.
    Nanoseconds simulationTime() const { return mStep * static_cast<std::int64_t>(mTotalSteps); }

  private:
    Nanoseconds mStep;
    int mMaxSteps;
    Nanoseconds mAccumulator{0};
    std::uint64_t mTotalSteps = 0;
};

} // namespace orosmo::core
