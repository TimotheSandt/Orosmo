#include "engine/renderer/debug_draw.hpp"

#include <algorithm>
#include <cmath>

namespace engine::renderer
{

void DebugDraw::push(const glm::vec3 &p, const glm::vec4 &c)
{
    const float v[kFloatsPerVertex] = {p.x, p.y, p.z, c.r, c.g, c.b, c.a};
    vertices_.insert(vertices_.end(), v, v + kFloatsPerVertex);
}

void DebugDraw::line(const glm::vec3 &a, const glm::vec3 &b, const glm::vec4 &color)
{
    if (!enabled_)
    {
        return;
    }
    push(a, color);
    push(b, color);
}

void DebugDraw::box(const glm::vec3 &min, const glm::vec3 &max, const glm::vec4 &color)
{
    if (!enabled_)
    {
        return;
    }
    const glm::vec3 c[8] = {{min.x, min.y, min.z}, {max.x, min.y, min.z}, {max.x, max.y, min.z}, {min.x, max.y, min.z},
                            {min.x, min.y, max.z}, {max.x, min.y, max.z}, {max.x, max.y, max.z}, {min.x, max.y, max.z}};
    const int edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (const auto &e : edges)
    {
        line(c[e[0]], c[e[1]], color);
    }
}

void DebugDraw::sphere(const glm::vec3 &center, float radius, const glm::vec4 &color, int segments)
{
    if (!enabled_)
    {
        return;
    }
    segments = std::max(segments, 3);
    constexpr float kTau = 6.28318530717958648f;
    for (int axis = 0; axis < 3; ++axis)
    {
        auto point = [&](int i)
        {
            const float t = kTau * static_cast<float>(i) / static_cast<float>(segments);
            const float a = radius * std::cos(t);
            const float b = radius * std::sin(t);
            return axis == 0 ? center + glm::vec3(0.0f, a, b) : axis == 1 ? center + glm::vec3(a, 0.0f, b) : center + glm::vec3(a, b, 0.0f);
        };
        for (int i = 0; i < segments; ++i)
        {
            line(point(i), point(i + 1), color);
        }
    }
}

void DebugDraw::axes(const glm::mat4 &transform, float size)
{
    if (!enabled_)
    {
        return;
    }
    const glm::vec3 o(transform * glm::vec4(0, 0, 0, 1));
    line(o, glm::vec3(transform * glm::vec4(size, 0, 0, 1)), {1, 0, 0, 1});
    line(o, glm::vec3(transform * glm::vec4(0, size, 0, 1)), {0, 1, 0, 1});
    line(o, glm::vec3(transform * glm::vec4(0, 0, size, 1)), {0, 0, 1, 1});
}

} // namespace engine::renderer
