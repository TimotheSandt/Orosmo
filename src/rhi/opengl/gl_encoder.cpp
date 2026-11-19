// CommandEncoder OpenGL : les commandes s'exécutent à l'appel (le contexte appartient au périphérique) ; `Device::submit`
// vérifie seulement qu'aucune erreur d'usage n'a été relevée. Chaque commande valide ses arguments avant d'appeler GL.
#include "gl_device.hpp"
#include "gl_translate.hpp"

#include "Logger.h"

#include <algorithm>
#include <limits>
#include <string>

namespace engine::rhi::gl
{

using engine::core::ErrorCode;
using engine::core::makeError;

class GlEncoder final : public CommandEncoder
{
  public:
    explicit GlEncoder(GlDevice &device) : device_(device) {}

    ~GlEncoder() override
    {
        // Une passe ou un groupe de debug laissé ouvert est une erreur d'usage, mais il ne doit pas fuir dans le contexte.
        if (inPass_)
        {
            endRenderPass();
        }
        while (debugDepth_ > 0)
        {
            endDebugGroup();
        }
    }

    const std::optional<Error> &error() const noexcept override { return error_; }
    bool hasOpenScope() const noexcept { return inPass_ || debugDepth_ > 0; }

    void beginRenderPass(const RenderPassDesc &desc) override;
    void endRenderPass() override;
    void setPipeline(PipelineHandle pipeline) override;
    void setBindGroup(std::uint32_t slot, BindGroupHandle group, std::span<const std::uint32_t> dynamicOffsets) override;
    void setVertexBuffer(std::uint32_t slot, BufferHandle buffer, std::uint64_t offset) override;
    void setIndexBuffer(BufferHandle buffer, IndexType type, std::uint64_t offset) override;
    void setPushConstants(std::span<const std::byte> data) override;
    void setViewport(const Viewport &viewport) override;
    void setScissor(const Rect &rect) override;
    void draw(std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex, std::uint32_t firstInstance) override;
    void drawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex, std::int32_t vertexOffset,
                     std::uint32_t firstInstance) override;
    void drawIndirect(BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride) override;
    void drawIndexedIndirect(BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride) override;
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

  private:
    bool check(bool condition, ErrorCode code, const std::string &message)
    {
        if (!condition)
        {
            if (!error_)
            {
                error_ = makeError(code, message).error();
            }
            LOG_ERROR(1, "RHI encoder: ", message);
        }
        return condition;
    }

    bool beforeDraw(bool indexed);
    void applyState(const PipelineRes &pipeline);
    void detachVertexBuffers();
    // Vérifie `offset + count * stride` contre `size` sans débordement (offset, pas et taille déjà bornés à 64 bits).
    static bool rangeFits(std::uint64_t offset, std::uint64_t required, std::uint64_t size) { return offset <= size && required <= size - offset; }

    GlDevice &device_;
    std::optional<Error> error_;

    // Passe
    bool inPass_ = false;
    std::vector<Format> passColorFormats_;
    Format passDepthFormat_ = Format::kUndefined;
    Extent2D passExtent_{};
    std::vector<GLenum> invalidate_;
    bool passGroupPushed_ = false;
    std::uint32_t debugDepth_ = 0;

    // État courant
    PipelineHandle pipeline_;
    PipelineHandle computePipeline_;
    struct VertexSlot
    {
        BufferHandle buffer;
        std::uint64_t offset = 0;
    };
    std::array<VertexSlot, 16> vertexBuffers_{};
    BufferHandle indexBuffer_;
    IndexType indexType_ = IndexType::kUint32;
    std::uint64_t indexOffset_ = 0;
    bool storageTouched_ = false;
    bool storageDrawInPass_ = false; // un dessin de la passe a utilisé un programme qui accède au stockage
};

std::unique_ptr<CommandEncoder> GlDevice::createCommandEncoder() { return std::make_unique<GlEncoder>(*this); }

Result<> GlDevice::submit(std::unique_ptr<CommandEncoder> encoder)
{
    if (!encoder)
    {
        return makeError(ErrorCode::kInvalidArgument, "submit: null encoder");
    }
    auto *glEncoder = static_cast<GlEncoder *>(encoder.get()); // seuls les encodeurs de ce périphérique existent
    std::optional<Error> error = glEncoder->error();
    if (!error && glEncoder->hasOpenScope())
    {
        error = makeError(ErrorCode::kInvalidArgument, "submit: a render pass or a debug group is still open").error();
    }
    encoder.reset(); // referme quand même la passe ou le groupe laissé ouvert, pour ne rien laisser dans le contexte
    if (error)
    {
        return std::unexpected(*error);
    }
    return {};
}

// ── Passes ─────────────────────────────────────────────────────────────────────────────────────────────────────────

