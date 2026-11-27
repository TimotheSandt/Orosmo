---
title: "Logique de jeu rechargeable"
description: "Écrire la logique du jeu dans src/game/, la recharger pendant que le programme tourne sans perdre l'état, migrer les composants, appeler les gestionnaires par nom, lire des paramètres JSON, puis la lier statiquement en version finale."
sidebar:
  order: 4
---

En développement, la logique du jeu est une **bibliothèque dynamique** (`bin/debug/game.so`, `.dylib` ou `.dll`) que le
programme recharge quand elle change : on modifie un fichier de `src/game/`, on reconstruit, et le comportement change sans
redémarrage ni perte d'état. En version finale (`make release`), la même logique est liée statiquement dans l'exécutable,
sans code de rechargement.

En-têtes : `orosmo/game.hpp` (en-tête léger de la bibliothèque de jeu), `orosmo/app/game_api.hpp`,
`orosmo/app/game_context.hpp`, `orosmo/app/game_module.hpp`, `orosmo/app/params_host.hpp`. Bibliothèque : `libapp.a`.

## La règle : aucun état dans la bibliothèque

Tout ce qui doit survivre à un rechargement vit **dans le moteur** : les composants (données réfléchies dans le monde), les
systèmes et les actions de l'interface, enregistrés par nom. La bibliothèque n'a ni variable globale modifiable, ni
variable `static`, ni `thread_local`. `make check` et chaque édition de liens de la bibliothèque le vérifient sur le binaire
(`tools/check_game_static.py`).

## Exemple : le gabarit d'orosmo init

Le composant, partagé par le jeu et le programme (`src/game/spin.hpp`) :

```cpp
namespace game {
struct Spin { float angleDegrees = 25.0f; float speedDegrees = 35.0f; float tiltDegrees = 18.0f; };
}
template <> struct orosmo::core::Reflect<game::Spin> {
    static constexpr auto fields() {
        return std::make_tuple(field("angleDegrees", &game::Spin::angleDegrees),
                               field("speedDegrees", &game::Spin::speedDegrees), field("tiltDegrees", &game::Spin::tiltDegrees));
    }
};
```

La logique (`src/game/game.cpp`) : un point d'entrée C, une fonction `init` qui enregistre le composant et un système.

```cpp
#include "game/spin.hpp"
#include <orosmo/game.hpp>

namespace {
using namespace orosmo;

bool init(app::GameContext *context)
{
    auto spin = context->registerComponent<game::Spin>("Spin");
    if (!spin)
        return false;
    const scene::ComponentType<game::Spin> type = *spin;
    return context->addSystem({"spin", sim::Phase::kUpdate, 0, [type](sim::StepContext &step) {
        step.world.view(type).each([&](scene::Entity, game::Spin &s) {
            s.angleDegrees += s.speedDegrees * static_cast<float>(step.dt);
        });
    }}).has_value();
}

constexpr OrosmoGameApi kApi{app::kGameAbiVersion, "game", &init, nullptr, nullptr, nullptr};
} // namespace

OROSMO_GAME_EXPORT const OrosmoGameApi *orosmoGameApi() { return &kApi; }
```

