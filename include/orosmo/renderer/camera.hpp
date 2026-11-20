// Caméra (REN-02) : position, orientation, projection perspective ou orthographique en REVERSE-Z (profondeur 1 au plan proche,
// 0 au loin ; plan lointain infini par défaut en perspective), repère du RHI (A26 : profondeur dans [0, 1], NDC y = +1 en haut),
// jitter sous-pixel pour le TAA, matrice relative à la caméra (rendu relatif à la caméra). Aucun accès au GPU, aux entrées ni à
// GLFW : le pilotage appartient à l'application.
#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <limits>

namespace orosmo::renderer
{

// Contenu du tampon uniforme de la caméra (std140, 80 octets) : `shaders/rhi/mesh.vert`.
struct CameraData
{
    float position[3];
    float padding;
    float matrix[16]; // colonne par colonne (glm)
};
static_assert(sizeof(CameraData) == 80, "CameraData doit suivre std140 : vec3 + padding, puis mat4 à l'offset 16");

class Camera
{
  public:
    Camera() = default;

    void setPosition(const glm::vec3 &position) { position_ = position; }
    void setOrientation(const glm::vec3 &orientation) { orientation_ = orientation; }
    void setUp(const glm::vec3 &up) { up_ = up; }
    void setFovDegrees(float degrees) { fovDegrees_ = degrees; }
    void setNearPlane(float nearPlane) { nearPlane_ = nearPlane; }
    // Infini (défaut) : plan lointain à l'infini (perspective). Fini : reverse-Z borné. L'orthographique exige une valeur finie
    // (infini : 10 000 m).
    void setFarPlane(float farPlane) { farPlane_ = farPlane; }
    enum class Projection : std::uint8_t
    {
        kPerspective,
        kOrthographic,
    };
    void setPerspective() { projection_ = Projection::kPerspective; }
    // Hauteur visible (m) ; la largeur suit l'aspect.
    void setOrthographic(float height)
    {
        projection_ = Projection::kOrthographic;
        orthoHeight_ = height;
    }
    Projection projection() const noexcept { return projection_; }
    float orthographicHeight() const noexcept { return orthoHeight_; }
    void setAspect(float aspect) { aspect_ = aspect; }
    void setAspect(unsigned width, unsigned height) { aspect_ = height != 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f; }
    void setWireframe(bool enabled) { wireframe_ = enabled; }
    // Décalage sous-pixel ajouté à la projection quand un mode temporel est actif (coordonnées normalisées).
    void setJitter(const glm::vec2 &jitter) { jitter_ = jitter; }

    void move(const glm::vec3 &delta) { position_ += delta; }
    void moveForward(float distance) { move(orientation_ * distance); }
    void moveRight(float distance) { move(right() * distance); }
    void moveUp(float distance) { move(up_ * distance); }
    void rotate(const glm::vec3 &eulerDegrees); // tangage, lacet, roulis
    void rotateYaw(float degrees);
    void rotatePitch(float degrees); // refuse de passer à moins de 10° de la verticale
    void rotateRoll(float degrees);

    const glm::vec3 &position() const noexcept { return position_; }
    const glm::vec3 &orientation() const noexcept { return orientation_; }
    const glm::vec3 &up() const noexcept { return up_; }
    glm::vec3 right() const { return glm::normalize(glm::cross(orientation_, up_)); }
    float fovDegrees() const noexcept { return fovDegrees_; }
    float nearPlane() const noexcept { return nearPlane_; }
    float farPlane() const noexcept { return farPlane_; }
    float aspect() const noexcept { return aspect_; }
    bool wireframe() const noexcept { return wireframe_; }
    const glm::vec2 &jitter() const noexcept { return jitter_; }

    glm::mat4 viewMatrix() const;
    glm::mat4 projectionMatrix() const; // reverse-Z, profondeur [0, 1], jitter inclus
    glm::mat4 matrix() const { return projectionMatrix() * viewMatrix(); }
    // Rendu relatif à la caméra : la vue n'a pas de translation (la caméra est à l'origine) ; les objets sont placés par rapport à
    // la position de la caméra (translation = position monde - position de la caméra, calculée en double par l'appelant).
    glm::mat4 relativeViewMatrix() const;
    glm::mat4 relativeMatrix() const { return projectionMatrix() * relativeViewMatrix(); }
    // `relative` : matrice relative et position (0, 0, 0), pour des sommets déjà exprimés par rapport à la caméra.
    CameraData data(bool relative = false) const;

  private:
    glm::vec3 position_{0.0f};
    glm::vec3 orientation_{0.0f, 0.0f, -1.0f};
    glm::vec3 up_{0.0f, 1.0f, 0.0f};
    float fovDegrees_ = 70.0f;
    float nearPlane_ = 0.1f;
    float farPlane_ = std::numeric_limits<float>::infinity();
    Projection projection_ = Projection::kPerspective;
    float orthoHeight_ = 100.0f;
    float aspect_ = 16.0f / 9.0f;
    bool wireframe_ = false;
    glm::vec2 jitter_{0.0f};
};

} // namespace orosmo::renderer
