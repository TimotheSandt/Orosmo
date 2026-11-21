#include "vk_device.hpp"
#include "vk_translate.hpp"

#include <orosmo/core/log.hpp>

namespace orosmo::rhi::vk
{

using orosmo::core::ErrorCode;
using orosmo::core::makeError;

// ── Squelette : tout est à écrire (voir README.md, « Shaders, pipelines et groupes de liaison ») ────────────

Result<> VulkanDevice::initPipelines() { return {}; }
void VulkanDevice::shutdownPipelines() noexcept {}

Result<ShaderHandle> VulkanDevice::createShader(const ShaderDesc &) { return makeError(ErrorCode::kUnsupported, "Vulkan createShader: not implemented"); }

Result<PipelineHandle> VulkanDevice::createGraphicsPipeline(const GraphicsPipelineDesc &)
{
    return makeError(ErrorCode::kUnsupported, "Vulkan createGraphicsPipeline: not implemented");
}

Result<PipelineHandle> VulkanDevice::createComputePipeline(const ComputePipelineDesc &)
{
    return makeError(ErrorCode::kUnsupported, "Vulkan createComputePipeline: not implemented");
}

Result<BindGroupHandle> VulkanDevice::createBindGroup(const BindGroupDesc &) { return makeError(ErrorCode::kUnsupported, "Vulkan createBindGroup: not implemented"); }

Result<const SetLayout *> VulkanDevice::setLayoutFor(const std::vector<DescriptorBinding> &)
{
    return makeError(ErrorCode::kUnsupported, "Vulkan setLayoutFor: not implemented");
}

Result<VkDescriptorSet> VulkanDevice::descriptorSetFor(BindGroupHandle, const SetLayout &)
{
    return makeError(ErrorCode::kUnsupported, "Vulkan descriptorSetFor: not implemented");
}

} // namespace orosmo::rhi::vk
