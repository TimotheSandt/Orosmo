// Caméra 2D (R2D-02) : projection orthographique pour le dessin 2D du monde. Repère du MONDE : X vers la droite, Y vers le HAUT ;
// repère de l'ÉCRAN (pixels) : origine en haut à gauche, Y vers le bas. `zoom` = pixels d'écran par unité de monde.
// Mode pixel parfait : échelle entière (jamais sous 1), centre aligné sur la grille des pixels, rotation ignorée ; le dessinateur
// (`Renderer2D`) y ajoute le filtrage au plus proche et l'alignement des sprites. Aucun accès au GPU, aux entrées ni à GLFW.
#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <utility>

namespace orosmo::renderer
{

class Camera2D
{
  public:
    Camera2D() = default;

    void setCenter(const glm::vec2 &center) { center_ = center; }
    void setZoom(float pixelsPerUnit) { zoom_ = pixelsPerUnit; }
    void setRotation(float radians) { rotation_ = radians; } // sens trigonométrique ; ignorée en mode pixel parfait
    void setViewport(std::uint32_t width, std::uint32_t height)
    {
        width_ = width;
        height_ = height;
    }
    void setPixelPerfect(bool enabled) { pixelPerfect_ = enabled; }
    void move(const glm::vec2 &delta) { center_ += delta; }

    const glm::vec2 &center() const noexcept { return center_; }
    float zoom() const noexcept { return zoom_; }
    float rotation() const noexcept { return rotation_; }
    std::uint32_t viewportWidth() const noexcept { return width_; }
    std::uint32_t viewportHeight() const noexcept { return height_; }
    bool pixelPerfect() const noexcept { return pixelPerfect_; }

    // Valeurs réellement utilisées : en mode pixel parfait, échelle = partie entière du zoom (au moins 1), centre aligné pour qu'un
    // point du monde à coordonnées multiples de 1/échelle tombe sur un bord de pixel, rotation nulle.
    float effectiveZoom() const;
    glm::vec2 effectiveCenter() const;
    float effectiveRotation() const noexcept { return pixelPerfect_ ? 0.0f : rotation_; }

    // Monde -> coordonnées normalisées du RHI (A26 : x et y dans [-1, 1], y = +1 en haut). Zoom <= 0 ou vue vide : matrice nulle.
    glm::mat4 matrix() const;
    glm::vec2 worldToScreen(const glm::vec2 &world) const; // pixels, origine en haut à gauche
    glm::vec2 screenToWorld(const glm::vec2 &pixel) const;
    // Rectangle de monde visible sans rotation : (min, max) ; avec rotation : sa boîte englobante.
    std::pair<glm::vec2, glm::vec2> visibleBounds() const;

  private:
    glm::vec2 center_{0.0f};
    float zoom_ = 1.0f;
    float rotation_ = 0.0f;
    std::uint32_t width_ = 1;
    std::uint32_t height_ = 1;
    bool pixelPerfect_ = false;
};

} // namespace orosmo::renderer
