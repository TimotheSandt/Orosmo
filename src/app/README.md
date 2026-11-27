# app

Niveau `app` de `tools/layers.json` (entre `sim` et `game`, A79 choix 3, **provisoire, à confirmer par l'auteur**) : ce qui
assemble les modules pour une application. Archive : `libapp.a` (`src/app/`), API publique `include/orosmo/app/`, namespace
`orosmo::app`. Créé en M8 (A79 lot L5) avec le module de jeu ; façade `Application` et rendu des composants de la scène au
lot L13.

Dépend de `sim` (systèmes), `scene` (composants), `ui` (`ActionRegistry`, `Ui`, police), `anim` (`AnimatedModel`,
`CrowdRenderer`), `assets` (`ModelLoader`), `renderer` (scène, 2D, lumières, ombres, tonemapping, instanciation), `rhi`
(périphérique), `platform` (`Window`, `DynamicLibrary`) et `core` (`FileWatcher`, `FrameTimer`, `Handle`, erreurs, journal).

| En-tête | Rôle |
| --- | --- |
| `game_api.hpp` | Interface C : `extern "C" const OrosmoGameApi *orosmoGameApi()` (`abiVersion`, `name`, `init`, `update`, `onReload`, `shutdown`), `kGameAbiVersion`, `OROSMO_GAME_EXPORT` (exporté en développement, fonction ordinaire avec `OROSMO_GAME_STATIC`) |
| `game_context.hpp` | `GameContext` passé au jeu : `world()`, `simulation()`, `actions()`, `registerComponent` (par nom), `addSystem`, `addAction`, `version()`, `reloading()` ; `GameTargets` |
| `application.hpp` | **L13** `Application` (A75.1) : `create(settings)` (fenêtre, périphérique avec repli Vulkan -> OpenGL, contexte de rendu, `SceneRenderer`, `CanvasRenderer`, ombres et HDR facultatifs, police et `Ui`, `Simulation`, jeu facultatif), `main(argc, argv, settings, setup)` (programme complet), `run`, `frame`, rappels `onUpdate`, `onUi`, `onRender` avec `FrameTime` (delta borné, pas fixe, pas joués : reste d'A75.9), `spawn(composant, transform)`, ressources (`addMesh`, `loadAnimatedModel`, `addAnimatedModel`), accès bas niveau ; `ApplicationSettings`, `parseArguments`, `usage`, `GameSource` (`fromApi`, `fromLibrary`) |
| `default_game.hpp` | `defaultGame()` : table liée (`OROSMO_GAME_STATIC`) ou bibliothèque `game` rechargée ; à inclure par le seul fichier qui crée l'application |
| `render_components.hpp` | **L13** composants de rendu définis par le moteur : `MeshRenderer`, `LightComponent`, `CameraComponent`, `AnimatedModelComponent`, mode de dessin `DrawMode` (seul, instancié, foule : A73 point 3) ; ressources par `core::Handle<T>` ; réflexion |
| `resource_pool.hpp` | `ResourcePool<T>` : objets possédés désignés par `core::Handle<T>` (génération : une poignée retirée ne retombe jamais sur un autre objet) |
| `scene_render_system.hpp` | `SceneRenderSystem` : `update` (transformations, lecteurs, lumières), `applyCamera`, `dispatch` (skinning hors passe, A75.2), `draw` ; `SceneResources`, `registerRenderComponents`, `SceneRenderStats` |
| `game_module.hpp` | `GameModule` : `load(settings)` (développement, rechargement à chaud), `fromApi(api, cibles)` (version finale statique), `update(dt)`, `reload()`, `stop()`, état (`version`, `failedReloads`, `lastError`, `systems`, `actions`, `components`, `orphanComponents`) |

## Façade d'application (L13)

```cpp
#include <orosmo/app/application.hpp>
namespace app = orosmo::app;

int main(int argc, char **argv)
{
    return app::Application::main(argc, argv, {.title = "Cube"}, [](app::Application &a) -> orosmo::core::Result<> {
        auto cube = a.addMesh(orosmo::renderer::MeshBuilder(orosmo::renderer::MeshLayout::kColored).addBox({1, 1, 1}));
        if (!cube) return std::unexpected(cube.error());
        const auto e = a.spawn(app::MeshRenderer{.mesh = *cube});
        a.spawn(app::LightComponent{.type = orosmo::renderer::LightType::kAmbient, .intensity = 0.3f});
        a.spawn(app::LightComponent{.direction = {-0.6f, -0.8f, -0.5f}});
        a.onUpdate([e](app::Application &self, const app::FrameTime &t) {
            self.world().editTransform(e)->rotation *= glm::dquat(glm::dvec3(0.0, t.dt, 0.0));
        });
        return {};
    });
}
```

Ordre d'une image : événements, temps (delta borné ou pas fixe sans fenêtre), jeu (`GameModule::update`), simulation
(`advance`), `onUpdate`, `Ui::update`, `onUi`, système de rendu (`update`, caméra active, `dispatch`), `beginFrame`,
`setCamera`, dessins des composants, `onRender`, `endFrame`, interface, présentation. Options de la ligne de commande :
`--api`, `--strict-backend`, `--headless`, `--size`, `--frames`, `--capture`, `--capture-frames` (`usage`).

Arrêt et erreurs : une erreur de `beginFrame` ou `present` (perte du périphérique) fait échouer `run` (kBusy toléré) ; la
limite implicite sans fenêtre ne coupe pas une capture, et un `--frames` plus petit que `--capture-frames` est un échec. Les
rappels et leurs captures sont libérés avant le module de jeu et le périphérique.

Modes de dessin : `MeshRenderer` `kSingle` (un `drawMesh` par entité) ou `kInstanced` (un dessin instancié par maillage) ;
`AnimatedModelComponent` `kSingle` (lecteur du modèle, avancé une fois par image) ou `kCrowd` (une foule par modèle, clip et
temps par entité, avancés par le jeu). Limites : G350 à G356.

## Écrire un jeu

```cpp
// game/game.cpp — compilé en bibliothèque (make / make debug) ou lié dans l'exécutable (make release, OROSMO_GAME_STATIC)
#include <orosmo/app/game_api.hpp>
#include <orosmo/app/game_context.hpp>

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

`tests/app/application_tests.cpp` : suite `app/application` (sans GPU : réserve, ligne de commande, composants sauvegardés) et
`app/application-gpu` (application sans fenêtre = même scène à la main, repli Vulkan -> OpenGL, ordre des rappels et temps,
modes de dessin, ombres et HDR, capture ; ignorée sans périphérique). `tests/app/game_module_tests.cpp` (`make demo-test-unit`, sans GPU) avec la bibliothèque `tests/app/game_module/game.cpp`
construite en quatre versions (`v1`, `v2`, `badabi`, `failinit`) ; `bin/tests/test_game_static` (version statique, lancé par
`make test`).

## Pour les lots suivants

- **L6** : migration d'un composant dont la disposition change (point d'accroche : `GameModule::Impl::switchTo`, avant
  l'`init` de la nouvelle version) ; journal de `ActionRegistry::missing()` après chaque rechargement ; décision sur
  `orphanComponents`.
- **L14** : gabarit d'`orosmo init` et tutoriel réécrits avec `Application::main`, `spawn`, `onUpdate` et `defaultGame()`.
- **L7** : cible qui ne reconstruit que la bibliothèque de jeu, en-tête léger `orosmo/game.hpp` (`game_context.hpp` inclut
  aujourd'hui `sim/simulation.hpp`, donc `scene` et la sérialisation).

Limites : `docs/GAPS.md` (G310 et suivants).
