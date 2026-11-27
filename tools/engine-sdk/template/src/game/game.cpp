// Game logic (src/game/): hot reloadable during development, linked statically in the final build.
//   make debug   -> bin/debug/game.so|.dylib|.dll, loaded by the program; rebuild it while the program runs (`make debug` in
//                   another terminal) and the new code is reloaded, the cube keeps its angle.
//   make release -> compiled into the executable with OROSMO_GAME_STATIC (no hot reload code).
// Rule: no state here (no mutable global, no `static` variable, no `thread_local`): everything lives in the engine (components,
// systems and actions registered by name through the context). `tools/check_game_static.py` checks the built library.
#include "game/spin.hpp"

#include <orosmo/game.hpp> // light header (no renderer, no full glm): keeps rebuilds of this library fast

#include <cmath>

namespace
{
using namespace orosmo;

// Tilted by `tiltDegrees` around X, then turned by `angleDegrees` around Y.
core::DQuat spinRotation(const game::Spin &s)
{
    constexpr double kHalfRadiansPerDegree = 3.14159265358979323846 / 360.0;
    const double yaw = s.angleDegrees * kHalfRadiansPerDegree;
    const double tilt = s.tiltDegrees * kHalfRadiansPerDegree;
    return core::DQuat(std::cos(yaw), 0.0, std::sin(yaw), 0.0) * core::DQuat(std::cos(tilt), std::sin(tilt), 0.0, 0.0);
}

// Every entity with a Spin component turns (src/main.cpp gives one to the cube); its data stays in the world across reloads.
bool init(app::GameContext *context)
{
    auto spin = context->registerComponent<game::Spin>("Spin");
    if (!spin)
        return false;
    const scene::ComponentType<game::Spin> type = *spin;
    return context
        ->addSystem({"spin", sim::Phase::kUpdate, 0,
                     [type](sim::StepContext &step) {
                         step.world.view(type).each([&](scene::Entity entity, game::Spin &s) {
                             s.angleDegrees += s.speedDegrees * static_cast<float>(step.dt);
                             if (scene::Transform *transform = step.world.editTransform(entity))
                                 transform->rotation = spinRotation(s);
                         });
                     }})
        .has_value();
}

constexpr OrosmoGameApi kApi{app::kGameAbiVersion, "game", &init, nullptr, nullptr, nullptr};
} // namespace

OROSMO_GAME_EXPORT const OrosmoGameApi *orosmoGameApi() { return &kApi; }
