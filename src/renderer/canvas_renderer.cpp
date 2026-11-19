#include "engine/renderer/canvas_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace engine::renderer
{

using core::ErrorCode;
using core::makeError;

namespace
{
struct RectPush
{
    float offsetScale[4];
    float container[4];
    float color[4];
};
struct GlyphPush
{
    float params[4];
    float color[4];
};
struct LayerPush
{
    float offsetScale[4];
    float containerScroll[4];
    float content[4];
    float color[4];
};
static_assert(sizeof(RectPush) == 48 && sizeof(GlyphPush) == 32 && sizeof(LayerPush) == 64, "blocs push_constant des shaders ui_*");

template <typename T> std::span<const std::byte> bytesOf(const T &v) { return std::as_bytes(std::span(&v, 1)); }
} // namespace

core::Result<std::unique_ptr<CanvasRenderer>> CanvasRenderer::create(rhi::Device &device, ShaderLibrary &library, PipelineCache &pipelines,
                                                                     const std::filesystem::path &shaderDirectory)
{
    std::unique_ptr<CanvasRenderer> canvas(new CanvasRenderer(device, pipelines));
    auto rect = library.loadGraphics(shaderDirectory / "ui_rect.vert", shaderDirectory / "ui_rect.frag");
    auto glyph = library.loadGraphics(shaderDirectory / "ui_glyph.vert", shaderDirectory / "ui_glyph.frag");
    auto layer = library.loadGraphics(shaderDirectory / "ui_layer.vert", shaderDirectory / "ui_layer.frag");
    if (!rect || !glyph || !layer)
    {
        return std::unexpected(!rect ? rect.error() : (!glyph ? glyph.error() : layer.error()));
    }
    canvas->rect_ = *rect;
    canvas->glyph_ = *glyph;
    canvas->layerImage_ = *layer;
    rhi::SamplerDesc sd;
    sd.mipFilter = std::nullopt;
    sd.addressU = rhi::AddressMode::kClampToEdge;
    sd.addressV = rhi::AddressMode::kClampToEdge;
    sd.debugName = "ui.atlas";
    auto sampler = device.createSampler(sd);
    if (!sampler)
    {
        return std::unexpected(sampler.error());
    }
    canvas->atlasSampler_ = *sampler;
    return canvas;
}

CanvasRenderer::~CanvasRenderer()
{
    clear();
    device_.destroy(atlasSampler_);
}

void CanvasRenderer::releaseLayer(Layer &layer)
{
    device_.destroy(layer.group);
    layer.group = {};
    layer.target.reset();
}

void CanvasRenderer::clear()
{
    for (auto &[id, layer] : layers_)
    {
        releaseLayer(layer);
    }
    layers_.clear();
    for (auto &[id, atlas] : atlases_)
    {
        device_.destroy(atlas.group);
        atlas.texture.reset();
    }
    atlases_.clear();
}

core::Result<rhi::PipelineHandle> CanvasRenderer::pipeline(const ShaderProgram &program, rhi::Format format, Blend blend, bool glyph)
{
    GraphicsState state;
    state.raster.cull = rhi::CullMode::kNone;
    rhi::ColorTargetState target;
    target.format = format;
    if (blend == Blend::kStraight)
    {
        // Même mélange que l'ancien SetAlphaBlend : couleur par alpha source, alpha cumulé. Sur une couche effacée en transparent,
        // il accumule une couleur PRÉMULTIPLIÉE (rgb * a), d'où le mélange kPremultiplied pour composer la couche ensuite.
        target.blend = {true, rhi::BlendFactor::kSrcAlpha, rhi::BlendFactor::kOneMinusSrcAlpha, rhi::BlendOp::kAdd,
                        rhi::BlendFactor::kOne, rhi::BlendFactor::kOneMinusSrcAlpha, rhi::BlendOp::kAdd};
    }
    else if (blend == Blend::kPremultiplied)
    {
        target.blend = {true, rhi::BlendFactor::kOne, rhi::BlendFactor::kOneMinusSrcAlpha, rhi::BlendOp::kAdd,
                        rhi::BlendFactor::kOne, rhi::BlendFactor::kOneMinusSrcAlpha, rhi::BlendOp::kAdd};
    }
    state.colorTargets.assign(1, target);
    if (glyph)
    {
        state.vertexBuffers.push_back({.binding = 0, .stride = 16, .stepRate = rhi::VertexStepRate::kVertex});
        state.vertexAttributes.push_back({.location = 0, .binding = 0, .format = rhi::VertexFormat::kFloat4, .offset = 0});
    }
    state.debugName = program.name() + (blend == Blend::kReplace ? ".replace" : blend == Blend::kPremultiplied ? ".premultiplied" : "");
    return pipelines_.graphics(program, state);
}

core::Result<CanvasRenderer::Atlas *> CanvasRenderer::atlasFor(const AtlasImage &image)
{
    if (auto it = atlases_.find(image.id); it != atlases_.end())
    {
        return &it->second;
    }
    if (image.width == 0 || image.height == 0 || image.rgba.size() != static_cast<std::size_t>(image.width) * image.height * 4)
    {
        return makeError(ErrorCode::kInvalidArgument, "CanvasRenderer: invalid glyph atlas");
    }
    TextureInfo info;
    info.width = image.width;
    info.height = image.height;
    info.filter = FilterMode::kLinearNoMipmaps;
    info.address = rhi::AddressMode::kClampToEdge;
    info.debugName = "ui.atlas." + std::to_string(image.id);
    // Les lignes de l'atlas sont envoyées telles quelles : la ligne 0 est v = 0 (les coordonnées des glyphes en dépendent).
    auto texture = Texture::create(device_, info, std::as_bytes(std::span(image.rgba)));
    if (!texture)
    {
        return std::unexpected(texture.error());
    }
    rhi::BindGroupDesc desc;
    desc.entries.push_back({.binding = 0, .type = rhi::BindingType::kSampledTexture, .texture = texture->handle(), .sampler = texture->sampler()});
    desc.debugName = info.debugName;
    auto group = device_.createBindGroup(desc);
    if (!group)
    {
        return std::unexpected(group.error());
    }
    Atlas atlas;
    atlas.texture = std::move(*texture);
    atlas.group = *group;
    return &atlases_.emplace(image.id, std::move(atlas)).first->second;
}

core::Result<CanvasRenderer::Layer *> CanvasRenderer::layerFor(std::uint64_t id, std::uint32_t width, std::uint32_t height, bool &created)
{
    created = false;
    auto it = layers_.find(id);
    if (it != layers_.end() && it->second.width == width && it->second.height == height)
    {
        return &it->second;
    }
    if (it != layers_.end())
    {
        releaseLayer(it->second);
        layers_.erase(it);
    }
    RenderTargetInfo info;
    info.size = {width, height};
    info.depth = false;
    info.debugName = "ui.layer." + std::to_string(id);
    auto target = RenderTarget::create(device_, info);
    if (!target)
    {
        return std::unexpected(target.error());
    }
    rhi::BindGroupDesc desc;
    desc.entries.push_back({.binding = 0, .type = rhi::BindingType::kSampledTexture, .texture = target->color(0), .sampler = target->sampler()});
    desc.debugName = info.debugName;
    auto group = device_.createBindGroup(desc);
    if (!group)
    {
        return std::unexpected(group.error());
    }
    Layer layer;
    layer.target = std::move(*target);
    layer.group = *group;
    layer.width = width;
    layer.height = height;
    created = true;
    return &layers_.emplace(id, std::move(layer)).first->second;
}

CanvasRenderer::Clip CanvasRenderer::fullClip(const Surface &s) const { return {0, 0, static_cast<int>(s.px.width), static_cast<int>(s.px.height)}; }

void CanvasRenderer::applyClip(rhi::CommandEncoder &encoder, const Surface &surface)
{
    const Clip c = surface.clips.empty() ? fullClip(surface) : surface.clips.back();
    encoder.setScissor({c.left, c.bottom, static_cast<std::uint32_t>(std::max(c.right - c.left, 0)), static_cast<std::uint32_t>(std::max(c.top - c.bottom, 0))});
}

void CanvasRenderer::beginPass(rhi::CommandEncoder &encoder, const Surface &surface, rhi::LoadOp load, const char *name)
{
    rhi::RenderPassDesc pass;
    pass.colors.push_back({.texture = surface.texture, .load = load, .clearColor = {0.0f, 0.0f, 0.0f, 0.0f}});
    pass.debugName = name;
    encoder.beginRenderPass(pass);
    applyClip(encoder, surface);
}

core::Result<> CanvasRenderer::render(rhi::CommandEncoder &encoder, const DrawList &list, const CanvasTarget &target, bool loadExisting)
{
    if (target.size.width == 0 || target.size.height == 0 || list.logicalSize.x <= 0.0f || list.logicalSize.y <= 0.0f)
    {
        return makeError(ErrorCode::kInvalidArgument, "CanvasRenderer::render: the target and the list's logical size must be non-empty");
    }
    // Un seul envoi pour tous les glyphes de la liste : chaque série a son décalage dans le tampon.
    std::size_t glyphFloats = 0;
    for (const DrawCommand &c : list.commands)
    {
        if (const auto *g = std::get_if<DrawGlyphs>(&c))
        {
            glyphFloats += g->vertices.size();
        }
    }
    if (glyphFloats > 0)
    {
        const std::uint64_t needed = glyphFloats * sizeof(float);
        if (!glyphBuffer_.valid() || glyphBuffer_.size() < needed)
        {
            rhi::BufferDesc bd;
            bd.size = std::max<std::uint64_t>(needed, 64 * 1024);
            bd.usage = rhi::BufferUsage::kVertex | rhi::BufferUsage::kCopyDst;
            bd.debugName = "ui.glyphs";
            auto buffer = GpuBuffer::create(device_, bd);
            if (!buffer)
            {
                return std::unexpected(buffer.error());
            }
            glyphBuffer_ = std::move(*buffer);
        }
        std::vector<float> all;
        all.reserve(glyphFloats);
        for (const DrawCommand &c : list.commands)
        {
            if (const auto *g = std::get_if<DrawGlyphs>(&c))
            {
                all.insert(all.end(), g->vertices.begin(), g->vertices.end());
            }
        }
        if (auto r = glyphBuffer_.update(0, std::as_bytes(std::span(all))); !r)
        {
            return r;
        }
    }

    for (auto &[id, layer] : layers_)
    {
        layer.used = false;
    }
    std::vector<Surface> stack;
    Surface main;
    main.texture = target.texture;
    main.format = target.format;
    main.px = target.size;
    main.logical = list.logicalSize;
    stack.push_back(main);
    const bool encoderFailedBefore = encoder.error().has_value();
    beginPass(encoder, stack.back(), loadExisting ? rhi::LoadOp::kLoad : rhi::LoadOp::kClear, "ui.canvas");
    std::uint64_t glyphOffset = 0;
    core::Result<> status;
    bool missingLayer = false;

    auto scaleOf = [](const Surface &s) { return glm::vec2(static_cast<float>(s.px.width) / s.logical.x, static_cast<float>(s.px.height) / s.logical.y); };
    auto drawRect = [&](const DrawRect &r, bool blend) -> core::Result<>
    {
        const Surface &s = stack.back();
        if (!s.clips.empty() && s.clips.back().empty())
        {
            return {};
        }
        auto pipe = pipeline(*rect_, s.format, blend ? Blend::kStraight : Blend::kReplace, false);
        if (!pipe)
        {
            return std::unexpected(pipe.error());
        }
        RectPush push{{r.position.x, r.position.y, r.size.x, r.size.y}, {s.logical.x, s.logical.y, 0, 0}, {r.color.r, r.color.g, r.color.b, r.color.a}};
        encoder.setPipeline(*pipe);
        encoder.setPushConstants(bytesOf(push));
        encoder.draw(6);
        return {};
    };

    for (const DrawCommand &command : list.commands)
    {
        if (!status)
        {
            break;
        }
        Surface &surface = stack.back();
        if (const auto *r = std::get_if<DrawRect>(&command))
        {
            status = drawRect(*r, true);
        }
        else if (const auto *c = std::get_if<DrawClearRect>(&command))
        {
            status = drawRect(DrawRect{c->position, c->size, c->color}, false);
        }
        else if (const auto *g = std::get_if<DrawGlyphs>(&command))
        {
            const std::uint64_t offset = glyphOffset;
            glyphOffset += g->vertices.size() * sizeof(float);
            if (g->vertices.empty() || !g->atlas || (!surface.clips.empty() && surface.clips.back().empty()))
            {
                continue;
            }
            auto atlas = atlasFor(*g->atlas);
            auto pipe = pipeline(*glyph_, surface.format, Blend::kStraight, true);
            if (!atlas || !pipe)
            {
                status = std::unexpected(!atlas ? atlas.error() : pipe.error());
                continue;
            }
            GlyphPush push{{surface.logical.x, surface.logical.y, g->msdfPixelRange, g->msdfWeight}, {g->color.r, g->color.g, g->color.b, 1.0f}};
            encoder.setPipeline(*pipe);
            encoder.setBindGroup(1, (*atlas)->group);
            encoder.setPushConstants(bytesOf(push));
            encoder.setVertexBuffer(0, glyphBuffer_.handle(), offset);
            encoder.draw(static_cast<std::uint32_t>(g->vertices.size() / 4));
        }
        else if (const auto *image = std::get_if<DrawLayerImage>(&command))
        {
            auto it = layers_.find(image->layerId);
            if (it == layers_.end())
            {
                missingLayer = true; // son contenu (mis en cache par l'interface) est perdu : signalé à la fin
                continue;
            }
            it->second.used = true; // référencée, même masquée par le découpage : elle peut redevenir visible sans être redessinée
            if (surface.layerId != 0)
            {
                auto parent = layers_.find(surface.layerId);
                if (parent != layers_.end() && std::ranges::find(parent->second.children, image->layerId) == parent->second.children.end())
                {
                    parent->second.children.push_back(image->layerId);
                }
            }
            if (!surface.clips.empty() && surface.clips.back().empty())
            {
                continue;
            }
            auto pipe = pipeline(*layerImage_, surface.format, Blend::kPremultiplied, false);
            if (!pipe)
            {
                status = std::unexpected(pipe.error());
                continue;
            }
            LayerPush push{{image->position.x, image->position.y, image->size.x, image->size.y},
                           {surface.logical.x, surface.logical.y, image->scrollOffset.x, image->scrollOffset.y},
                           {image->contentSize.x, image->contentSize.y, 0, 0},
                           {image->color.r, image->color.g, image->color.b, image->color.a}};
            encoder.setPipeline(*pipe);
            encoder.setBindGroup(1, it->second.group);
            encoder.setPushConstants(bytesOf(push));
            encoder.draw(6);
        }
        else if (const auto *begin = std::get_if<BeginLayer>(&command))
        {
            // Taille validée avant toute conversion (une valeur non finie ou hors de l'entier non signé est un comportement indéfini).
            const float maxSize = static_cast<float>(device_.capabilities().maxTextureSize);
            if (!std::isfinite(begin->size.x) || !std::isfinite(begin->size.y) || begin->size.x > maxSize || begin->size.y > maxSize)
            {
                status = makeError(ErrorCode::kOutOfRange, "CanvasRenderer::render: BeginLayer size is not finite or exceeds the device limit");
                continue;
            }
            const auto w = static_cast<std::uint32_t>(std::max(begin->size.x, 1.0f));
            const auto h = static_cast<std::uint32_t>(std::max(begin->size.y, 1.0f));
            bool created = false;
            auto layer = layerFor(begin->id, w, h, created);
            if (!layer)
            {
                status = std::unexpected(layer.error());
                continue;
            }
            (*layer)->used = true;
            if (begin->clear || created)
            {
                (*layer)->children.clear(); // contenu refait en entier : ses dépendances aussi
            }
            encoder.endRenderPass();
            Surface inner;
            inner.texture = (*layer)->target.color(0);
            inner.format = rhi::Format::kRGBA8Unorm;
            inner.px = {w, h};
            inner.logical = {static_cast<float>(w), static_cast<float>(h)};
            inner.layerId = begin->id;
            stack.push_back(inner);
            beginPass(encoder, stack.back(), (begin->clear || created) ? rhi::LoadOp::kClear : rhi::LoadOp::kLoad, "ui.layer");
        }
        else if (std::holds_alternative<EndLayer>(command))
        {
            if (stack.size() < 2)
            {
                status = makeError(ErrorCode::kInvalidArgument, "CanvasRenderer::render: EndLayer without BeginLayer");
                continue;
            }
            encoder.endRenderPass();
            stack.pop_back();
            beginPass(encoder, stack.back(), rhi::LoadOp::kLoad, stack.back().layerId == 0 ? "ui.canvas" : "ui.layer");
        }
        else if (const auto *clip = std::get_if<PushClip>(&command))
        {
            // Intersection avec le découpage parent EN FLOTTANTS, puis conversion : les bornes sont alors dans la surface (une
            // coordonnée immense ou non finie donne un découpage vide au lieu d'un dépassement d'entier).
            const glm::vec2 k = scaleOf(surface);
            const Clip parent = surface.clips.empty() ? fullClip(surface) : surface.clips.back();
            const float left = std::floor(clip->position.x * k.x);
            const float bottom = std::floor((surface.logical.y - (clip->position.y + clip->size.y)) * k.y);
            const float right = std::ceil((clip->position.x + clip->size.x) * k.x);
            const float top = std::ceil((surface.logical.y - clip->position.y) * k.y);
            Clip c{};
            if (std::isfinite(left) && std::isfinite(bottom) && std::isfinite(right) && std::isfinite(top))
            {
                auto clampTo = [](float v, int lo, int hi) { return static_cast<int>(std::clamp(v, static_cast<float>(lo), static_cast<float>(hi))); };
                c.left = clampTo(left, parent.left, parent.right);
                c.right = clampTo(right, parent.left, parent.right);
                c.bottom = clampTo(bottom, parent.bottom, parent.top);
                c.top = clampTo(top, parent.bottom, parent.top);
            }
            surface.clips.push_back(c); // non fini : {0, 0, 0, 0}, vide
            applyClip(encoder, surface);
        }
        else if (std::holds_alternative<PopClip>(command))
        {
            if (surface.clips.empty())
            {
                status = makeError(ErrorCode::kInvalidArgument, "CanvasRenderer::render: PopClip without PushClip");
                continue;
            }
            surface.clips.pop_back();
            applyClip(encoder, surface);
        }
    }
    // Referme tout ce qui est resté ouvert (liste mal formée) pour ne pas laisser de passe ouverte à l'appelant.
    encoder.endRenderPass();
    if (stack.size() > 1 && status)
    {
        status = makeError(ErrorCode::kInvalidArgument, "CanvasRenderer::render: a layer was left open");
    }
    // Une commande refusée par l'encodeur pendant ce rendu (cible périmée...) : signalée tout de suite, pas seulement à submit.
    if (status && !encoderFailedBefore && encoder.error())
    {
        status = std::unexpected(*encoder.error());
    }
    if (status && missingLayer)
    {
        status = makeError(ErrorCode::kNotFound, "CanvasRenderer::render: a DrawLayerImage refers to a layer that does not exist (collect the UI again in full)");
    }

    // Couches gardées : celles que la liste a référencées, et celles dont l'image est dans une couche gardée (son contenu en
    // cache les redessine sans les nommer).
    std::vector<std::uint64_t> work;
    for (const auto &[id, layer] : layers_)
    {
        if (layer.used)
        {
            work.push_back(id);
        }
    }
    while (!work.empty())
    {
        const std::uint64_t id = work.back();
        work.pop_back();
        for (std::uint64_t child : layers_[id].children)
        {
            if (auto it = layers_.find(child); it != layers_.end() && !it->second.used)
            {
                it->second.used = true;
                work.push_back(child);
            }
        }
    }
    for (auto it = layers_.begin(); it != layers_.end();)
    {
        if (!it->second.used)
        {
            releaseLayer(it->second);
            it = layers_.erase(it);
        }
        else
        {
            ++it;
        }
    }
    return status;
}

} // namespace engine::renderer
