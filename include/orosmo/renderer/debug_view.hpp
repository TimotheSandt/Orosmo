// REN-35 : modes d'affichage de debug de la scène (normales, albédo, surdessin, couleurs de LOD, filaire, vecteurs de mouvement) et
// gel du frustum de culling. `SceneRenderer::setDebugView` les active ; aucun n'est actif par défaut, et alors l'image et les cibles
// allouées sont celles d'avant (aucun coût).
#pragma once

#include "orosmo/core/error.hpp"
#include "orosmo/renderer/culling.hpp"
#include "orosmo/renderer/debug_draw.hpp"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

namespace orosmo::renderer
{

enum class DebugView : std::uint8_t
{
    kNone,
    kNormals,   // normales monde (AOV kNormals décodé) : demande l'AOV, nécessite un programme compilé avec aovShaderDefines
    kAlbedo,    // couleur de base sans éclairage (programme de remplacement)
    kOverdraw,  // nombre de fragments par pixel, en gris (1/8 par couche, 8 couches au plus), sans test de profondeur
    kLod,       // une couleur par niveau de détail : voir `lodDebugColor` et `VisibilityOptions::lodColors` (objets instanciés)
    kWireframe, // arêtes blanches sur fond sombre
    kMotion,    // vecteurs de mouvement (AOV kMotion) en couleur : 0 = gris, direction = teinte
};

std::string_view debugViewName(DebugView view) noexcept; // "none", "normals", "albedo", "overdraw", "lod", "wireframe", "motion"
std::optional<DebugView> debugViewFromName(std::string_view name) noexcept;

// Couleur du niveau de détail `level` (0 = le plus détaillé) : rouge, orange, jaune, vert, cyan, bleu, violet, puis cycle.
glm::vec3 lodDebugColor(std::uint32_t level) noexcept;

// Gel du frustum de culling : une fois gelé, `apply` renvoie la vue de culling de l'image du gel, quelle que soit la caméra ; on
// peut alors se déplacer et voir ce que la vue gelée a retenu. Ne dépend d'aucun état global.
class FrustumFreeze
{
  public:
    // `viewProj` : matrice de la même vue (sert à dessiner la pyramide gelée).
    void freeze(const CullView &view, const glm::mat4 &viewProj)
    {
        frozen_ = view;
        viewProj_ = viewProj;
    }
    void unfreeze() noexcept { frozen_.reset(); }
    // Dessine la pyramide gelée (12 arêtes) ; la profondeur reverse-Z va de 1 (plan proche) à `farDepth` (plan lointain choisi pour
    // l'affichage : une projection à plan lointain infini n'a pas de coin à l'infini). Sans effet si rien n'est gelé.
    void drawFrozen(DebugDraw &draw, const glm::vec4 &color = {1.0f, 0.2f, 0.9f, 1.0f}, float farDepth = 0.01f) const;
    bool frozen() const noexcept { return frozen_.has_value(); }
    // Vue à utiliser pour le culling : la vue gelée si elle existe, sinon `live`.
    const CullView &apply(const CullView &live) const noexcept { return frozen_ ? *frozen_ : live; }

  private:
    std::optional<CullView> frozen_;
    glm::mat4 viewProj_{1.0f};
};

} // namespace orosmo::renderer
