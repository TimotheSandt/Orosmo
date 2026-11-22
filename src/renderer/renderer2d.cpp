#include "orosmo/renderer/renderer2d.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>
#include <numeric>

namespace orosmo::renderer
{

using core::ErrorCode;
using core::makeError;

namespace
{
constexpr std::uint32_t kSpriteFloats = 16;
constexpr std::uint32_t kShapeVertexFloats = 6;
struct Push
{
    float viewProj[16];
};
static_assert(sizeof(Push) == 64, "bloc push_constant des shaders sprite2d et shape2d");

glm::vec2 perp(const glm::vec2 &v) { return {-v.y, v.x}; }

float signedArea(std::span<const glm::vec2> p)
{
    float a = 0.0f;
    for (std::size_t i = 0; i < p.size(); ++i)
    {
        const glm::vec2 &u = p[i];
        const glm::vec2 &v = p[(i + 1) % p.size()];
        a += u.x * v.y - v.x * u.y;
    }
    return a * 0.5f;
}
float cross2(const glm::vec2 &o, const glm::vec2 &a, const glm::vec2 &b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); }
bool inTriangle(const glm::vec2 &p, const glm::vec2 &a, const glm::vec2 &b, const glm::vec2 &c)
{
    return cross2(a, b, p) >= 0.0f && cross2(b, c, p) >= 0.0f && cross2(c, a, p) >= 0.0f;
}
} // namespace

core::Result<std::unique_ptr<Renderer2D>> Renderer2D::create(rhi::Device &device, ShaderLibrary &library, PipelineCache &pipelines,
                                                             const std::filesystem::path &shaderDirectory)
{
    std::unique_ptr<Renderer2D> r(new Renderer2D(device, pipelines));
    auto sprite = library.loadGraphics(shaderDirectory / "sprite2d.vert", shaderDirectory / "sprite2d.frag");
    auto shape = library.loadGraphics(shaderDirectory / "shape2d.vert", shaderDirectory / "shape2d.frag");
    if (!sprite || !shape)
    {
        return std::unexpected(!sprite ? sprite.error() : shape.error());
    }
    r->spriteProgram_ = *sprite;
    r->shapeProgram_ = *shape;
    for (const bool nearest : {false, true})
    {
        rhi::SamplerDesc sd;
        sd.minFilter = sd.magFilter = nearest ? rhi::Filter::kNearest : rhi::Filter::kLinear;
        sd.mipFilter = std::nullopt;
        sd.addressU = sd.addressV = rhi::AddressMode::kClampToEdge;
        sd.debugName = nearest ? "renderer2d.nearest" : "renderer2d.linear";
        auto s = device.createSampler(sd);
        if (!s)
        {
            return std::unexpected(s.error());
        }
        (nearest ? r->nearestSampler_ : r->linearSampler_) = *s;
    }
    return r;
}

Renderer2D::~Renderer2D()
{
    clearMaterials();
    device_.destroy(linearSampler_);
    device_.destroy(nearestSampler_);
}

void Renderer2D::clearMaterials()
{
    for (auto &[id, m] : materials_)
    {
        device_.destroy(m.linear);
        device_.destroy(m.nearest);
    }
    materials_.clear();
}

core::Result<SpriteMaterial> Renderer2D::material(const Texture &texture, bool nearest)
{
    if (!texture.valid())
    {
        return makeError(ErrorCode::kInvalidArgument, "Renderer2D::material: invalid texture");
    }
    for (auto &[id, m] : materials_)
    {
        if (m.texture == texture.handle())
        {
            m.forceNearest = m.forceNearest || nearest;
            return SpriteMaterial{id};
        }
    }
    Material m;
    m.texture = texture.handle();
    m.forceNearest = nearest;
    for (const bool useNearest : {false, true})
    {
        rhi::BindGroupDesc desc;
        desc.entries.push_back({.binding = 0, .type = rhi::BindingType::kSampledTexture, .texture = texture.handle(),
                                .sampler = useNearest ? nearestSampler_ : linearSampler_});
        desc.debugName = "renderer2d.material";
        auto group = device_.createBindGroup(desc);
        if (!group)
        {
            device_.destroy(m.linear);
            return std::unexpected(group.error());
        }
        (useNearest ? m.nearest : m.linear) = *group;
    }
    const std::uint32_t id = nextMaterial_++;
    materials_.emplace(id, m);
    return SpriteMaterial{id};
}

