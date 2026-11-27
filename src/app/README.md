# app

Niveau `app` de `tools/layers.json` (entre `sim` et `game`, A79 choix 3, **provisoire, à confirmer par l'auteur**) : ce qui
assemble les modules pour une application. Archive : `libapp.a` (`src/app/`), API publique `include/orosmo/app/`, namespace
`orosmo::app`. Créé en M8 (A79 lot L5) avec le module de jeu ; la façade `Application` viendra au lot L13.

Dépend de `sim` (systèmes), `scene` (composants), `ui` (`ActionRegistry`), `platform` (`DynamicLibrary`) et `core`
(`FileWatcher`, erreurs, journal).

| En-tête | Rôle |
| --- | --- |
| `game_api.hpp` | Interface C : `extern "C" const OrosmoGameApi *orosmoGameApi()` (`abiVersion`, `name`, `init`, `update`, `onReload`, `shutdown`), `kGameAbiVersion`, `OROSMO_GAME_EXPORT` (exporté en développement, fonction ordinaire avec `OROSMO_GAME_STATIC`) |
| `game_context.hpp` | `GameContext` passé au jeu : `world()`, `simulation()`, `actions()`, `registerComponent` (par nom), `addSystem`, `addAction`, `version()`, `reloading()` ; `GameTargets` |
| `game_module.hpp` | `GameModule` : `load(settings)` (développement, rechargement à chaud), `fromApi(api, cibles)` (version finale statique), `update(dt)`, `reload()`, `stop()`, état (`version`, `failedReloads`, `lastError`, `systems`, `actions`, `components`, `orphanComponents`) |

## Écrire un jeu

```cpp
// game/game.cpp — compilé en bibliothèque (make / make debug) ou lié dans l'exécutable (make release, OROSMO_GAME_STATIC)
#include <orosmo/game.hpp> // en-tête léger : interface C, contexte, monde, systèmes, types mathématiques

namespace {
bool init(orosmo::app::GameContext *ctx)
{
    auto spin = ctx->registerComponent<Spin>("Spin");          // par nom ; même disposition au rechargement : données gardées
    if (!spin) return false;
    const auto type = *spin;                                   // capturé par valeur : la copie est tenue par le moteur
    return ctx->addSystem({"spin", orosmo::sim::Phase::kUpdate, 0, [type](orosmo::sim::StepContext &step) {
        step.world.view(type).each([&](orosmo::scene::Entity, Spin &s) { s.angle += s.speed * float(step.dt); });
    }}).has_value();
}
constexpr OrosmoGameApi kApi{orosmo::app::kGameAbiVersion, "my game", &init, nullptr, nullptr, nullptr};
}
OROSMO_GAME_EXPORT const OrosmoGameApi *orosmoGameApi() { return &kApi; }
```

Hôte :

```cpp
#if defined(OROSMO_GAME_STATIC)
auto game = orosmo::app::GameModule::fromApi(orosmoGameApi(), {.simulation = &sim, .actions = &ui.actions()});
#else
auto game = orosmo::app::GameModule::load({.path = exeDir / "game.so", .targets = {.simulation = &sim, .actions = &ui.actions()}});
#endif
// chaque image : game->update(dt); sim.advance(frameDelta);
```

## Règles

- **Règle 13 (GAME-02)** : la bibliothèque n'a aucun état (ni global modifiable, ni `static` local, ni `thread_local`).
  `tools/check_game_static.py` le vérifie sur le binaire (`nm`) : symboles définis en `.data`/`.bss`/`.tdata`/`.tbss` (ELF),
  `.data`/`.bss`/`.tls` (PE), `__DATA,__data|__bss|__common|__thread_*` (Mach-O) refusés ; constantes et tables en lecture seule
  (`constexpr`, `.rodata`, `.data.rel.ro`) permises ; liste blanche des symboles du démarrage C/C++ commentée dans le script (symboles de section COFF ignorés). Un binaire refusé
  est effacé par le Makefile (sinon il resterait « à jour ») ; un échec de `nm` fait échouer le contrôle.
  Lancé par `make check` (auto-test + bibliothèque de test) et après chaque édition de liens de la bibliothèque de test
  (`apps/demo/Makefile`).
- **Enregistrement par nom** : tout passe par `GameContext` ; le module note ce que chaque version a enregistré. Composants
  dans le monde (description copiée, `scene::World::registerComponent`), systèmes dans la `sim::Simulation`, actions dans
  l'`ui::ActionRegistry`. Un nom déjà pris (par l'hôte ou une autre version active) est refusé (`kAlreadyExists`).
