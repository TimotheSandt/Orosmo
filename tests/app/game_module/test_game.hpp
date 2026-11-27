// Composants de la bibliothèque de jeu de test (tests/app/game_module/game.cpp), lus aussi par les tests de l'hôte.
#pragma once

#include <orosmo/core/reflection.hpp>

#include <cstdint>
#include <tuple>

namespace test_game
{
struct Counter
{
    std::int64_t value = 0;         // somme des pas (« count » : +1 en v1, +10 en v2)
    std::int64_t frames = 0;        // appels à `update`
    std::int64_t reloads = 0;       // appels à `onReload`
    std::int64_t loadedVersion = 1; // `GameContext::version()` vu par le dernier `onReload`
    std::int64_t shutdowns = 0;     // appels à `shutdown`
    std::int64_t clicked = 0;       // version qui a répondu à l'action « game.version »
};

// Version v3 (GAME-03) : `Counter` avec un champ ajouté au milieu (décalages différents), relu par la migration.
struct CounterV3
{
    std::int64_t value = 0;
    std::int64_t bonus = 42; // nouveau : valeur par défaut après migration
    std::int64_t frames = 0;
    std::int64_t reloads = 0;
    std::int64_t loadedVersion = 1;
    std::int64_t shutdowns = 0;
    std::int64_t clicked = 0;
};

// Paramètres de la v3 (GAME-05), fichier « tuning.json » : pas ajouté par « count ».
struct Tuning
{
    std::int64_t step = 100;
};

struct V1Only
{
    std::int32_t value = 7;
};
} // namespace test_game

template <> struct orosmo::core::Reflect<test_game::Counter>
{
    static constexpr auto fields()
    {
        using C = test_game::Counter;
        return std::make_tuple(field("value", &C::value), field("frames", &C::frames), field("reloads", &C::reloads),
                               field("loadedVersion", &C::loadedVersion), field("shutdowns", &C::shutdowns),
                               field("clicked", &C::clicked));
    }
};

template <> struct orosmo::core::Reflect<test_game::V1Only>
{
    static constexpr auto fields() { return std::make_tuple(field("value", &test_game::V1Only::value)); }
};

template <> struct orosmo::core::Reflect<test_game::CounterV3>
{
    static constexpr int kVersion = 2;
    static constexpr auto fields()
    {
        using C = test_game::CounterV3;
        return std::make_tuple(field("value", &C::value), field("bonus", &C::bonus), field("frames", &C::frames),
                               field("reloads", &C::reloads), field("loadedVersion", &C::loadedVersion),
                               field("shutdowns", &C::shutdowns), field("clicked", &C::clicked));
    }
};

template <> struct orosmo::core::Reflect<test_game::Tuning>
{
    static constexpr auto fields() { return std::make_tuple(field("step", &test_game::Tuning::step)); }
};
