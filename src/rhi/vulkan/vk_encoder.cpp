#include "vk_encoder.hpp"

#include <orosmo/core/log.hpp>

namespace orosmo::rhi::vk
{

using orosmo::core::ErrorCode;
using orosmo::core::makeError;

// ── Squelette : tout est à écrire (voir README.md, « Encodeur ») ─────────────────────

VkEncoder::VkEncoder(VulkanDevice &device) : device_(device) { error_ = makeError(ErrorCode::kUnsupported, "Vulkan encoder: not implemented").error(); }

VkEncoder::~VkEncoder() = default;

void VkEncoder::beginRenderPass(const RenderPassDesc &) {}
void VkEncoder::endRenderPass() {}
void VkEncoder::setPipeline(PipelineHandle) {}
void VkEncoder::setBindGroup(std::uint32_t, BindGroupHandle, std::span<const std::uint32_t>) {}
void VkEncoder::setVertexBuffer(std::uint32_t, BufferHandle, std::uint64_t) {}
void VkEncoder::setIndexBuffer(BufferHandle, IndexType, std::uint64_t) {}
void VkEncoder::setPushConstants(std::span<const std::byte>) {}
void VkEncoder::setViewport(const Viewport &) {}
void VkEncoder::setScissor(const Rect &) {}
void VkEncoder::draw(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) {}
void VkEncoder::drawIndexed(std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t, std::uint32_t) {}
void VkEncoder::drawIndirect(BufferHandle, std::uint64_t, std::uint32_t, std::uint32_t) {}
void VkEncoder::drawIndexedIndirect(BufferHandle, std::uint64_t, std::uint32_t, std::uint32_t) {}
void VkEncoder::bindComputePipeline(PipelineHandle) {}
void VkEncoder::dispatch(std::uint32_t, std::uint32_t, std::uint32_t) {}
void VkEncoder::dispatchIndirect(BufferHandle, std::uint64_t) {}
void VkEncoder::copyBuffer(BufferHandle, BufferHandle, const BufferCopy &) {}
void VkEncoder::copyBufferToTexture(BufferHandle, TextureHandle, const TextureCopy &) {}
void VkEncoder::copyTextureToBuffer(TextureHandle, BufferHandle, const TextureCopy &) {}
void VkEncoder::generateMips(TextureHandle) {}
void VkEncoder::beginDebugGroup(const char *) {}
void VkEncoder::endDebugGroup() {}
void VkEncoder::writeTimestamp(QueryPoolHandle, std::uint32_t) {}

Result<VkCommandBuffer> VkEncoder::finish() { return std::unexpected(*error_); }

std::unique_ptr<CommandEncoder> VulkanDevice::createCommandEncoder() { return std::make_unique<VkEncoder>(*this); }

Result<> VulkanDevice::submit(std::unique_ptr<CommandEncoder> encoder)
{
    auto *vkEncoder = dynamic_cast<VkEncoder *>(encoder.get());
    if (vkEncoder == nullptr)
    {
        return makeError(ErrorCode::kInvalidArgument, "submit: the encoder does not come from this Vulkan device");
    }
    auto commandBuffer = vkEncoder->finish();
    if (!commandBuffer)
    {
        return std::unexpected(commandBuffer.error());
    }
    return makeError(ErrorCode::kUnsupported, "Vulkan submit: not implemented");
}

} // namespace orosmo::rhi::vk