void GlEncoder::beginRenderPass(const RenderPassDesc &desc)
{
    if (!check(!inPass_, ErrorCode::kInvalidArgument, "beginRenderPass: a pass is already open"))
    {
        return;
    }
    const Capabilities &caps = device_.capabilities();
    if (!check(desc.colors.size() <= caps.maxColorAttachments, ErrorCode::kOutOfRange, "beginRenderPass: too many color attachments") ||
        !check(!desc.colors.empty() || desc.depth.has_value(), ErrorCode::kInvalidArgument, "beginRenderPass: no attachment"))
    {
        return;
    }

    FboKey key{};
    std::array<const TextureRes *, kMaxAttachments + 1> attachments{};
    std::vector<Format> formats;
    Extent2D extent{};
    auto resolve = [&](TextureHandle handle, std::uint32_t mip, std::uint32_t layer, bool depth, std::size_t slot) -> bool
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
        {
            return false;
        }
        const Extent2D e{mipDim(t->desc.size.width, mip), mipDim(t->desc.size.height, mip)};
        if (extent.width == 0)
        {
            extent = e;
        }
        if (!check(extent == e, ErrorCode::kInvalidArgument, "beginRenderPass: attachments differ in size"))
        {
            return false;
        }
        key.texture[slot] = t->id;
        key.mip[slot] = mip;
        key.layer[slot] = layer;
        attachments[slot] = t;
        return true;
    };
    for (std::size_t i = 0; i < desc.colors.size(); ++i)
    {
        const ColorAttachment &c = desc.colors[i];
        if (!resolve(c.texture, c.mipLevel, c.layer, false, i))
        {
            return;
        }
        formats.push_back(attachments[i]->desc.format);
    }
    Format depthFormat = Format::kUndefined;
    if (desc.depth)
    {
        if (!resolve(desc.depth->texture, 0, 0, true, kMaxAttachments))
        {
            return;
        }
        depthFormat = attachments[kMaxAttachments]->desc.format;
    }

    // Toutes les valeurs d'effacement sont validées AVANT le premier effacement : un refus ne laisse aucune cible modifiée.
    for (std::size_t i = 0; i < desc.colors.size(); ++i)
    {
        const float v = desc.colors[i].clearColor[0];
        if (desc.colors[i].load == LoadOp::kClear && formatInfo(formats[i]).kind == FormatKind::kUint &&
            !check(v >= 0.0f && v < 4294967296.0f, ErrorCode::kInvalidArgument,
                   "beginRenderPass: the clear value of an integer target must be in [0, 2^32)")) // NaN, négatif, >= 2^32
        {
            return;
        }
    }

    bool toWindow = false;
    for (std::size_t i = 0; i < desc.colors.size(); ++i)
    {
        toWindow = toWindow || attachments[i]->defaultFramebuffer;
    }
    if (toWindow && !check(desc.colors.size() == 1 && !desc.depth, ErrorCode::kInvalidArgument,
                           "beginRenderPass: the window backbuffer must be the only attachment of its pass (no depth, no MRT)"))
    {
        return;
    }
    const GLuint fbo = toWindow ? 0 : device_.framebufferFor(key, static_cast<std::uint32_t>(desc.colors.size()), attachments);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    // Le backbuffer d'une fenêtre est RGBA8 linéaire par convention : le framebuffer par défaut peut être sRGB selon le visuel
    // choisi par le système (l'attribut GLFW n'est qu'une préférence), on coupe donc la conversion pour lui.
    if (toWindow)
    {
        glDisable(GL_FRAMEBUFFER_SRGB);
    }
    else
    {
        glEnable(GL_FRAMEBUFFER_SRGB);
    }
    std::array<GLenum, kMaxAttachments> drawBuffers{};
    for (std::size_t i = 0; i < desc.colors.size(); ++i)
    {
        drawBuffers[i] = toWindow ? GL_BACK : GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(i);
    }
    if (desc.colors.empty())
    {
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
    }
    else
    {
        if (toWindow)
        {
            glDrawBuffer(GL_BACK); // glDrawBuffers n'accepte pas GL_BACK (GL_INVALID_ENUM) : un seul tampon, le backbuffer
        }
        else
        {
            glDrawBuffers(static_cast<GLsizei>(desc.colors.size()), drawBuffers.data());
        }
        glReadBuffer(toWindow ? GL_BACK : GL_COLOR_ATTACHMENT0);
    }
    if (!check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, ErrorCode::kUnsupported,
               "beginRenderPass: framebuffer incomplete (" + desc.debugName + ")"))
    {
        return;
    }

    if (!desc.debugName.empty())
    {
        glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, static_cast<GLsizei>(desc.debugName.size()), desc.debugName.c_str());
        passGroupPushed_ = true;
    }

    // Effacements : l'état de masque et de ciseau d'un pipeline précédent ne doit pas les altérer.
    glDisable(GL_SCISSOR_TEST);
    glDepthMask(GL_TRUE);
    for (GLuint i = 0; i < static_cast<GLuint>(desc.colors.size()); ++i)
    {
        glColorMaski(i, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    }
    invalidate_.clear();
    for (std::size_t i = 0; i < desc.colors.size(); ++i)
    {
        const ColorAttachment &c = desc.colors[i];
        if (c.load == LoadOp::kClear)
        {
            if (formatInfo(formats[i]).kind == FormatKind::kUint)
            {
                const GLuint value[4] = {static_cast<GLuint>(c.clearColor[0]), 0, 0, 0}; // représentable : validé plus haut
                glClearBufferuiv(GL_COLOR, static_cast<GLint>(i), value);
            }
            else
            {
                glClearBufferfv(GL_COLOR, static_cast<GLint>(i), c.clearColor.data());
            }
        }
        if (c.store == StoreOp::kDontCare)
        {
            invalidate_.push_back(toWindow ? GL_COLOR : GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(i));
        }
    }
    if (desc.depth)
    {
        const DepthAttachment &d = *desc.depth;
        if (d.load == LoadOp::kClear)
        {
            if (depthFormat == Format::kD24UnormS8Uint)
            {
                glClearBufferfi(GL_DEPTH_STENCIL, 0, d.clearDepth, static_cast<GLint>(d.clearStencil));
            }
            else
            {
                glClearBufferfv(GL_DEPTH, 0, &d.clearDepth);
            }
        }
        if (d.store == StoreOp::kDontCare)
        {
            invalidate_.push_back(depthFormat == Format::kD24UnormS8Uint ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT);
        }
    }
    glEnable(GL_SCISSOR_TEST);
    glViewport(0, 0, static_cast<GLsizei>(extent.width), static_cast<GLsizei>(extent.height));
    glScissor(0, 0, static_cast<GLsizei>(extent.width), static_cast<GLsizei>(extent.height));
    glDepthRangef(0.0f, 1.0f); // la plage d'une passe précédente (setViewport) ne fuit pas

    inPass_ = true;
    passColorFormats_ = std::move(formats);
    passDepthFormat_ = depthFormat;
    passExtent_ = extent;
    pipeline_ = {};
    storageTouched_ = false;
    storageDrawInPass_ = false;
}

