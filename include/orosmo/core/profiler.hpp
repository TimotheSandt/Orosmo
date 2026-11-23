// Mesures nommées par image (CPU, ou GPU quand un fournisseur IGpuTimerFactory est installé) : Profiler::Profile,
// Profiler::ProfileGPU, moyennes / min / max par nom (Profiler::GetAverageTime).
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <queue>
#include <vector>
#include <memory>
#include "orosmo/core/gpu_timer.hpp"
#include "orosmo/core/ring_buffer.hpp"

namespace orosmo::core
{

inline constexpr std::size_t kProfilerMaxQueries = 12;
inline constexpr std::size_t kProfilerBufferSize = 512;
inline constexpr int kProfilerSaveHistoryEveryMs = 3000;
inline constexpr int kProfilerQueryTimeoutMs = 10000;
inline constexpr std::size_t kProfilerMaxHistory = 1024;

class Profiler
{
  private:
    Profiler() = delete;
    ~Profiler() = delete;

    // Delete copy and move operations
    Profiler(const Profiler &) = delete;
    Profiler &operator=(const Profiler &) = delete;
    Profiler(Profiler &&) = delete;
    Profiler &operator=(Profiler &&) = delete;

  private:
    struct ProfilerData;
    struct QueryData;

  public:
    // Clé de mesure précalculée : pointeurs vers les entrées du nom (stables : tables à nœuds, jamais effacées). À créer UNE fois
    // (MakeKey) et à passer aux Profile / ProfileGPU de la boucle chaude : aucune std::string construite ni recherche de table par
    // appel. Les surcharges par nom (std::string) restent, pour les usages rares ; les noms affichés ne changent pas.
    class Key
    {
      public:
        Key() = default;
        const std::string &name() const noexcept { return *name_; }
        bool valid() const noexcept { return name_ != nullptr; }

      private:
        friend class Profiler;
        const std::string *name_ = nullptr;
        ProfilerData *data_ = nullptr;
        QueryData *queries_ = nullptr;
    };

    // Crée (ou retrouve) la clé du nom ; la seule opération qui construit des chaînes et cherche dans les tables.
    static Key MakeKey(const std::string &name)
    {
        Key key;
        {
            std::lock_guard<std::mutex> lock(getProfilerMutex());
            auto it = getProfilerData().try_emplace(name).first;
            key.name_ = &it->first;
            key.data_ = &it->second;
        }
        {
            std::lock_guard<std::mutex> lock(getQueryMutex());
            key.queries_ = &getQueryData().try_emplace(name).first->second;
        }
        return key;
    }

    static void Profile(const std::string &name, const std::function<void()> &func) { Profile(MakeKey(name), func); }

    static void Profile(const Key &key, const std::function<void()> &func)
    {
        auto start = std::chrono::high_resolution_clock::now();
        func();
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        AddTime(*key.data_, duration);
    }

    template <typename Func, typename... Args>
    static auto Profile(const std::string &name, Func &&func, Args &&...args) -> std::invoke_result_t<Func, Args...>
    {
        return Profile(MakeKey(name), std::forward<Func>(func), std::forward<Args>(args)...);
    }

    template <typename Func, typename... Args>
    static auto Profile(const Key &key, Func &&func, Args &&...args) -> std::invoke_result_t<Func, Args...>
    {
        auto start = std::chrono::high_resolution_clock::now();

        if constexpr (std::is_same_v<std::invoke_result_t<Func, Args...>, void>)
        {
            std::invoke(std::forward<Func>(func), std::forward<Args>(args)...);
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
            AddTime(*key.data_, duration);
        }
        else
        {
            auto result = std::invoke(std::forward<Func>(func), std::forward<Args>(args)...);
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
            AddTime(*key.data_, duration);

            return result;
        }
    }

    template <typename Func, typename... Args> static std::chrono::nanoseconds Profile(Func &&func, int times, Args &&...args)
    {
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < times; i++)
        {
            std::invoke(std::forward<Func>(func), std::forward<Args>(args)...);
        }
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    }

