// Encodeur de commandes du backend Vulkan (privé). Enregistre DIRECTEMENT dans un `VkCommandBuffer` ; voir README.md :
// dispositions « de repos », barrières déduites, rendu dynamique, descripteurs liés au dessin.
#pragma once

#include "vk_device.hpp"

#include <string>

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
    // est renvoyée, le tampon de commandes rendu au périphérique et rien n'est soumis. Sinon renvoie les tampons prêts à soumettre,
    // dans l'ordre : le prologue (remises à zéro de requêtes écrites dans une passe) s'il existe, puis les commandes.
    Result<std::vector<VkCommandBuffer>> finish();

  private:
    bool check(bool condition, core::ErrorCode code, const std::string &message);
    bool recording() const noexcept { return commandBuffer_ != VK_NULL_HANDLE; }
    // Liaisons au moment du dessin ou du dispatch : jeux de descripteurs (décalages dynamiques), push constants.
    bool bindResources(const PipelineRes &pipeline, VkPipelineBindPoint bindPoint, const char *what);
    bool beforeDraw(bool indexed, const char *what);
    void applyViewport();
    void releaseCommandBuffers() noexcept;
    // Transition d'une sous-ressource (niveau, couche ou tranche) d'une texture vivante.
    void transition(const TextureRes &texture, VkImageLayout from, VkImageLayout to, std::uint32_t mip, std::uint32_t mipCount, std::uint32_t layer,
                    std::uint32_t layerCount);

    VulkanDevice &device_;
    VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
    VkCommandBuffer prologue_ = VK_NULL_HANDLE; // remises à zéro de requêtes demandées dans une passe
    std::optional<Error> error_;

    // Passe
    struct PassAttachment
    {
        TextureHandle texture;
        std::uint32_t mip = 0;
        std::uint32_t layer = 0;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    };
    bool inPass_ = false;
    std::vector<PassAttachment> passAttachments_;
    std::vector<Format> passColorFormats_;
    Format passDepthFormat_ = Format::kUndefined;
    Extent2D passExtent_{};
    bool passLabel_ = false;
    std::uint32_t debugDepth_ = 0;
    Viewport viewport_{};

    // État courant
    PipelineHandle pipeline_;
    PipelineHandle computePipeline_;
    struct BoundGroup
    {
        BindGroupHandle group;
        std::array<std::uint32_t, kBindingsPerGroup> dynamicOffset{}; // par liaison (0 : entrée non dynamique)
    };
    std::array<BoundGroup, kGroupCount> groups_{};
    struct VertexSlot
    {
        BufferHandle buffer;
        std::uint64_t offset = 0;
    };
    std::array<VertexSlot, kMaxVertexBindings> vertexBuffers_{};
    BufferHandle indexBuffer_;
    IndexType indexType_ = IndexType::kUint32;
    std::uint64_t indexOffset_ = 0;
    std::vector<std::byte> pushConstants_;
    // Requêtes écrites par cet encodeur (une requête ne peut être réécrite qu'après une remise à zéro).
    std::vector<std::pair<VkQueryPool, std::uint32_t>> writtenQueries_;
};

} // namespace orosmo::rhi::vk
