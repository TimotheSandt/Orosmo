// ECS-02 (A79 lot L2) : transformation d'une entité, composant du moteur enregistré sous le nom « Transform » par chaque
// `World`. Partie locale (relative au parent) en double : position en mètres, rotation, échelle. Partie monde (cache) calculée
// par `World::updateTransforms` seulement pour les entités marquées sales et leurs descendants ; elle n'est pas décrite par la
// réflexion (ni sauvegardée ni éditée : elle se recalcule). Les grandes coordonnées restent exactes au millimètre à 10^7 m ;
// le passage en float pour le GPU se fait relativement à une origine (la caméra), par `relativeMatrix`.
// Calcul sans fonction transcendante (produits de quaternions et de matrices) : même résultat sur toutes les plateformes avec
// `-ffp-contract=off` (règle 9, lot L3).
#pragma once

#include <orosmo/core/math_types.hpp>
#include <orosmo/core/reflection.hpp>

#include <tuple>

namespace orosmo::scene
{

struct Transform
{
    core::DVec3 position{0.0};
    core::DQuat rotation{1.0, 0.0, 0.0, 0.0};
    core::DVec3 scale{1.0};

    // Cache monde : écrit par World::updateTransforms. `worldLinear` = rotation et échelle cumulées (cisaillement compris
    // quand un parent a une échelle non uniforme).
    core::DVec3 worldPosition{0.0};
    core::DMat3 worldLinear{1.0};
};

// Matrice locale -> monde en float, translation relative à `origin` (soustraction faite en double avant l'arrondi).
core::Mat4 relativeMatrix(const Transform &transform, const core::WorldPosition &origin);
inline core::WorldPosition worldPosition(const Transform &transform) { return core::WorldPosition(transform.worldPosition); }

// Compose la partie monde de `child` à partir de celle du parent (`parent` nul : la racine, transformation identité).
void composeWorld(Transform &child, const Transform *parent);

} // namespace orosmo::scene

template <> struct orosmo::core::Reflect<orosmo::scene::Transform>
{
    static constexpr int kVersion = 1;
    static constexpr auto fields()
    {
        using T = orosmo::scene::Transform;
        return std::make_tuple(field("position", &T::position, {}, "local position relative to the parent (meters)"),
                               field("rotation", &T::rotation), field("scale", &T::scale));
    }
};
