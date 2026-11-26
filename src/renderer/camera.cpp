#include "orosmo/renderer/camera.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/rotate_vector.hpp>
#include <glm/gtx/vector_angle.hpp>

#include <cmath>
#include <cstring>

namespace orosmo::renderer
{

namespace
{
glm::vec3 normalizeOrFallback(const glm::vec3 &vector, const glm::vec3 &fallback)
{
    const float length = glm::length(vector);
    return length <= 1e-6f ? fallback : vector / length;
}
} // namespace

void Camera::setForward(const glm::vec3 &forward) { orientation_ = normalizeOrFallback(forward, orientation_); }

void Camera::rotate(const glm::vec3 &eulerDegrees)
{
    rotatePitch(eulerDegrees.x);
    rotateYaw(eulerDegrees.y);
    rotateRoll(eulerDegrees.z);
}

void Camera::rotateYaw(float degrees)
{
    orientation_ = normalizeOrFallback(glm::rotate(orientation_, glm::radians(degrees), up_), orientation_);
}

void Camera::rotatePitch(float degrees)
{
    const glm::vec3 rotated = normalizeOrFallback(glm::rotate(orientation_, glm::radians(degrees), right()), orientation_);
    const float minimumPitchRadians = glm::radians(10.0f);
    if (glm::angle(rotated, up_) > minimumPitchRadians && glm::angle(rotated, -up_) > minimumPitchRadians)
    {
        orientation_ = rotated;
    }
}

void Camera::rotateRoll(float degrees) { up_ = normalizeOrFallback(glm::rotate(up_, glm::radians(degrees), orientation_), up_); }

glm::mat4 Camera::viewMatrix() const { return glm::lookAt(position_, position_ + orientation_, up_); }

glm::mat4 Camera::relativeViewMatrix() const { return glm::lookAt(glm::vec3(0.0f), orientation_, up_); }

glm::mat4 Camera::projectionMatrix() const
{
    // Reverse-Z dans [0, 1] (repère du RHI, A26) : profondeur 1 au plan proche, 0 au plan lointain ou à l'infini ; le test de
    // profondeur est « plus grand » et l'effacement vaut 0. Bien plus précis au loin qu'une profondeur classique en float.
    glm::mat4 projection(0.0f);
    if (projection_ == Projection::kOrthographic)
    {
        const float far = std::isfinite(farPlane_) ? farPlane_ : 10000.0f;
        const float halfH = 0.5f * orthoHeight_;
        const float halfW = halfH * aspect_;
        projection = glm::orthoRH_ZO(-halfW, halfW, -halfH, halfH, far, nearPlane_); // plans inversés : reverse-Z
        projection[3][0] += jitter_.x;
        projection[3][1] += jitter_.y;
        return projection;
    }
    const float f = 1.0f / std::tan(0.5f * glm::radians(fovDegrees_));
    if (std::isfinite(farPlane_))
    {
        projection = glm::perspectiveRH_ZO(glm::radians(fovDegrees_), aspect_, farPlane_, nearPlane_); // plans inversés : reverse-Z
    }
    else
    {
        // Plan lointain infini : z_clip = proche, w_clip = -z_vue, donc profondeur = proche / distance.
        projection[0][0] = f / aspect_;
        projection[1][1] = f;
        projection[2][3] = -1.0f;
        projection[3][2] = nearPlane_;
    }
    // w_clip = -z_vue : le terme de la colonne 2 est multiplié par -w, d'où le signe moins pour décaler le NDC de +jitter (comme la
    // projection orthographique, et comme le retire `aovMotionVector`, REN-19).
    projection[2][0] -= jitter_.x;
    projection[2][1] -= jitter_.y;
    return projection;
}

CameraData Camera::data(bool relative) const
{
    CameraData out{};
    out.position[0] = relative ? 0.0f : position_.x;
    out.position[1] = relative ? 0.0f : position_.y;
    out.position[2] = relative ? 0.0f : position_.z;
    const glm::mat4 m = relative ? relativeMatrix() : matrix();
    std::memcpy(out.matrix, glm::value_ptr(m), sizeof out.matrix);
    return out;
}

} // namespace orosmo::renderer