void Renderer2D::begin(const Camera2D &camera)
{
    camera_ = camera;
    items_.clear();
    spriteData_.clear();
    shapeData_.clear();
    sequence_ = 0;
    stats_ = {};
}

core::Result<> Renderer2D::draw(const Sprite &s)
{
    if (!materials_.contains(s.material.id))
    {
        return makeError(ErrorCode::kInvalidArgument, "Renderer2D::draw: unknown sprite material");
    }
    const auto first = static_cast<std::uint32_t>(spriteData_.size() / kSpriteFloats);
    spriteData_.insert(spriteData_.end(), {s.position.x, s.position.y, s.size.x, s.size.y, s.uvMin.x, s.uvMin.y, s.uvMax.x, s.uvMax.y, s.color.r,
                                           s.color.g, s.color.b, s.color.a, s.pivot.x, s.pivot.y, s.rotation, 0.0f});
    items_.push_back({s.z, Kind::kSprite, s.material.id, sequence_++, first, 1});
    ++stats_.sprites;
    return {};
}

void Renderer2D::vertex(const glm::vec2 &p, const glm::vec4 &c)
{
    shapeData_.insert(shapeData_.end(), {p.x, p.y, c.r, c.g, c.b, c.a});
}
void Renderer2D::quad(const glm::vec2 &a, const glm::vec2 &b, const glm::vec2 &c, const glm::vec2 &d, const glm::vec4 &color)
{
    vertex(a, color);
    vertex(b, color);
    vertex(c, color);
    vertex(a, color);
    vertex(c, color);
    vertex(d, color);
}
void Renderer2D::addShape(float z, std::uint32_t firstVertex)
{
    const auto end = static_cast<std::uint32_t>(shapeData_.size() / kShapeVertexFloats);
    if (end == firstVertex)
    {
        return;
    }
    items_.push_back({z, Kind::kShape, 0, sequence_++, firstVertex, end - firstVertex});
    ++stats_.shapes;
}

std::uint32_t Renderer2D::segmentsFor(float radius, std::uint32_t requested) const
{
    if (requested >= 3)
    {
        return std::min<std::uint32_t>(requested, 1024);
    }
    // Écart maximal d'un quart de pixel entre le cercle et son polygone.
    const float radiusPx = radius * camera_.effectiveZoom();
    if (!(radiusPx > 0.25f))
    {
        return 8;
    }
    const float step = 2.0f * std::acos(1.0f - 0.25f / radiusPx);
    return std::clamp<std::uint32_t>(static_cast<std::uint32_t>(std::ceil(2.0f * std::numbers::pi_v<float> / step)), 8, 256);
}

void Renderer2D::line(const glm::vec2 &a, const glm::vec2 &b, float thickness, const glm::vec4 &color, float z)
{
    const glm::vec2 d = b - a;
    const float length = glm::length(d);
    if (!(length > 0.0f) || !(thickness > 0.0f) || !std::isfinite(length))
    {
        return;
    }
    const glm::vec2 n = perp(d / length) * (thickness * 0.5f);
    const auto first = static_cast<std::uint32_t>(shapeData_.size() / kShapeVertexFloats);
    quad(a - n, b - n, b + n, a + n, color);
    addShape(z, first);
}

