// Game logic (src/game/): hot reloadable during development, linked statically in the final build.
//   make debug   -> bin/debug/game.so|.dylib|.dll, loaded by the program; rebuild it while the program runs (`make debug` in
//                   another terminal) and the new code is reloaded, the cube keeps its angle.
//   make release -> compiled into the executable with OROSMO_GAME_STATIC (no hot reload code).
// Rule: no state here (no mutable global, no `static` variable, no `thread_local`): everything lives in the engine (components,
// systems and actions registered by name through the context). `tools/check_game_static.py` checks the built library.
#include "game/spin.hpp"

#include <orosmo/game.hpp> // light header (no renderer, no full glm): keeps rebuilds of this library fast

namespace
{
using namespace orosmo;

bool init(app::GameContext *context)
{
    auto spin = context->registerComponent<game::Spin>("Spin");
    if (!spin)
        return false;
    scene::World &world = context->world();
    if (world.componentEntities(spin->id).empty()) // first load only: after a reload, the cube is already in the world
        world.add(world.create(), *spin, game::Spin{});

    const scene::ComponentType<game::Spin> type = *spin;
    return context
        ->addSystem({"spin", sim::Phase::kUpdate, 0,
                     [type](sim::StepContext &step) {
                         step.world.view(type).each([&](scene::Entity, game::Spin &s) {
                             s.angleDegrees += s.speedDegrees * static_cast<float>(step.dt);
                         });
                     }})
        .has_value();
}

constexpr OrosmoGameApi kApi{app::kGameAbiVersion, "game", &init, nullptr, nullptr, nullptr};
} // namespace

OROSMO_GAME_EXPORT const OrosmoGameApi *orosmoGameApi() { return &kApi; }
