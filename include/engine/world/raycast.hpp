// TER-13 : lancer de rayon sur le terrain, sur le PROCESSEUR et cohérent avec le rendu : la surface touchée est celle du maillage
// du niveau de détail 0 (deux triangles par cellule d'échantillons, même découpe que `buildTileMesh`), pas la fonction de bruit
// continue. Sert au picking de la souris et à la hauteur en un point.
#pragma once

#include "engine/core/math.hpp"
#include "engine/world/terrain.hpp"

#include <optional>

namespace engine::world
{

// Hauteur de la surface maillée (niveau 0) en (x, z) : interpolation dans le triangle de la cellule qui contient le point.
float meshHeightAt(const TerrainParams &params, std::uint32_t seed, float x, float z) noexcept;

struct RayHit
{
    core::Vec3 position{0.0f};
    float distance = 0.0f; // le long du rayon (direction normalisée), mètres
};

// Premier point où le rayon passe sous la surface maillée, à moins de `maxDistance` (m). Origine sous la surface : touché en 0.
// Le rayon avance par pas d'un quart d'espacement, puis le point est affiné par dichotomie (précision ~1e-4 de l'espacement).
std::optional<RayHit> raycastTerrain(const TerrainParams &params, std::uint32_t seed, core::Vec3 origin, core::Vec3 direction, float maxDistance);

} // namespace engine::world