void GlEncoder::endRenderPass()
{
    if (!check(inPass_, ErrorCode::kInvalidArgument, "endRenderPass: no pass is open"))
    {
        return;
    }
    detachVertexBuffers();
    if (!invalidate_.empty())
    {
        glInvalidateFramebuffer(GL_FRAMEBUFFER, static_cast<GLsizei>(invalidate_.size()), invalidate_.data());
    }
    if (storageTouched_ || storageDrawInPass_)
    {
        glMemoryBarrier(GL_ALL_BARRIER_BITS); // RHI-09 : le RHI calcule les barrières ; ici, conservatrice
    }
    if (passGroupPushed_)
    {
        glPopDebugGroup();
        passGroupPushed_ = false;
    }
    inPass_ = false;
    pipeline_ = {};
}

// ── État ───────────────────────────────────────────────────────────────────────────────────────────────────────────

void GlEncoder::applyState(const PipelineRes &p)
{
    glUseProgram(p.program);
    glBindVertexArray(p.vao);
    glPolygonMode(GL_FRONT_AND_BACK, p.raster.fill == FillMode::kLine ? GL_LINE : GL_FILL);
    if (p.raster.cull == CullMode::kNone)
    {
        glDisable(GL_CULL_FACE);
    }
    else
    {
        glEnable(GL_CULL_FACE);
        glCullFace(p.raster.cull == CullMode::kFront ? GL_FRONT : GL_BACK);
    }
    glFrontFace(p.raster.frontFace == FrontFace::kClockwise ? GL_CW : GL_CCW);
    if (p.depth.test)
    {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(glCompare(p.depth.compare));
    }
    else
    {
        glDisable(GL_DEPTH_TEST);
    }
    glDepthMask(p.depth.write ? GL_TRUE : GL_FALSE);
    for (GLuint i = 0; i < static_cast<GLuint>(p.colorTargets.size()); ++i)
    {
        const ColorTargetState &t = p.colorTargets[i];
        if (t.blend.enabled)
        {
            glEnablei(GL_BLEND, i);
            glBlendFuncSeparatei(i, glBlendFactor(t.blend.srcColor), glBlendFactor(t.blend.dstColor), glBlendFactor(t.blend.srcAlpha),
                                 glBlendFactor(t.blend.dstAlpha));
            glBlendEquationSeparatei(i, glBlendOp(t.blend.colorOp), glBlendOp(t.blend.alphaOp));
        }
        else
        {
            glDisablei(GL_BLEND, i);
        }
        const auto m = static_cast<std::uint8_t>(t.writeMask);
        glColorMaski(i, (m & 1) != 0, (m & 2) != 0, (m & 4) != 0, (m & 8) != 0);
    }
    if (p.topology == GL_POINTS)
    {
        glEnable(GL_PROGRAM_POINT_SIZE);
    }
    else
    {
        glDisable(GL_PROGRAM_POINT_SIZE);
    }
}

