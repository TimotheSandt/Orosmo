// TEST-03 : somme de contrôle des sorties de génération (docs/03 §8.4). FNV-1a 64 bits sur la représentation binaire des valeurs,
// octets pris en petit-boutiste quel que soit le système : deux machines qui produisent les mêmes floats donnent la même somme.
#pragma once

#include <bit>
#include <cstdint>
#include <span>

namespace orosmo::world
{

constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

constexpr std::uint64_t checksumBytes(std::uint64_t hash, std::uint32_t word) noexcept
{
    for (int i = 0; i < 4; ++i)
    {
        hash ^= (word >> (8 * i)) & 0xffU;
        hash *= kFnvPrime;
    }
    return hash;
}

inline std::uint64_t checksum64(std::span<const float> values, std::uint64_t hash = kFnvOffset) noexcept
{
    for (const float v : values)
    {
        hash = checksumBytes(hash, std::bit_cast<std::uint32_t>(v));
    }
    return hash;
}

inline std::uint64_t checksum64(std::span<const std::uint16_t> values, std::uint64_t hash = kFnvOffset) noexcept
{
    for (const std::uint16_t v : values)
    {
        hash ^= v & 0xffU;
        hash *= kFnvPrime;
        hash ^= (v >> 8) & 0xffU;
        hash *= kFnvPrime;
    }
    return hash;
}

} // namespace orosmo::world
