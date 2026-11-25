// ANIM-02 : poses. Une pose est la transformation locale (TRS) de CHAQUE nœud d'un modèle (`assets::Model::nodes`), dans le même
// ordre : les clips l'écrivent, le mélange et les couches additives la combinent, `worldMatrices` la compose (parent avant enfant).
// Tout est déterministe (aucune horloge, aucun aléa).
#pragma once

#include "orosmo/assets/model.hpp"
#include "orosmo/core/error.hpp"
#include "orosmo/core/math.hpp"

#include <span>
#include <vector>

namespace orosmo::anim
{

struct Transform
{
    core::Vec3 translation{0.0f};
    core::Quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    core::Vec3 scale{1.0f};

    core::Mat4 matrix() const;
    friend bool operator==(const Transform &, const Transform &) = default;
};

struct Pose
{
    std::vector<Transform> nodes;
};

// Pose de repos : les TRS des nœuds du fichier.
Pose restPose(const assets::Model &model);
// Parent de chaque nœud (-1 : racine), pour `worldMatrices` sans garder le modèle.
std::vector<int> nodeParents(const assets::Model &model);
// Matrices monde de la pose (parent avant enfant, sans récursion). kInvalidArgument si les tailles diffèrent.
core::Result<std::vector<core::Mat4>> worldMatrices(std::span<const int> parents, const Pose &pose);

// Mélange de deux transformations : translation et échelle linéaires, rotation par slerp (chemin le plus court).
Transform blend(const Transform &a, const Transform &b, float weight);
// Mélange nœud par nœud ; `mask` (facultatif, un poids par nœud dans [0, 1]) multiplie `weight` : un masque à 0 garde `a`.
core::Result<Pose> blendPoses(const Pose &a, const Pose &b, float weight, std::span<const float> mask = {});

// Couches additives : `additiveDelta` est la différence de `pose` à `reference` (translation - réf., réf.⁻¹ × rotation, échelle /
// réf.) ; `applyAdditive` l'ajoute à `base` avec un poids (0 : `base` inchangée, 1 : delta entier), masque facultatif par nœud.
core::Result<Pose> additiveDelta(const Pose &pose, const Pose &reference);
core::Result<Pose> applyAdditive(const Pose &base, const Pose &delta, float weight, std::span<const float> mask = {});

} // namespace orosmo::anim
