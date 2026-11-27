#include "orosmo/renderer/canvas_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace orosmo::renderer
{

using core::ErrorCode;
using core::makeError;

namespace
{
struct RectPush
{
    float container[4]; // taille de la surface en pixels ; les données du rectangle sont des attributs par instance
};
// Instance d'un rectangle dans le tampon de lot : 16 flottants (position + taille, couleur, forme, couleur de bordure).
constexpr std::uint32_t kRectInstanceFloats = 16;
// Sommet de glyphe ou d'image dans le tampon de lot : x, y, u, v puis la couleur (ou la teinte) de la commande.
constexpr std::uint32_t kColoredVertexFloats = 8;
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
static_assert(sizeof(RectPush) == 16 && sizeof(GlyphPush) == 32 && sizeof(LayerPush) == 64, "blocs push_constant des shaders ui_*");

template <typename T> std::span<const std::byte> bytesOf(const T &v) { return std::as_bytes(std::span(&v, 1)); }

// --- Plan de lots (R2D-01) -----------------------------------------------------------------------------------------------------
// Entre deux changements d'état (découpage, couche), les dessins forment un « segment ». Un dessin peut rejoindre un lot compatible
// plus ancien du segment s'il ne recouvre (boîte englobante, intérieurs stricts) aucun dessin des lots qu'il dépasse : l'ordre de
// recouvrement, donc le résultat, est inchangé. Compatibles : même type, et même atlas / mêmes réglages MSDF pour le texte.
enum class Kind : std::uint8_t
{
    kRect,
    kClearRect,
    kGlyphs,
    kImage,
};
struct Box
{
    float x0, y0, x1, y1;
};
bool overlaps(const Box &a, const Box &b) { return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1; }
Box everything() { return {-std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()}; }
Box boxOfRect(const glm::vec2 &position, const glm::vec2 &size)
{
    const Box b{std::min(position.x, position.x + size.x), std::min(position.y, position.y + size.y), std::max(position.x, position.x + size.x),
                std::max(position.y, position.y + size.y)};
    return std::isfinite(b.x0) && std::isfinite(b.y0) && std::isfinite(b.x1) && std::isfinite(b.y1) ? b : everything(); // non fini : gêne tout
}
Box boxOfVertices(const std::vector<float> &v)
{
    Box b{std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
          -std::numeric_limits<float>::infinity()};
    for (std::size_t i = 0; i + 3 < v.size(); i += 4)
    {
        b = {std::min(b.x0, v[i]), std::min(b.y0, v[i + 1]), std::max(b.x1, v[i]), std::max(b.y1, v[i + 1])};
    }
    return std::isfinite(b.x0) && std::isfinite(b.y0) && std::isfinite(b.x1) && std::isfinite(b.y1) ? b : everything();
}
struct Item
{
    Kind kind;
    const DrawCommand *command;
    const AtlasImage *atlas; // glyphes et images
    float range;             // msdfPixelRange
    float weight;            // msdfWeight
    Box box;
};
struct Batch
{
    Kind kind;
    const AtlasImage *atlas;
    float range, weight;
    Box box;
    std::vector<std::uint32_t> items;
};
// Étape de l'exécution : un lot (dessiné d'un coup) ou une commande qui change l'état (couche, découpage) ou dessine une couche.
struct Op
{
    bool batch = false;
    const DrawCommand *command = nullptr;
    Kind kind = Kind::kRect;
    const AtlasImage *atlas = nullptr;
    float range = 0.0f;
    float weight = 0.0f;
    std::uint64_t byteOffset = 0;
    std::uint32_t count = 0; // instances (rectangles) ou sommets
    std::uint32_t items = 0; // commandes de la liste regroupées dans ce lot
};
struct Plan
{
    std::vector<Op> ops;
    std::vector<float> data; // instances et sommets de tous les lots
    std::uint32_t drawCommands = 0;
};

bool compatible(const Batch &b, const Item &i)
{
    return b.kind == i.kind && b.atlas == i.atlas && b.range == i.range && b.weight == i.weight;
}

void appendItem(Plan &plan, const Item &item, bool inLayer)
{
    std::vector<float> &d = plan.data;
    if (const auto *r = std::get_if<DrawRect>(&*item.command))
    {
        d.insert(d.end(), {r->position.x, r->position.y, r->size.x, r->size.y, r->color.r, r->color.g, r->color.b, r->color.a, r->radius, r->borderWidth,
                           r->softness, 0.0f, r->borderColor.r, r->borderColor.g, r->borderColor.b, r->borderColor.a});
    }
    else if (const auto *c = std::get_if<DrawClearRect>(&*item.command))
    {
        // Une couche contient une couleur prémultipliée : la couleur de remplacement l'est aussi (le transparent ne change pas).
        const glm::vec4 color = inLayer ? glm::vec4(glm::vec3(c->color) * c->color.a, c->color.a) : c->color;
        d.insert(d.end(), {c->position.x, c->position.y, c->size.x, c->size.y, color.r, color.g, color.b, color.a, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f});
    }
    else
    {
        const std::vector<float> *vertices = nullptr;
        glm::vec4 color{0.0f};
        if (const auto *g = std::get_if<DrawGlyphs>(&*item.command))
        {
            vertices = &g->vertices;
            color = {g->color.r, g->color.g, g->color.b, g->alpha};
        }
        else if (const auto *img = std::get_if<DrawImage>(&*item.command))
        {
            vertices = &img->vertices;
            color = img->tint;
        }
        for (std::size_t i = 0; i + 3 < vertices->size(); i += 4)
        {
            d.insert(d.end(), {(*vertices)[i], (*vertices)[i + 1], (*vertices)[i + 2], (*vertices)[i + 3], color.r, color.g, color.b, color.a});
        }
    }
}

constexpr std::size_t kSearchBudget = 512; // comparaisons de lots et d'éléments par commande

void flushSegment(Plan &plan, std::vector<Item> &segment, bool inLayer)
{
    std::vector<Batch> batches;
    for (std::uint32_t index = 0; index < segment.size(); ++index)
    {
        const Item &item = segment[index];
        std::size_t target = batches.size();
        // Budget de comparaisons par commande (lots visités + éléments comparés) : borne le coût d'un segment de milliers de dessins.
        // Épuisé, on garde le lot compatible déjà trouvé (atteignable : tout ce qui est plus récent a été vérifié) ou on ouvre un
        // nouveau lot en fin de segment : l'ordre des dessins qui se recouvrent est toujours préservé.
        std::size_t budget = kSearchBudget;
        for (std::size_t k = batches.size(); k-- > 0;)
        {
            if (budget == 0)
            {
                break;
            }
            --budget;
            Batch &b = batches[k];
            if (compatible(b, item))
            {
                target = k; // le plus ancien lot compatible atteignable
            }
            if (overlaps(b.box, item.box))
            {
                bool covered = false;
                for (std::uint32_t other : b.items)
                {
                    if (overlaps(segment[other].box, item.box))
                    {
                        covered = true;
                        break;
                    }
                    if (budget == 0)
                    {
                        break;
                    }
                    --budget;
                }
                if (covered)
                {
                    break; // un dessin de ce lot est recouvert par celui-ci : on ne peut pas passer devant
                }
                if (budget == 0)
                {
                    break; // comparaison interrompue : on ne sait pas si on peut passer devant ce lot
                }
            }
        }
        if (target == batches.size())
        {
            batches.push_back({item.kind, item.atlas, item.range, item.weight, item.box, {}});
        }
        Batch &b = batches[target];
        b.items.push_back(index);
        b.box = {std::min(b.box.x0, item.box.x0), std::min(b.box.y0, item.box.y0), std::max(b.box.x1, item.box.x1), std::max(b.box.y1, item.box.y1)};
    }
    for (const Batch &b : batches)
    {
        Op op;
        op.batch = true;
        op.kind = b.kind;
        op.atlas = b.atlas;
        op.range = b.range;
        op.weight = b.weight;
        op.byteOffset = plan.data.size() * sizeof(float);
        op.items = static_cast<std::uint32_t>(b.items.size());
        for (std::uint32_t index : b.items)
        {
            appendItem(plan, segment[index], inLayer);
        }
        const std::size_t floats = plan.data.size() - op.byteOffset / sizeof(float);
        op.count = static_cast<std::uint32_t>(floats / (b.kind == Kind::kRect || b.kind == Kind::kClearRect ? kRectInstanceFloats : kColoredVertexFloats));
        plan.ops.push_back(op);
    }
    segment.clear();
}

Plan buildPlan(const DrawList &list)
{
    Plan plan;
    std::vector<Item> segment;
    std::vector<std::uint64_t> layers; // couches ouvertes (couleur de remplacement prémultipliée dans une couche)
    auto barrier = [&](const DrawCommand &command)
    {
        flushSegment(plan, segment, !layers.empty());
        Op op;
        op.command = &command;
        plan.ops.push_back(op);
    };
    for (const DrawCommand &command : list.commands)
    {
        if (const auto *r = std::get_if<DrawRect>(&command))
        {
            segment.push_back({Kind::kRect, &command, nullptr, 0.0f, 0.0f, boxOfRect(r->position, r->size)});
            ++plan.drawCommands;
        }
        else if (const auto *c = std::get_if<DrawClearRect>(&command))
        {
            segment.push_back({Kind::kClearRect, &command, nullptr, 0.0f, 0.0f, boxOfRect(c->position, c->size)});
            ++plan.drawCommands;
        }
        else if (const auto *g = std::get_if<DrawGlyphs>(&command))
        {
            if (!g->vertices.empty() && g->atlas)
            {
                segment.push_back({Kind::kGlyphs, &command, g->atlas.get(), g->msdfPixelRange, g->msdfWeight, boxOfVertices(g->vertices)});
                ++plan.drawCommands;
            }
        }
        else if (const auto *i = std::get_if<DrawImage>(&command))
        {
            if (!i->vertices.empty() && i->image)
            {
                segment.push_back({Kind::kImage, &command, i->image.get(), 0.0f, 0.0f, boxOfVertices(i->vertices)});
                ++plan.drawCommands;
            }
        }
        else
        {
            if (std::holds_alternative<DrawLayerImage>(command))
            {
                ++plan.drawCommands;
            }
            barrier(command);
            if (const auto *begin = std::get_if<BeginLayer>(&command))
            {
                layers.push_back(begin->id);
            }
            else if (std::holds_alternative<EndLayer>(command) && !layers.empty())
            {
                layers.pop_back();
            }
        }
    }
    flushSegment(plan, segment, !layers.empty());
    return plan;
}
} // namespace

core::Result<std::unique_ptr<CanvasRenderer>> CanvasRenderer::create(rhi::Device &device, ShaderLibrary &library, PipelineCache &pipelines,
                                                                     const std::filesystem::path &shaderDirectory)
{
    std::unique_ptr<CanvasRenderer> canvas(new CanvasRenderer(device, pipelines));
    canvas->arena_ = BatchArena(device.framesInFlight()); // A61 G : une zone par image en vol du périphérique
    auto rect = library.loadGraphics(shaderDirectory / "ui_rect.vert", shaderDirectory / "ui_rect.frag");
    auto glyph = library.loadGraphics(shaderDirectory / "ui_glyph.vert", shaderDirectory / "ui_glyph.frag");
    auto layer = library.loadGraphics(shaderDirectory / "ui_layer.vert", shaderDirectory / "ui_layer.frag");
    auto image = library.loadGraphics(shaderDirectory / "ui_image.vert", shaderDirectory / "ui_image.frag");
    if (!rect || !glyph || !layer || !image)
    {
        return std::unexpected(!rect ? rect.error() : (!glyph ? glyph.error() : (!layer ? layer.error() : image.error())));
    }
    canvas->image_ = *image;
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
    for (auto &[id, external] : externals_)
    {
        device_.destroy(external.group);
    }
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
    atlases_.clear(); // les images externes restent inscrites : leur texture n'est pas à `clear`
}

core::Result<rhi::PipelineHandle> CanvasRenderer::pipeline(const ShaderProgram &program, rhi::Format format, Blend blend, Layout layout)
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
    if (layout == Layout::kRectInstances)
    {
        state.vertexBuffers.push_back({.binding = 0, .stride = kRectInstanceFloats * sizeof(float), .stepRate = rhi::VertexStepRate::kInstance});
        for (std::uint32_t i = 0; i < 4; ++i)
        {
            state.vertexAttributes.push_back({.location = i, .binding = 0, .format = rhi::VertexFormat::kFloat4, .offset = i * 16});
        }
    }
    else if (layout == Layout::kColoredVertices)
    {
        state.vertexBuffers.push_back({.binding = 0, .stride = kColoredVertexFloats * sizeof(float), .stepRate = rhi::VertexStepRate::kVertex});
        state.vertexAttributes.push_back({.location = 0, .binding = 0, .format = rhi::VertexFormat::kFloat4, .offset = 0});
        state.vertexAttributes.push_back({.location = 1, .binding = 0, .format = rhi::VertexFormat::kFloat4, .offset = 16});
    }
    state.debugName = program.name() + (blend == Blend::kReplace ? ".replace" : blend == Blend::kPremultiplied ? ".premultiplied" : "");
    return pipelines_.graphics(program, state);
}

