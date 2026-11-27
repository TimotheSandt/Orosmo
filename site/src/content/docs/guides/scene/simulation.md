---
title: "Simulation à pas fixe"
description: "Faire avancer un monde avec sim::Simulation : systèmes par nom, entrées datées par pas, capture, sauvegarde et rejeu avec la même graine, et thread de simulation optionnel à cadence réglable."
sidebar:
  order: 3
---

`sim::Simulation` fait avancer un `scene::World` par **pas fixes**. Elle possède le monde, un générateur pseudo-aléatoire
(`core::Random`, PCG32) et la liste des systèmes. Même graine, mêmes systèmes et mêmes entrées donnent le même état, au bit
près, sur toutes les plateformes : une partie se rejoue.

En-têtes : `orosmo/sim/simulation.hpp`, `orosmo/sim/simulation_thread.hpp`. Bibliothèque : `libsim.a` (dépend de `scene`
et de `core`). La façade [`app::Application`](/guides/application/facade/) en crée une (`a.simulation()`) et l'avance à
chaque image.

## Exemple minimal

```cpp
#include <orosmo/sim/simulation.hpp>
namespace sim = orosmo::sim;

sim::Simulation simulation({.step = std::chrono::milliseconds(10), .seed = 42});
auto velocity = simulation.world().registerComponent<Velocity>("Velocity");
if (!velocity)
    return std::unexpected(velocity.error());
const auto type = *velocity;
auto added = simulation.addSystem({"move", sim::Phase::kUpdate, 0, [type](sim::StepContext &ctx) {
    ctx.world.view(ctx.world.transformType(), type).each([&](auto e, orosmo::scene::Transform &t, Velocity &v) {
        t.position += orosmo::core::widen(v.value) * ctx.dt;
        ctx.world.markTransformDirty(e);
    });
}});
if (!added)
    return std::unexpected(added.error());

if (auto queued = simulation.queueInput({.step = 30, .name = "jump", .data = 2.5}); !queued)   // livrée au pas 30
    return std::unexpected(queued.error());
simulation.advance(frameDelta);     // 0 à maxStepsPerFrame pas, selon le temps cumulé
```

## Pas fixe

| Réglage (`SimulationSettings`) | Défaut | Rôle |
| --- | --- | --- |
| `step` | 16,67 ms (60 Hz) | durée d'un pas |
| `maxStepsPerFrame` | 8 | plafond de pas par `advance` (le surplus est abandonné) |
| `seed`, `stream` | 0, flux par défaut | graine du générateur |
| `scratchBytes` | 64 Kio | arène remise à zéro au début de chaque pas (`ctx.scratch`) |

- `advance(delta)` joue autant de pas que le temps cumulé le permet (`core::FixedStepper`) et rend leur nombre ; `alpha()`
  donne la fraction du pas suivant déjà écoulée, pour interpoler l'affichage.
- `step()` et `run(n)` jouent des pas sans horloge (tests, programme sans fenêtre).
- Après chaque pas, `World::updateTransforms()` est appelé.

## Systèmes

Un système est une fonction enregistrée **par nom** (`SystemDesc` : nom, phase, ordre, fonction). Ils s'exécutent par phase
(`kInput`, `kUpdate`, `kPostUpdate`), puis par ordre, puis par nom : l'ordre ne dépend pas de celui des enregistrements.
`StepContext` donne au système le monde, le pas (`dt`, numéro), le générateur `random` (le seul hasard permis), les
entrées du pas (`inputs`), `emit(nom, données)` pour publier un événement repris par `takeEvents()`, et l'arène `scratch`.

Pendant un pas, on ne peut ni ajouter ni retirer de système, ni rejouer un pas imbriqué (refusé et journalisé). Un système
qui lève une exception ne laisse pas la simulation bloquée dans un pas.

## Entrées datées

Les entrées du joueur sont datées par **numéro de pas** (`Input` : pas, nom, données JSON) et livrées dans l'ordre d'ajout.
Une entrée d'un pas déjà joué est refusée. Les données ne contiennent ni infini ni NaN. Toutes les entrées livrées sont
gardées dans `inputLog()` : c'est ce qui rend la partie rejouable.

## Capturer, sauvegarder, rejouer

