#include "Game/debug_world.hpp"

#include <glm/geometric.hpp>

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
    if (c->direction != pulled_.direction && glm::length(c->direction) > 1.0e-6f)
    {
        camera.setOrientation(glm::normalize(c->direction));
        changed = true;
    }
    return changed;
}

} // namespace demo