core::Result<> CanvasRenderer::registerExternalImage(std::uint64_t id, rhi::TextureHandle texture, rhi::SamplerHandle sampler)
{
    if (id == 0 || texture.isNull())
    {
        return makeError(ErrorCode::kInvalidArgument, "CanvasRenderer::registerExternalImage: invalid id or texture");
    }
    rhi::BindGroupDesc desc;
    desc.entries.push_back({.binding = 0, .type = rhi::BindingType::kSampledTexture, .texture = texture, .sampler = sampler});
    desc.debugName = "ui.external." + std::to_string(id);
    auto group = device_.createBindGroup(desc);
    if (!group)
    {
        return std::unexpected(group.error());
    }
    unregisterExternalImage(id);
    Atlas atlas;
    atlas.group = *group;
    externals_.emplace(id, std::move(atlas));
    return {};
}

void CanvasRenderer::unregisterExternalImage(std::uint64_t id)
{
    if (auto it = externals_.find(id); it != externals_.end())
    {
        device_.destroy(it->second.group);
        externals_.erase(it);
    }
}

core::Result<CanvasRenderer::Atlas *> CanvasRenderer::atlasFor(const AtlasImage &image)
{
    if (auto it = externals_.find(image.id); it != externals_.end())
    {
        return &it->second;
    }
    if (image.width == 0 || image.height == 0 || image.rgba.size() != static_cast<std::size_t>(image.width) * image.height * 4)
    {
        return makeError(ErrorCode::kInvalidArgument, "CanvasRenderer: invalid glyph atlas or image");
    }
    if (auto it = atlases_.find(image.id); it != atlases_.end())
    {
        Atlas &cached = it->second;
        cached.lastUsed = renderCount_;
        if (cached.version == image.version)
        {
            return &cached;
        }
        if (cached.width == image.width && cached.height == image.height)
        {
            // Nouvelle version des pixels : envoyée dans la même texture.
            if (auto r = device_.updateTexture(cached.texture.handle(), rhi::TextureCopy{}, std::as_bytes(std::span(image.rgba))); !r)
            {
                return std::unexpected(r.error());
            }
            cached.version = image.version;
            return &cached;
        }
        device_.destroy(cached.group);
        atlases_.erase(it); // nouvelle taille : texture recréée
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
    atlas.version = image.version;
    atlas.width = image.width;
    atlas.height = image.height;
    atlas.lastUsed = renderCount_;
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
    // Repère du RHI (A26) : le rectangle de ciseau part du haut de la cible, comme l'interface. `bottom` < `top` en pixels depuis le haut.
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
    ++renderCount_;
    stats_ = {};
    if (target.size.width == 0 || target.size.height == 0 || list.logicalSize.x <= 0.0f || list.logicalSize.y <= 0.0f)
    {
        return makeError(ErrorCode::kInvalidArgument, "CanvasRenderer::render: the target and the list's logical size must be non-empty");
    }
    // Plan de lots (R2D-01) : les dessins compatibles d'un segment (entre deux changements d'état) sont regroupés dans l'ordre qui
    // garde le résultat. Un seul envoi pour tout le tampon : chaque lot a son décalage.
    const Plan plan = buildPlan(list);
    stats_.drawCommands = plan.drawCommands;
    BatchArena::Range batch{};
    if (!plan.data.empty())
    {
        auto uploaded = arena_.upload(device_, encoder.id(), std::as_bytes(std::span(plan.data)), "ui.batch");
        if (!uploaded)
        {
            return std::unexpected(uploaded.error());
        }
        batch = *uploaded;
        stats_.bufferBytes = plan.data.size() * sizeof(float);
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
    core::Result<> status;
    bool missingLayer = false;

    auto scaleOf = [](const Surface &s) { return glm::vec2(static_cast<float>(s.px.width) / s.logical.x, static_cast<float>(s.px.height) / s.logical.y); };

    for (const Op &op : plan.ops)
    {
        if (!status)
        {
            break;
        }
        Surface &surface = stack.back();
        const bool clipped = !surface.clips.empty() && surface.clips.back().empty();
        if (op.batch)
        {
            if (clipped)
            {
                continue;
            }
            if (op.kind == Kind::kRect || op.kind == Kind::kClearRect)
            {
                auto pipe = pipeline(*rect_, surface.format, op.kind == Kind::kRect ? Blend::kStraight : Blend::kReplace, Layout::kRectInstances);
                if (!pipe)
                {
                    status = std::unexpected(pipe.error());
                    continue;
                }
                const RectPush push{{surface.logical.x, surface.logical.y, 0, 0}};
                encoder.setPipeline(*pipe);
                encoder.setPushConstants(bytesOf(push));
                encoder.setVertexBuffer(0, batch.buffer, batch.offset + op.byteOffset);
                encoder.draw(6, op.count);
            }
            else
            {
                const bool isGlyphs = op.kind == Kind::kGlyphs;
                auto atlas = atlasFor(*op.atlas);
                auto pipe = pipeline(isGlyphs ? *glyph_ : *image_, surface.format, Blend::kStraight, Layout::kColoredVertices);
                if (!atlas || !pipe)
                {
                    status = std::unexpected(!atlas ? atlas.error() : pipe.error());
                    continue;
                }
                const GlyphPush push{{surface.logical.x, surface.logical.y, op.range, op.weight}, {0, 0, 0, 0}};
                encoder.setPipeline(*pipe);
                encoder.setBindGroup(1, (*atlas)->group);
                encoder.setPushConstants(bytesOf(push));
                encoder.setVertexBuffer(0, batch.buffer, batch.offset + op.byteOffset);
                encoder.draw(op.count);
            }
            ++stats_.drawCalls;
            stats_.batchedCommands += op.items;
            continue;
        }
        const DrawCommand &command = *op.command;
        if (const auto *image = std::get_if<DrawLayerImage>(&command))
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
            if (clipped)
            {
                continue;
            }
            auto pipe = pipeline(*layerImage_, surface.format, Blend::kPremultiplied, Layout::kNone);
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
            ++stats_.drawCalls;
            ++stats_.batchedCommands;
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
            // Bornes verticales depuis le HAUT (repère du RHI) : `bottom` = première ligne, `top` = ligne après la dernière.
            const float bottom = std::floor(clip->position.y * k.y);
            const float right = std::ceil((clip->position.x + clip->size.x) * k.x);
            const float top = std::ceil((clip->position.y + clip->size.y) * k.y);
            Clip c{};
            // Parent vide : le découpage reste vide (et std::clamp n'est jamais appelé avec des bornes inversées).
            if (!parent.empty() && std::isfinite(left) && std::isfinite(bottom) && std::isfinite(right) && std::isfinite(top))
            {
                auto clampTo = [](float v, int lo, int hi) { return static_cast<int>(std::clamp(v, static_cast<float>(lo), static_cast<float>(hi))); };
                c.left = clampTo(left, parent.left, parent.right);
                c.right = clampTo(right, parent.left, parent.right);
                c.bottom = clampTo(bottom, parent.bottom, parent.top);
                c.top = clampTo(top, parent.bottom, parent.top);
                c.right = std::max(c.right, c.left); // largeur ou hauteur négative : vide, bornes ordonnées
                c.top = std::max(c.top, c.bottom);
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

    // Liste interrompue par une erreur : rien n'est évincé (une couche que la suite aurait référencée resterait sinon détruite).
    // Le contenu des couches redessinées avant l'erreur n'est pas fiable : l'appelant redemande une liste complète.
    if (!status)
    {
        return status;
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
    // Images qui ne servent plus (image du jeu remplacée, thème changé) : libérées après un délai.
    for (auto it = atlases_.begin(); it != atlases_.end();)
    {
        if (renderCount_ - it->second.lastUsed > kAtlasIdleRenders)
        {
            device_.destroy(it->second.group);
            it = atlases_.erase(it);
        }
        else
        {
            ++it;
        }
    }
    return status;
}

} // namespace orosmo::renderer
