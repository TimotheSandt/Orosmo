// Générateur pseudo-aléatoire du cœur (M8, A79 lot L1) : PCG32 (O'Neill, variante XSH RR, état de 64 bits et flux choisi).
// Même suite sur toutes les plateformes et tous les compilateurs pour la même graine (règle 9 : seuls des entiers de largeur fixe,
// aucun `rand()`, aucune horloge, aucune distribution de la bibliothèque standard, dont les algorithmes varient d'une
// implémentation à l'autre). L'initialisation suit `pcg32_srandom_r` : `Random(42, 54)` donne la suite de l'exemple de
// référence `pcg32-demo` (0xa15c02b7, 0x7b47f409, …), figée par `tests/core/random_tests.cpp`.
#pragma once

#include <bit>
#include <cstdint>

namespace orosmo::core
{

class Random
{
  public:
    // Flux par défaut : celui de l'exemple de référence de PCG32. Deux flux différents donnent deux suites indépendantes.
    static constexpr std::uint64_t kDefaultStream = 54u;

    constexpr explicit Random(std::uint64_t seed = 0, std::uint64_t stream = kDefaultStream) noexcept { reseed(seed, stream); }

    constexpr void reseed(std::uint64_t seed, std::uint64_t stream = kDefaultStream) noexcept
    {
        state_ = 0u;
        increment_ = (stream << 1u) | 1u;
        nextU32();
        state_ += seed;
        nextU32();
    }

    // 32 bits uniformes.
    constexpr std::uint32_t nextU32() noexcept
    {
        const std::uint64_t old = state_;
        state_ = old * kMultiplier + increment_;
        const auto xorShifted = static_cast<std::uint32_t>(((old >> 18u) ^ old) >> 27u);
        const auto rotation = static_cast<std::uint32_t>(old >> 59u);
        return (xorShifted >> rotation) | (xorShifted << ((0u - rotation) & 31u));
    }

    // 64 bits : deux tirages, le premier en poids fort.
    constexpr std::uint64_t nextU64() noexcept
    {
        const std::uint64_t high = nextU32();
        return (high << 32u) | nextU32();
    }

    // Entier uniforme dans [0, bound), sans biais (rejet de `pcg32_boundedrand_r`). `bound == 0` renvoie 0 sans tirer.
    constexpr std::uint32_t nextBelow(std::uint32_t bound) noexcept
    {
        if (bound == 0u)
            return 0u;
        const std::uint32_t threshold = (0u - bound) % bound;
        for (;;)
        {
            const std::uint32_t r = nextU32();
            if (r >= threshold)
                return r % bound;
        }
    }

    // Entier uniforme dans [low, high] (bornes comprises ; `low > high` : les bornes sont échangées).
    constexpr std::int32_t range(std::int32_t low, std::int32_t high) noexcept
    {
        if (low > high)
        {
            const std::int32_t t = low;
            low = high;
            high = t;
        }
        const auto span = static_cast<std::uint64_t>(static_cast<std::int64_t>(high) - low) + 1u;
        if (span > 0xffffffffu) // tout l'intervalle des int32 : un tirage brut
            return static_cast<std::int32_t>(static_cast<std::int64_t>(nextU32()) + INT32_MIN);
        return static_cast<std::int32_t>(low + static_cast<std::int64_t>(nextBelow(static_cast<std::uint32_t>(span))));
    }

    // Réel uniforme dans [0, 1) : 24 bits (float) ou 53 bits (double), conversion exacte, donc identique partout.
    constexpr float nextFloat() noexcept { return static_cast<float>(nextU32() >> 8u) * 0x1.0p-24f; }
    constexpr double nextDouble() noexcept { return static_cast<double>(nextU64() >> 11u) * 0x1.0p-53; }

    // Réel uniforme dans [low, high) (un tirage). Calcul en double : aucune différence de bornes finies ne déborde ; un résultat
    // que l'arrondi vers float amène sur `high` est ramené au float précédent. `low >= high` (ou NaN) renvoie `low`.
    constexpr float range(float low, float high) noexcept
    {
        const float t = nextFloat();
        if (!(low < high))
            return low;
        const double value = static_cast<double>(low) + (static_cast<double>(high) - static_cast<double>(low)) * t;
        const auto result = static_cast<float>(value);
        return result < high ? result : floatBelow(high);
    }

    // Avance de `delta` tirages en O(log delta) (`pcg32_advance_r`) : sauter à un pas donné d'une simulation rejouée.
    constexpr void advance(std::uint64_t delta) noexcept
    {
        std::uint64_t accMult = 1u;
        std::uint64_t accPlus = 0u;
        std::uint64_t curMult = kMultiplier;
        std::uint64_t curPlus = increment_;
        while (delta > 0u)
        {
            if ((delta & 1u) != 0u)
            {
                accMult *= curMult;
                accPlus = accPlus * curMult + curPlus;
            }
            curPlus = (curMult + 1u) * curPlus;
            curMult *= curMult;
            delta >>= 1u;
        }
        state_ = accMult * state_ + accPlus;
    }

    // État complet (sauvegarde d'une partie, rejouabilité) : `Random::fromState(r.state(), r.increment())` reprend la suite.
    constexpr std::uint64_t state() const noexcept { return state_; }
    constexpr std::uint64_t increment() const noexcept { return increment_; }
    static constexpr Random fromState(std::uint64_t state, std::uint64_t increment) noexcept
    {
        Random r;
        r.state_ = state;
        r.increment_ = increment | 1u;
        return r;
    }

    friend constexpr bool operator==(const Random &, const Random &) noexcept = default;

  private:
    // Plus grand float strictement inférieur à `x` (fini, > plus petit float) : un pas sur la représentation.
    static constexpr float floatBelow(float x) noexcept
    {
        if (x == 0.0f)
            return -0x1.0p-149f;
        const auto bits = std::bit_cast<std::uint32_t>(x);
        return std::bit_cast<float>(x > 0.0f ? bits - 1u : bits + 1u);
    }

    static constexpr std::uint64_t kMultiplier = 6364136223846793005u;
    std::uint64_t state_ = 0u;
    std::uint64_t increment_ = 1u;
};

} // namespace orosmo::core
