#include "engine/renderer/camera.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/rotate_vector.hpp>
#include <glm/gtx/vector_angle.hpp>

#include <cstring>

namespace engine::renderer
{

namespace
{
glm::vec3 normalizeOrFallback(const glm::vec3 &vector, const glm::vec3 &fallback)
{
    const float length = glm::length(vector);
    return length <= 1e-6f ? fallback : vector / length;
}
} // namespace

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

glm::mat4 Camera::projectionMatrix() const
{
    glm::mat4 projection = glm::perspective(glm::radians(fovDegrees_), aspect_, nearPlane_, farPlane_);
    if (jitter_.x != 0.0f || jitter_.y != 0.0f)
    {
        projection[2][0] += jitter_.x;
        projection[2][1] += jitter_.y;
    }
    return projection;
}

CameraData Camera::data() const
{
    CameraData out{};
    out.position[0] = position_.x;
    out.position[1] = position_.y;
    out.position[2] = position_.z;
    const glm::mat4 m = matrix();
    std::memcpy(out.matrix, glm::value_ptr(m), sizeof out.matrix);
    return out;
}

} // namespace engine::renderer