(Le fichier du gabarit écrit aussi la rotation du `Transform` à partir de l'angle.) Le programme (`src/main.cpp`) passe
`app::defaultSettings(titre)` à [`Application::main`](/guides/application/facade/) : bibliothèque chargée et surveillée en
debug, table liée avec `OROSMO_GAME_STATIC` en version finale.

```bash
orosmo run                # ou make debug && ./bin/debug/main
# programme lancé : modifiez src/game/game.cpp, puis dans un autre terminal
orosmo build --game       # ou make game : ne refait que la bibliothèque (quelques secondes)
```

Le journal affiche `game: reloaded, version 2` ; le cube garde son angle et tourne selon le nouveau code.

## Interface C

`OrosmoGameApi` (`game_api.hpp`) : `abiVersion` (`kGameAbiVersion`, lu avant tout appel), `name`, puis quatre fonctions
facultatives sauf `init` :

| Fonction | Appelée |
| --- | --- |
| `bool init(GameContext *)` | au chargement et à chaque rechargement ; rendre `false` refuse la version |
| `void update(GameContext *, double dt)` | à chaque `GameModule::update` |
| `void onReload(GameContext *)` | après l'`init` d'une version qui en remplace une autre |
| `void shutdown(GameContext *)` | à l'arrêt définitif (`GameModule::stop`, destruction), jamais au rechargement |

`GameContext` donne `world()`, `simulation()`, `actions()`, `version()` (1, puis +1 à chaque rechargement réussi),
`reloading()`, et les enregistrements par nom : `registerComponent`, `addSystem`, `addAction`, `registerParams`.

## Ce qui se passe au rechargement

1. Le fichier surveillé change ; après un délai de stabilisation (250 ms), il est copié sous un nom unique et chargé.
2. Une `abiVersion` différente est refusée (`kVersionMismatch`) : l'ancienne version continue.
3. Les systèmes et actions de l'ancienne version sont retirés, une copie du monde est prise, puis l'`init` de la nouvelle
   version est appelé.
4. Si l'`init` échoue, le monde est remis à l'identique et l'ancienne version reprend avec ses enregistrements.

| Changement du code | Effet sur l'état |
| --- | --- |
| corps d'une fonction | rien : les données restent dans le monde |
| champ ajouté à un composant | migration par réflexion : champs communs copiés, nouveau champ à sa valeur par défaut |
| champ retiré, type changé, sous-structure modifiée | repli sauvegarde/restauration par nom de champ ; un champ non convertible prend sa valeur par défaut ; un avertissement par champ |
| type que la nouvelle version n'enregistre plus | gardé avec ses données (`orphanComponents`, `removeOrphanComponents()`) |

`GameModule` expose l'état : `version()`, `failedReloads()`, `lastError()`, `lastMigrations()`, `missingActions()`.

## Gestionnaires appelés par nom

Une vue `.cui` nomme ses gestionnaires (`.onClick(startGame)`) ; le jeu les enregistre par `addAction("startGame", fn)`.
L'appel cherche le nom au moment du clic : après un rechargement, la nouvelle fonction est appelée. Un nom absent est
journalisé une fois, sans plantage, et listé par `missingActions()`.

## Paramètres JSON

```cpp
struct Tuning { float speed = 35.0f; };          // réfléchi comme un composant
auto tuning = context->registerParams<Tuning>("tuning", "params/tuning.json");   // Result<Params<Tuning>>
if (!tuning)
    return false;                                // dans init
const app::Params<Tuning> params = *tuning;      // poignée à capturer par valeur dans un système
// dans le système : params->speed
```

Le fichier est un objet JSON plat (`{"speed": 50}`), relu par le moteur à chaque `GameModule::update` quand il change, sans
compilation. Un fichier invalide est ignoré (dernière valeur valide gardée) ; un champ absent prend sa valeur par défaut.
Côté programme, `ParamsHost<T>` fait la même chose sans bibliothèque de jeu.

## Limites

- Vérifié sous Linux en local ; Windows et macOS par la CI (G310, G311).
- Un `init` qui boucle ou plante arrête le programme : pas de bac à sable (G314).
- Si un en-tête partagé avec `src/main.cpp` change la disposition d'un composant, reconstruisez tout et relancez (G331).
- Le contrôle de la règle « aucun état » ne voit que les symboles (G312).

## Voir aussi

- [Compiler avec Make ou CMake](/reference/construction/#logique-de-jeu-rechargeable) (`make game`, `ccache`, `CUI_IN_GAME`)
- [Vues rechargées à chaud](/guides/interface/rechargement-vues/)
- [Simulation à pas fixe](/guides/scene/simulation/)