// Les liaisons de tampons (sommets, indices) sont un état du VAO du pipeline : laissées en place, elles retiendraient un tampon
// détruit tant que ce VAO vit. On les retire quand le pipeline change et à la fin de la passe.
void GlEncoder::detachVertexBuffers()
{
    const PipelineRes *p = device_.get(pipeline_);
    if (p == nullptr)
    {
        return;
    }
    glBindVertexArray(p->vao);
    for (std::uint32_t binding = 0; binding < vertexBuffers_.size(); ++binding)
    {
        if (p->bindingUsed[binding])
        {
            glBindVertexBuffer(binding, 0, 0, static_cast<GLsizei>(p->strides[binding]));
        }
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBindVertexArray(device_.emptyVertexArray());
}

void GlEncoder::setPipeline(PipelineHandle handle)
{
    if (!check(inPass_, ErrorCode::kInvalidArgument, "setPipeline: outside a render pass"))
    {
        return;
    }
    const PipelineRes *p = device_.get(handle);
    if (!check(p != nullptr && !p->compute, ErrorCode::kNotFound, "setPipeline: stale, null or compute pipeline handle"))
    {
        return;
    }
    bool compatible = p->colorTargets.size() == passColorFormats_.size() && p->depthFormat == passDepthFormat_;
    for (std::size_t i = 0; compatible && i < passColorFormats_.size(); ++i)
    {
        compatible = p->colorTargets[i].format == passColorFormats_[i];
    }
    if (!check(compatible, ErrorCode::kInvalidArgument, "setPipeline: pipeline attachment formats differ from the render pass"))
    {
        return;
    }
    detachVertexBuffers();
    applyState(*p);
    pipeline_ = handle;
}

void GlEncoder::setBindGroup(std::uint32_t slot, BindGroupHandle handle, std::span<const std::uint32_t> dynamicOffsets)
{
    const BindGroupRes *group = device_.get(handle);
    if (!check(slot < kGroupCount, ErrorCode::kOutOfRange, "setBindGroup: slot >= kGroupCount") ||
        !check(group != nullptr, ErrorCode::kNotFound, "setBindGroup: stale or null bind group"))
    {
        return;
    }
    std::size_t expectedDynamic = 0;
    for (const BindGroupEntry &e : group->entries)
    {
        expectedDynamic += e.dynamicOffset ? 1 : 0;
    }
    if (!check(dynamicOffsets.size() == expectedDynamic, ErrorCode::kInvalidArgument, "setBindGroup: wrong number of dynamic offsets"))
    {
        return;
    }
    const Capabilities &caps = device_.capabilities();
    std::size_t dyn = 0;
    for (const BindGroupEntry &e : group->entries)
    {
        const GLuint unit = flatBinding(slot, e.binding);
        switch (e.type)
        {
            case BindingType::kUniformBuffer:
            case BindingType::kStorageBuffer:
            {
                const BufferRes *b = device_.get(e.buffer);
                if (!check(b != nullptr, ErrorCode::kNotFound, "setBindGroup: a buffer of the group was destroyed"))
                {
                    return;
                }
                const std::uint64_t extra = e.dynamicOffset ? dynamicOffsets[dyn++] : 0;
                const std::uint64_t offset = e.offset + extra;
                const std::uint64_t align = e.type == BindingType::kUniformBuffer ? caps.minUniformAlignment : caps.minStorageAlignment;
                const std::uint64_t size = e.size != 0 ? e.size : b->desc.size - std::min(offset, b->desc.size);
                if (!check(offset % align == 0, ErrorCode::kInvalidArgument, "setBindGroup: dynamic offset is not aligned") ||
                    !check(offset <= b->desc.size && size <= b->desc.size - offset && size > 0, ErrorCode::kOutOfRange,
                           "setBindGroup: range exceeds the buffer (" + b->desc.debugName + ")"))
                {
                    return;
                }
                if (!check(e.type != BindingType::kStorageBuffer || unit < caps.maxStorageBufferBindings, ErrorCode::kOutOfRange,
                           "setBindGroup: the flat storage buffer binding exceeds the device limit (Capabilities::maxStorageBufferBindings)"))
                {
                    return;
                }
                const GLenum target = e.type == BindingType::kUniformBuffer ? GL_UNIFORM_BUFFER : GL_SHADER_STORAGE_BUFFER;
                glBindBufferRange(target, unit, b->id, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(size));
                storageTouched_ = storageTouched_ || e.type == BindingType::kStorageBuffer;
                break;
            }
            case BindingType::kSampledTexture:
            {
                const TextureRes *t = device_.get(e.texture);
                const SamplerRes *s = device_.get(e.sampler);
                if (!check(t != nullptr && s != nullptr, ErrorCode::kNotFound, "setBindGroup: a texture or sampler of the group was destroyed"))
                {
                    return;
                }
                glActiveTexture(GL_TEXTURE0 + unit);
                glBindTexture(t->target, t->id);
                glBindSampler(unit, s->id);
                break;
            }
            case BindingType::kStorageImage:
            {
                const TextureRes *t = device_.get(e.texture);
                if (!check(t != nullptr, ErrorCode::kNotFound, "setBindGroup: a texture of the group was destroyed") ||
                    !check(unit < static_cast<GLuint>(device_.maxImageUnits_), ErrorCode::kOutOfRange,
                           "setBindGroup: the flat image unit exceeds GL_MAX_IMAGE_UNITS; use a lower group/binding for storage images") ||
                    !check(e.mipLevel < t->levels, ErrorCode::kOutOfRange, "setBindGroup: storage image mip level out of range"))
                {
                    return;
                }
                const bool layered = t->desc.dimension != TextureDimension::k2D;
                glBindImageTexture(unit, t->id, static_cast<GLint>(e.mipLevel), layered ? GL_TRUE : GL_FALSE, 0, GL_READ_WRITE,
                                   glFormatOf(t->desc.format).internal);
                storageTouched_ = true;
                break;
            }
        }
    }
}

void GlEncoder::setVertexBuffer(std::uint32_t slot, BufferHandle handle, std::uint64_t offset)
{
    const BufferRes *b = device_.get(handle);
    if (check(slot < vertexBuffers_.size(), ErrorCode::kOutOfRange, "setVertexBuffer: slot >= 16") &&
        check(b != nullptr, ErrorCode::kNotFound, "setVertexBuffer: stale or null buffer") &&
        check(hasFlag(b->desc.usage, BufferUsage::kVertex), ErrorCode::kInvalidArgument, "setVertexBuffer: buffer lacks the Vertex usage") &&
        check(offset <= b->desc.size, ErrorCode::kOutOfRange, "setVertexBuffer: offset exceeds the buffer"))
    {
        vertexBuffers_[slot] = {handle, offset};
    }
}

void GlEncoder::setIndexBuffer(BufferHandle handle, IndexType type, std::uint64_t offset)
{
    const BufferRes *b = device_.get(handle);
    if (check(b != nullptr, ErrorCode::kNotFound, "setIndexBuffer: stale or null buffer") &&
        check(hasFlag(b->desc.usage, BufferUsage::kIndex), ErrorCode::kInvalidArgument, "setIndexBuffer: buffer lacks the Index usage") &&
        check(offset <= b->desc.size, ErrorCode::kOutOfRange, "setIndexBuffer: offset exceeds the buffer"))
    {
        indexBuffer_ = handle;
        indexType_ = type;
        indexOffset_ = offset;
    }
}

void GlEncoder::setPushConstants(std::span<const std::byte> data)
{
    if (check(data.size() <= device_.capabilities().maxPushConstantSize && data.size() % 4 == 0, ErrorCode::kInvalidArgument,
              "setPushConstants: size must be a multiple of 4 and at most maxPushConstantSize"))
    {
        glBindBuffer(GL_UNIFORM_BUFFER, device_.pushConstantBuffer());
        glBufferSubData(GL_UNIFORM_BUFFER, 0, static_cast<GLsizeiptr>(data.size()), data.data());
    }
}

void GlEncoder::setViewport(const Viewport &v)
{
    if (check(inPass_, ErrorCode::kInvalidArgument, "setViewport: outside a render pass"))
    {
        glViewport(static_cast<GLint>(v.x), static_cast<GLint>(v.y), static_cast<GLsizei>(v.width), static_cast<GLsizei>(v.height));
        glDepthRangef(v.minDepth, v.maxDepth);
    }
}

void GlEncoder::setScissor(const Rect &r)
{
    if (check(inPass_, ErrorCode::kInvalidArgument, "setScissor: outside a render pass"))
    {
        glScissor(r.x, r.y, static_cast<GLsizei>(r.width), static_cast<GLsizei>(r.height));
    }
}

// ── Dessins ────────────────────────────────────────────────────────────────────────────────────────────────────────

bool GlEncoder::beforeDraw(bool indexed)
{
    const PipelineRes *p = device_.get(pipeline_);
    if (!check(inPass_, ErrorCode::kInvalidArgument, "draw: outside a render pass") ||
        !check(p != nullptr, ErrorCode::kInvalidArgument, "draw: no pipeline set (or it was destroyed)"))
    {
        return false;
    }
    for (std::uint32_t binding = 0; binding < vertexBuffers_.size(); ++binding)
    {
        if (!p->bindingUsed[binding])
        {
            continue;
        }
        const BufferRes *b = device_.get(vertexBuffers_[binding].buffer);
        if (!check(b != nullptr, ErrorCode::kInvalidArgument, "draw: vertex buffer binding " + std::to_string(binding) + " is not set"))
        {
            return false;
        }
        glBindVertexBuffer(binding, b->id, static_cast<GLintptr>(vertexBuffers_[binding].offset), static_cast<GLsizei>(p->strides[binding]));
    }
    if (indexed)
    {
        const BufferRes *ib = device_.get(indexBuffer_);
        if (!check(ib != nullptr, ErrorCode::kInvalidArgument, "drawIndexed: no index buffer set (or it was destroyed)"))
        {
            return false;
        }
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib->id);
    }
    // Un dessin précédent de la passe dont le programme accède au stockage (SSBO, images) a pu écrire ce que celui-ci lit : sans
    // barrière, OpenGL n'en garantit pas l'ordre. Le critère est le PROGRAMME (réflexion à l'édition de liens), pas l'historique
    // des liaisons : une liaison faite avant la passe est couverte, et les passes sans stockage (interface) n'en paient rien.
    if (p->usesStorage && storageDrawInPass_)
    {
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT |
                        GL_ELEMENT_ARRAY_BARRIER_BIT | GL_COMMAND_BARRIER_BIT | GL_UNIFORM_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    }
    storageDrawInPass_ = storageDrawInPass_ || p->usesStorage;
    return true;
}