void Renderer2D::polyline(std::span<const glm::vec2> points, float thickness, const glm::vec4 &color, bool closed, float z)
{
    if (points.size() < 2 || !(thickness > 0.0f))
    {
        return;
    }
    const auto first = static_cast<std::uint32_t>(shapeData_.size() / kShapeVertexFloats);
    const std::size_t segments = closed ? points.size() : points.size() - 1;
    for (std::size_t i = 0; i < segments; ++i)
    {
        const glm::vec2 &a = points[i];
        const glm::vec2 &b = points[(i + 1) % points.size()];
        const glm::vec2 d = b - a;
        const float length = glm::length(d);
        if (length > 0.0f && std::isfinite(length))
        {
            const glm::vec2 n = perp(d / length) * (thickness * 0.5f);
            quad(a - n, b - n, b + n, a + n, color);
        }
    }
    // Jonctions arrondies (disques aux sommets intérieurs) : pas de trou au coude ; une couleur translucide y est mélangée deux fois.
    const std::uint32_t n = segmentsFor(thickness * 0.5f, 0);
    for (std::size_t i = closed ? 0 : 1; i + (closed ? 0 : 1) < points.size(); ++i)
    {
        const glm::vec2 &c = points[i];
        for (std::uint32_t k = 0; k < n; ++k)
        {
            const float a0 = 2.0f * std::numbers::pi_v<float> * static_cast<float>(k) / static_cast<float>(n);
            const float a1 = 2.0f * std::numbers::pi_v<float> * static_cast<float>(k + 1) / static_cast<float>(n);
            const float r = thickness * 0.5f;
            vertex(c, color);
            vertex(c + r * glm::vec2(std::cos(a0), std::sin(a0)), color);
            vertex(c + r * glm::vec2(std::cos(a1), std::sin(a1)), color);
        }
    }
    addShape(z, first);
}

void Renderer2D::rect(const glm::vec2 &position, const glm::vec2 &size, const glm::vec4 &color, float rotation, float z)
{
    const glm::vec2 h = size * 0.5f;
    const glm::vec2 c = position + h;
    const float cs = std::cos(rotation);
    const float sn = std::sin(rotation);
    auto at = [&](float x, float y) { return c + glm::vec2(cs * x - sn * y, sn * x + cs * y); };
    const auto first = static_cast<std::uint32_t>(shapeData_.size() / kShapeVertexFloats);
    quad(at(-h.x, -h.y), at(h.x, -h.y), at(h.x, h.y), at(-h.x, h.y), color);
    addShape(z, first);
}

void Renderer2D::rectOutline(const glm::vec2 &position, const glm::vec2 &size, float thickness, const glm::vec4 &color, float z)
{
    if (!(thickness > 0.0f))
    {
        return;
    }
    const float t = std::min({thickness, size.x * 0.5f, size.y * 0.5f});
    const glm::vec2 lo = position;
    const glm::vec2 hi = position + size;
    const auto first = static_cast<std::uint32_t>(shapeData_.size() / kShapeVertexFloats);
    auto box = [&](glm::vec2 a, glm::vec2 b) { quad({a.x, a.y}, {b.x, a.y}, {b.x, b.y}, {a.x, b.y}, color); };
    // Quatre bandes sans recouvrement (un contour translucide ne double pas aux coins).
    box({lo.x, lo.y}, {hi.x, lo.y + t});
    box({lo.x, hi.y - t}, {hi.x, hi.y});
    box({lo.x, lo.y + t}, {lo.x + t, hi.y - t});
    box({hi.x - t, lo.y + t}, {hi.x, hi.y - t});
    addShape(z, first);
}

void Renderer2D::circle(const glm::vec2 &center, float radius, const glm::vec4 &color, std::uint32_t segments, float z)
{
    if (!(radius > 0.0f) || !std::isfinite(radius))
    {
        return;
    }
    const std::uint32_t n = segmentsFor(radius, segments);
    const auto first = static_cast<std::uint32_t>(shapeData_.size() / kShapeVertexFloats);
    for (std::uint32_t k = 0; k < n; ++k)
    {
        const float a0 = 2.0f * std::numbers::pi_v<float> * static_cast<float>(k) / static_cast<float>(n);
        const float a1 = 2.0f * std::numbers::pi_v<float> * static_cast<float>(k + 1) / static_cast<float>(n);
        vertex(center, color);
        vertex(center + radius * glm::vec2(std::cos(a0), std::sin(a0)), color);
        vertex(center + radius * glm::vec2(std::cos(a1), std::sin(a1)), color);
    }
    addShape(z, first);
}

