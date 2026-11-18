#include "engine/renderer/light.hpp"

#include <algorithm>
#include <cstring>

namespace engine::renderer
{

bool Light::operator==(const Light &o) const
{
    if (type != o.type)
    {
        return false;
    }
    switch (type)
    {
        case LightType::kAmbient: return color == o.color && strength == o.strength;
        case LightType::kDirectional: return direction == o.direction && color == o.color && strength == o.strength;
        case LightType::kPoint: return position == o.position && color == o.color && strength == o.strength && a == o.a && b == o.b;
        case LightType::kSpot:
            return position == o.position && direction == o.direction && color == o.color && strength == o.strength && outerCone == o.outerCone &&
                   innerCone == o.innerCone && a == o.a && b == o.b;
        case LightType::kNone: return true;
    }
    return false;
}

Light directionalLight(const glm::vec3 &direction, const glm::vec3 &color, float strength)
{
    Light l;
    l.type = LightType::kDirectional;
    l.direction = direction;
    l.color = color;
    l.strength = strength;
    return l;
}

Light pointLight(const glm::vec3 &position, const glm::vec3 &color, float strength, float a, float b)
{
    Light l;
    l.type = LightType::kPoint;
    l.position = position;
    l.color = color;
    l.strength = strength;
    l.a = a;
    l.b = b;
    return l;
}

Light spotLight(const glm::vec3 &position, const glm::vec3 &direction, const glm::vec3 &color, float strength, float outerCone, float innerCone,
                float a, float b)
{
    Light l;
    l.type = LightType::kSpot;
    l.position = position;
    l.direction = direction;
    l.color = color;
    l.strength = strength;
    l.outerCone = outerCone;
    l.innerCone = innerCone;
    l.a = a;
    l.b = b;
    return l;
}

Light ambientLight(const glm::vec3 &color, float strength)
{
    Light l;
    l.type = LightType::kAmbient;
    l.color = color;
    l.strength = strength;
    return l;
}

core::Result<LightManager> LightManager::create(rhi::Device &device, std::uint32_t initialCapacity)
{
    LightManager out;
    out.capacity_ = std::max(initialCapacity, 1u);
    rhi::BufferDesc desc;
    desc.size = sizeof(GpuLightHeader) + static_cast<std::uint64_t>(out.capacity_) * sizeof(GpuLight);
    desc.usage = rhi::BufferUsage::kStorage | rhi::BufferUsage::kCopyDst | rhi::BufferUsage::kCopySrc;
    desc.debugName = "lights";
    auto buffer = GpuBuffer::create(device, desc);
    if (!buffer)
    {
        return std::unexpected(buffer.error());
    }
    out.buffer_ = std::move(*buffer);
    return out;
}

void LightManager::setAmbientLight(const glm::vec3 &color, float strength)
{
    ambient_ = ambientLight(color, strength);
    dirty_ = true;
}

void LightManager::resetAmbientLight()
{
    ambient_ = ambientLight(glm::vec3(1.0f), 0.0f);
    dirty_ = true;
}

void LightManager::addLight(const Light &light)
{
    lights_.push_back(light);
    dirty_ = true;
}

void LightManager::addLights(const std::vector<Light> &lights)
{
    lights_.insert(lights_.end(), lights.begin(), lights.end());
    dirty_ = true;
}

void LightManager::setLight(std::size_t index, const Light &light)
{
    lights_.at(index) = light;
    dirty_ = true;
}

void LightManager::removeLight(std::size_t index)
{
    if (index < lights_.size())
    {
        lights_.erase(lights_.begin() + static_cast<std::ptrdiff_t>(index));
        dirty_ = true;
    }
}

void LightManager::removeLight(const Light &light)
{
    if (auto it = std::ranges::find(lights_, light); it != lights_.end())
    {
        lights_.erase(it);
        dirty_ = true;
    }
}

core::Result<> LightManager::upload()
{
    if (!buffer_.valid())
    {
        return core::makeError(core::ErrorCode::kInvalidArgument, "LightManager::upload: not created");
    }
    if (!dirty_)
    {
        return {};
    }
    if (lights_.size() > capacity_)
    {
        std::uint32_t grown = capacity_;
        while (grown < lights_.size())
        {
            grown *= 2;
        }
        if (auto r = buffer_.resize(sizeof(GpuLightHeader) + static_cast<std::uint64_t>(grown) * sizeof(GpuLight), false); !r)
        {
            return r;
        }
        capacity_ = grown;
    }
    std::vector<std::byte> bytes(sizeof(GpuLightHeader) + lights_.size() * sizeof(GpuLight));
    GpuLightHeader header{};
    header.size = static_cast<std::int32_t>(lights_.size());
    header.ambientColor[0] = ambient_.color.x;
    header.ambientColor[1] = ambient_.color.y;
    header.ambientColor[2] = ambient_.color.z;
    header.ambientColor[3] = 1.0f;
    header.ambientStrength = ambient_.strength;
    std::memcpy(bytes.data(), &header, sizeof header);
    for (std::size_t i = 0; i < lights_.size(); ++i)
    {
        const Light &l = lights_[i];
        GpuLight g{};
        g.type = static_cast<std::int32_t>(l.type);
        g.position[0] = l.position.x;
        g.position[1] = l.position.y;
        g.position[2] = l.position.z;
        g.direction[0] = l.direction.x;
        g.direction[1] = l.direction.y;
        g.direction[2] = l.direction.z;
        g.color[0] = l.color.x;
        g.color[1] = l.color.y;
        g.color[2] = l.color.z;
        g.strength = l.strength;
        g.outerCone = l.outerCone;
        g.innerCone = l.innerCone;
        g.a = l.a;
        g.b = l.b;
        std::memcpy(bytes.data() + sizeof header + i * sizeof g, &g, sizeof g);
    }
    if (auto r = buffer_.update(0, bytes); !r)
    {
        return r;
    }
    dirty_ = false;
    return {};
}

} // namespace engine::renderer
