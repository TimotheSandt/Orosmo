// CommandEncoder Vulkan : enregistre directement dans un tampon de commandes primaire, soumis par `Device::submit`. Mêmes
// validations et codes d'erreur que l'encodeur OpenGL (src/rhi/opengl/gl_encoder.cpp). Barrières : README.md, « Dispositions de
// repos et barrières déduites » ; descripteurs liés au dessin : README.md, « Descripteurs ».
#include "vk_encoder.hpp"

#include "vk_region.hpp"
#include "vk_sync.hpp"
#include "vk_translate.hpp"

#include <orosmo/core/log.hpp>

#include <algorithm>
#include <limits>

namespace orosmo::rhi::vk
{

using orosmo::core::ErrorCode;
using orosmo::core::makeError;

namespace
{

bool rangeFits(std::uint64_t offset, std::uint64_t required, std::uint64_t size) { return offset <= size && required <= size - offset; }

// Octets réellement touchés dans le tampon : toutes les tranches (ou couches) et lignes au pas `rowLength` (texels, 0 : serrées),
// sauf le remplissage après la dernière ligne de la dernière tranche (comme le backend OpenGL).
std::uint64_t transferBytes(const ResolvedRegion &region, std::uint32_t rowLength, std::uint32_t bytesPerTexel)
{
    const std::uint64_t row = rowLength != 0 ? rowLength : region.extent.width;
    const std::uint64_t slice = row * region.extent.height;
    const std::uint64_t slices = static_cast<std::uint64_t>(region.extent.depth) * region.layerCount;
    return ((slices - 1) * slice + (region.extent.height - 1) * row + region.extent.width) * bytesPerTexel;
}

// Vrai si une pièce jointe précédente vise déjà le même niveau d'une image 3D (une seule transition par niveau 3D).
template <typename Attachments>
bool isRepeatedLevel(const Attachments &attachments, std::size_t index, const TextureRes &texture)
{
    if (texture.desc.dimension != TextureDimension::k3D)
        return false;
    for (std::size_t j = 0; j < index; ++j)
    {
        if (attachments[j].texture == attachments[index].texture && attachments[j].mip == attachments[index].mip)
            return true;
    }
    return false;
}

VkClearValue clearColor(const ColorAttachment &c, Format format)
{
    VkClearValue value{};
    if (formatInfo(format).kind == FormatKind::kUint)
    {
        value.color.uint32[0] = static_cast<std::uint32_t>(c.clearColor[0]); // représentable : validé avant
    }
    else
    {
        for (int i = 0; i < 4; ++i)
            value.color.float32[i] = c.clearColor[static_cast<std::size_t>(i)];
    }
    return value;
}

} // namespace

// ── Cycle de vie ─────────────────────────────────────────────────────────────

VkEncoder::VkEncoder(VulkanDevice &device) : device_(device)
{
    auto commandBuffer = device_.acquireCommandBuffer();
    if (commandBuffer)
        commandBuffer_ = *commandBuffer;
    else
        error_ = commandBuffer.error();
}

VkEncoder::~VkEncoder() { releaseCommandBuffers(); }

// Un encodeur abandonné (erreur, jamais soumis) rend ses tampons à leur réserve, quel que soit leur état (même en cours
// d'enregistrement) : ils seront remis à zéro avec elle.
void VkEncoder::releaseCommandBuffers() noexcept
{
    for (VkCommandBuffer *cb : {&commandBuffer_, &prologue_})
    {
        if (*cb != VK_NULL_HANDLE)
        {
            device_.releaseUnsubmitted(*cb);
            *cb = VK_NULL_HANDLE;
        }
    }
}

void VkEncoder::noteBuffer(BufferHandle handle, const BufferRes *buffer)
{
    // Seuls les tampons kUpload comptent (écriture directe de `updateBuffer`) ; doublons consécutifs évités.
    if (buffer != nullptr && buffer->desc.memory == MemoryKind::kUpload && (uploadBuffers_.empty() || uploadBuffers_.back() != handle))
        uploadBuffers_.push_back(handle);
}

bool VkEncoder::check(bool condition, ErrorCode code, const std::string &message)
{
    if (!condition)
    {
        if (!error_)
            error_ = makeError(code, message).error();
        LOG_ERROR(1, "RHI encoder: ", message);
    }
    return condition;
}

Result<std::vector<VkCommandBuffer>> VkEncoder::finish()
{
    if (!error_ && (inPass_ || debugDepth_ > 0))
        error_ = makeError(ErrorCode::kInvalidArgument, "submit: a render pass or a debug group is still open").error();
    if (error_)
    {
        releaseCommandBuffers();
        return std::unexpected(*error_);
    }
    std::vector<VkCommandBuffer> out;
    for (VkCommandBuffer cb : {prologue_, commandBuffer_})
    {
        if (cb == VK_NULL_HANDLE)
            continue;
        if (auto r = device_.fromVk(vkEndCommandBuffer(cb), "vkEndCommandBuffer"); !r)
        {
            releaseCommandBuffers();
            return std::unexpected(r.error());
        }
        out.push_back(cb);
    }
    prologue_ = VK_NULL_HANDLE;
    commandBuffer_ = VK_NULL_HANDLE;
    return out;
}

std::unique_ptr<CommandEncoder> VulkanDevice::createCommandEncoder() { return std::make_unique<VkEncoder>(*this); }

Result<> VulkanDevice::submit(std::unique_ptr<CommandEncoder> encoder)
{
    if (!encoder)
        return makeError(ErrorCode::kInvalidArgument, "submit: null encoder");
    auto *vkEncoder = dynamic_cast<VkEncoder *>(encoder.get());
    if (vkEncoder == nullptr || &vkEncoder->device() != this)
        return makeError(ErrorCode::kInvalidArgument, "submit: the encoder does not come from this Vulkan device");
    auto commandBuffers = vkEncoder->finish();
    if (!commandBuffers)
        return std::unexpected(commandBuffers.error());
    auto serial = submitCommandBuffers(*commandBuffers);
    if (serial)
    {
        // Tampons kUpload lus ou écrits par ce travail : pas d'écriture directe tant que cette série n'est pas finie.
        for (BufferHandle h : vkEncoder->uploadBuffers())
        {
            if (BufferRes *b = buffers_.get(h); b != nullptr)
                b->gpuUse = std::max(b->gpuUse, *serial);
        }
        // Horodatages : prêts seulement quand cette série est terminée (G192).
        for (QueryPoolHandle h : vkEncoder->queryPools())
        {
            if (QueryPoolRes *q = queryPools_.get(h); q != nullptr)
            {
                q->lastWrite = std::max(q->lastWrite, *serial);
                q->unsubmitted = false;
            }
        }
    }
    if (!serial)
    {
        // Rien n'a été soumis : les tampons reviennent à leur réserve.
        for (VkCommandBuffer cb : *commandBuffers)
            releaseUnsubmitted(cb);
        return std::unexpected(serial.error());
    }
    return {};
}

void VkEncoder::transition(const TextureRes &texture, VkImageLayout from, VkImageLayout to, std::uint32_t mip, std::uint32_t mipCount, std::uint32_t layer,
                           std::uint32_t layerCount)
{
    // Image 3D : une seule couche Vulkan (les tranches d'une cible de rendu 3D sont des couches de vue, pas d'image).
    if (texture.desc.dimension == TextureDimension::k3D)
    {
        layer = 0;
        layerCount = 1;
    }
    transitionImage(commandBuffer_, texture.image, texture.aspect, from, to, mip, mipCount, layer, layerCount);
}

// ── Passes ───────────────────────────────────────────────────────────────────

void VkEncoder::beginRenderPass(const RenderPassDesc &desc)
{
    if (!check(recording(), ErrorCode::kUnknown, "beginRenderPass: the encoder has no command buffer") ||
        !check(!inPass_, ErrorCode::kInvalidArgument, "beginRenderPass: a pass is already open"))
        return;
    const Capabilities &caps = device_.capabilities();
    if (!check(desc.colors.size() <= caps.maxColorAttachments, ErrorCode::kOutOfRange, "beginRenderPass: too many color attachments") ||
        !check(!desc.colors.empty() || desc.depth.has_value(), ErrorCode::kInvalidArgument, "beginRenderPass: no attachment"))
        return;

    std::vector<PassAttachment> attachments;
    std::vector<Format> formats;
    Extent2D extent{};
    auto resolve = [&](TextureHandle handle, std::uint32_t mip, std::uint32_t layer, bool depth) -> bool
    {
        const TextureRes *t = device_.get(handle);
        if (!check(t != nullptr, ErrorCode::kNotFound, "beginRenderPass: stale or null attachment texture") ||
            !check(hasFlag(t->desc.usage, TextureUsage::kRenderTarget), ErrorCode::kInvalidArgument,
                   "beginRenderPass: attachment lacks the RenderTarget usage (" + t->desc.debugName + ")") ||
            !check(formatInfo(t->desc.format).isDepth() == depth, ErrorCode::kInvalidArgument,
                   "beginRenderPass: wrong attachment kind for format " + std::string(toString(t->desc.format))) ||
            !check(mip < t->levels, ErrorCode::kOutOfRange, "beginRenderPass: mip level out of range") ||
            // Texture 3D : la « couche » est une tranche du niveau choisi ; tableau et cube : une couche.
            !check(layer < (t->desc.dimension == TextureDimension::k3D ? mipDim(t->desc.size.depth, mip) : t->layers), ErrorCode::kOutOfRange,
                   "beginRenderPass: layer or 3D slice out of range"))
            return false;
        const Extent2D e{mipDim(t->desc.size.width, mip), mipDim(t->desc.size.height, mip)};
        if (extent.width == 0)
            extent = e;
        if (!check(extent == e, ErrorCode::kInvalidArgument, "beginRenderPass: attachments differ in size"))
            return false;
        attachments.push_back({handle, mip, layer, depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
        if (!depth)
            formats.push_back(t->desc.format);
        return true;
    };
    for (const ColorAttachment &c : desc.colors)
    {
        if (!resolve(c.texture, c.mipLevel, c.layer, false))
            return;
    }
    Format depthFormat = Format::kUndefined;
    if (desc.depth)
    {
        if (!resolve(desc.depth->texture, 0, 0, true))
            return;
        depthFormat = device_.get(desc.depth->texture)->desc.format;
    }
    for (std::size_t i = 0; i < desc.colors.size(); ++i)
    {
        const float v = desc.colors[i].clearColor[0];
        if (desc.colors[i].load == LoadOp::kClear && formatInfo(formats[i]).kind == FormatKind::kUint &&
            !check(v >= 0.0f && v < 4294967296.0f, ErrorCode::kInvalidArgument, "beginRenderPass: the clear value of an integer target must be in [0, 2^32)"))
            return;
    }
    for (std::size_t i = 0; i < attachments.size(); ++i)
    {
        for (std::size_t j = 0; j < i; ++j)
        {
            if (!check(attachments[i].texture != attachments[j].texture || attachments[i].mip != attachments[j].mip || attachments[i].layer != attachments[j].layer,
                       ErrorCode::kInvalidArgument, "beginRenderPass: the same texture level and layer is attached twice"))
                return;
        }
    }

    // Vues (créées au besoin) : une couche et un niveau ; une tranche d'une 3D est une couche d'une vue 2D (image 2D_ARRAY_COMPATIBLE).
    std::vector<VkImageView> views;
    for (const PassAttachment &a : attachments)
    {
        ImageViewKey key;
        key.baseMip = a.mip;
        key.mipCount = 1;
        key.baseLayer = a.layer;
        key.layerCount = 1;
        key.type = VK_IMAGE_VIEW_TYPE_2D;
        auto view = device_.imageView(a.texture, key);
        if (!check(view.has_value(), view ? ErrorCode::kUnknown : view.error().code, view ? "" : "beginRenderPass: " + view.error().message))
            return;
        views.push_back(*view);
    }

    // Tout ce qui précède est fini et visible ; chaque attachement quitte sa disposition de repos.
    memoryBarrierAll(commandBuffer_);
    // Une image 3D n'a qu'une couche Vulkan : plusieurs tranches du même niveau partagent une seule transition.
    for (std::size_t i = 0; i < attachments.size(); ++i)
    {
        const PassAttachment &a = attachments[i];
        const TextureRes &t = *device_.get(a.texture);
        if (!isRepeatedLevel(attachments, i, t))
            transition(t, t.restLayout, a.layout, a.mip, 1, a.layer, 1);
    }

    if (!desc.debugName.empty() && device_.hasDebugUtils() && vkCmdBeginDebugUtilsLabelEXT != nullptr)
    {
        auto label = makeStruct<VkDebugUtilsLabelEXT>(VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT);
        label.pLabelName = desc.debugName.c_str();
        vkCmdBeginDebugUtilsLabelEXT(commandBuffer_, &label);
        passLabel_ = true;
    }

    std::vector<VkRenderingAttachmentInfo> colors;
    for (std::size_t i = 0; i < desc.colors.size(); ++i)
    {
        const ColorAttachment &c = desc.colors[i];
        auto info = makeStruct<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
        info.imageView = views[i];
        info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        info.loadOp = toVk(c.load);
        info.storeOp = toVk(c.store);
        if (c.load == LoadOp::kClear) // la valeur n'est validée (entier) que pour un effacement
            info.clearValue = clearColor(c, formats[i]);
        colors.push_back(info);
    }
    auto depthInfo = makeStruct<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
    auto rendering = makeStruct<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
    rendering.renderArea = {{0, 0}, {extent.width, extent.height}};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = static_cast<std::uint32_t>(colors.size());
    rendering.pColorAttachments = colors.data();
    if (desc.depth)
    {
        const DepthAttachment &d = *desc.depth;
        depthInfo.imageView = views.back();
        depthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthInfo.loadOp = toVk(d.load);
        depthInfo.storeOp = toVk(d.store);
        depthInfo.clearValue.depthStencil = {d.clearDepth, d.clearStencil};
        rendering.pDepthAttachment = &depthInfo;
        if ((aspectOf(depthFormat) & VK_IMAGE_ASPECT_STENCIL_BIT) != 0)
            rendering.pStencilAttachment = &depthInfo;
    }
    vkCmdBeginRendering(commandBuffer_, &rendering);

    inPass_ = true;
    passAttachments_ = std::move(attachments);
    passColorFormats_ = std::move(formats);
    passDepthFormat_ = depthFormat;
    passExtent_ = extent;
    pipeline_ = {};
    viewport_ = {0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f};
    applyViewport();
    const VkRect2D scissor{{0, 0}, {extent.width, extent.height}};
    vkCmdSetScissor(commandBuffer_, 0, 1, &scissor);
}

void VkEncoder::endRenderPass()
{
    if (!check(inPass_, ErrorCode::kInvalidArgument, "endRenderPass: no pass is open"))
        return;
    vkCmdEndRendering(commandBuffer_);
    // Chaque attachement revient à sa disposition de repos ; les écritures de la passe sont visibles pour la suite.
    for (std::size_t i = 0; i < passAttachments_.size(); ++i)
    {
        const PassAttachment &a = passAttachments_[i];
        const TextureRes *t = device_.get(a.texture);
        if (t != nullptr && !isRepeatedLevel(passAttachments_, i, *t))
            transition(*t, a.layout, t->restLayout, a.mip, 1, a.layer, 1);
    }
    memoryBarrierAll(commandBuffer_);
    if (passLabel_)
    {
        vkCmdEndDebugUtilsLabelEXT(commandBuffer_);
        passLabel_ = false;
    }
    passAttachments_.clear();
    inPass_ = false;
    pipeline_ = {};
}

// ── État ─────────────────────────────────────────────────────────────────────

void VkEncoder::setPipeline(PipelineHandle handle)
{
    if (!check(inPass_, ErrorCode::kInvalidArgument, "setPipeline: outside a render pass"))
        return;
    const PipelineRes *p = device_.get(handle);
    if (!check(p != nullptr && !p->compute, ErrorCode::kNotFound, "setPipeline: stale, null or compute pipeline handle"))
        return;
    const bool compatible = p->colorFormats == passColorFormats_ && p->depthFormat == passDepthFormat_;
    if (!check(compatible, ErrorCode::kInvalidArgument, "setPipeline: pipeline attachment formats differ from the render pass"))
        return;
    vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, p->pipeline);
    pipeline_ = handle;
}

void VkEncoder::setBindGroup(std::uint32_t slot, BindGroupHandle handle, std::span<const std::uint32_t> dynamicOffsets)
{
    const BindGroupRes *group = device_.get(handle);
    if (!check(slot < kGroupCount, ErrorCode::kOutOfRange, "setBindGroup: slot >= kGroupCount") ||
        !check(group != nullptr, ErrorCode::kNotFound, "setBindGroup: stale or null bind group"))
        return;
    std::size_t expectedDynamic = 0;
    for (const BindGroupEntry &e : group->entries)
        expectedDynamic += e.dynamicOffset ? 1 : 0;
    if (!check(dynamicOffsets.size() == expectedDynamic, ErrorCode::kInvalidArgument, "setBindGroup: wrong number of dynamic offsets"))
        return;
    const Capabilities &caps = device_.capabilities();
    BoundGroup bound;
    bound.group = handle;
    std::size_t dyn = 0;
    for (const BindGroupEntry &e : group->entries)
    {
        switch (e.type)
        {
            case BindingType::kUniformBuffer:
            case BindingType::kStorageBuffer:
            {
                const BufferRes *b = device_.get(e.buffer);
                noteBuffer(e.buffer, b);
                if (!check(b != nullptr, ErrorCode::kNotFound, "setBindGroup: a buffer of the group was destroyed"))
                    return;
                const std::uint32_t extra = e.dynamicOffset ? dynamicOffsets[dyn++] : 0;
                const std::uint64_t offset = e.offset + extra;
                const std::uint64_t align = e.type == BindingType::kUniformBuffer ? caps.minUniformAlignment : caps.minStorageAlignment;
                const std::uint64_t size = e.size != 0 ? e.size : b->desc.size - std::min(offset, b->desc.size);
                // Taille 0 = jusqu'à la fin : le jeu de descripteurs Vulkan est écrit avec VK_WHOLE_SIZE depuis `e.offset` ; un décalage dynamique
                // non nul ferait dépasser le tampon. (OpenGL accepte : le décalage dynamique y retranche la taille.) Voir le README.
                if (!check(!(e.dynamicOffset && e.size == 0 && extra != 0), ErrorCode::kUnsupported,
                           "setBindGroup: a non-zero dynamic offset needs an explicit entry size (size 0 means 'to the end of the buffer') (" + b->desc.debugName + ")") ||
                    !check(offset % align == 0, ErrorCode::kInvalidArgument, "setBindGroup: dynamic offset is not aligned") ||
                    !check(offset <= b->desc.size && size <= b->desc.size - offset && size > 0, ErrorCode::kOutOfRange,
                           "setBindGroup: range exceeds the buffer (" + b->desc.debugName + ")") ||
                    !check(e.type != BindingType::kUniformBuffer || size <= caps.maxUniformBufferSize, ErrorCode::kOutOfRange,
                           "setBindGroup: uniform range larger than Capabilities::maxUniformBufferSize (" + b->desc.debugName + ")"))
                    return;
                bound.dynamicOffset[e.binding] = extra;
                break;
            }
            case BindingType::kSampledTexture:
            {
                if (!check(device_.get(e.texture) != nullptr && device_.get(e.sampler) != nullptr, ErrorCode::kNotFound,
                           "setBindGroup: a texture or sampler of the group was destroyed"))
                    return;
                break;
            }
            case BindingType::kStorageImage:
            {
                const TextureRes *t = device_.get(e.texture);
                if (!check(t != nullptr, ErrorCode::kNotFound, "setBindGroup: a texture of the group was destroyed") ||
                    !check(e.mipLevel < t->levels, ErrorCode::kOutOfRange, "setBindGroup: storage image mip level out of range"))
                    return;
                break;
            }
        }
    }
    groups_[slot] = bound;
}

void VkEncoder::setVertexBuffer(std::uint32_t slot, BufferHandle handle, std::uint64_t offset)
{
    const BufferRes *b = device_.get(handle);
    noteBuffer(handle, b);
    if (check(slot < vertexBuffers_.size(), ErrorCode::kOutOfRange, "setVertexBuffer: slot >= 16") &&
        check(b != nullptr, ErrorCode::kNotFound, "setVertexBuffer: stale or null buffer") &&
        check(hasFlag(b->desc.usage, BufferUsage::kVertex), ErrorCode::kInvalidArgument, "setVertexBuffer: buffer lacks the Vertex usage") &&
        check(offset < b->desc.size, ErrorCode::kOutOfRange, "setVertexBuffer: offset must be inside the buffer"))
        vertexBuffers_[slot] = {handle, offset};
}

void VkEncoder::setIndexBuffer(BufferHandle handle, IndexType type, std::uint64_t offset)
{
    const BufferRes *b = device_.get(handle);
    noteBuffer(handle, b);
    if (check(b != nullptr, ErrorCode::kNotFound, "setIndexBuffer: stale or null buffer") &&
        check(hasFlag(b->desc.usage, BufferUsage::kIndex), ErrorCode::kInvalidArgument, "setIndexBuffer: buffer lacks the Index usage") &&
        check(offset < b->desc.size, ErrorCode::kOutOfRange, "setIndexBuffer: offset must be inside the buffer") &&
        check(offset % (type == IndexType::kUint32 ? 4u : 2u) == 0, ErrorCode::kInvalidArgument, "setIndexBuffer: offset is not a multiple of the index size"))
    {
        indexBuffer_ = handle;
        indexType_ = type;
        indexOffset_ = offset;
    }
}

void VkEncoder::setPushConstants(std::span<const std::byte> data)
{
    // Comme OpenGL : les valeurs persistent d'un pipeline à l'autre ; elles sont poussées au dessin ou au dispatch.
    if (check(data.size() <= device_.capabilities().maxPushConstantSize && data.size() % 4 == 0, ErrorCode::kInvalidArgument,
              "setPushConstants: size must be a multiple of 4 and at most maxPushConstantSize"))
        pushConstants_.assign(data.begin(), data.end());
}

// Repère du RHI (A26) : y = +1 en haut. Viewport à hauteur négative (noyau 1.1) : la ligne 0 de la cible reçoit NDC y = +1.
void VkEncoder::applyViewport()
{
    const VkViewport v{viewport_.x, viewport_.y + viewport_.height, viewport_.width, -viewport_.height, std::clamp(viewport_.minDepth, 0.0f, 1.0f),
                       std::clamp(viewport_.maxDepth, 0.0f, 1.0f)};
    vkCmdSetViewport(commandBuffer_, 0, 1, &v);
}

void VkEncoder::setViewport(const Viewport &v)
{
    if (check(inPass_, ErrorCode::kInvalidArgument, "setViewport: outside a render pass") &&
        check(v.width > 0.0f && v.height > 0.0f, ErrorCode::kInvalidArgument, "setViewport: width and height must be positive"))
    {
        viewport_ = v;
        applyViewport();
    }
}

void VkEncoder::setScissor(const Rect &r)
{
    if (!check(inPass_, ErrorCode::kInvalidArgument, "setScissor: outside a render pass"))
        return;
    // En 64 bits, puis intersection avec la cible : un rectangle immense couvre la cible, un rectangle hors d'elle est vide.
    const auto clampTo = [](std::int64_t v, std::int64_t hi) { return std::clamp<std::int64_t>(v, 0, hi); };
    const std::int64_t x0 = clampTo(r.x, passExtent_.width);
    const std::int64_t x1 = clampTo(static_cast<std::int64_t>(r.x) + static_cast<std::int64_t>(r.width), passExtent_.width);
    const std::int64_t y0 = clampTo(r.y, passExtent_.height); // depuis le haut (A26), comme l'origine native de Vulkan
    const std::int64_t y1 = clampTo(static_cast<std::int64_t>(r.y) + static_cast<std::int64_t>(r.height), passExtent_.height);
    const VkRect2D scissor{{static_cast<std::int32_t>(x0), static_cast<std::int32_t>(y0)},
                           {static_cast<std::uint32_t>(std::max<std::int64_t>(0, x1 - x0)), static_cast<std::uint32_t>(std::max<std::int64_t>(0, y1 - y0))}};
    vkCmdSetScissor(commandBuffer_, 0, 1, &scissor);
}

// ── Liaisons au dessin ───────────────────────────────────────────────────────

bool VkEncoder::bindResources(const PipelineRes &p, VkPipelineBindPoint bindPoint, const char *what)
{
    for (std::uint32_t g = 0; g < kGroupCount; ++g)
    {
        const SetLayout *layout = p.sets[g];
        if (layout == nullptr)
            continue;
        const BoundGroup &bound = groups_[g];
        if (!check(static_cast<bool>(bound.group), ErrorCode::kInvalidArgument,
                   std::string(what) + ": the pipeline uses group " + std::to_string(g) + " but no bind group is set there"))
            return false;
        auto set = device_.descriptorSetFor(bound.group, *layout);
        if (!check(set.has_value(), set ? ErrorCode::kUnknown : set.error().code, set ? "" : std::string(what) + ": " + set.error().message))
            return false;
        // Un décalage par tampon de la disposition, dans l'ordre croissant des liaisons (élément par élément pour un tableau).
        std::vector<std::uint32_t> offsets;
        offsets.reserve(layout->dynamicCount);
        for (const DescriptorBinding &b : layout->bindings)
        {
            if (!isBufferDescriptor(b.type))
                continue;
            for (std::uint32_t i = 0; i < b.count; ++i)
                offsets.push_back(bound.dynamicOffset[b.binding + i]);
        }
        vkCmdBindDescriptorSets(commandBuffer_, bindPoint, p.layout, g, 1, &*set, static_cast<std::uint32_t>(offsets.size()), offsets.data());
    }
    if (p.pushConstantSize > 0)
    {
        // Toute la plage du pipeline est poussée (zéros au-delà des valeurs reçues) : Vulkan exige des valeurs définies.
        std::vector<std::byte> data(p.pushConstantSize, std::byte{0});
        std::copy_n(pushConstants_.begin(), std::min<std::size_t>(pushConstants_.size(), data.size()), data.begin());
        vkCmdPushConstants(commandBuffer_, p.layout, p.pushStages, 0, p.pushConstantSize, data.data());
    }
    return true;
}

bool VkEncoder::beforeDraw(bool indexed, const char *what)
{
    const PipelineRes *p = device_.get(pipeline_);
    if (!check(inPass_, ErrorCode::kInvalidArgument, std::string(what) + ": outside a render pass") ||
        !check(p != nullptr, ErrorCode::kInvalidArgument, std::string(what) + ": no pipeline set (or it was destroyed)"))
        return false;
    for (std::uint32_t binding = 0; binding < kMaxVertexBindings; ++binding)
    {
        if (!p->vertexBindingUsed[binding])
            continue;
        const BufferRes *b = device_.get(vertexBuffers_[binding].buffer);
        if (!check(b != nullptr, ErrorCode::kInvalidArgument, std::string(what) + ": vertex buffer binding " + std::to_string(binding) + " is not set"))
            return false;
        const VkDeviceSize offset = vertexBuffers_[binding].offset;
        vkCmdBindVertexBuffers(commandBuffer_, binding, 1, &b->buffer, &offset);
    }
    if (indexed)
    {
        const BufferRes *ib = device_.get(indexBuffer_);
        if (!check(ib != nullptr, ErrorCode::kInvalidArgument, std::string(what) + ": no index buffer set (or it was destroyed)"))
            return false;
        vkCmdBindIndexBuffer(commandBuffer_, ib->buffer, indexOffset_, toVk(indexType_));
    }
    return bindResources(*p, VK_PIPELINE_BIND_POINT_GRAPHICS, what);
}

// ── Dessins ──────────────────────────────────────────────────────────────────

void VkEncoder::draw(std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex, std::uint32_t firstInstance)
{
    if (beforeDraw(false, "draw"))
        vkCmdDraw(commandBuffer_, vertexCount, instanceCount, firstVertex, firstInstance);
}

void VkEncoder::drawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex, std::int32_t vertexOffset,
                            std::uint32_t firstInstance)
{
    if (beforeDraw(true, "drawIndexed"))
        vkCmdDrawIndexed(commandBuffer_, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void VkEncoder::drawIndirect(BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride)
{
    const BufferRes *b = device_.get(args);
    noteBuffer(args, b);
    const std::uint64_t step = stride != 0 ? stride : sizeof(DrawIndirectCommand);
    if (!check(b != nullptr && hasFlag(b->desc.usage, BufferUsage::kIndirect), ErrorCode::kInvalidArgument,
               "drawIndirect: args buffer is stale or lacks the Indirect usage") ||
        !check(offset % 4 == 0 && stride % 4 == 0 && (stride == 0 || stride >= sizeof(DrawIndirectCommand)), ErrorCode::kInvalidArgument,
               "drawIndirect: offset and stride must be multiples of 4, stride 0 or at least one command") ||
        !check(drawCount > 0 && rangeFits(offset, step * (drawCount - 1) + sizeof(DrawIndirectCommand), b->desc.size), ErrorCode::kOutOfRange,
               "drawIndirect: commands exceed the buffer") ||
        // Le contenu des commandes est inconnu à l'enregistrement : sans la fonctionnalité, firstInstance non nul serait invalide.
        !check(device_.features_.drawIndirectFirstInstance != VK_FALSE, ErrorCode::kUnsupported,
               "drawIndirect: the device lacks drawIndirectFirstInstance") ||
        !beforeDraw(false, "drawIndirect"))
        return;
    const VkBuffer buffer = b->buffer;
    // Sans multiDrawIndirect (ou au-delà de la limite), une commande par appel.
    if (device_.features_.multiDrawIndirect && drawCount <= device_.properties().limits.maxDrawIndirectCount)
    {
        vkCmdDrawIndirect(commandBuffer_, buffer, offset, drawCount, static_cast<std::uint32_t>(step));
        return;
    }
    for (std::uint32_t i = 0; i < drawCount; ++i)
        vkCmdDrawIndirect(commandBuffer_, buffer, offset + step * i, 1, static_cast<std::uint32_t>(step));
}

void VkEncoder::drawIndexedIndirect(BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride)
{
    const BufferRes *b = device_.get(args);
    noteBuffer(args, b);
    const std::uint64_t step = stride != 0 ? stride : sizeof(DrawIndexedIndirectCommand);
    if (!check(b != nullptr && hasFlag(b->desc.usage, BufferUsage::kIndirect), ErrorCode::kInvalidArgument,
               "drawIndexedIndirect: args buffer is stale or lacks the Indirect usage") ||
        !check(offset % 4 == 0 && stride % 4 == 0 && (stride == 0 || stride >= sizeof(DrawIndexedIndirectCommand)), ErrorCode::kInvalidArgument,
               "drawIndexedIndirect: offset and stride must be multiples of 4, stride 0 or at least one command") ||
        !check(drawCount > 0 && rangeFits(offset, step * (drawCount - 1) + sizeof(DrawIndexedIndirectCommand), b->desc.size), ErrorCode::kOutOfRange,
               "drawIndexedIndirect: commands exceed the buffer") ||
        // Même contrat que le backend OpenGL (qui lit les indices depuis l'octet 0 plus firstIndex).
        !check(indexOffset_ == 0, ErrorCode::kUnsupported, "drawIndexedIndirect: a non-zero index buffer offset is not supported") ||
        // Le contenu des commandes est inconnu à l'enregistrement : sans la fonctionnalité, firstInstance non nul serait invalide.
        !check(device_.features_.drawIndirectFirstInstance != VK_FALSE, ErrorCode::kUnsupported,
               "drawIndexedIndirect: the device lacks drawIndirectFirstInstance") ||
        !beforeDraw(true, "drawIndexedIndirect"))
        return;
    const VkBuffer buffer = b->buffer;
    if (device_.features_.multiDrawIndirect && drawCount <= device_.properties().limits.maxDrawIndirectCount)
    {
        vkCmdDrawIndexedIndirect(commandBuffer_, buffer, offset, drawCount, static_cast<std::uint32_t>(step));
        return;
    }
    for (std::uint32_t i = 0; i < drawCount; ++i)
        vkCmdDrawIndexedIndirect(commandBuffer_, buffer, offset + step * i, 1, static_cast<std::uint32_t>(step));
}

// ── Calcul ───────────────────────────────────────────────────────────────────

void VkEncoder::bindComputePipeline(PipelineHandle handle)
{
    const PipelineRes *p = device_.get(handle);
    if (check(!inPass_, ErrorCode::kInvalidArgument, "bindComputePipeline: inside a render pass") &&
        check(p != nullptr && p->compute, ErrorCode::kNotFound, "bindComputePipeline: stale, null or graphics pipeline handle"))
        computePipeline_ = handle;
}

void VkEncoder::dispatch(std::uint32_t x, std::uint32_t y, std::uint32_t z)
{
    const Capabilities &caps = device_.capabilities();
    const PipelineRes *p = device_.get(computePipeline_);
    if (!check(recording(), ErrorCode::kUnknown, "dispatch: the encoder has no command buffer") ||
        !check(!inPass_, ErrorCode::kInvalidArgument, "dispatch: inside a render pass") ||
        !check(p != nullptr, ErrorCode::kInvalidArgument, "dispatch: no compute pipeline bound") ||
        !check(x <= caps.maxComputeWorkGroupCount[0] && y <= caps.maxComputeWorkGroupCount[1] && z <= caps.maxComputeWorkGroupCount[2], ErrorCode::kOutOfRange,
               "dispatch: group count exceeds the device limit"))
        return;
    memoryBarrierAll(commandBuffer_);
    vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
    if (!bindResources(*p, VK_PIPELINE_BIND_POINT_COMPUTE, "dispatch"))
        return;
    vkCmdDispatch(commandBuffer_, x, y, z);
    memoryBarrierAll(commandBuffer_);
}

void VkEncoder::dispatchIndirect(BufferHandle args, std::uint64_t offset)
{
    const BufferRes *b = device_.get(args);
    noteBuffer(args, b);
    const PipelineRes *p = device_.get(computePipeline_);
    if (!check(recording(), ErrorCode::kUnknown, "dispatchIndirect: the encoder has no command buffer") ||
        !check(!inPass_, ErrorCode::kInvalidArgument, "dispatchIndirect: inside a render pass") ||
        !check(p != nullptr, ErrorCode::kInvalidArgument, "dispatchIndirect: no compute pipeline bound") ||
        !check(b != nullptr && hasFlag(b->desc.usage, BufferUsage::kIndirect), ErrorCode::kInvalidArgument,
               "dispatchIndirect: args buffer is stale or lacks the Indirect usage") ||
        !check(rangeFits(offset, sizeof(DispatchIndirectCommand), b->desc.size) && offset % 4 == 0, ErrorCode::kOutOfRange,
               "dispatchIndirect: offset out of range or unaligned"))
        return;
    const VkBuffer buffer = b->buffer;
    memoryBarrierAll(commandBuffer_);
    vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
    if (!bindResources(*p, VK_PIPELINE_BIND_POINT_COMPUTE, "dispatchIndirect"))
        return;
    vkCmdDispatchIndirect(commandBuffer_, buffer, offset);
    memoryBarrierAll(commandBuffer_);
}

// ── Copies ───────────────────────────────────────────────────────────────────

void VkEncoder::copyBuffer(BufferHandle src, BufferHandle dst, const BufferCopy &r)
{
    const BufferRes *s = device_.get(src);
    noteBuffer(src, s);
    const BufferRes *d = device_.get(dst);
    noteBuffer(dst, d);
    if (check(recording(), ErrorCode::kUnknown, "copyBuffer: the encoder has no command buffer") &&
        check(!inPass_, ErrorCode::kInvalidArgument, "copyBuffer: inside a render pass") &&
        check(s != nullptr && d != nullptr, ErrorCode::kNotFound, "copyBuffer: stale or null buffer") &&
        check(hasFlag(s->desc.usage, BufferUsage::kCopySrc) && hasFlag(d->desc.usage, BufferUsage::kCopyDst), ErrorCode::kInvalidArgument,
              "copyBuffer: source needs CopySrc and destination CopyDst") &&
        check(r.size > 0 && rangeFits(r.srcOffset, r.size, s->desc.size) && rangeFits(r.dstOffset, r.size, d->desc.size), ErrorCode::kOutOfRange,
              "copyBuffer: empty range or range exceeds a buffer") &&
        check(src != dst || r.srcOffset + r.size <= r.dstOffset || r.dstOffset + r.size <= r.srcOffset, ErrorCode::kInvalidArgument,
              "copyBuffer: overlapping ranges in the same buffer"))
    {
        memoryBarrierAll(commandBuffer_);
        const VkBufferCopy region{r.srcOffset, r.dstOffset, r.size};
        vkCmdCopyBuffer(commandBuffer_, s->buffer, d->buffer, 1, &region);
        memoryBarrierAll(commandBuffer_);
    }
}

void VkEncoder::copyBufferToTexture(BufferHandle src, TextureHandle dst, const TextureCopy &copy)
{
    const BufferRes *b = device_.get(src);
    noteBuffer(src, b);
    const TextureRes *t = device_.get(dst);
    if (!check(recording(), ErrorCode::kUnknown, "copyBufferToTexture: the encoder has no command buffer") ||
        !check(!inPass_, ErrorCode::kInvalidArgument, "copyBufferToTexture: inside a render pass") ||
        !check(b != nullptr && t != nullptr, ErrorCode::kNotFound, "copyBufferToTexture: stale or null buffer/texture") ||
        !check(hasFlag(b->desc.usage, BufferUsage::kCopySrc) && hasFlag(t->desc.usage, TextureUsage::kCopyDst), ErrorCode::kInvalidArgument,
               "copyBufferToTexture: buffer needs CopySrc and texture CopyDst"))
        return;
    auto region = resolveRegion(*t, copy);
    if (!check(region.has_value(), ErrorCode::kOutOfRange, region ? "" : region.error().message))
        return;
    const std::uint32_t bpt = formatInfo(t->desc.format).bytesPerTexel;
    const std::uint64_t bytes = transferBytes(*region, copy.bufferRowLength, bpt);
    if (!check(copy.bufferRowLength == 0 || copy.bufferRowLength >= region->extent.width, ErrorCode::kInvalidArgument,
               "copyBufferToTexture: bufferRowLength is shorter than the region") ||
        !check(copy.bufferOffset % std::max(bpt, 4u) == 0 || (copy.bufferOffset % bpt == 0 && !formatInfo(t->desc.format).isDepth()), ErrorCode::kInvalidArgument,
               "copyBufferToTexture: bufferOffset must be a multiple of the texel size") ||
        !check(rangeFits(copy.bufferOffset, bytes, b->desc.size), ErrorCode::kOutOfRange, "copyBufferToTexture: region exceeds the buffer"))
        return;
    const VkBufferImageCopy imageCopy = bufferImageCopy(*t, *region, copy.bufferOffset, copy.bufferRowLength);
    memoryBarrierAll(commandBuffer_);
    transition(*t, t->restLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, region->mip, 1, region->layer, region->layerCount);
    vkCmdCopyBufferToImage(commandBuffer_, b->buffer, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageCopy);
    transition(*t, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, t->restLayout, region->mip, 1, region->layer, region->layerCount);
    memoryBarrierAll(commandBuffer_);
}

void VkEncoder::copyTextureToBuffer(TextureHandle src, BufferHandle dst, const TextureCopy &copy)
{
    const TextureRes *t = device_.get(src);
    const BufferRes *b = device_.get(dst);
    noteBuffer(dst, b);
    if (!check(recording(), ErrorCode::kUnknown, "copyTextureToBuffer: the encoder has no command buffer") ||
        !check(!inPass_, ErrorCode::kInvalidArgument, "copyTextureToBuffer: inside a render pass") ||
        !check(b != nullptr && t != nullptr, ErrorCode::kNotFound, "copyTextureToBuffer: stale or null buffer/texture") ||
        !check(hasFlag(b->desc.usage, BufferUsage::kCopyDst) && hasFlag(t->desc.usage, TextureUsage::kCopySrc), ErrorCode::kInvalidArgument,
               "copyTextureToBuffer: texture needs CopySrc and buffer CopyDst"))
        return;
    auto region = resolveRegion(*t, copy);
    if (!check(region.has_value(), ErrorCode::kOutOfRange, region ? "" : region.error().message))
        return;
    const std::uint32_t bpt = formatInfo(t->desc.format).bytesPerTexel;
    const std::uint64_t bytes = transferBytes(*region, copy.bufferRowLength, bpt);
    if (!check(copy.bufferRowLength == 0 || copy.bufferRowLength >= region->extent.width, ErrorCode::kInvalidArgument,
               "copyTextureToBuffer: bufferRowLength is shorter than the region") ||
        !check(copy.bufferOffset % std::max(bpt, 4u) == 0 || (copy.bufferOffset % bpt == 0 && !formatInfo(t->desc.format).isDepth()), ErrorCode::kInvalidArgument,
               "copyTextureToBuffer: bufferOffset must be a multiple of the texel size") ||
        !check(rangeFits(copy.bufferOffset, bytes, b->desc.size), ErrorCode::kOutOfRange, "copyTextureToBuffer: region exceeds the buffer"))
        return;
    const VkBufferImageCopy imageCopy = bufferImageCopy(*t, *region, copy.bufferOffset, copy.bufferRowLength);
    memoryBarrierAll(commandBuffer_);
    transition(*t, t->restLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, region->mip, 1, region->layer, region->layerCount);
    vkCmdCopyImageToBuffer(commandBuffer_, t->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, b->buffer, 1, &imageCopy);
    transition(*t, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t->restLayout, region->mip, 1, region->layer, region->layerCount);
    memoryBarrierAll(commandBuffer_);
}

void VkEncoder::generateMips(TextureHandle handle)
{
    const TextureRes *t = device_.get(handle);
    if (!check(recording(), ErrorCode::kUnknown, "generateMips: the encoder has no command buffer") ||
        !check(!inPass_, ErrorCode::kInvalidArgument, "generateMips: inside a render pass") ||
        !check(t != nullptr, ErrorCode::kNotFound, "generateMips: stale or null texture") ||
        !check(!formatInfo(t->desc.format).isDepth() && formatInfo(t->desc.format).kind != FormatKind::kUint, ErrorCode::kInvalidArgument,
               "generateMips: depth and integer formats have no mip generation"))
        return;
    VkFormatProperties props{};
    vkGetPhysicalDeviceFormatProperties(device_.physicalDevice(), t->format, &props);
    const VkFormatFeatureFlags need = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if (!check((props.optimalTilingFeatures & need) == need, ErrorCode::kUnsupported,
               std::string("generateMips: format ") + toString(t->desc.format) + " cannot be filtered by a blit on this device"))
        return;
    if (t->levels < 2)
        return;
    const bool is3D = t->desc.dimension == TextureDimension::k3D;
    const std::uint32_t layers = is3D ? 1 : t->layers;
    memoryBarrierAll(commandBuffer_);
    transition(*t, t->restLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, t->levels, 0, layers);
    for (std::uint32_t level = 1; level < t->levels; ++level)
    {
        transition(*t, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, level - 1, 1, 0, layers);
        VkImageBlit blit{};
        blit.srcSubresource = {t->aspect, level - 1, 0, layers};
        blit.srcOffsets[1] = {static_cast<std::int32_t>(mipDim(t->desc.size.width, level - 1)), static_cast<std::int32_t>(mipDim(t->desc.size.height, level - 1)),
                              static_cast<std::int32_t>(is3D ? mipDim(t->desc.size.depth, level - 1) : 1)};
        blit.dstSubresource = {t->aspect, level, 0, layers};
        blit.dstOffsets[1] = {static_cast<std::int32_t>(mipDim(t->desc.size.width, level)), static_cast<std::int32_t>(mipDim(t->desc.size.height, level)),
                              static_cast<std::int32_t>(is3D ? mipDim(t->desc.size.depth, level) : 1)};
        vkCmdBlitImage(commandBuffer_, t->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                       VK_FILTER_LINEAR);
    }
    transition(*t, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t->restLayout, 0, t->levels - 1, 0, layers);
    transition(*t, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, t->restLayout, t->levels - 1, 1, 0, layers);
    memoryBarrierAll(commandBuffer_);
}

// ── Debug et minuteries ──────────────────────────────────────────────────────

void VkEncoder::beginDebugGroup(const char *name)
{
    if (!check(recording(), ErrorCode::kUnknown, "beginDebugGroup: the encoder has no command buffer"))
        return;
    if (device_.hasDebugUtils() && vkCmdBeginDebugUtilsLabelEXT != nullptr)
    {
        auto label = makeStruct<VkDebugUtilsLabelEXT>(VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT);
        label.pLabelName = name != nullptr ? name : "";
        vkCmdBeginDebugUtilsLabelEXT(commandBuffer_, &label);
    }
    ++debugDepth_;
}

void VkEncoder::endDebugGroup()
{
    if (check(debugDepth_ > 0, ErrorCode::kInvalidArgument, "endDebugGroup: no open group"))
    {
        if (device_.hasDebugUtils() && vkCmdEndDebugUtilsLabelEXT != nullptr)
            vkCmdEndDebugUtilsLabelEXT(commandBuffer_);
        --debugDepth_;
    }
}

void VkEncoder::writeTimestamp(QueryPoolHandle handle, std::uint32_t index)
{
    QueryPoolRes *pool = device_.get(handle);
    if (!check(recording(), ErrorCode::kUnknown, "writeTimestamp: the encoder has no command buffer") ||
        !check(pool != nullptr, ErrorCode::kNotFound, "writeTimestamp: stale or null query pool") ||
        !check(index < pool->count, ErrorCode::kOutOfRange, "writeTimestamp: index exceeds the pool"))
        return;
    // Une requête n'est écrite que remise à zéro (OpenGL permet de la réécrire à volonté). Hors passe : remise à zéro juste avant.
    // Dans une passe (interdit) : dans un prologue soumis avant l'encodeur ; une seconde écriture de la même requête dans la même
    // passe n'est alors pas possible.
    const std::pair<VkQueryPool, std::uint32_t> query{pool->pool, index};
    const bool already = std::find(writtenQueries_.begin(), writtenQueries_.end(), query) != writtenQueries_.end();
    if (!inPass_)
    {
        vkCmdResetQueryPool(commandBuffer_, pool->pool, index, 1);
    }
    else
    {
        if (!check(!already, ErrorCode::kUnsupported, "writeTimestamp: a query written twice by one encoder must be rewritten outside a render pass"))
            return;
        if (prologue_ == VK_NULL_HANDLE)
        {
            auto commandBuffer = device_.acquireCommandBuffer();
            if (!check(commandBuffer.has_value(), commandBuffer ? ErrorCode::kUnknown : commandBuffer.error().code,
                       commandBuffer ? "" : commandBuffer.error().message))
                return;
            prologue_ = *commandBuffer;
        }
        vkCmdResetQueryPool(prologue_, pool->pool, index, 1);
    }
    vkCmdWriteTimestamp2(commandBuffer_, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, pool->pool, index);
    if (!already)
        writtenQueries_.push_back(query);
    pool->unsubmitted = true; // pas prête avant la fin de la soumission de cet encodeur (G192)
    if (std::find(queryPools_.begin(), queryPools_.end(), handle) == queryPools_.end())
        queryPools_.push_back(handle);
}

} // namespace orosmo::rhi::vk