void GlEncoder::draw(std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex, std::uint32_t firstInstance)
{
    if (!beforeDraw(false))
    {
        return;
    }
    const GLenum topology = device_.get(pipeline_)->topology;
    glDrawArraysInstancedBaseInstance(topology, static_cast<GLint>(firstVertex), static_cast<GLsizei>(vertexCount),
                                      static_cast<GLsizei>(instanceCount), firstInstance);
}

void GlEncoder::drawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex, std::int32_t vertexOffset,
                            std::uint32_t firstInstance)
{
    if (!beforeDraw(true))
    {
        return;
    }
    const bool wide = indexType_ == IndexType::kUint32;
    const std::uint64_t offset = indexOffset_ + static_cast<std::uint64_t>(firstIndex) * (wide ? 4u : 2u);
    glDrawElementsInstancedBaseVertexBaseInstance(device_.get(pipeline_)->topology, static_cast<GLsizei>(indexCount),
                                                  wide ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT, reinterpret_cast<const void *>(offset),
                                                  static_cast<GLsizei>(instanceCount), vertexOffset, firstInstance);
}

void GlEncoder::drawIndirect(BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride)
{
    const BufferRes *b = device_.get(args);
    const std::uint64_t step = stride != 0 ? stride : sizeof(DrawIndirectCommand);
    if (!check(b != nullptr && hasFlag(b->desc.usage, BufferUsage::kIndirect), ErrorCode::kInvalidArgument,
               "drawIndirect: args buffer is stale or lacks the Indirect usage") ||
        !check(offset % 4 == 0 && stride % 4 == 0 && (stride == 0 || stride >= sizeof(DrawIndirectCommand)), ErrorCode::kInvalidArgument,
               "drawIndirect: offset and stride must be multiples of 4, stride 0 or at least one command") ||
        !check(drawCount > 0 && rangeFits(offset, step * (drawCount - 1) + sizeof(DrawIndirectCommand), b->desc.size), ErrorCode::kOutOfRange,
               "drawIndirect: commands exceed the buffer") ||
        !beforeDraw(false))
    {
        return;
    }
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, b->id);
    glMultiDrawArraysIndirect(device_.get(pipeline_)->topology, reinterpret_cast<const void *>(offset), static_cast<GLsizei>(drawCount),
                              static_cast<GLsizei>(stride));
}