void Renderer2D::ring(const glm::vec2 &center, float inner, float outer, const glm::vec4 &color, std::uint32_t n)
{
    for (std::uint32_t k = 0; k < n; ++k)
    {
        const float a0 = 2.0f * std::numbers::pi_v<float> * static_cast<float>(k) / static_cast<float>(n);
        const float a1 = 2.0f * std::numbers::pi_v<float> * static_cast<float>(k + 1) / static_cast<float>(n);
        const glm::vec2 d0(std::cos(a0), std::sin(a0));
        const glm::vec2 d1(std::cos(a1), std::sin(a1));
        quad(center + inner * d0, center + outer * d0, center + outer * d1, center + inner * d1, color);
    }
}

void Renderer2D::circleOutline(const glm::vec2 &center, float radius, float thickness, const glm::vec4 &color, std::uint32_t segments, float z)
{
    if (!(radius > 0.0f) || !(thickness > 0.0f) || !std::isfinite(radius))
    {
        return;
    }
    const float t = std::min(thickness, 2.0f * radius);
    const auto first = static_cast<std::uint32_t>(shapeData_.size() / kShapeVertexFloats);
    ring(center, radius - t * 0.5f, radius + t * 0.5f, color, segmentsFor(radius + t * 0.5f, segments));
    addShape(z, first);
}

void Renderer2D::polygon(std::span<const glm::vec2> points, const glm::vec4 &color, float z)
{
    if (points.size() < 3)
    {
        return;
    }
    // Découpage en oreilles, O(n²), déterministe. Sens horaire ramené à trigonométrique.
    std::vector<std::uint32_t> idx(points.size());
    std::iota(idx.begin(), idx.end(), 0u);
    if (signedArea(points) < 0.0f)
    {
        std::ranges::reverse(idx);
    }
    if (signedArea(points) == 0.0f)
    {
        return;
    }
    const auto first = static_cast<std::uint32_t>(shapeData_.size() / kShapeVertexFloats);
    std::size_t guard = idx.size() * idx.size();
    std::size_t i = 0;
    while (idx.size() > 3 && guard-- > 0)
    {
        const std::size_t n = idx.size();
        const glm::vec2 &a = points[idx[(i + n - 1) % n]];
        const glm::vec2 &b = points[idx[i % n]];
        const glm::vec2 &c = points[idx[(i + 1) % n]];
        bool ear = cross2(a, b, c) > 0.0f;
        for (std::size_t j = 0; ear && j < n; ++j)
        {
            const glm::vec2 &p = points[idx[j]];
            if (j == (i + n - 1) % n || j == i % n || j == (i + 1) % n || p == a || p == b || p == c)
            {
                continue;
            }
            ear = !inTriangle(p, a, b, c);
        }
        if (ear)
        {
            vertex(a, color);
            vertex(b, color);
            vertex(c, color);
            idx.erase(idx.begin() + static_cast<std::ptrdiff_t>(i % n));
            i = i % idx.size();
        }
        else
        {
            ++i;
        }
    }
    if (idx.size() == 3)
    {
        const glm::vec2 &a = points[idx[0]];
        const glm::vec2 &b = points[idx[1]];
        const glm::vec2 &c = points[idx[2]];
        if (cross2(a, b, c) != 0.0f)
        {
            vertex(a, color);
            vertex(b, color);
            vertex(c, color);
        }
    }
    addShape(z, first);
}

