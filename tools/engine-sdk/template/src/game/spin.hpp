// Game component shared by the game logic (src/game/game.cpp) and the host (src/main.cpp).
// Components are plain data (trivially copyable, reflected): the engine keeps them across hot reloads.
#pragma once

#include <orosmo/core/reflection.hpp> // light: types only

#include <tuple>

namespace game
{
struct Spin
{
    float angleDegrees = 25.0f;
    float speedDegrees = 35.0f; // degrees per second
};
} // namespace game

template <> struct orosmo::core::Reflect<game::Spin>
{
    static constexpr auto fields()
    {
        return std::make_tuple(field("angleDegrees", &game::Spin::angleDegrees),
                               field("speedDegrees", &game::Spin::speedDegrees));
    }
};