void GlEncoder::drawIndexedIndirect(BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride)
{
    const BufferRes *b = device_.get(args);
    const std::uint64_t step = stride != 0 ? stride : sizeof(DrawIndexedIndirectCommand);
    if (!check(b != nullptr && hasFlag(b->desc.usage, BufferUsage::kIndirect), ErrorCode::kInvalidArgument,
               "drawIndexedIndirect: args buffer is stale or lacks the Indirect usage") ||
        !check(offset % 4 == 0 && stride % 4 == 0 && (stride == 0 || stride >= sizeof(DrawIndexedIndirectCommand)), ErrorCode::kInvalidArgument,
               "drawIndexedIndirect: offset and stride must be multiples of 4, stride 0 or at least one command") ||
        !check(drawCount > 0 && rangeFits(offset, step * (drawCount - 1) + sizeof(DrawIndexedIndirectCommand), b->desc.size),
               ErrorCode::kOutOfRange, "drawIndexedIndirect: commands exceed the buffer") ||
        !check(indexOffset_ == 0, ErrorCode::kUnsupported,
               "drawIndexedIndirect: a non-zero index buffer offset is not supported (GL reads indices from byte 0 plus firstIndex)") ||
        !beforeDraw(true))
    {
        return;
    }
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, b->id);
    glMultiDrawElementsIndirect(device_.get(pipeline_)->topology, indexType_ == IndexType::kUint32 ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT,
                                reinterpret_cast<const void *>(offset), static_cast<GLsizei>(drawCount), static_cast<GLsizei>(stride));
}

// ── Calcul ─────────────────────────────────────────────────────────────────────────────────────────────────────────

void GlEncoder::bindComputePipeline(PipelineHandle handle)
{
    const PipelineRes *p = device_.get(handle);
    if (check(!inPass_, ErrorCode::kInvalidArgument, "bindComputePipeline: inside a render pass") &&
        check(p != nullptr && p->compute, ErrorCode::kNotFound, "bindComputePipeline: stale, null or graphics pipeline handle"))
    {
        glUseProgram(p->program);
        computePipeline_ = handle;
    }
}