core::Result<rhi::PipelineHandle> Renderer2D::pipeline(const ShaderProgram &program, rhi::Format format, bool sprite, rhi::Format depthFormat)
{
    GraphicsState state;
    state.raster.cull = rhi::CullMode::kNone;
    if (depthFormat != rhi::Format::kUndefined)
    {
        state.depth = {true, false, rhi::CompareOp::kGreaterEqual}; // reverse-Z : le plus proche a la plus grande profondeur ; lecture seule
        state.depthFormat = depthFormat;
    }
    rhi::ColorTargetState target;
    target.format = format;
    target.blend = {true, rhi::BlendFactor::kSrcAlpha, rhi::BlendFactor::kOneMinusSrcAlpha, rhi::BlendOp::kAdd,
                    rhi::BlendFactor::kOne, rhi::BlendFactor::kOneMinusSrcAlpha, rhi::BlendOp::kAdd};
    state.colorTargets.assign(1, target);
    if (sprite)
    {
        state.vertexBuffers.push_back({.binding = 0, .stride = kSpriteFloats * sizeof(float), .stepRate = rhi::VertexStepRate::kInstance});
        for (std::uint32_t i = 0; i < 4; ++i)
        {
            state.vertexAttributes.push_back({.location = i, .binding = 0, .format = rhi::VertexFormat::kFloat4, .offset = i * 16});
        }
    }
    else
    {
        state.vertexBuffers.push_back({.binding = 0, .stride = kShapeVertexFloats * sizeof(float), .stepRate = rhi::VertexStepRate::kVertex});
        state.vertexAttributes.push_back({.location = 0, .binding = 0, .format = rhi::VertexFormat::kFloat2, .offset = 0});
        state.vertexAttributes.push_back({.location = 1, .binding = 0, .format = rhi::VertexFormat::kFloat4, .offset = 8});
    }
    state.debugName = program.name();
    return pipelines_.graphics(program, state);
}

core::Result<> Renderer2D::render(rhi::CommandEncoder &encoder, rhi::TextureHandle target, rhi::Format format, rhi::Extent2D size, bool loadExisting,
                                  const glm::vec4 &clearColor)
{
    Render2DOptions options;
    options.loadExisting = loadExisting;
    options.clearColor = clearColor;
    return render(encoder, target, format, size, options);
}

