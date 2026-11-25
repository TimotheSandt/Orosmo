---
title: Exemples
description: Le projet minimal du paquet, le projet de démarrage d'orosmo init et la démo unique du dépôt.
sidebar:
  order: 3
---

## `examples/minimal` (paquet)

Source : `tools/engine-sdk/examples/minimal/` du dépôt, compilé et lancé par `make e2e-init` en CI. Il n'utilise que le
noyau : système de tâches, réflexion et sérialisation JSON.

```cpp
#include <orosmo/core/reflection.hpp>
#include <orosmo/core/serialization.hpp>
#include <orosmo/core/tasks.hpp>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>

struct AppSummary
{
    std::string name = "minimal";
    std::uint32_t tasksCompleted = 0;
    std::uint64_t sum = 0;
};

template <> struct orosmo::core::Reflect<AppSummary>
{
    static constexpr int kVersion = 1;
    static constexpr auto fields()
    {
        return std::make_tuple(field("name", &AppSummary::name), field("tasksCompleted", &AppSummary::tasksCompleted),
                               field("sum", &AppSummary::sum));
    }
};

int main()
{
    constexpr std::size_t kCount = 1024;
    orosmo::core::TaskSystem tasks;
    std::atomic<std::uint64_t> total{0};
    tasks.parallelFor(kCount, 64, [&](std::size_t i) { total.fetch_add(static_cast<std::uint64_t>(i + 1), std::memory_order_relaxed); });

    AppSummary summary;
    summary.tasksCompleted = static_cast<std::uint32_t>(kCount);
    summary.sum = total.load(std::memory_order_relaxed);

    const std::string json = orosmo::core::dumpJson(orosmo::core::saveDocument(summary), -1);
    std::printf("%s\n", json.c_str());
}
```

Résultat : `{"data":{"name":"minimal","sum":524800,"tasksCompleted":1024},"version":1}`.

## Le projet de démarrage (`orosmo init`)

Source : `tools/engine-sdk/template/src/` (`main.cpp`, `hud_state.hpp`, `HudView.cui`). Une fenêtre, une scène 3D sur le
RHI (`SceneRenderer`, `Mesh`, `LightManager`) et un panneau `.cui` lié à une structure d'état (voir
[Le langage d'interface](/guides/interface/langage-cui/)).

## La démo unique du dépôt (`apps/demo`)

Elle rassemble la vitrine complète d'`orosmo::ui` et des fonctions du moteur : scène 3D, interface complète (onglets
widgets, contrôles, couleur, fenêtres, mise en page, thèmes), panneau de performances.

```bash
make run                                                    # fenêtre, Vulkan par défaut
apps/demo/bin/debug/main --api opengl --headless --capture /tmp/captures   # scene.png + ui.png
apps/demo/bin/debug/main --scene mixed --post full --shadows on            # transparents, post-traitements, ombres
```

Les captures de la [page d'accueil](/) viennent de cette démo.