void GlEncoder::dispatch(std::uint32_t x, std::uint32_t y, std::uint32_t z)
{
    const Capabilities &caps = device_.capabilities();
    if (check(!inPass_, ErrorCode::kInvalidArgument, "dispatch: inside a render pass") &&
        check(device_.get(computePipeline_) != nullptr, ErrorCode::kInvalidArgument, "dispatch: no compute pipeline bound") &&
        check(x <= caps.maxComputeWorkGroupCount[0] && y <= caps.maxComputeWorkGroupCount[1] && z <= caps.maxComputeWorkGroupCount[2],
              ErrorCode::kOutOfRange, "dispatch: group count exceeds the device limit"))
    {
        glUseProgram(device_.get(computePipeline_)->program); // une passe graphique a pu changer le programme courant
        glDispatchCompute(x, y, z);
        glMemoryBarrier(GL_ALL_BARRIER_BITS);
    }
}

void GlEncoder::dispatchIndirect(BufferHandle args, std::uint64_t offset)
{
    const BufferRes *b = device_.get(args);
    if (check(!inPass_, ErrorCode::kInvalidArgument, "dispatchIndirect: inside a render pass") &&
        check(device_.get(computePipeline_) != nullptr, ErrorCode::kInvalidArgument, "dispatchIndirect: no compute pipeline bound") &&
        check(b != nullptr && hasFlag(b->desc.usage, BufferUsage::kIndirect), ErrorCode::kInvalidArgument,
              "dispatchIndirect: args buffer is stale or lacks the Indirect usage") &&
        check(rangeFits(offset, sizeof(DispatchIndirectCommand), b->desc.size) && offset % 4 == 0, ErrorCode::kOutOfRange,
              "dispatchIndirect: offset out of range or unaligned"))
    {
        glUseProgram(device_.get(computePipeline_)->program);
        glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, b->id);
        glDispatchComputeIndirect(static_cast<GLintptr>(offset));
        glMemoryBarrier(GL_ALL_BARRIER_BITS);
    }
}

// ── Copies ─────────────────────────────────────────────────────────────────────────────────────────────────────────

namespace
{
// Octets réellement touchés dans le tampon : toutes les tranches et lignes au pas `rowLength` (texels, 0 : serrées), sauf le
// remplissage après la dernière ligne de la dernière tranche.
std::uint64_t transferBytes(const SliceRegion &region, std::uint32_t rowLength, std::uint32_t bytesPerTexel)
{
    const std::uint64_t row = rowLength != 0 ? rowLength : region.size.width;
    const std::uint64_t slice = row * region.size.height;
    return ((region.size.depth - 1) * slice + (region.size.height - 1) * row + region.size.width) * bytesPerTexel;
}
} // namespace

void GlEncoder::copyBuffer(BufferHandle src, BufferHandle dst, const BufferCopy &r)
{
    const BufferRes *s = device_.get(src);
    const BufferRes *d = device_.get(dst);
    if (check(!inPass_, ErrorCode::kInvalidArgument, "copyBuffer: inside a render pass") &&
        check(s != nullptr && d != nullptr, ErrorCode::kNotFound, "copyBuffer: stale or null buffer") &&
        check(hasFlag(s->desc.usage, BufferUsage::kCopySrc) && hasFlag(d->desc.usage, BufferUsage::kCopyDst), ErrorCode::kInvalidArgument,
              "copyBuffer: source needs CopySrc and destination CopyDst") &&
        check(r.size > 0 && rangeFits(r.srcOffset, r.size, s->desc.size) && rangeFits(r.dstOffset, r.size, d->desc.size), ErrorCode::kOutOfRange,
              "copyBuffer: empty range or range exceeds a buffer") &&
        check(src != dst || r.srcOffset + r.size <= r.dstOffset || r.dstOffset + r.size <= r.srcOffset, ErrorCode::kInvalidArgument,
              "copyBuffer: overlapping ranges in the same buffer"))
    {
        glBindBuffer(GL_COPY_READ_BUFFER, s->id);
        glBindBuffer(GL_COPY_WRITE_BUFFER, d->id);
        glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, static_cast<GLintptr>(r.srcOffset), static_cast<GLintptr>(r.dstOffset),
                            static_cast<GLsizeiptr>(r.size));
    }
}