core::Result<> Renderer2D::render(rhi::CommandEncoder &encoder, rhi::TextureHandle target, rhi::Format format, rhi::Extent2D size,
                                  const Render2DOptions &options)
{
    const bool loadExisting = options.loadExisting;
    const glm::vec4 &clearColor = options.clearColor;
    const bool depthTested = !options.depth.isNull();
    struct Cleanup
    {
        Renderer2D &self;
        ~Cleanup()
        {
            self.items_.clear();
            self.spriteData_.clear();
            self.shapeData_.clear();
        }
    } cleanup{*this};

    if (size.width == 0 || size.height == 0)
    {
        return makeError(ErrorCode::kInvalidArgument, "Renderer2D::render: the target must be non-empty");
    }
    camera_.setViewport(size.width, size.height);
    const bool pixelPerfect = camera_.pixelPerfect() && !options.viewProjection;
    const float zoom = camera_.effectiveZoom();
    stats_.vertices = static_cast<std::uint32_t>(shapeData_.size() / kShapeVertexFloats);
    stats_.drawCalls = 0;
    stats_.bufferBytes = 0;

    // Tri stable : z, puis type, puis matériau ; l'ordre de dépôt départage le reste.
    std::ranges::sort(items_, [](const Item &a, const Item &b)
                      {
                          if (a.z != b.z)
                          {
                              return a.z < b.z;
                          }
                          if (a.kind != b.kind)
                          {
                              return a.kind < b.kind;
                          }
                          if (a.material != b.material)
                          {
                              return a.material < b.material;
                          }
                          return a.sequence < b.sequence;
                      });

    // Tampon unique dans l'ordre trié ; un lot = suite d'éléments du même type et du même matériau.
    struct Run
    {
        Kind kind;
        std::uint32_t material;
        std::uint64_t byteOffset;
        std::uint32_t count;
    };
    std::vector<float> data;
    std::vector<Run> runs;
    for (const Item &item : items_)
    {
        const std::uint32_t floats = item.kind == Kind::kSprite ? kSpriteFloats : kShapeVertexFloats;
        const std::vector<float> &source = item.kind == Kind::kSprite ? spriteData_ : shapeData_;
        if (runs.empty() || runs.back().kind != item.kind || runs.back().material != item.material)
        {
            runs.push_back({item.kind, item.material, data.size() * sizeof(float), 0});
        }
        const std::size_t at = data.size();
        data.insert(data.end(), source.begin() + static_cast<std::ptrdiff_t>(item.first) * floats,
                    source.begin() + static_cast<std::ptrdiff_t>(item.first + item.count) * floats);
        if (item.kind == Kind::kSprite && pixelPerfect)
        {
            for (std::size_t s = at; s < data.size(); s += kSpriteFloats)
            {
                float *f = &data[s];
                if (f[14] != 0.0f)
                {
                    continue; // un sprite tourné ne peut pas être aligné sur la grille des pixels
                }
                // Coin bas-gauche et taille alignés sur les pixels (taille d'au moins un pixel), pivot ramené au coin.
                const glm::vec2 corner{f[0] - f[12] * f[2], f[1] - f[13] * f[3]};
                const glm::vec2 snapped = glm::floor(corner * zoom + 0.5f);
                const glm::vec2 extent = glm::max(glm::floor(glm::vec2(f[2], f[3]) * zoom + 0.5f), glm::vec2(1.0f));
                f[0] = snapped.x / zoom;
                f[1] = snapped.y / zoom;
                f[2] = extent.x / zoom;
                f[3] = extent.y / zoom;
                f[12] = f[13] = 0.0f;
            }
        }
        runs.back().count += item.count;
    }

    if (!data.empty())
    {
        const std::uint64_t needed = data.size() * sizeof(float);
        if (!buffer_.valid() || buffer_.size() < needed)
        {
            rhi::BufferDesc bd;
            bd.size = std::max<std::uint64_t>(needed, 64 * 1024);
            bd.usage = rhi::BufferUsage::kVertex | rhi::BufferUsage::kCopyDst;
            bd.debugName = "renderer2d.batch";
            auto created = GpuBuffer::create(device_, bd);
            if (!created)
            {
                return std::unexpected(created.error());
            }
            buffer_ = std::move(*created);
        }
        if (auto r = buffer_.update(0, std::as_bytes(std::span(data))); !r)
        {
            return r;
        }
        stats_.bufferBytes = needed;
    }

    Push push{};
    const glm::mat4 viewProj = options.viewProjection ? *options.viewProjection : camera_.matrix();
    std::memcpy(push.viewProj, &viewProj[0][0], sizeof(push.viewProj));

    rhi::RenderPassDesc pass;
    pass.colors.push_back({.texture = target, .load = loadExisting ? rhi::LoadOp::kLoad : rhi::LoadOp::kClear,
                           .clearColor = {clearColor.r, clearColor.g, clearColor.b, clearColor.a}});
    if (depthTested)
    {
        pass.depth = rhi::DepthAttachment{.texture = options.depth, .load = rhi::LoadOp::kLoad, .store = rhi::StoreOp::kStore};
    }
    pass.debugName = "renderer2d";
    const bool encoderFailedBefore = encoder.error().has_value();
    encoder.beginRenderPass(pass);
    encoder.setScissor({0, 0, size.width, size.height});
    core::Result<> status;
    for (const Run &run : runs)
    {
        const bool sprite = run.kind == Kind::kSprite;
        auto pipe = pipeline(sprite ? *spriteProgram_ : *shapeProgram_, format, sprite, depthTested ? options.depthFormat : rhi::Format::kUndefined);
        if (!pipe)
        {
            status = std::unexpected(pipe.error());
            break;
        }
        encoder.setPipeline(*pipe);
        encoder.setPushConstants(std::as_bytes(std::span(&push, 1)));
        if (sprite)
        {
            const Material &m = materials_.at(run.material);
            encoder.setBindGroup(1, (pixelPerfect || m.forceNearest) ? m.nearest : m.linear);
        }
        encoder.setVertexBuffer(0, buffer_.handle(), run.byteOffset);
        if (sprite)
        {
            encoder.draw(6, run.count);
        }
        else
        {
            encoder.draw(run.count);
        }
        ++stats_.drawCalls;
    }
    encoder.endRenderPass();
    if (status && !encoderFailedBefore && encoder.error())
    {
        status = std::unexpected(*encoder.error());
    }
    return status;
}

} // namespace orosmo::renderer
