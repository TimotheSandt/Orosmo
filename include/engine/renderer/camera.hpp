// Caméra : position, orientation, projection en perspective (mêmes formules que l'ancienne `Camera`). Aucun accès au GPU, aux
// entrées ni à GLFW : le pilotage au clavier et à la souris appartient à l'application (lot C : `platform`).
#pragma once

#include <glm/glm.hpp>

namespace engine::renderer
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
    void setFarPlane(float farPlane) { farPlane_ = farPlane; }
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
    glm::mat4 projectionMatrix() const; // jitter inclus
    glm::mat4 matrix() const { return projectionMatrix() * viewMatrix(); }
    CameraData data() const;

  private:
    glm::vec3 position_{0.0f};
    glm::vec3 orientation_{0.0f, 0.0f, -1.0f};
    glm::vec3 up_{0.0f, 1.0f, 0.0f};
    float fovDegrees_ = 70.0f;
    float nearPlane_ = 0.1f;
    float farPlane_ = 1000.0f;
    float aspect_ = 16.0f / 9.0f;
    bool wireframe_ = false;
    glm::vec2 jitter_{0.0f};
};

} // namespace engine::renderer
