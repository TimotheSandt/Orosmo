---
title: "Tâches en parallèle"
description: "Lancer des tâches sur un pool de threads avec TaskSystem, exprimer des dépendances, attendre un groupe, découper une boucle avec parallelFor, et savoir ce qui peut tourner hors du thread principal."
sidebar:
  order: 3
---

`orosmo::core::TaskSystem` est un pool de threads. On lui soumet des fonctions, éventuellement avec des dépendances, et on
attend une tâche, un groupe ou toutes les tâches. `parallelFor` découpe une boucle en blocs.

En-tête : `orosmo/core/tasks.hpp`.

## Exemple minimal

```cpp title="main.cpp"
#include <orosmo/core/tasks.hpp>

#include <atomic>
#include <cstdint>
#include <cstdio>

int main()
{
    orosmo::core::TaskSystem tasks;   // hardware_concurrency() - 1 threads, au moins 1
    std::atomic<std::uint64_t> total{0};
    tasks.parallelFor(1024, 64, [&](std::size_t i) {
        total.fetch_add(i + 1, std::memory_order_relaxed);
    });
    std::printf("%llu\n", static_cast<unsigned long long>(total.load()));   // 524800
}
```

Adapté de [`tools/engine-sdk/examples/minimal/main.cpp`](https://github.com/TimotheSandt/Orosmo/blob/dev/tools/engine-sdk/examples/minimal/main.cpp).

## Soumettre et attendre

`submit(fn, dependencies)` rend un `TaskHandle`. La tâche démarre quand toutes ses dépendances sont terminées.

```cpp
using orosmo::core::TaskHandle;

TaskHandle load = tasks.submit([&] { data = readFile(); });
TaskHandle parse = tasks.submit([&] { other = parseOther(); });
const TaskHandle deps[] = {load, parse};
TaskHandle build = tasks.submit([&] { result = combine(data, other); }, deps);
tasks.wait(build);   // load et parse sont finies, build aussi
```

Adapté de [`tests/core/tasks_tests.cpp`](https://github.com/TimotheSandt/Orosmo/blob/dev/tests/core/tasks_tests.cpp).

| Membre | Effet |
| --- | --- |
| `TaskSystem(threads)` | `threads = 0` (défaut) : `std::thread::hardware_concurrency() - 1`, au moins 1. |
| `submit(fn, deps)` | Soumet `fn` ; `deps` est un `std::span<const TaskHandle>`, vide par défaut. Une dépendance déjà finie ou vide ne bloque pas. |
| `wait(task)` | Attend la tâche. Le thread appelant l'exécute lui-même si elle est prête, ou exécute d'autres tâches prêtes en attendant. |
| `waitIdle()` | Attend la fin de toutes les tâches soumises. |
| `parallelFor(count, grain, fn)` | Appelle `fn(i)` pour `i` dans `[0, count)`, par blocs de `grain`, et attend la fin. |
| `threadCount()` | Nombre de threads du pool. |
| `pendingTasks()` | Tâches soumises pas encore terminées. |
| `TaskHandle::valid()`, `done()` | Handle non vide ; tâche terminée. |

Le destructeur de `TaskSystem` termine les tâches déjà soumises, puis arrête les threads.

Comme `wait` fait travailler le thread qui attend, une tâche peut attendre une tâche fille sans bloquer le pool, même avec
un seul thread.

## Groupe de tâches

`TaskGroup` lance plusieurs tâches et les attend toutes, par `wait()` ou à sa destruction :

```cpp
{
    orosmo::core::TaskGroup group(tasks);
    for (auto &chunk : chunks)
        group.run([&chunk] { process(chunk); });
}   // le destructeur attend les tâches du groupe
```

## Une tâche ne lance pas d'exception

La règle du moteur (pas d'exception dans le flux normal) vaut dans les tâches. Si une tâche lance quand même, l'exception
est journalisée, la tâche compte comme terminée et ses dépendantes s'exécutent. Pour rendre une erreur, la tâche l'écrit
dans une variable que l'appelant lit après `wait`, par exemple un `Result<T>`.

## Ce qui peut tourner dans une tâche

Le `rhi::Device` et ses encodeurs s'utilisent depuis le thread qui les a créés. Tout ce qui reçoit un `rhi::Device &` (créer
un `Mesh`, un `RenderModel`, une texture, enregistrer des commandes) reste donc sur le thread principal.

| Objet ou fonction | Depuis une tâche |
| --- | --- |
| `TaskSystem`, `ResourceCache`, `TraceRecorder`, `Logger` | Oui, thread-safe. |
| `EventBus`, `Config`, `Vfs`, `FileWatcher`, `ArenaAllocator`, `ObjectPool` | Non : un seul propriétaire, ou un verrou extérieur. |
| `rhi::Device`, `CommandEncoder`, et tout objet qui prend un `rhi::Device &` | Non, thread du périphérique seulement. |

Un usage courant : décoder les fichiers dans des tâches (travail CPU pur), puis créer les ressources GPU sur le thread
principal une fois les tâches finies.

## Limites

- CORE-04 est Fait (test de charge de 10 000 tâches, dépendances, arrêt propre).
- G56 (`docs/GAPS.md`) : une tâche prête exécutée « par aide » dans `wait` est empilée sur l'appelant ; si elle attend
  elle-même cet appelant, le pool peut se bloquer. Il faudrait des tâches suspendables.
- Pas de priorités ni d'annulation.

## Voir aussi

- [Fichiers, ressources, rechargement](/guides/noyau/fichiers-ressources/) : chargement asynchrone par `ResourceCache`.
- [Mesurer](/guides/noyau/profilage/) : `ENGINE_TRACE_SCOPE` depuis plusieurs threads.