    // Fournisseur de minuteries GPU (installé par le runtime graphique, voir Profiler/GpuTimer.h). nullptr : mesure CPU.
    // Le stockage de ce pointeur est dans src/Profiler/Profiler.cpp (pas d'état global défini dans un en-tête).
    // Abandonne les mesures GPU en attente (à appeler avant de détruire le périphérique graphique dont dépendent les minuteurs).
    static void ClearGpuQueries();
    static void SetGpuTimerFactory(const IGpuTimerFactory *factory) noexcept;
    static const IGpuTimerFactory *GetGpuTimerFactory() noexcept;
    // Minuteries GPU actives (défaut : oui). Désactivées, ProfileGPU mesure sur le CPU et ne demande AUCUNE minuterie au fournisseur
    // (aucune pool de requêtes créée, aucun horodatage écrit) ; les mesures GPU déjà en attente se terminent normalement.
    // À couper quand personne ne lit les temps GPU (panneau de performances masqué).
    static void SetGpuTimingsEnabled(bool enabled) noexcept;
    static bool GpuTimingsEnabled() noexcept;

    static void ProfileGPU(const std::string &name, const std::function<void()> &func) { ProfileGPU(MakeKey(name), func); }

    static void ProfileGPU(const Key &key, const std::function<void()> &func)
    {
        std::unique_ptr<IGpuTimer> query = CreateGpuTimer(key);
        if (query == nullptr)
        {
            Profile(key, func);
            return;
        }

        query->Begin();
        func();
        query->End();

        AddQuery(*key.queries_, std::move(query));

        ProcessQueries(*key.data_, *key.queries_);
    }

    template <typename Func, typename... Args>
    static auto ProfileGPU(const std::string &name, Func &&func, Args &&...args) -> std::invoke_result_t<Func, Args...>
    {
        return ProfileGPU(MakeKey(name), std::forward<Func>(func), std::forward<Args>(args)...);
    }

    template <typename Func, typename... Args>
    static auto ProfileGPU(const Key &key, Func &&func, Args &&...args) -> std::invoke_result_t<Func, Args...>
    {
        std::unique_ptr<IGpuTimer> query = CreateGpuTimer(key);
        if (query == nullptr)
        {
            return Profile(key, std::forward<Func>(func), std::forward<Args>(args)...);
        }

        query->Begin();
        if constexpr (std::is_void_v<std::invoke_result_t<Func, Args...>>)
        {
            std::invoke(std::forward<Func>(func), std::forward<Args>(args)...);
            query->End();

            AddQuery(*key.queries_, std::move(query));
            ProcessQueries(*key.data_, *key.queries_);
        }
        else
        {
            auto result = std::invoke(std::forward<Func>(func), std::forward<Args>(args)...);
            query->End();

            AddQuery(*key.queries_, std::move(query));
            ProcessQueries(*key.data_, *key.queries_);

            return result;
        }
    }

    static void Process()
    {
        for (auto &[name, queriesData] : getQueryData())
        {
            ProcessQueries(getProfilerData()[name], queriesData);
        }

        for (auto &[name, profilerData] : getProfilerData())
        {
            auto now = std::chrono::high_resolution_clock::now();
            // Minuterie sans nouvelle mesure depuis kProfilerSaveHistoryEveryMs (passe qui ne tourne plus) : moyenne périmée, vidée.
            if (profilerData.hasSample && (now - profilerData.lastSample) > std::chrono::milliseconds(kProfilerSaveHistoryEveryMs))
            {
                profilerData.buffer.Clear();
            }
        }
    }

    static std::chrono::nanoseconds GetLastTime(const std::string &name) { return getTimer(name).Get(0); }
    static std::chrono::nanoseconds GetAverageTime(const std::string &name) { return getTimer(name).GetAverage(); }
    static std::chrono::nanoseconds GetMaxTime(const std::string &name) { return getTimer(name).GetMax(); }
    static std::chrono::nanoseconds GetMinTime(const std::string &name) { return getTimer(name).GetMin(); }
    static double GetAverageTimeMs(const std::string &name) { return static_cast<double>(GetAverageTime(name).count()) * 1e-6; }

