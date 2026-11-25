// REN-31 : choix du niveau de détail par distance, sans dépendance graphique. Autour de chaque distance de bascule, une bande de
// transition où les deux niveaux voisins sont dessinés avec des masques de tramage COMPLÉMENTAIRES (mode `Dithered` de docs/03) :
// chaque pixel où les DEUX niveaux produisent un fragment est couvert par exactement un des deux : ni trou ni double couverture
// pendant le fondu pour ces pixels seulement. Là où un seul niveau a de la géométrie (silhouettes différentes), il n'y a pas de
// garantie ; c'est le cas d'un imposteur, dont la silhouette n'est pas celle du maillage. Le dernier niveau peut être un imposteur
// (n'importe quel maillage, par exemple un quad) ; sa génération automatique n'existe pas encore.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace orosmo::renderer
{

struct LodSettings
{
    // Distances de bascule croissantes : le niveau i est utilisé jusqu'à switchDistances[i], puis le niveau i + 1. N distances pour
    // N + 1 niveaux ; vide : un seul niveau.
    std::vector<float> switchDistances;
    // Largeur (mètres) de la bande de fondu centrée sur chaque bascule ; 0 : bascule nette.
    float transitionWidth = 0.0f;
    // Distance au-delà de laquelle l'objet n'est plus dessiné (0 : jamais) ; un fondu de largeur transitionWidth le fait disparaître.
    float cullDistance = 0.0f;
    // Multiplie les distances mesurées (> 1 : niveaux grossiers plus tôt). Réglage global de qualité.
    float bias = 1.0f;
};

// Tramage par instance (shaders/rhi/mesh_instanced.vert, mesh.frag avec OROSMO_INSTANCED) : seuil s du motif de Bayer 4×4
// (valeurs n dans [0, 1)). s = 0 : pixel toujours dessiné. s > 0 : dessiné si n >= s (l'objet s'efface : couverture 1 - s).
// s < 0 : dessiné si n < -s (l'objet apparaît : couverture -s). Les seuils f et -f couvrent donc chaque pixel une fois.
struct LodDraw
{
    std::uint8_t level = 0;
    float dither = 0.0f;
};

struct LodSelection
{
    std::uint8_t count = 0; // 0 (rejeté au-delà de cullDistance), 1 (un niveau) ou 2 (fondu entre deux niveaux)
    std::array<LodDraw, 2> draws{};
};

// Niveaux disponibles : `levelCount` (>= 1) ; une distance au-delà des niveaux connus garde le dernier. Les bascules doivent être
// espacées de plus de transitionWidth (sinon la première bande l'emporte) ; dans la bande de cullDistance, seul le dernier niveau
// choisi s'efface (pas de fondu croisé simultané).
LodSelection selectLod(const LodSettings &settings, std::uint8_t levelCount, float distance) noexcept;

// Règle du shader, sur le CPU (tests) : le pixel de valeur de Bayer `n` (dans [0, 1)) est-il dessiné avec le seuil `dither` ?
bool ditherKeeps(float dither, float n) noexcept;
// Couverture (fraction des pixels dessinés) d'un seuil : 1 pour 0, 1 - s pour s > 0, -s pour s < 0.
float ditherCoverage(float dither) noexcept;

} // namespace orosmo::renderer