- **Rechargement** (développement) : `update` interroge le fichier (`FileWatcher`, `pollInterval`) et recharge
  `settleDelay` après la dernière modification (l'éditeur de liens écrit en plusieurs fois). Séquence : chargement d'une copie
  sous un nom unique ; contrôle de `abiVersion` ; retrait des systèmes et actions de l'ancienne version (copies gardées) ;
  `init` de la nouvelle ; puis `onReload`. Succès : les fonctions de l'ancienne version sont détruites, PUIS sa bibliothèque est
  déchargée et sa copie effacée. Échec (fichier illisible, symbole absent, ABI, `init` faux) : les enregistrements de la nouvelle
  sont détruits, ceux de l'ancienne remis, le monde et le générateur de la simulation remis à leur état d'avant l'`init`
  (copie, G319), la nouvelle déchargée ; `failedReloads`, `lastError`, message au journal.
- **État** : les composants et leurs données restent dans le monde au rechargement et à l'arrêt (GAME-02). Un type que la
  nouvelle version n'enregistre plus est gardé (`orphanComponents`, avertissement). Changer la disposition d'un composant fait
  échouer l'`init` (kAlreadyExists) : la migration est le lot L6.
- **`shutdown`** n'est appelé qu'à l'arrêt définitif (`stop`, destruction), pas au rechargement.
- **Version finale (GAME-07)** : `fromApi` n'utilise ni `DynamicLibrary` ni `FileWatcher` ; tout le rechargement est dans
  `game_reloader.cpp`, objet à part de `libapp.a` que l'éditeur de liens ne prend que si `GameModule::load` est appelé.
  `bin/tests/test_game_static` le prouve (`nm` : ni `platform::DynamicLibrary` ni `GameModule::load`).
- **Ordre de vie** : les cibles survivent au module ; à appeler hors d'un pas de simulation.

## Liaison (A79 L1)

La bibliothèque de jeu n'embarque pas le moteur : `ENGINE_MODULE_CXXFLAGS` (`-fPIC`, `-fno-gnu-unique` avec GCC) et
`engine_module_ldflags` pour elle, `engine_host_ldflags` pour l'exécutable (`tools/engine-sdk/engine.mk`).

## Tests

`tests/app/game_module_tests.cpp` (`make demo-test-unit`, sans GPU) avec la bibliothèque `tests/app/game_module/game.cpp`
construite en quatre versions (`v1`, `v2`, `badabi`, `failinit`) ; `bin/tests/test_game_static` (version statique, lancé par
`make test`).

## Pour les lots suivants

- **L6** : migration d'un composant dont la disposition change (point d'accroche : `GameModule::Impl::switchTo`, avant
  l'`init` de la nouvelle version) ; journal de `ActionRegistry::missing()` après chaque rechargement ; décision sur
  `orphanComponents`.

## Construction incrémentale (GAME-06, L7)

- **`orosmo/game.hpp`** : seul en-tête à inclure dans la bibliothèque de jeu. Il ne tire ni `renderer`, `rhi`, `platform`,
  `ui`, ni `sim::Simulation`, ni `nlohmann/json`, ni `glm/glm.hpp` (`tools/check_headers.py`, `LIGHT_HEADERS`). Les systèmes
  viennent de `sim/system.hpp` (`StepContext::inputs` est une vue ; pour lire le contenu d'une `Input`, inclure
  `sim/simulation.hpp`, plus lourd), les types mathématiques de `core/math_types.hpp`. `game_context.hpp` n'inclut plus
  `sim/simulation.hpp` ; c'est `game_module.hpp` (côté hôte) qui le fait.
- **`make game`** (gabarit, `apps/demo/Makefile`) : ne refait que la bibliothèque ; **`orosmo build --game`** l'appelle.
  `ccache` est utilisé s'il est dans le `PATH` (`make CCACHE=` pour s'en passer).
- **Mesure** : `make e2e-init` modifie `src/game/game.cpp` d'un programme lancé (`tools/e2e/reload_host.cpp`,
  `tools/e2e_reload.py`), reconstruit et vérifie le code rechargé en moins de `E2E_RELOAD_MAX` secondes (5 par défaut) ;
  valeurs dans `docs/BASELINE/game-reload-M8-L7.txt`.

Limites : `docs/GAPS.md` (G310 et suivants).
