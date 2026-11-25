#include "orosmo/anim/pose.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <string>

namespace orosmo::anim
{

using core::ErrorCode;
using core::makeError;

core::Mat4 Transform::matrix() const
{
    return glm::translate(core::Mat4(1.0f), translation) * glm::mat4_cast(rotation) * glm::scale(core::Mat4(1.0f), scale);
}

Pose restPose(const assets::Model &model)
{
    Pose pose;
    pose.nodes.reserve(model.nodes.size());
    for (const assets::NodeData &n : model.nodes)
    {
        pose.nodes.push_back({n.translation, n.rotation, n.scale});
    }
    return pose;
}

std::vector<int> nodeParents(const assets::Model &model)
{
    std::vector<int> parents;
    parents.reserve(model.nodes.size());
    for (const assets::NodeData &n : model.nodes)
    {
        parents.push_back(n.parent);
    }
    return parents;
}

core::Result<std::vector<core::Mat4>> worldMatrices(std::span<const int> parents, const Pose &pose)
{
    if (parents.size() != pose.nodes.size())
    {
        return makeError(ErrorCode::kInvalidArgument, "worldMatrices: " + std::to_string(parents.size()) + " parents for " +
                                                          std::to_string(pose.nodes.size()) + " pose nodes");
    }
    const std::size_t count = parents.size();
    std::vector<core::Mat4> world(count, core::Mat4(1.0f));
    std::vector<char> done(count, 0);
    std::vector<int> chain;
    for (std::size_t i = 0; i < count; ++i)
    {
        chain.clear();
        int n = static_cast<int>(i);
        while (n >= 0 && static_cast<std::size_t>(n) < count && done[static_cast<std::size_t>(n)] == 0 && chain.size() <= count)
        {
            chain.push_back(n);
            n = parents[static_cast<std::size_t>(n)];
        }
        for (auto it = chain.rbegin(); it != chain.rend(); ++it)
        {
            const auto idx = static_cast<std::size_t>(*it);
            const int parent = parents[idx];
            const core::Mat4 local = pose.nodes[idx].matrix();
            world[idx] = parent >= 0 && static_cast<std::size_t>(parent) < count ? world[static_cast<std::size_t>(parent)] * local : local;
            done[idx] = 1;
        }
    }
    return world;
}

Transform blend(const Transform &a, const Transform &b, float weight)
{
    return {glm::mix(a.translation, b.translation, weight), glm::normalize(glm::slerp(a.rotation, b.rotation, weight)),
            glm::mix(a.scale, b.scale, weight)};
}

namespace
{
core::Result<> checkSizes(const char *what, std::size_t a, std::size_t b, std::span<const float> mask)
{
    if (a != b || (!mask.empty() && mask.size() != a))
    {
        return makeError(ErrorCode::kInvalidArgument, std::string(what) + ": poses and mask must have the same node count");
    }
    return {};
}

float maskAt(std::span<const float> mask, std::size_t i) { return mask.empty() ? 1.0f : std::clamp(mask[i], 0.0f, 1.0f); }
} // namespace

core::Result<Pose> blendPoses(const Pose &a, const Pose &b, float weight, std::span<const float> mask)
{
    if (auto r = checkSizes("blendPoses", a.nodes.size(), b.nodes.size(), mask); !r)
    {
        return std::unexpected(r.error());
    }
    Pose out;
    out.nodes.resize(a.nodes.size());
    for (std::size_t i = 0; i < a.nodes.size(); ++i)
    {
        out.nodes[i] = blend(a.nodes[i], b.nodes[i], weight * maskAt(mask, i));
    }
    return out;
}

core::Result<Pose> additiveDelta(const Pose &pose, const Pose &reference)
{
    if (auto r = checkSizes("additiveDelta", pose.nodes.size(), reference.nodes.size(), {}); !r)
    {
        return std::unexpected(r.error());
    }
    Pose delta;
    delta.nodes.resize(pose.nodes.size());
    for (std::size_t i = 0; i < pose.nodes.size(); ++i)
    {
        const Transform &p = pose.nodes[i];
        const Transform &r = reference.nodes[i];
        delta.nodes[i] = {p.translation - r.translation, glm::normalize(glm::inverse(r.rotation) * p.rotation), p.scale / r.scale};
    }
    return delta;
}

core::Result<Pose> applyAdditive(const Pose &base, const Pose &delta, float weight, std::span<const float> mask)
{
    if (auto r = checkSizes("applyAdditive", base.nodes.size(), delta.nodes.size(), mask); !r)
    {
        return std::unexpected(r.error());
    }
    Pose out;
    out.nodes.resize(base.nodes.size());
    const core::Quat identity(1.0f, 0.0f, 0.0f, 0.0f);
    for (std::size_t i = 0; i < base.nodes.size(); ++i)
    {
        const float w = weight * maskAt(mask, i);
        const Transform &b = base.nodes[i];
        const Transform &d = delta.nodes[i];
        out.nodes[i] = {b.translation + d.translation * w, glm::normalize(b.rotation * glm::slerp(identity, d.rotation, w)),
                        b.scale * glm::mix(core::Vec3(1.0f), d.scale, w)};
    }
    return out;
}

} // namespace orosmo::anim
