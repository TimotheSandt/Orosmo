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

  public:
    static void Profile(const std::string &name, const std::function<void()> &func)
    {
        auto start = std::chrono::high_resolution_clock::now();
        func();
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        AddTime(name, duration);
    }

    template <typename Func, typename... Args>
    static auto Profile(const std::string &name, Func &&func, Args &&...args) -> std::invoke_result_t<Func, Args...>
    {
        auto start = std::chrono::high_resolution_clock::now();

        if constexpr (std::is_same_v<std::invoke_result_t<Func, Args...>, void>)
        {
            std::invoke(std::forward<Func>(func), std::forward<Args>(args)...);
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
            AddTime(name, duration);
        }
        else
        {
            auto result = std::invoke(std::forward<Func>(func), std::forward<Args>(args)...);
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
            AddTime(name, duration);

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

    static void ProfileGPU(const std::string &name, const std::function<void()> &func)
    {
        std::unique_ptr<IGpuTimer> query = CreateGpuTimer(name);
        if (query == nullptr)
        {
            Profile(name, func);
            return;
        }

        query->Begin();
        func();
        query->End();

        AddQuery(name, std::move(query));

        ProcessQueries(name);
    }

    template <typename Func, typename... Args>
    static auto ProfileGPU(const std::string &name, Func &&func, Args &&...args) -> std::invoke_result_t<Func, Args...>
    {
        std::unique_ptr<IGpuTimer> query = CreateGpuTimer(name);
        if (query == nullptr)
        {
            return Profile(name, std::forward<Func>(func), std::forward<Args>(args)...);
        }

        query->Begin();
        if constexpr (std::is_void_v<std::invoke_result_t<Func, Args...>>)
        {
            std::invoke(std::forward<Func>(func), std::forward<Args>(args)...);
            query->End();

            AddQuery(name, std::move(query));
            ProcessQueries(name);
        }
        else
        {
            auto result = std::invoke(std::forward<Func>(func), std::forward<Args>(args)...);
            query->End();

            AddQuery(name, std::move(query));
            ProcessQueries(name);

            return result;
        }
    }

    static void Process()
    {
        for (auto &[name, queriesData] : getQueryData())
        {
            ProcessQueries(name);
        }

        for (auto &[name, profilerData] : getProfilerData())
        {
            auto now = std::chrono::high_resolution_clock::now();
            if (profilerData.history.size() != 0 &&
                (now - profilerData.history.back().time) > std::chrono::milliseconds(kProfilerSaveHistoryEveryMs))
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
    static std::unique_ptr<IGpuTimer> CreateGpuTimer(const std::string &name)
    {
        const IGpuTimerFactory *factory = GetGpuTimerFactory();
        return factory != nullptr ? factory->CreateTimer(name) : nullptr;
    }

    static void ProcessQueries(const std::string &name)
    {
        while (!getQueries(name).empty())
        {
            const auto &q = getQueries(name).front();
            if (q.query != nullptr && q.query->IsReady())
            {
                AddTime(name, q.query->GetElapsedTime());
                PopQuery(name);
            }
            else
            {
                auto now = std::chrono::high_resolution_clock::now();
                if ((now - getQueryData()[name].lastUpdate) >= std::chrono::milliseconds(kProfilerQueryTimeoutMs))
                {
                    PopQuery(name);
                }
                else
                {
                    break;
                }
            }
        }
    }

    static void AddTime(const std::string &name, std::chrono::nanoseconds time)
    {
        std::lock_guard<std::mutex> lock(getProfilerMutex());
        getTimer(name).Push(time);
        auto now = std::chrono::high_resolution_clock::now();
        if (getProfilerData()[name].history.size() == 0 ||
            (now - getProfilerData()[name].history.back().time).count() >= kProfilerSaveHistoryEveryMs)
        {
            getProfilerData()[name].history.push_back({now, getTimer(name).GetAverage(), getTimer(name).GetMax(), getTimer(name).GetMin()});
        }
    }

    static void AddQuery(const std::string &name, std::unique_ptr<IGpuTimer> query)
    {
        std::lock_guard<std::mutex> lock(getQueryMutex());
        if (getQueries(name).size() >= kProfilerMaxQueries)
        {
            getQueries(name).pop();
        }
        getQueries(name).push({.query = std::move(query)});
        getQueryData()[name].lastUpdate = std::chrono::high_resolution_clock::now();
    }

    static void PopQuery(const std::string &name)
    {
        std::lock_guard<std::mutex> lock(getQueryMutex());
        getQueries(name).pop();
        getQueryData()[name].lastUpdate = std::chrono::high_resolution_clock::now();
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

    using QueryItem = QueryData::QueryItem;

    static std::queue<QueryItem> &getQueries(const std::string &name) { return getQueryData()[name].queries; }

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
