#include "orosmo/world/raycast.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace orosmo::world
{

namespace
{
// Au-delà, floor(x / espacement) ne tient plus dans un std::int64_t (conversion indéfinie).
constexpr double kMaxIndex = 4.0e18;

bool validSpacing(const TerrainParams &params) noexcept { return std::isfinite(params.sampleSpacing) && params.sampleSpacing > 0.0f; }

// Position (float) de l'échantillon global g du niveau 0 : la même que celle des tuiles (samplePosition).
float position(double spacing, std::int64_t g) noexcept { return static_cast<float>(static_cast<double>(g) * spacing); }

// Cellule (gi, gj) du maillage du niveau 0 : positions de ses bords et hauteurs de ses quatre coins.
struct Cell
{
    double x0, x1, z0, z1;
    double ha, hb, hc, hd; // a = (i, j), b = (i + 1, j), c = (i, j + 1), d = (i + 1, j + 1)

    // Coordonnées locales (u, v) dans [0, 1] ; une cellule dégénérée (deux bords confondus en float, très loin de l'origine)
    // reprend la coordonnée calculée sur les indices, en double.
    static double local(double p, double lo, double hi, double fallback) noexcept { return hi > lo ? (p - lo) / (hi - lo) : fallback; }

    // Hauteur du maillage : diagonale b-c, triangles a-c-b (u + v <= 1) et b-c-d.
    double height(double u, double v) const noexcept
    {
        if (u + v <= 1.0)
        {
            return ha + u * (hb - ha) + v * (hc - ha);
        }
        return hd + (1.0 - u) * (hc - hd) + (1.0 - v) * (hb - hd);
    }
};

Cell cellAt(const TerrainParams &params, std::uint32_t seed, std::int64_t gi, std::int64_t gj) noexcept
{
    const double s = static_cast<double>(params.sampleSpacing);
    const float x0 = position(s, gi), x1 = position(s, gi + 1), z0 = position(s, gj), z1 = position(s, gj + 1);
    return {x0, x1, z0, z1, heightAt(params, seed, x0, z0), heightAt(params, seed, x1, z0), heightAt(params, seed, x0, z1), heightAt(params, seed, x1, z1)};
}

} // namespace

float meshHeightAt(const TerrainParams &params, std::uint32_t seed, float x, float z) noexcept
{
    const double s = static_cast<double>(params.sampleSpacing);
    const double fi = std::floor(static_cast<double>(x) / s);
    const double fj = std::floor(static_cast<double>(z) / s);
    if (!validSpacing(params) || !(std::abs(fi) < kMaxIndex) || !(std::abs(fj) < kMaxIndex))
    {
        return std::numeric_limits<float>::quiet_NaN();
    }
    // Échantillons globaux du niveau 0 aux coins de la cellule : mêmes positions et mêmes valeurs que les tuiles.
    const auto gi = static_cast<std::int64_t>(fi);
    const auto gj = static_cast<std::int64_t>(fj);
    const Cell c = cellAt(params, seed, gi, gj);
    const double u = Cell::local(x, c.x0, c.x1, static_cast<double>(x) / s - fi);
    const double v = Cell::local(z, c.z0, c.z1, static_cast<double>(z) / s - fj);
    return static_cast<float>(c.height(u, v));
}

std::optional<RayHit> raycastTerrain(const TerrainParams &params, std::uint32_t seed, core::Vec3 origin, core::Vec3 direction, float maxDistance)
{
    if (!validSpacing(params) || !(maxDistance > 0.0f) || !std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z))
    {
        return std::nullopt;
    }
    // Normalisation sans débordement : la direction est d'abord ramenée à une composante maximale de 1.
    double dx = direction.x, dy = direction.y, dz = direction.z;
    const double largest = std::max({std::abs(dx), std::abs(dy), std::abs(dz)});
    if (!(largest > 0.0) || !std::isfinite(largest))
    {
        return std::nullopt;
    }
    dx /= largest;
    dy /= largest;
    dz /= largest;
    const double length = std::sqrt(dx * dx + dy * dy + dz * dz);
    dx /= length;
    dy /= length;
    dz /= length;
    const double ox = origin.x, oy = origin.y, oz = origin.z;
    const double s = static_cast<double>(params.sampleSpacing);
    const double limit = std::isfinite(maxDistance) ? static_cast<double>(maxDistance) : std::numeric_limits<double>::max();
    auto hit = [&](double t) { return RayHit{core::Vec3{static_cast<float>(ox + dx * t), static_cast<float>(oy + dy * t), static_cast<float>(oz + dz * t)}, static_cast<float>(t)}; };

    double fi = std::floor(ox / s), fj = std::floor(oz / s);
    if (!(std::abs(fi) < kMaxIndex) || !(std::abs(fj) < kMaxIndex))
    {
        return std::nullopt;
    }
    if (oy - static_cast<double>(meshHeightAt(params, seed, origin.x, origin.z)) <= 0.0)
    {
        return hit(0.0); // départ sous la surface
    }
    auto gi = static_cast<std::int64_t>(fi);
    auto gj = static_cast<std::int64_t>(fj);
    const std::int64_t stepI = dx > 0.0 ? 1 : -1;
    const std::int64_t stepJ = dz > 0.0 ? 1 : -1;
    const double inf = std::numeric_limits<double>::infinity();
    // Parcours des cellules dans l'ordre du rayon (DDA) : dans chaque cellule, le rayon coupe au plus la diagonale une fois, et sur
    // chaque morceau la hauteur au-dessus du triangle est AFFINE en t : le premier passage sous la surface se calcule exactement.
    // Chaque tour avance d'une cellule (indices entiers) : la boucle se termine pour toute distance finie.
    double t0 = 0.0;
    while (t0 < limit)
    {
        const Cell c = cellAt(params, seed, gi, gj);
        const double tx = dx > 0.0 ? (c.x1 - ox) / dx : (dx < 0.0 ? (c.x0 - ox) / dx : inf);
        const double tz = dz > 0.0 ? (c.z1 - oz) / dz : (dz < 0.0 ? (c.z0 - oz) / dz : inf);
        const double t1 = std::max(t0, std::min({tx, tz, limit}));
        auto uv = [&](double t)
        {
            const double x = ox + dx * t, z = oz + dz * t;
            return std::pair{Cell::local(x, c.x0, c.x1, x / s - static_cast<double>(gi)), Cell::local(z, c.z0, c.z1, z / s - static_cast<double>(gj))};
        };
        // Coupure à la diagonale (u + v = 1).
        auto diagonal = [&](double t)
        {
            const auto [u, v] = uv(t);
            return u + v - 1.0;
        };
        double cuts[3] = {t0, t1, t1};
        int pieces = 1;
        const double g0 = diagonal(t0), g1 = diagonal(t1);
        if ((g0 < 0.0 && g1 > 0.0) || (g0 > 0.0 && g1 < 0.0))
        {
            cuts[1] = t0 + (t1 - t0) * g0 / (g0 - g1);
            pieces = 2;
        }
        for (int k = 0; k < pieces; ++k)
        {
            const double a = cuts[k], b = cuts[k + 1];
            // Triangle du morceau, choisi au milieu ; hauteur au-dessus de ce plan aux deux bouts.
            const double mid = 0.5 * (a + b);
            const auto [um, vm] = uv(mid);
            const bool first = um + vm <= 1.0;
            auto above = [&](double t)
            {
                const auto [u, v] = uv(t);
                const double h = first ? c.ha + u * (c.hb - c.ha) + v * (c.hc - c.ha) : c.hd + (1.0 - u) * (c.hc - c.hd) + (1.0 - v) * (c.hb - c.hd);
                return oy + dy * t - h;
            };
            const double fa = above(a), fb = above(b);
            if (fb <= 0.0)
            {
                const double t = fa > 0.0 ? a + (b - a) * fa / (fa - fb) : a;
                return hit(std::clamp(t, a, b));
            }
        }
        if (t1 >= limit)
        {
            break;
        }
        if (tx <= tz)
        {
            gi += stepI;
        }
        else
        {
            gj += stepJ;
        }
        t0 = t1;
    }
    return std::nullopt;
}

} // namespace orosmo::world
