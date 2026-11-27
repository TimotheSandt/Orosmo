#include "Game/debug_world.hpp"

#include <glm/geometric.hpp>

#include <cmath>

namespace demo
{

orosmo::core::Result<> DebugWorld::init()
{
    auto type = world_.registerComponent<DemoCamera>("DemoCamera");
    if (!type)
    {
        return std::unexpected(type.error());
    }
    camera_ = *type;
    cameraEntity_ = world_.create();
    world_.add(cameraEntity_, camera_, DemoCamera{});
    return {};
}

void DebugWorld::pull(const orosmo::renderer::Camera &camera)
{
    DemoCamera *c = world_.get(cameraEntity_, camera_);
    if (c == nullptr)
    {
        return;
    }
    c->position = camera.position();
    c->direction = camera.orientation();
    pulled_ = *c;
}

bool DebugWorld::push(orosmo::renderer::Camera &camera) const
{
    const DemoCamera *c = world_.get(cameraEntity_, camera_);
    if (c == nullptr)
    {
        return false;
    }
    bool changed = false;
    if (c->position != pulled_.position)
    {
        camera.setPosition(c->position);
        changed = true;
    }
    // Refusées : direction nulle (ou non finie) et direction parallèle à `up` (vue indéfinie, voir `Camera::setForward`).
    const float length = glm::length(c->direction);
    if (c->direction != pulled_.direction && length > 1.0e-6f && std::isfinite(length))
    {
        const glm::vec3 direction = c->direction / length;
        if (glm::length(glm::cross(direction, glm::normalize(camera.up()))) > 1.0e-4f)
        {
            camera.setOrientation(direction);
            changed = true;
        }
    }
    return changed;
}

} // namespace demo