  private:
    // Fournisseur seulement si les minuteries sont actives.
    static const IGpuTimerFactory *GetGpuTimingsEnabledFactory() noexcept { return GpuTimingsEnabled() ? GetGpuTimerFactory() : nullptr; }

    static std::unique_ptr<IGpuTimer> CreateGpuTimer(const Key &key)
    {
        const IGpuTimerFactory *factory = GetGpuTimingsEnabledFactory();
        return factory != nullptr ? factory->CreateTimer(key.name()) : nullptr;
    }

    static void ProcessQueries(ProfilerData &data, QueryData &queries)
    {
        while (!queries.queries.empty())
        {
            const auto &q = queries.queries.front();
            if (q.query != nullptr && q.query->IsReady())
            {
                AddTime(data, q.query->GetElapsedTime());
                PopQuery(queries);
            }
            else
            {
                auto now = std::chrono::high_resolution_clock::now();
                if ((now - queries.lastUpdate) >= std::chrono::milliseconds(kProfilerQueryTimeoutMs))
                {
                    PopQuery(queries);
                }
                else
                {
                    break;
                }
            }
        }
    }

    static void AddTime(ProfilerData &data, std::chrono::nanoseconds time)
    {
        std::lock_guard<std::mutex> lock(getProfilerMutex());
        data.buffer.Push(time);
        auto now = std::chrono::high_resolution_clock::now();
        data.lastSample = now;
        data.hasSample = true;
        // Un point d'historique toutes les kProfilerSaveHistoryEveryMs (durée comparée en millisecondes : la comparaison d'un
        // nombre de nanosecondes ajoutait un point à chaque mesure, sans limite de mémoire), kProfilerMaxHistory points au plus.
        if (data.history.empty() || (now - data.history.back().time) >= std::chrono::milliseconds(kProfilerSaveHistoryEveryMs))
        {
            if (data.history.size() >= kProfilerMaxHistory)
            {
                data.history.erase(data.history.begin(), data.history.begin() + static_cast<std::ptrdiff_t>(kProfilerMaxHistory / 2));
            }
            data.history.push_back({now, data.buffer.GetAverage(), data.buffer.GetMax(), data.buffer.GetMin()});
        }
    }

    static void AddQuery(QueryData &data, std::unique_ptr<IGpuTimer> query)
    {
        std::lock_guard<std::mutex> lock(getQueryMutex());
        if (data.queries.size() >= kProfilerMaxQueries)
        {
            data.queries.pop();
        }
        data.queries.push({.query = std::move(query)});
        data.lastUpdate = std::chrono::high_resolution_clock::now();
    }

    static void PopQuery(QueryData &data)
    {
        std::lock_guard<std::mutex> lock(getQueryMutex());
        data.queries.pop();
        data.lastUpdate = std::chrono::high_resolution_clock::now();
    }

    struct TimerData
    {
        std::chrono::high_resolution_clock::time_point time;
        std::chrono::nanoseconds avg;
        std::chrono::nanoseconds max;
        std::chrono::nanoseconds min;
    };

    struct ProfilerData
    {
        RingBuffer<std::chrono::nanoseconds> buffer{kProfilerBufferSize};
        std::vector<TimerData> history;
        std::chrono::high_resolution_clock::time_point lastSample{};
        bool hasSample = false;
    };

    struct QueryData
    {
        struct QueryItem
        {
            std::unique_ptr<IGpuTimer> query;
        };

        std::queue<QueryItem> queries;
        std::chrono::high_resolution_clock::time_point lastUpdate;
    };

    static std::unordered_map<std::string, ProfilerData> &getProfilerData()
    {
        static std::unordered_map<std::string, ProfilerData> profilerData;
        return profilerData;
    }

    static std::unordered_map<std::string, QueryData> &getQueryData()
    {
        static std::unordered_map<std::string, QueryData> queryData;
        return queryData;
    }

    static RingBuffer<std::chrono::nanoseconds> &getTimer(const std::string &name) { return getProfilerData()[name].buffer; }

    static std::mutex &getProfilerMutex()
    {
        static std::mutex mutex;
        return mutex;
    }

    static std::mutex &getQueryMutex()
    {
        static std::mutex mutex;
        return mutex;
    }
};

} // namespace orosmo::core
