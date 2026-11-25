---
title: "Mesurer : profiler, traces, benchmarks"
description: "Mesurer des portions de l'image sur le CPU et le GPU avec Profiler, enregistrer une trace lisible par Perfetto avec TraceRecorder, écrire un benchmark reproductible, compter les images par seconde."
sidebar:
  order: 6
---

Le noyau a trois outils de mesure, qui ne servent pas au même moment :

- `Profiler` : moyennes glissantes par nom, image après image (panneau de performances, journal) ; CPU, ou GPU si un
  fournisseur de minuteries est installé ;
- `TraceRecorder` : chaque mesure gardée avec son début et sa durée, exportée en « Chrome trace » pour
  [Perfetto](https://ui.perfetto.dev) ou `chrome://tracing` ;
- `core::bench` : des scénarios répétés avec préchauffage, statistiques (moyenne, p95, p99) et rapport JSON.

En-têtes : `orosmo/core/profiler.hpp`, `trace.hpp`, `benchmark.hpp`, `fps_counter.hpp`.

## Exemple minimal

```cpp
#include <orosmo/core/profiler.hpp>

using orosmo::core::Profiler;

const Profiler::Key keyUpdate = Profiler::MakeKey("CPU/Update");   // une fois, à l'initialisation

// à chaque image :
Profiler::Profile(keyUpdate, [&] { update(); });

LOG_INFO("update: ", Profiler::GetAverageTimeMs("CPU/Update"), " ms");
```

Adapté de [`apps/demo/src/Game/Game.cpp`](https://github.com/TimotheSandt/Orosmo/blob/dev/apps/demo/src/Game/Game.cpp).

## Profiler

Le `Profiler` est une classe statique (pas d'objet à créer). Chaque nom a un tampon des 512 dernières mesures
(`kProfilerBufferSize`).

| Fonction | Effet |
| --- | --- |
| `MakeKey(name)` | Crée ou retrouve la clé d'un nom. À faire une fois : les appels par clé ne construisent aucune chaîne. |
| `Profile(key, fn, args...)` | Mesure `fn` sur le CPU ; rend la valeur de `fn`. Il existe aussi une surcharge par nom (`std::string`). |
| `ProfileGPU(key, fn, args...)` | Mesure sur le GPU si un fournisseur est installé et actif, sinon sur le CPU. |
| `Profile(fn, times, args...)` | Appelle `fn` `times` fois et rend la durée totale, sans rien enregistrer. |
| `GetLastTime`, `GetAverageTime`, `GetMinTime`, `GetMaxTime` | Par nom, en `std::chrono::nanoseconds`. |
| `GetAverageTimeMs(name)` | Moyenne en millisecondes (`double`). |
| `SetGpuTimerFactory(factory)` | Installe le fournisseur de minuteries GPU ; `nullptr` revient au CPU. |
| `SetGpuTimingsEnabled(bool)` | `true` par défaut. Coupées, `ProfileGPU` mesure sur le CPU et ne crée aucune requête GPU. |
| `ClearGpuQueries()` | Abandonne les mesures GPU en attente (avant de détruire le périphérique). |
| `Process()` | Lit les mesures GPU terminées et vide les noms sans mesure depuis 3 s. `Window::newFrame` l'appelle. |

Une mesure GPU n'est lue que quelques images plus tard, quand le GPU l'a terminée ; une requête jamais prête est abandonnée
après 10 s (`kProfilerQueryTimeoutMs`). Un point d'historique est gardé toutes les 3 s, 1024 au plus (`kProfilerMaxHistory`).

### Minuteries GPU

Le renderer fournit `renderer::ProfilerGpuTimerFactory`. Les minuteries écrivent leurs horodatages dans l'encodeur de
l'image courante, que l'application lui donne au début de l'image et lui retire à la fin :

```cpp
#include <orosmo/renderer/gpu_timer.hpp>

auto gpuTimers = std::make_unique<rd::ProfilerGpuTimerFactory>(**device);
Profiler::SetGpuTimerFactory(gpuTimers.get());
const Profiler::Key keyRender = Profiler::MakeKey("Render");

// image :
auto encoder = (*device)->createCommandEncoder();
gpuTimers->setEncoder(encoder.get());
Profiler::ProfileGPU(keyRender, [&] { render(*encoder); });
gpuTimers->setEncoder(nullptr);
(void)(*device)->submit(std::move(encoder));

// fin du programme, avant de détruire le périphérique :
(*device)->waitIdle();
Profiler::SetGpuTimerFactory(nullptr);
Profiler::ClearGpuQueries();
```

Sans `Capabilities::timestampQueries`, le fournisseur ne crée pas de minuterie et la mesure se fait sur le CPU. Le graphe de
passes mesure chaque passe sous le nom `RenderGraph/<passe>` (`RenderGraph/scene`, `RenderGraph/upscale`...).

## TraceRecorder : traces pour Perfetto

```cpp
#include <orosmo/core/trace.hpp>

orosmo::core::SteadyClock clock;
orosmo::core::TraceRecorder trace(clock);

{
    ENGINE_TRACE_SCOPE(trace, "frame");
    {
        ENGINE_TRACE_SCOPE(trace, "update");
        update();
    }
    render();
}

if (auto r = trace.writeChromeTrace("trace.json"); !r)
    orosmo::core::logError(r.error());
```

Ouvrir `trace.json` dans [ui.perfetto.dev](https://ui.perfetto.dev). Adapté de
[`tests/core/trace_tests.cpp`](https://github.com/TimotheSandt/Orosmo/blob/dev/tests/core/trace_tests.cpp).

| Membre | Défaut | Effet |
| --- | --- | --- |
| `TraceRecorder(clock, maxSamples)` | `maxSamples = 1 << 20` | Au-delà, les nouveaux échantillons sont comptés dans `dropped()` et pas gardés. |
| `ENGINE_TRACE_SCOPE(recorder, name)` | — | Mesure la portée courante. `name` doit vivre jusqu'à la fin de la portée (un littéral convient). |
| `setEnabled(bool)` | `true` | Désactivé, rien n'est enregistré. |
| `addSample(name, start, duration, track)` | `TraceTrack::kCpu` | Ajoute une mesure faite ailleurs, par exemple sur le GPU (`TraceTrack::kGpu`). |
| `samples()`, `stats()` | — | Copie des échantillons ; une ligne par nom triée par temps total décroissant (`count`, `total`, `min`, `max`, `averageMs()`). |
| `writeChromeTrace(path)`, `chromeTraceJson()` | — | Export : piste CPU en `pid` 1, GPU en `pid` 2, un fil par thread. |

L'enregistreur est thread-safe : des tâches peuvent mesurer en même temps. Avec un `ManualClock`, les durées sont exactes,
ce qui sert aux tests.

## Benchmarks reproductibles

Un scénario dérive de `core::bench::Scenario`. `Runner` le prépare, le préchauffe, le mesure, et rend un rapport.

```cpp
#include <orosmo/core/benchmark.hpp>

namespace bench = orosmo::core::bench;

class SortScenario final : public bench::Scenario
{
  public:
    std::string name() const override { return "sort-10k"; }
    void setup() override { data_.assign(10000, 0); }
    void runIteration() override { std::sort(data_.begin(), data_.end()); }

  private:
    std::vector<int> data_;
};

bench::ScenarioRegistry registry;
registry.registerScenario("sort-10k", [] { return std::make_unique<SortScenario>(); });

bench::BenchmarkConfig config;
config.scenarios = {{.name = "sort-10k", .measureSeconds = 2.0}};
const bench::BenchmarkReport report = bench::Runner(config).run(registry);
bench::writeReport(report, config.outputPath);   // benchmark_report.json
```

Construit d'après [`tests/core/benchmark_tests.cpp`](https://github.com/TimotheSandt/Orosmo/blob/dev/tests/core/benchmark_tests.cpp).

| Champ de `ScenarioConfig` | Défaut | Effet |
| --- | --- | --- |
| `name` | — | Nom enregistré dans le `ScenarioRegistry`. Un nom inconnu est signalé sur `stderr` et sauté. |
| `warmupSeconds` | 0,5 | Durée de préchauffage, non mesurée. |
| `measureSeconds` | 5,0 | Durée minimale de mesure. |
| `minSamples` | 100 | Itérations minimales mesurées. |
| `maxSamples` | 0 (sans limite) | Arrêt dès ce nombre d'itérations. |
| `measureSetup` | `false` | Mesure aussi `setup()` (cycles `setup` / `teardown`). |
| `setupSamples` | 30 | Nombre de cycles mesurés si `measureSetup`. |

| Champ de `BenchmarkConfig` | Défaut | Effet |
| --- | --- | --- |
| `scenarios` | vide | Scénarios lancés, dans l'ordre. |
| `outputPath` | `"benchmark_report.json"` | Chemin à passer à `writeReport`. |
| `build` | `"release"` | Recopié dans le rapport. |

Le rapport donne pour chaque scénario `Statistics` : `samples`, `totalMs`, `meanMs`, `stddevMs`, `minMs`, `maxMs`, `p50Ms`,
`p95Ms`, `p99Ms`, `throughput` (itérations par seconde), et les compteurs que `collectCounters` a ajoutés (moyenne par
itération). Rien ne doit être alloué ni chargé dans `runIteration` : c'est le rôle de `setup`.

Pour comparer OpenGL et Vulkan sur la démo, `tools/perf_compare.py` lance la démo plusieurs fois par backend et relit les
moyennes du `Profiler` dans le journal (à lancer sur un binaire release).

## FPSCounter

`FPSCounter::newFrame(maxFPS)` compte les images et, si `maxFPS > 0`, attend pour ne pas dépasser cette cadence
(`setFrameRateLimitingMode` : 0 aucun, 1 hybride par défaut, 2 adaptatif, 3 proche du vsync). `getFPS()`,
`getAverageFPS()`, `getElapseTimeInMilliseconds()` donnent les mesures. `platform::Window` en contient un : `Window::newFrame` l'appelle avec la limite
posée par `Window::setMaxFPS`. Une application passe en général par `renderer::FramePacer`
([Cadence d'images](/guides/application/cadence-captures/)).

## Limites

- Les mesures GPU du `Profiler` passent par le RHI, mais `TraceRecorder` ne les reçoit pas toutes seules
  (`addSample` avec `TraceTrack::kGpu` est à appeler à la main). Pas d'export vers Tracy.
- `bench::writeReport` lance `std::runtime_error` si le fichier ne s'ouvre pas (exception de l'ancien code).
- Le `Profiler` garde ses anciens noms (`Profile`, `GetAverageTime`) et un état global.

## Voir aussi

- [Erreurs et journal](/guides/noyau/erreurs-journal/)
- [Renderer, graphe de passes et plugins](/interne/renderer/) : mesure par passe.
- [Tests, contrôles et images de référence](/interne/tests-controles/)
