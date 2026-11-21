// Encodeur de commandes du backend Vulkan (privé). Enregistre DIRECTEMENT dans un `VkCommandBuffer` ; voir README.md :
// dispositions « de repos », barrières déduites, rendu dynamique, descripteurs liés au dessin.
#pragma once

#include "vk_device.hpp"

namespace orosmo::rhi::vk
{

class VkEncoder final : public CommandEncoder
{
  public:
    explicit VkEncoder(VulkanDevice &device);
    ~VkEncoder() override;

    VkEncoder(const VkEncoder &) = delete;
    VkEncoder &operator=(const VkEncoder &) = delete;

    void beginRenderPass(const RenderPassDesc &desc) override;
    void endRenderPass() override;
    void setPipeline(PipelineHandle pipeline) override;
    void setBindGroup(std::uint32_t slot, BindGroupHandle group, std::span<const std::uint32_t> dynamicOffsets = {}) override;
    void setVertexBuffer(std::uint32_t slot, BufferHandle buffer, std::uint64_t offset = 0) override;
    void setIndexBuffer(BufferHandle buffer, IndexType type, std::uint64_t offset = 0) override;
    void setPushConstants(std::span<const std::byte> data) override;
    void setViewport(const Viewport &viewport) override;
    void setScissor(const Rect &rect) override;
    void draw(std::uint32_t vertexCount, std::uint32_t instanceCount = 1, std::uint32_t firstVertex = 0, std::uint32_t firstInstance = 0) override;
    void drawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount = 1, std::uint32_t firstIndex = 0, std::int32_t vertexOffset = 0,
                     std::uint32_t firstInstance = 0) override;
    void drawIndirect(BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride = 0) override;
    void drawIndexedIndirect(BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride = 0) override;
    void bindComputePipeline(PipelineHandle pipeline) override;
    void dispatch(std::uint32_t x, std::uint32_t y, std::uint32_t z) override;
    void dispatchIndirect(BufferHandle args, std::uint64_t offset) override;
    void copyBuffer(BufferHandle src, BufferHandle dst, const BufferCopy &region) override;
    void copyBufferToTexture(BufferHandle src, TextureHandle dst, const TextureCopy &region) override;
    void copyTextureToBuffer(TextureHandle src, BufferHandle dst, const TextureCopy &region) override;
    void generateMips(TextureHandle texture) override;
    void beginDebugGroup(const char *name) override;
    void endDebugGroup() override;
    void writeTimestamp(QueryPoolHandle pool, std::uint32_t index) override;
    const std::optional<Error> &error() const noexcept override { return error_; }

    // Termine l'enregistrement. Une erreur d'usage déjà notée (error()), une passe ou un groupe de debug resté ouvert : l'erreur
    // est renvoyée, le tampon de commandes rendu au périphérique et rien n'est soumis. Sinon renvoie le tampon prêt à soumettre.
    Result<VkCommandBuffer> finish();

  private:
    VulkanDevice &device_;
    VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
    std::optional<Error> error_;
};

} // namespace orosmo::rhi::vk
