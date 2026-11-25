#include "orosmo/renderer/camera2d.hpp"

#include <algorithm>
#include <cmath>

namespace orosmo::renderer
{

float Camera2D::effectiveZoom() const
{
    if (!pixelPerfect_)
    {
        return zoom_;
    }
    return std::max(1.0f, std::floor(zoom_));
}

glm::vec2 Camera2D::effectiveCenter() const
{
    if (!pixelPerfect_)
    {
        return center_;
    }
    const float z = effectiveZoom();
    // Le centre de la vue est au milieu de la fenêtre : un milieu sur un demi-pixel (largeur impaire) demande un centre décalé de 1/2.
    const glm::vec2 half{(width_ % 2) * 0.5f, (height_ % 2) * 0.5f};
    return (glm::floor(center_ * z - half + 0.5f) + half) / z;
}

glm::mat4 Camera2D::matrix() const
{
    const float z = effectiveZoom();
    if (!(z > 0.0f) || width_ == 0 || height_ == 0)
    {
        return glm::mat4(0.0f);
    }
    const float c = std::cos(effectiveRotation());
    const float s = std::sin(effectiveRotation());
    const glm::vec2 center = effectiveCenter();
    const float sx = 2.0f * z / static_cast<float>(width_);
    const float sy = 2.0f * z / static_cast<float>(height_);
    // ndc = S * R(-rotation) * (world - center)
    glm::mat4 m(1.0f);
    m[0][0] = sx * c;
    m[0][1] = -sy * s;
    m[1][0] = sx * s;
    m[1][1] = sy * c;
    const glm::vec2 t = {-(c * center.x + s * center.y), -(-s * center.x + c * center.y)};
    m[3][0] = sx * t.x;
    m[3][1] = sy * t.y;
    return m;
}

glm::vec2 Camera2D::worldToScreen(const glm::vec2 &world) const
{
    const glm::vec4 ndc = matrix() * glm::vec4(world, 0.0f, 1.0f);
    return {(ndc.x * 0.5f + 0.5f) * static_cast<float>(width_), (0.5f - ndc.y * 0.5f) * static_cast<float>(height_)};
}

glm::vec2 Camera2D::screenToWorld(const glm::vec2 &pixel) const
{
    const float z = effectiveZoom();
    const glm::vec2 p{(pixel.x - static_cast<float>(width_) * 0.5f) / z, (static_cast<float>(height_) * 0.5f - pixel.y) / z};
    const float c = std::cos(effectiveRotation());
    const float s = std::sin(effectiveRotation());
    return effectiveCenter() + glm::vec2(c * p.x - s * p.y, s * p.x + c * p.y);
}

std::pair<glm::vec2, glm::vec2> Camera2D::visibleBounds() const
{
    const glm::vec2 corners[4] = {screenToWorld({0, 0}), screenToWorld({static_cast<float>(width_), 0}),
                                  screenToWorld({0, static_cast<float>(height_)}), screenToWorld({static_cast<float>(width_), static_cast<float>(height_)})};
    glm::vec2 lo = corners[0];
    glm::vec2 hi = corners[0];
    for (const glm::vec2 &p : corners)
    {
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
    }
    return {lo, hi};
}

} // namespace orosmo::renderer
