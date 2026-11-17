// Démo M1 : le noyau (`engine/core`) sans aucune fenêtre ni appel graphique.
//   1. lance 10 000 tâches sur le TaskSystem et mesure le débit ;
//   2. recharge à chaud un fichier de configuration modifié, sans redémarrer (FileWatcher + Config) ;
//   3. affiche les mesures (TraceRecorder) et écrit une trace Chrome (user://trace.json).
//
//   make core-demo            construit et lance (voir README du module core)
//   bin/core_demo --tasks 20000 --config.name=alpha --trace out.json
#include <engine/core/config.hpp>
#include <engine/core/events.hpp>
#include <engine/core/file_watcher.hpp>
#include <engine/core/tasks.hpp>
#include <engine/core/time.hpp>
#include <engine/core/trace.hpp>
#include <engine/core/vfs.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace engine::core;
namespace fs = std::filesystem;

namespace
{
struct ConfigReloaded
{
    std::vector<std::string> keys;
};

void writeFile(const fs::path &p, const std::string &text)
{
    std::ofstream(p, std::ios::trunc) << text;
}
} // namespace

int main(int argc, char **argv)
{
    SteadyClock clock;
    TraceRecorder trace(clock);

    // ── Configuration : défauts < fichier < ligne de commande ─────────────────
    Config config;
    config.declare("tasks", 10000, "number of tasks to run");
    config.declare("threads", 0, "worker threads (0 = hardware concurrency - 1)");
    config.declare("config.name", std::string("default"), "demo setting that is hot reloaded");
    config.declare("config.octaves", 4, "demo setting that is hot reloaded");
    config.declare("trace", std::string(""), "write a Chrome trace to this file (open it in https://ui.perfetto.dev)");
    config.declare("help", false, "show the settings");

    auto positional = config.parseArgs(argc, argv);
    if (!positional)
    {
        std::fprintf(stderr, "%s\n", positional.error().describe().c_str());
        return 2;
    }
    if (config.getOr<bool>("help", false))
    {
        std::printf("core_demo settings:\n%s", config.describe().c_str());
        return 0;
    }

    // ── 1. 10 000 tâches ──────────────────────────────────────────────────────
    const int taskCount = config.getOr<int>("tasks", 10000);
    TaskSystem tasks(static_cast<unsigned>(config.getOr<int>("threads", 0)));
    std::atomic<long> checksum{0};
    {
        ENGINE_TRACE_SCOPE(trace, "run tasks");
        for (int i = 0; i < taskCount; ++i)
            tasks.submit([&trace, &checksum, i] {
                ENGINE_TRACE_SCOPE(trace, "task");
                checksum.fetch_add(i);
            });
        tasks.waitIdle();
    }
    const long expected = static_cast<long>(taskCount) * (taskCount - 1) / 2;
    std::printf("[tasks]  %d tasks on %u threads, checksum %ld (expected %ld) -> %s\n", taskCount, tasks.threadCount(),
                checksum.load(), expected, checksum.load() == expected ? "OK" : "MISMATCH");

    // ── 2. Rechargement à chaud de la configuration ───────────────────────────
    const fs::path dir = fs::temp_directory_path() / "engine_core_demo";
    fs::create_directories(dir);
    Vfs vfs;
    vfs.mount("user", dir);
    const fs::path file = dir / "settings.json";
    writeFile(file, R"({"config":{"name":"first","octaves":4}})");

    EventBus bus;
    auto sub = bus.subscribe<ConfigReloaded>([&](const ConfigReloaded &e) {
        for (const auto &key : e.keys)
            std::printf("[reload] %-16s = %s\n", key.c_str(), config.get(key)->dump().c_str());
    });

    FileWatcher watcher;
    watcher.watch(file, [&](const FileEvent &) {
        ENGINE_TRACE_SCOPE(trace, "reload config");
        if (auto changed = config.loadFile(file))
            bus.publish(ConfigReloaded{*changed});
        else
            std::printf("[reload] failed: %s\n", changed.error().describe().c_str());
    });

    config.loadFile(file);
    std::printf("[config] name=%s octaves=%d (file read)\n", config.getOr<std::string>("config.name", "?").c_str(),
                config.getOr<int>("config.octaves", -1));

    // Simule l'éditeur : le fichier change pendant que le programme tourne, trois images plus tard il est pris en compte.
    FrameTimer frames(clock);
    FixedStepper sim(std::chrono::milliseconds(10));
    int stepsRun = 0;
    for (int frame = 0; frame < 6; ++frame)
    {
        const Nanoseconds dt = frames.tick();
        stepsRun += sim.advance(dt);
        if (frame == 2)
        {
            writeFile(file, R"({"config":{"name":"edited while running","octaves":9}})");
            fs::last_write_time(file, fs::file_time_type::clock::now() + std::chrono::seconds(1));
        }
        watcher.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(12));
    }
    std::printf("[time]   6 frames, %d fixed steps of 10 ms, simulation time %.0f ms\n", stepsRun,
                std::chrono::duration<double, std::milli>(sim.simulationTime()).count());
    fs::remove_all(dir);

    // ── 3. Mesures ────────────────────────────────────────────────────────────
    std::printf("[trace]  %-16s %8s %12s %10s\n", "scope", "count", "total (ms)", "avg (us)");
    for (const auto &s : trace.stats())
        std::printf("         %-16s %8llu %12.3f %10.2f\n", s.name.c_str(), static_cast<unsigned long long>(s.count),
                    std::chrono::duration<double, std::milli>(s.total).count(), s.averageMs() * 1000.0);

    const std::string out = config.getOr<std::string>("trace", "");
    if (!out.empty())
    {
        if (auto r = trace.writeChromeTrace(out))
            std::printf("[trace]  written to %s\n", out.c_str());
        else
            std::printf("[trace]  %s\n", r.error().describe().c_str());
    }
    return checksum.load() == expected ? 0 : 1;
}
