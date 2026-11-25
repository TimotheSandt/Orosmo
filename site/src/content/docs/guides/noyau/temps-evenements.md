---
title: "Temps, pas fixe et événements"
description: "Mesurer le temps d'image avec FrameTimer, faire avancer une simulation à pas fixe avec FixedStepper, tester avec ManualClock, publier des événements typés, garder les positions du monde en double."
sidebar:
  order: 4
---

Le rendu avance avec un delta variable (le temps réel entre deux images). La simulation avance par pas fixes : elle reçoit
des durées, jamais l'heure, donc une même suite de deltas donne le même résultat. Les événements typés passent par un
`EventBus`.

En-têtes : `orosmo/core/time.hpp`, `orosmo/core/events.hpp`, `orosmo/core/math.hpp`.

## Exemple minimal

```cpp
#include <orosmo/core/time.hpp>

using namespace orosmo::core;
using std::chrono::milliseconds;

SteadyClock clock;                          // horloge monotone réelle
FrameTimer timer(clock);                    // delta ramené à 250 ms au plus
FixedStepper stepper(milliseconds(10));     // simulation à 100 Hz, 8 pas au plus par image

while (running)
{
    const int steps = stepper.advance(timer.tick());      // tick() : delta de l'image, 0 au premier appel
    for (int i = 0; i < steps; ++i)
        simulate(stepper.step());                         // pas de 10 ms, toujours le même
    render(stepper.alpha());                              // fraction du pas suivant, pour interpoler
}
```

`simulate` et `render` sont les fonctions de l'application.

## Horloges

| Type | Effet |
| --- | --- |
| `IClock` | Interface : `now()` rend un instant monotone en `Nanoseconds` (`std::chrono::nanoseconds`). |
| `SteadyClock` | Horloge réelle (`std::chrono::steady_clock`). |
| `ManualClock` | Horloge avancée à la main par `advance(d)` : tests déterministes. |

## FrameTimer : delta variable

| Membre | Défaut | Effet |
| --- | --- | --- |
| `FrameTimer(clock, maxDelta)` | `maxDelta = 250 ms` | Un delta plus grand (pause du débogueur, fenêtre déplacée) est ramené à `maxDelta`. |
| `tick()` | — | Rend le delta depuis l'appel précédent ; le premier rend 0. |
| `deltaSeconds()` | — | Dernier delta en secondes (`double`). |
| `frameIndex()` | — | Nombre de `tick()`. |

L'horloge est gardée par adresse : elle doit vivre plus longtemps que le `FrameTimer`.

## FixedStepper : pas fixe

| Membre | Défaut | Effet |
| --- | --- | --- |
| `FixedStepper(step, maxSteps)` | `maxSteps = 8` | Taille du pas et nombre maximal de pas par appel. |
| `advance(frameDelta)` | — | Ajoute le delta et rend le nombre de pas à jouer. Au-delà de `maxSteps`, le temps en trop est abandonné. |
| `alpha()` | — | Fraction `[0, 1)` du prochain pas déjà écoulée. |
| `step()` | — | Taille du pas. |
| `stepCount()` | — | Pas joués depuis la création. |
| `simulationTime()` | — | `stepCount() * step()`, exact. |

Cent images de 10 ms ou cinquante images de 20 ms donnent les mêmes 100 pas et la même seconde de simulation. La limite
`maxSteps` évite qu'une image lente demande encore plus de pas à l'image suivante (« spirale de la mort »).

Pour tester une simulation sans attendre, on la pilote avec `ManualClock` (test doctest) :

```cpp
ManualClock clock;
FrameTimer timer(clock, milliseconds(100));
clock.advance(milliseconds(16));
timer.tick();                       // 0 : premier tick
clock.advance(milliseconds(16));
CHECK(timer.tick() == milliseconds(16));
```

Source : [`tests/core/time_tests.cpp`](https://github.com/TimotheSandt/Orosmo/blob/dev/tests/core/time_tests.cpp).

## Événements

`EventBus` distribue des événements d'un type C++ quelconque aux abonnés de ce type, dans l'ordre d'abonnement.

```cpp
#include <orosmo/core/events.hpp>

struct Damage { int amount; };

orosmo::core::EventBus bus;
orosmo::core::Subscription sub = bus.subscribe<Damage>([&](const Damage &d) { health -= d.amount; });
bus.publish(Damage{3});
sub.reset();   // ou destruction de `sub` : désabonnement
```

Adapté de [`tests/core/events_tests.cpp`](https://github.com/TimotheSandt/Orosmo/blob/dev/tests/core/events_tests.cpp).

| Membre | Effet |
| --- | --- |
| `subscribe<E>(handler)` | Abonne `handler(const E &)` et rend une `Subscription`. Le type `E` s'écrit explicitement. Peut allouer : à faire au chargement. |
| `publish(event)` | Appelle les abonnés du type de `event`. N'alloue pas. |
| `subscriberCount<E>()` | Abonnés actifs de ce type. |
| `Subscription::reset()`, `active()` | Désabonne ; abonnement en cours. |

Règles :

- La `Subscription` désabonne à sa destruction (RAII). Elle est marquée `[[nodiscard]]` : si on ne la garde pas,
  l'abonnement s'arrête tout de suite.
- Le bus peut être détruit avant ses abonnements : leur destruction ne fait alors plus rien.
- Pendant `publish`, un abonné peut s'abonner ou se désabonner. Un nouvel abonné ne reçoit pas l'événement en cours ; un
  désabonné ne le reçoit plus.
- `EventBus` n'est pas thread-safe : publier et s'abonner depuis le même thread.

## Positions du monde en double

Les vecteurs et matrices (`Vec3`, `Mat4`, `Quat`) sont en `float`, comme sur le GPU. Une position dans un grand monde perd
sa précision en `float` loin de l'origine ; `WorldPosition` la garde en `double`. Elle ne se convertit pas toute seule en
`Vec3`, pour qu'on n'envoie pas par erreur une position mondiale au GPU.

```cpp
#include <orosmo/core/math.hpp>

using namespace orosmo::core;

WorldPosition ship(1.0e7, 0.0, 2.5);            // à 10 000 km de l'origine
WorldPosition camera(1.0e7 - 3.0, 1.0, 2.5);
Vec3 local = toLocal(ship, camera);             // (3, -1, 0) : soustraction en double, puis arrondi en float
WorldPosition back = toWorld(camera, local);
```

| Fonction | Effet |
| --- | --- |
| `toLocal(position, origin)` | Position relative à `origin` (souvent la caméra), en `Vec3`. |
| `toWorld(origin, local)` | Inverse. |
| `narrow(DVec3)`, `widen(Vec3)` | Conversions explicites pour les directions et les matrices (`DMat4` ↔ `Mat4`). |
| `radians`, `degrees`, `lerp`, `saturate`, `approxEqual` | Aides courantes. `kPi` est défini. |

Le renderer fait le même calcul pour le dessin quand `SceneRenderer::setCameraRelative(true)` est actif.

## Limites

- Il n'y a pas de boucle de simulation intégrée au moteur (prévue avec la scène et les entités, M8). L'application écrit sa boucle avec
  `FrameTimer` et `FixedStepper`, comme ci-dessus.
- `Camera` garde sa position en `float` : le rendu relatif à la caméra ne gagne rien tant que la caméra elle-même ne porte
  pas de `double` (`src/renderer/README.md`, limites).

## Voir aussi

- [Tâches en parallèle](/guides/noyau/taches/)
- [Cadence d'images, mode sans fenêtre, captures](/guides/application/cadence-captures/)
- [Lire la boucle d'une application](/demarrer/premiere-application/)
