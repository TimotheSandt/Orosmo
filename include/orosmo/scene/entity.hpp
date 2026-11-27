// ECS-01 (A79 lot L2) : identifiant d'entité. Un indice dans la table du `World` et une génération : détruire une entité
// incrémente la génération de son indice, ce qui invalide toutes les copies de l'ancien identifiant même si l'indice est
// réutilisé. `kNullEntity` ne désigne aucune entité.
#pragma once

#include <orosmo/core/reflection.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <tuple>

namespace orosmo::scene
{

struct Entity
{
    static constexpr std::uint32_t kInvalidIndex = 0xFFFFFFFFu;

    std::uint32_t index = kInvalidIndex;
    std::uint32_t generation = 0;

    constexpr bool isNull() const noexcept { return index == kInvalidIndex; }
    friend constexpr bool operator==(const Entity &, const Entity &) = default;
};

inline constexpr Entity kNullEntity{};

} // namespace orosmo::scene

// Réfléchi : une référence à une entité dans un composant se sauvegarde comme indice + génération.
template <> struct orosmo::core::Reflect<orosmo::scene::Entity>
{
    static constexpr int kVersion = 1;
    static constexpr auto fields()
    {
        return std::make_tuple(field("index", &orosmo::scene::Entity::index), field("generation", &orosmo::scene::Entity::generation));
    }
};

template <> struct std::hash<orosmo::scene::Entity>
{
    std::size_t operator()(const orosmo::scene::Entity &e) const noexcept
    {
        return std::hash<std::uint64_t>{}((static_cast<std::uint64_t>(e.generation) << 32u) | e.index);
    }
};
