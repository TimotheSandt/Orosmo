// REN-34 : dessin de debug : lignes, boîtes, sphères, repères, en coordonnées monde. Les appels accumulent des segments dans un
// tampon CPU ; `SceneRenderer::drawDebug` les dessine dans la passe de scène (test de profondeur au choix) puis l'appelant vide la
// liste. Hors debug : désactivé par défaut dans les builds de release (NDEBUG) ; désactivé, chaque appel revient au premier test
// sans rien allouer ni écrire, et `drawDebug` ne fait rien.
#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <vector>

namespace engine::renderer
{

class DebugDraw
{
  public:
#ifdef NDEBUG
    static constexpr bool kEnabledByDefault = false;
#else
    static constexpr bool kEnabledByDefault = true;
#endif

    void setEnabled(bool enabled) noexcept { enabled_ = enabled; }
    bool enabled() const noexcept { return enabled_; }
    // Vrai : les segments cachés par la scène ne sont pas dessinés ; faux : toujours visibles.
    void setDepthTest(bool enabled) noexcept { depthTest_ = enabled; }
    bool depthTest() const noexcept { return depthTest_; }

    void line(const glm::vec3 &a, const glm::vec3 &b, const glm::vec4 &color);
    void box(const glm::vec3 &min, const glm::vec3 &max, const glm::vec4 &color);   // 12 arêtes
    void sphere(const glm::vec3 &center, float radius, const glm::vec4 &color, int segments = 24); // trois grands cercles
    void axes(const glm::mat4 &transform, float size);                                // X rouge, Y vert, Z bleu
    void clear() noexcept { vertices_.clear(); }

    // 7 flottants par sommet (position x, y, z, couleur r, g, b, a), 2 sommets par segment.
    static constexpr std::size_t kFloatsPerVertex = 7;
    const std::vector<float> &vertices() const noexcept { return vertices_; }
    std::size_t lineCount() const noexcept { return vertices_.size() / (2 * kFloatsPerVertex); }

  private:
    void push(const glm::vec3 &p, const glm::vec4 &c);
    bool enabled_ = kEnabledByDefault;
    bool depthTest_ = true;
    std::vector<float> vertices_;
};

} // namespace engine::renderer
