// TER-11 : export du terrain : carte de hauteur (PNG 16 bits en niveaux de gris, EXR float 32 bits), maillage (OBJ, glTF 2.0,
// STL binaire fermé, donc imprimable), carte de biomes (PNG RGB). Les fichiers sont DÉTERMINISTES : mêmes octets pour la même
// graine et les mêmes paramètres (PNG 16 bits et EXR sans compression, écrits ici octet par octet ; biomes par stb).
#pragma once

#include "orosmo/core/error.hpp"
#include "orosmo/core/reflection.hpp"
#include "orosmo/world/terrain.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace orosmo::core
{
class TaskSystem;
}

namespace orosmo::world
{

// Grille de hauteurs rectangulaire au niveau de détail 0 : l'échantillon (i, j) est l'échantillon global
// (firstX + i, firstZ + j) du terrain, à la même position que dans les tuiles (`generateTile`). Rangée par rangée (z croissant).
struct HeightField
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    float spacing = 1.0f;  // m
    float originX = 0.0f;  // position monde de l'échantillon (0, 0)
    float originZ = 0.0f;
    std::vector<float> heights;
    // Indices globaux de l'échantillon (0, 0), donnés par generateRegion : les positions exportées se recalculent alors depuis
    // l'indice global, comme les hauteurs (originX arrondi en float + i * spacing peut tomber sur une autre position).
    std::optional<std::int64_t> firstX;
    std::optional<std::int64_t> firstZ;

    float at(std::uint32_t i, std::uint32_t j) const noexcept { return heights[static_cast<std::size_t>(j) * width + i]; }
    float xAt(std::uint32_t i) const noexcept { return positionAt(firstX, originX, i); }
    float zAt(std::uint32_t j) const noexcept { return positionAt(firstZ, originZ, j); }
    float minHeight() const noexcept;
    float maxHeight() const noexcept;

private:
    float positionAt(const std::optional<std::int64_t> &first, float origin, std::uint32_t i) const noexcept
    {
        return first ? static_cast<float>(static_cast<double>(*first + i) * static_cast<double>(spacing)) : origin + static_cast<float>(i) * spacing;
    }
};

// Génère `width` x `height` échantillons à partir de l'échantillon global (firstX, firstZ). `tasks` (facultatif) parallélise par
// rangée ; le résultat ne dépend pas du nombre de threads.
HeightField generateRegion(const TerrainParams &params, std::uint32_t seed, std::int64_t firstX, std::int64_t firstZ, std::uint32_t width,
                           std::uint32_t height, core::TaskSystem *tasks = nullptr);

// ── Cartes de hauteur ───────────────────────────────────────────────────────────────────────────────────────────────

// PNG 16 bits, un canal : 0 = `minHeight`, 65535 = `maxHeight` (bornes incluses, valeurs hors bornes écrêtées).
core::Result<> writeHeightPng16(const std::filesystem::path &path, const HeightField &field, float minHeight, float maxHeight);
// EXR (OpenEXR 2, lignes, sans compression), un canal FLOAT « Y » : hauteurs en mètres, sans perte.
core::Result<> writeHeightExr(const std::filesystem::path &path, const HeightField &field);
// Relit un EXR écrit par `writeHeightExr` (un canal FLOAT, sans compression) : `heights`, `width`, `height` seulement.
core::Result<HeightField> readHeightExr(const std::filesystem::path &path);

// ── Maillages ───────────────────────────────────────────────────────────────────────────────────────────────────────

// Même triangulation que les tuiles (deux triangles par cellule, anti-horaires vus du dessus), normales par différences.
core::Result<> writeObj(const std::filesystem::path &path, const HeightField &field);
// glTF 2.0 en un seul fichier .gltf (tampon intégré en base64) : POSITION, NORMAL, indices 32 bits.
core::Result<> writeGltf(const std::filesystem::path &path, const HeightField &field);
// STL binaire FERMÉ (imprimable) : surface, quatre jupes jusqu'au socle, fond ; socle à `baseDepth` m sous le point le plus bas.
// `baseDepth` doit être > 0 et assez grand pour que le socle passe strictement sous ce point (sinon kInvalidArgument).
core::Result<> writeStl(const std::filesystem::path &path, const HeightField &field, float baseDepth);

// ── Biomes ──────────────────────────────────────────────────────────────────────────────────────────────────────────

enum class Biome : std::uint8_t
{
    kWater,
    kSand,
    kGrass,
    kRock,
    kSnow,
};

struct BiomeParams
{
    float seaLevel = 0.0f;   // m : eau en dessous
    float sandHeight = 2.0f; // m : sable jusqu'à
    float snowHeight = 90.0f;
    float rockSlope = 0.35f; // 1 - normale.y : roche au-delà
};

Biome classify(float height, float slope, const BiomeParams &params) noexcept;
// Vide si `heights` ne contient pas width x height valeurs.
std::vector<Biome> biomeMap(const HeightField &field, const BiomeParams &params);
// PNG RGB 8 bits : eau bleue, sable, herbe, roche, neige.
core::Result<> writeBiomePng(const std::filesystem::path &path, const HeightField &field, const BiomeParams &params);

} // namespace orosmo::world

template <> struct orosmo::core::Reflect<orosmo::world::BiomeParams>
{
    static constexpr int kVersion = 1;
    static constexpr auto fields()
    {
        using P = orosmo::world::BiomeParams;
        return std::make_tuple(field("seaLevel", &P::seaLevel, range(-1000.0, 1000.0), "niveau de la mer (m)"),
                               field("sandHeight", &P::sandHeight, range(-1000.0, 1000.0), "sable jusqu'à (m)"),
                               field("snowHeight", &P::snowHeight, range(-1000.0, 5000.0), "neige au-dessus (m)"),
                               field("rockSlope", &P::rockSlope, range(0.0, 1.0), "roche au-delà de cette pente"));
    }
};
