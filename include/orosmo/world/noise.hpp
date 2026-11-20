// TER-01 : bruit procédural DÉTERMINISTE (règle 9 de CLAUDE.md, TEST-03) : Perlin, Simplex, Worley, fBm et distorsion de domaine.
//
// Mêmes octets sur tous les systèmes pour la même graine : les gradients et points caractéristiques viennent d'un hachage
// d'entiers (aucune table aléatoire, aucune horloge), les calculs n'utilisent que + - * / floor sqrt en float IEEE (ni sin, ni
// exp, ni pow de la bibliothèque, dont les résultats varient d'un système à l'autre), et le moteur compile avec
// -ffp-contract=off (pas de fusion en FMA). Domaine de validité : |coordonnée| < 2^23 (au-delà, la partie fractionnaire d'un float
// est nulle). Le même algorithme existe en GLSL (lot C) pour la génération sur GPU.
#pragma once

#include <cstdint>

namespace orosmo::world
{

// Au-delà de cette valeur absolue (ou pour NaN), une coordonnée ne peut plus être ramenée sans débordement à la grille entière :
// Perlin et Simplex rendent 0, Worley rend f1 = f2 = 0,5 (soit 0 pour la base Worley du fBm). Comportement défini et identique
// sur tous les systèmes ; une octave fBm dont la fréquence fait sortir la coordonnée de ce domaine contribue donc 0.
inline constexpr float kNoiseCoordinateLimit = 536870912.0f; // 2^29

// ── Hachage ────────────────────────────────────────────────────────────────────────────────────────────────────────

// Hachage entier 32 bits à faible biais (« lowbias32 », C. Wellons), bijectif.
constexpr std::uint32_t hashMix(std::uint32_t x) noexcept
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

constexpr std::uint32_t hash2(std::int32_t x, std::int32_t y, std::uint32_t seed) noexcept
{
    return hashMix(static_cast<std::uint32_t>(x) ^ hashMix(static_cast<std::uint32_t>(y) ^ hashMix(seed)));
}

constexpr std::uint32_t hash3(std::int32_t x, std::int32_t y, std::int32_t z, std::uint32_t seed) noexcept
{
    return hashMix(static_cast<std::uint32_t>(x) ^ hashMix(static_cast<std::uint32_t>(y) ^ hashMix(static_cast<std::uint32_t>(z) ^ hashMix(seed))));
}

// Graine dérivée (octave, axe de distorsion...) : décorrélée de la graine d'origine.
constexpr std::uint32_t deriveSeed(std::uint32_t seed, std::uint32_t salt) noexcept { return hashMix(seed ^ hashMix(salt + 0x9e3779b9U)); }

// ── Bruits de base ──────────────────────────────────────────────────────────────────────────────────────────────────

// Perlin « amélioré » à gradients unitaires. 2D : valeur dans [-1, 1]. 3D : environ [-1, 1].
float perlin2(float x, float y, std::uint32_t seed) noexcept;
float perlin3(float x, float y, float z, std::uint32_t seed) noexcept;

// Simplex (Gustavson), gradients hachés. Environ [-1, 1].
float simplex2(float x, float y, std::uint32_t seed) noexcept;
float simplex3(float x, float y, float z, std::uint32_t seed) noexcept;

// Worley (cellulaire) : distances euclidiennes au point caractéristique le plus proche (f1) et au second (f2), f1 <= f2. Un point
// par cellule unité, position hachée.
struct Worley
{
    float f1 = 0.0f;
    float f2 = 0.0f;
};
Worley worley2(float x, float y, std::uint32_t seed) noexcept;
Worley worley3(float x, float y, float z, std::uint32_t seed) noexcept;

// ── Compositions ────────────────────────────────────────────────────────────────────────────────────────────────────

enum class NoiseBasis : std::uint8_t
{
    kPerlin,
    kSimplex,
    kWorley, // 1 - 2 * min(f1, 1) : dans [-1, 1], creux aux points caractéristiques
};

struct FbmParams
{
    NoiseBasis basis = NoiseBasis::kPerlin;
    int octaves = 6;          // 1 à 16
    float frequency = 1.0f;   // de la première octave
    float lacunarity = 2.0f;  // multiplicateur de fréquence par octave
    float gain = 0.5f;        // multiplicateur d'amplitude par octave
};

// Mouvement brownien fractionnaire : somme d'octaves (chacune sa graine dérivée), normalisée par la somme des amplitudes : la
// valeur reste dans l'intervalle de la base.
float fbm2(float x, float y, std::uint32_t seed, const FbmParams &params) noexcept;
float fbm3(float x, float y, float z, std::uint32_t seed, const FbmParams &params) noexcept;

struct WarpParams
{
    float amplitude = 0.0f; // déplacement maximal (unités des coordonnées) ; 0 : pas de distorsion
    float frequency = 1.0f; // fréquence du champ de déplacement
    int octaves = 3;
};

// Distorsion de domaine : p' = p + amplitude * (fbm_a(p), fbm_b(p)), puis fbm(p'). Graines du champ dérivées de `seed`.
float warpedFbm2(float x, float y, std::uint32_t seed, const FbmParams &params, const WarpParams &warp) noexcept;

} // namespace orosmo::world