| Appel | Effet |
| --- | --- |
| `capture()` / `restore(snapshot)` | copie profonde en mémoire : monde, générateur, pas, entrées en attente |
| `saveState()` / `loadState(json)` | le même état en JSON (scène par [scene_file](/guides/scene/sauvegarde/)), tout ou rien |
| `recording(premierPas)` | `Replay` : graine, pas et entrées, à rejouer avec `play` ; `replayToJson`, `replayFromJson` |
| `fingerprint()` | empreinte du monde, du générateur, du pas et des entrées en attente |

Les systèmes ne font pas partie de l'état : après un `loadState`, ils doivent être enregistrés comme au départ.

## Déterminisme

Pour que la même graine donne le même résultat partout (règle 9 du projet), un système n'utilise ni horloge, ni `rand()`,
ni fonction transcendante (`sin`, `exp`, `pow` diffèrent d'une bibliothèque C à l'autre), et le code est compilé avec
`-ffp-contract=off` (fait par le moteur et le gabarit). `make check` cherche ces appels dans `src/sim`, `src/scene` et le
scénario de la démo.

La démo le montre sans fenêtre ni GPU :

```bash
apps/demo/bin/debug/main --sim-agents 500 --seed 42 --sim-steps 1000   # affiche l'empreinte ; deux lancements : même valeur
```

## Thread de simulation

Optionnel (le mono-thread reste le défaut) : `SimulationThread::start(simulation, horloge, réglages)` fait tourner la
simulation sur un thread dédié. Entre `start` et `stop`, seul ce thread touche la simulation.

| Cadence (`Cadence`) | Fonctionnement |
| --- | --- |
| `kStepsPerFrame` | le rendu appelle `frame()` à chaque image : N pas par image, une image de latence au plus |
| `kFixedRate` | le thread lit l'horloge et joue à fréquence fixe (par exemple 20, 50 ou 120 Hz), indépendante du rendu |

Les réglages se lisent aussi dans `core::Config` (`declareSimulationConfig`, `readSimulationConfig`) : `sim.thread`,
`sim.cadence` (`rate` ou `frame`), `sim.rateHz`, `sim.stepsPerFrame`.

```cpp
orosmo::core::SteadyClock clock;
auto thread = sim::SimulationThread::start(simulation, clock, {.cadence = sim::Cadence::kFixedRate,
                                                               .renderComponents = {"Tint"}});
if (!thread)
    return std::unexpected(thread.error());
(*thread)->pushInput({.step = (*thread)->stepIndex() + 2, .name = "jump", .data = 2.5});  // daté avec une marge
const sim::RenderSnapshot &snapshot = (*thread)->acquire();   // inchangé jusqu'au prochain acquire
const double alpha = snapshot.alpha(clock.now());
for (std::size_t i = 0; i < snapshot.entities.size(); ++i)
    draw(sim::relativeMatrix(snapshot.transformAt(i, alpha), cameraPosition));
while (auto event = (*thread)->popEvent())
    play(*event);
(*thread)->stop();                                            // la simulation revient au thread appelant
```

Le seul point d'échange est un **instantané immuable** (`RenderSnapshot`, publié sans verrou après chaque lot de pas) : les
transformations du pas courant et du pas d'avant (interpolées par `transformAt`) et les composants listés dans
`renderComponents`. Les entrées vont vers la simulation et les événements en reviennent par des files à un producteur et
un consommateur (`core::SpscQueue`) ; aucun événement n'est perdu. Même graine et mêmes entrées : même empreinte qu'en
mono-thread. Ce fonctionnement est vérifié sous ThreadSanitizer en CI.

## Limites

- Pendant que le thread tourne, ni ajout de système, ni capture, ni rejeu : `stop()` puis un nouveau `start` (G300).
- L'instantané copie toutes les transformations à chaque lot de pas (G301) ; une entrée arrivée après son pas est
  seulement comptée et refusée (G304).
- La façade `Application` utilise la simulation mono-thread et lit le monde directement, sans instantané (G356) ; la démo
  n'a pas d'option pour le thread (G305).
- L'empreinte figée de la démo n'est vérifiée que sous Linux x64 en local ; Windows et macOS par la CI (G280).

## Voir aussi

- [Temps, pas fixe et événements](/guides/noyau/temps-evenements/)
- [Logique de jeu rechargeable](/guides/scene/logique-rechargeable/)
- [Options de la démo](/reference/options-demo/#simulation)
