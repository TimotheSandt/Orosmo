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
// Le rayon parcourt les cellules dans son ordre ; dans chaque triangle, la hauteur au-dessus de la surface est affine le long du
// rayon et l'intersection se calcule exactement (en double) : aucune crête fine n'est franchie sans être vue. Direction nulle ou
// non finie, espacement invalide ou origine hors du domaine des indices : pas d'intersection.
// meshHeightAt rend NaN pour un espacement invalide (<= 0, non fini) ou une coordonnée hors du domaine des indices entiers.
std::optional<RayHit> raycastTerrain(const TerrainParams &params, std::uint32_t seed, core::Vec3 origin, core::Vec3 direction, float maxDistance);

} // namespace engine::world