void GlEncoder::copyBufferToTexture(BufferHandle src, TextureHandle dst, const TextureCopy &copy)
{
    const BufferRes *b = device_.get(src);
    const TextureRes *t = device_.get(dst);
    if (!check(!inPass_, ErrorCode::kInvalidArgument, "copyBufferToTexture: inside a render pass") ||
        !check(b != nullptr && t != nullptr, ErrorCode::kNotFound, "copyBufferToTexture: stale or null buffer/texture") ||
        !check(hasFlag(b->desc.usage, BufferUsage::kCopySrc) && hasFlag(t->desc.usage, TextureUsage::kCopyDst), ErrorCode::kInvalidArgument,
               "copyBufferToTexture: buffer needs CopySrc and texture CopyDst"))
    {
        return;
    }
    auto region = resolveRegion(*t, copy);
    if (!check(region.has_value(), ErrorCode::kOutOfRange, region ? "" : region.error().message) ||
        !check(!t->defaultFramebuffer, ErrorCode::kInvalidArgument, "copyBufferToTexture: the window backbuffer cannot be a copy destination"))
    {
        return;
    }
    const std::uint64_t bytes = transferBytes(*region, copy.bufferRowLength, formatInfo(t->desc.format).bytesPerTexel);
    if (!check(copy.bufferRowLength == 0 || copy.bufferRowLength >= region->size.width, ErrorCode::kInvalidArgument,
               "copyBufferToTexture: bufferRowLength is shorter than the region") ||
        !check(copy.bufferRowLength <= static_cast<std::uint32_t>(std::numeric_limits<GLint>::max()), ErrorCode::kOutOfRange,
               "copyBufferToTexture: bufferRowLength does not fit in a GLint") ||
        !check(rangeFits(copy.bufferOffset, bytes, b->desc.size), ErrorCode::kOutOfRange, "copyBufferToTexture: region exceeds the buffer"))
    {
        return;
    }
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, b->id);
    device_.uploadRegion(*t, *region, copy.bufferRowLength, static_cast<std::uintptr_t>(copy.bufferOffset));
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
}

void GlEncoder::copyTextureToBuffer(TextureHandle src, BufferHandle dst, const TextureCopy &copy)
{
    const TextureRes *t = device_.get(src);
    const BufferRes *b = device_.get(dst);
    if (!check(!inPass_, ErrorCode::kInvalidArgument, "copyTextureToBuffer: inside a render pass") ||
        !check(b != nullptr && t != nullptr, ErrorCode::kNotFound, "copyTextureToBuffer: stale or null buffer/texture") ||
        !check(hasFlag(b->desc.usage, BufferUsage::kCopyDst) && hasFlag(t->desc.usage, TextureUsage::kCopySrc), ErrorCode::kInvalidArgument,
               "copyTextureToBuffer: texture needs CopySrc and buffer CopyDst"))
    {
        return;
    }
    auto region = resolveRegion(*t, copy);
    if (!check(region.has_value(), ErrorCode::kOutOfRange, region ? "" : region.error().message))
    {
        return;
    }
    const std::uint64_t bytes = transferBytes(*region, copy.bufferRowLength, formatInfo(t->desc.format).bytesPerTexel);
    if (!check(copy.bufferRowLength == 0 || copy.bufferRowLength >= region->size.width, ErrorCode::kInvalidArgument,
               "copyTextureToBuffer: bufferRowLength is shorter than the region") ||
        !check(copy.bufferRowLength <= static_cast<std::uint32_t>(std::numeric_limits<GLint>::max()), ErrorCode::kOutOfRange,
               "copyTextureToBuffer: bufferRowLength does not fit in a GLint") ||
        !check(rangeFits(copy.bufferOffset, bytes, b->desc.size), ErrorCode::kOutOfRange, "copyTextureToBuffer: region exceeds the buffer"))
    {
        return;
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, b->id);
    auto r = device_.readRegion(*t, *region, copy.bufferRowLength, static_cast<std::uintptr_t>(copy.bufferOffset));
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    check(r.has_value(), ErrorCode::kUnsupported, r ? "" : r.error().message);
}

void GlEncoder::generateMips(TextureHandle handle)
{
    const TextureRes *t = device_.get(handle);
    if (check(!inPass_, ErrorCode::kInvalidArgument, "generateMips: inside a render pass") &&
        check(t != nullptr, ErrorCode::kNotFound, "generateMips: stale or null texture") &&
        check(!t->defaultFramebuffer, ErrorCode::kInvalidArgument, "generateMips: the window backbuffer has no mip chain") &&
        check(!formatInfo(t->desc.format).isDepth() && formatInfo(t->desc.format).kind != FormatKind::kUint, ErrorCode::kInvalidArgument,
              "generateMips: depth and integer formats have no mip generation"))
    {
        GlDevice::bindScratchTexture(t->target, t->id); // n'écrase pas une texture liée par setBindGroup
        glGenerateMipmap(t->target);
    }
}

// ── Debug et minuteries ────────────────────────────────────────────────────────────────────────────────────────────

void GlEncoder::beginDebugGroup(const char *name)
{
    glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, name);
    ++debugDepth_;
}

void GlEncoder::endDebugGroup()
{
    if (check(debugDepth_ > 0, ErrorCode::kInvalidArgument, "endDebugGroup: no open group"))
    {
        glPopDebugGroup();
        --debugDepth_;
    }
}

void GlEncoder::writeTimestamp(QueryPoolHandle handle, std::uint32_t index)
{
    QueryPoolRes *pool = device_.get(handle);
    if (check(pool != nullptr, ErrorCode::kNotFound, "writeTimestamp: stale or null query pool") &&
        check(index < pool->ids.size(), ErrorCode::kOutOfRange, "writeTimestamp: index exceeds the pool"))
    {
        glQueryCounter(pool->ids[index], GL_TIMESTAMP);
        if (!pool->written[index])
        {
            GlDevice::labelQuery(*pool, index); // l'objet existe maintenant
        }
        pool->written[index] = true;
    }
}

} // namespace engine::rhi::gl
