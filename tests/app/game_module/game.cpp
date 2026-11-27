// Bibliothèque de jeu de test de `app::GameModule` (GAME-01, GAME-02, GAME-07 ; A79 lot L5). Construite par `make test` en
// plusieurs versions du MÊME fichier (apps/demo/Makefile, `bin/tests/test_game_<variante><ext>`), qui ne diffèrent que par le
// corps des fonctions :
//   v1       : système « count » qui ajoute 1, système « v1.only », action « game.v1 », composant « V1Only » ;
//   v2       : « count » ajoute 10, système « v2.only », action « game.v2 » ; plus de « V1Only » ;
//   badabi   : comme v1, mais `abiVersion` différente (refusée) ;
//   failinit : enregistre un système puis rend faux à `init` (l'ancienne version doit rester active).
// Avec OROSMO_GAME_STATIC (v1), le même fichier est lié dans `bin/tests/test_game_static` (version finale).
//
// Règle 13 : aucun état ici (ni global modifiable, ni `static` local, ni `thread_local`) : les identifiants de composants sont
// capturés par valeur dans les systèmes (copies tenues par le moteur) ou retrouvés par nom. `tools/check_game_static.py` le
// vérifie sur le binaire ; les constantes ci-dessous (`constexpr`) sont en lecture seule.
#include "app/game_module/test_game.hpp"

#include <orosmo/core/log.hpp>
#include <orosmo/game.hpp> // GAME-06 : l'en-tête léger suffit à écrire un jeu
#include <orosmo/ui/ui.hpp>

#include <cstdint>

#if defined(OROSMO_TEST_GAME_VARIANT_v2)
#define TEST_GAME_VERSION 2
#else
#define TEST_GAME_VERSION 1
#endif

namespace
{
using namespace orosmo;
using test_game::Counter;

constexpr std::int64_t kIncrement = TEST_GAME_VERSION == 2 ? 10 : 1;
constexpr const char *kOnlySystem = TEST_GAME_VERSION == 2 ? "v2.only" : "v1.only";
constexpr const char *kOwnAction = TEST_GAME_VERSION == 2 ? "game.v2" : "game.v1";

bool init(app::GameContext *context)
{
    auto counter = context->registerComponent<Counter>("Counter");
    if (!counter)
    {
        LOG_WARNING("test game: " + counter.error().describe());
        return false;
    }
#if TEST_GAME_VERSION == 1
    if (!context->registerComponent<test_game::V1Only>("V1Only"))
        return false;
#endif

    // État initial créé une seule fois : au rechargement, les entités sont déjà dans le monde (côté moteur).
    scene::World &world = context->world();
    if (world.componentEntities(counter->id).empty())
        world.add(world.create(), *counter, Counter{});

    const scene::ComponentType<Counter> type = *counter;
    if (!context->addSystem({"count", sim::Phase::kUpdate, 0, [type](sim::StepContext &step) {
                                 step.world.view(type).each([](scene::Entity, Counter &c) { c.value += kIncrement; });
                             }}))
        return false;
    if (!context->addSystem({kOnlySystem, sim::Phase::kPostUpdate, 0, [](sim::StepContext &) {}}))
        return false;
    if (!context->addAction(kOwnAction, [](const ui::Event &) {}))
        return false;
    if (!context->addAction("game.version", [world = &world, type](const ui::Event &) {
            world->view(type).each([](scene::Entity, Counter &c) { c.clicked = TEST_GAME_VERSION; });
        }))
        return false;

#if defined(OROSMO_TEST_GAME_VARIANT_failinit)
    (void)context->addSystem({"failinit.system", sim::Phase::kUpdate, 0, [](sim::StepContext &) {}});
    return false;
#else
    return true;
#endif
}

void update(app::GameContext *context, double)
{
    auto type = context->world().componentType<Counter>("Counter");
    if (type)
        context->world().view(*type).each([](scene::Entity, Counter &c) { ++c.frames; });
}

void onReload(app::GameContext *context)
{
    auto type = context->world().componentType<Counter>("Counter");
    if (type)
        context->world().view(*type).each([&](scene::Entity, Counter &c) {
            ++c.reloads;
            c.loadedVersion = static_cast<std::int64_t>(context->version());
        });
}

void shutdown(app::GameContext *context)
{
    auto type = context->world().componentType<Counter>("Counter");
    if (type)
        context->world().view(*type).each([](scene::Entity, Counter &c) { c.shutdowns += 1; });
}

#if defined(OROSMO_TEST_GAME_VARIANT_badabi)
constexpr std::uint32_t kAbi = app::kGameAbiVersion + 100;
#else
constexpr std::uint32_t kAbi = app::kGameAbiVersion;
#endif

constexpr OrosmoGameApi kApi{kAbi, "test game", &init, &update, &onReload, &shutdown};
} // namespace

OROSMO_GAME_EXPORT const OrosmoGameApi *orosmoGameApi() { return &kApi; }
