#include "orosmo/renderer/instance_batch.hpp"

#include <algorithm>
#include <numeric>

namespace orosmo::renderer
{

GpuInstance makeGpuInstance(const glm::mat4 &model, const glm::vec4 &color, float dither) noexcept
{
    GpuInstance out{};
    for (int c = 0; c < 4; ++c)
    {
        out.model[c] = model[c];
    }
    // Inverse transposée de la partie 3x3 (juste avec une échelle non uniforme) ; non inversible (échelle nulle) : identité.
    const glm::mat3 linear(model);
    const glm::mat3 normal = glm::determinant(linear) != 0.0f ? glm::transpose(glm::inverse(linear)) : glm::mat3(1.0f);
    for (int c = 0; c < 3; ++c)
    {
        out.normal[c] = glm::vec4(normal[c], 0.0f);
    }
    out.normal[0].w = dither;
    out.color = color;
    return out;
}

void InstanceBatchBuilder::clear() noexcept
{
    keys_.clear();
    pending_.clear();
    sorted_.clear();
    batches_.clear();
    built_ = false;
}

void InstanceBatchBuilder::reserve(std::size_t instances)
{
    keys_.reserve(instances);
    pending_.reserve(instances);
}

void InstanceBatchBuilder::add(InstanceKey key, const GpuInstance &instance)
{
    keys_.push_back(key);
    pending_.push_back(instance);
    built_ = false;
}

void InstanceBatchBuilder::build()
{
    order_.resize(keys_.size());
    std::iota(order_.begin(), order_.end(), 0u);
    std::stable_sort(order_.begin(), order_.end(), [&](std::uint32_t a, std::uint32_t b) { return keys_[a] < keys_[b]; });
    sorted_.resize(order_.size());
    batches_.clear();
    for (std::uint32_t i = 0; i < order_.size(); ++i)
    {
        const InstanceKey key = keys_[order_[i]];
        sorted_[i] = pending_[order_[i]];
        if (batches_.empty() || batches_.back().key != key)
        {
            batches_.push_back({key, i, 0});
        }
        ++batches_.back().instanceCount;
    }
    built_ = true;
}

} // namespace orosmo::renderer
