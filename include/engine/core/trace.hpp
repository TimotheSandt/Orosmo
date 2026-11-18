// CORE-05 : profilage de base. Marqueurs CPU (ENGINE_TRACE_SCOPE), échantillons GPU injectés par le RHI
// (addSample avec la piste GPU), statistiques par nom, export au format « Chrome trace » (JSON lisible par
// chrome://tracing et https://ui.perfetto.dev). Le Profiler existant (FPS, minuteries d'image) est conservé tel quel ;
// ce module est la base neuve. Pas de dépendance à Tracy : l'export fichier suffit pour M1, un adaptateur Tracy
// pourra s'ajouter derrière TraceRecorder (docs/GAPS.md).
#pragma once

#include <engine/core/error.hpp>
#include <engine/core/time.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace engine::core
{

enum class TraceTrack : std::uint8_t
{
    kCpu,
    kGpu,
};

struct TraceSample
{
    std::string name;
    Nanoseconds start;
    Nanoseconds duration;
    std::uint64_t threadId; // numéro compact attribué par l'enregistreur (1, 2, ...)
    TraceTrack track;
};

struct TraceStats
{
    std::string name;
    std::uint64_t count = 0;
    Nanoseconds total{0};
    Nanoseconds min{0};
    Nanoseconds max{0};
    double averageMs() const { return count ? std::chrono::duration<double, std::milli>(total).count() / static_cast<double>(count) : 0.0; }
};

class TraceRecorder
{
  public:
    explicit TraceRecorder(const IClock &clock, std::size_t maxSamples = 1u << 20);

    void setEnabled(bool enabled) { mEnabled.store(enabled); }
    bool enabled() const { return mEnabled.load(); }

    // Instant courant de l'horloge de l'enregistreur.
    Nanoseconds now() const { return mClock->now(); }

    // Ajoute un échantillon terminé (CPU depuis un autre système, ou GPU fourni par le RHI : début/durée déjà convertis).
    void addSample(std::string_view name, Nanoseconds start, Nanoseconds duration, TraceTrack track = TraceTrack::kCpu);

    // Échantillons enregistrés (copie) ; au-delà de maxSamples les plus anciens sont conservés et les nouveaux comptés dans dropped().
    std::vector<TraceSample> samples() const;
    std::uint64_t dropped() const;
    void clear();

    // Une ligne par nom, triée par temps total décroissant.
    std::vector<TraceStats> stats() const;

    // Écrit {"traceEvents":[...]} (événements complets « X », microsecondes). Piste GPU : pid 2, CPU : pid 1.
    Result<> writeChromeTrace(const std::filesystem::path &path) const;
    std::string chromeTraceJson() const;

  private:
    std::uint64_t threadNumber();

    const IClock *mClock;
    std::size_t mMax;
    std::atomic<bool> mEnabled{true};
    mutable std::mutex mMutex;
    std::vector<TraceSample> mSamples;
    std::uint64_t mDropped = 0;
    std::vector<std::thread::id> mThreads;
};

// RAII : mesure la durée de la portée courante.
class ScopedTrace
{
  public:
    ScopedTrace(TraceRecorder &recorder, const char *name)
        : mRecorder(recorder.enabled() ? &recorder : nullptr), mName(name), mStart(mRecorder ? mRecorder->now() : Nanoseconds{0})
    {
    }
    ~ScopedTrace()
    {
        if (mRecorder)
            mRecorder->addSample(mName, mStart, mRecorder->now() - mStart);
    }
    ScopedTrace(const ScopedTrace &) = delete;
    ScopedTrace &operator=(const ScopedTrace &) = delete;

  private:
    TraceRecorder *mRecorder;
    const char *mName;
    Nanoseconds mStart;
};

} // namespace engine::core

#define ENGINE_TRACE_CONCAT_INNER(a, b) a##b
#define ENGINE_TRACE_CONCAT(a, b) ENGINE_TRACE_CONCAT_INNER(a, b)
#define ENGINE_TRACE_SCOPE(recorder, name) \
    ::engine::core::ScopedTrace ENGINE_TRACE_CONCAT(engineTraceScope_, __LINE__)((recorder), (name))
