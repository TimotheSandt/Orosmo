// Empreinte stable (M8, A79 lot L3) : FNV-1a sur 64 bits, alimenté valeur par valeur en petit-boutiste explicite.
// Même résultat sur toutes les plateformes et tous les compilateurs pour la même suite de valeurs (règle 9) : sert à comparer
// deux états de simulation (rejeu, capture et restauration) et à figer une valeur dans un test. Pas cryptographique.
#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace orosmo::core
{

class Fnv1a
{
  public:
    static constexpr std::uint64_t kOffsetBasis = 0xcbf29ce484222325u;
    static constexpr std::uint64_t kPrime = 0x100000001b3u;

    constexpr void byte(std::uint8_t b) noexcept
    {
        hash_ ^= b;
        hash_ *= kPrime;
    }
    // Entiers : toujours 8 octets, petit-boutiste (indépendant de l'ordre des octets de la machine).
    constexpr void u64(std::uint64_t v) noexcept
    {
        for (int i = 0; i < 8; ++i)
            byte(static_cast<std::uint8_t>(v >> (8 * i)));
    }
    constexpr void i64(std::int64_t v) noexcept { u64(static_cast<std::uint64_t>(v)); }
    // Réels : motif de bits exact (-0 et +0 diffèrent), sauf NaN : tout NaN (signe et charge quelconques) est haché comme le
    // NaN silencieux canonique (0x7FC00000, 0x7FF8000000000000), celui que rend la lecture de "nan" dans une sauvegarde JSON :
    // l'empreinte ne change pas après un aller-retour par le texte.
    static constexpr std::uint32_t kCanonicalNan32 = 0x7FC00000u;
    static constexpr std::uint64_t kCanonicalNan64 = 0x7FF8000000000000u;
    constexpr void f32(float v) noexcept { u64(v != v ? kCanonicalNan32 : std::bit_cast<std::uint32_t>(v)); }
    constexpr void f64(double v) noexcept { u64(v != v ? kCanonicalNan64 : std::bit_cast<std::uint64_t>(v)); }
    // Chaîne : longueur puis octets (« ab » + « c » diffère de « a » + « bc »).
    constexpr void string(std::string_view s) noexcept
    {
        u64(s.size());
        for (char c : s)
            byte(static_cast<std::uint8_t>(c));
    }

    constexpr std::uint64_t value() const noexcept { return hash_; }

  private:
    std::uint64_t hash_ = kOffsetBasis;
};

} // namespace orosmo::core
