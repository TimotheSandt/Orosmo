// ECS-02 (A79 lot L2) : composition des transformations en double.
#include <orosmo/scene/transform.hpp>

#include <glm/gtc/quaternion.hpp>

namespace orosmo::scene
{

void composeWorld(Transform &child, const Transform *parent)
{
    core::DMat3 local = glm::mat3_cast(child.rotation);
    local[0] *= child.scale.x;
    local[1] *= child.scale.y;
    local[2] *= child.scale.z;
    if (parent == nullptr)
    {
        child.worldLinear = local;
        child.worldPosition = child.position;
        return;
    }
    child.worldLinear = parent->worldLinear * local;
    child.worldPosition = parent->worldPosition + parent->worldLinear * child.position;
}

core::Mat4 relativeMatrix(const Transform &transform, const core::WorldPosition &origin)
{
    core::Mat4 m(core::Mat3(transform.worldLinear));
    m[3] = core::Vec4(core::toLocal(worldPosition(transform), origin), 1.f);
    return m;
}

} // namespace orosmo::scene
