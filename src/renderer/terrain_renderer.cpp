#include "orosmo/renderer/terrain_renderer.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>

namespace orosmo::renderer
{

using core::ErrorCode;
using core::makeError;

namespace
{

// Couches par défaut : couleur de base et variation pseudo-aléatoire DÉTERMINISTE par texel (hachage entier, pas d'horloge).
std::vector<std::uint8_t> defaultLayer(std::uint32_t layer, std::uint32_t size)
{
    static constexpr std::array<std::array<int, 3>, 4> kBase = {{{194, 178, 128}, {86, 125, 70}, {112, 106, 100}, {236, 238, 244}}};
    static constexpr std::array<int, 4> kVariation = {14, 22, 26, 8};
    std::vector<std::uint8_t> texels(static_cast<std::size_t>(size) * size * 4);
    for (std::uint32_t y = 0; y < size; ++y)
    {
        for (std::uint32_t x = 0; x < size; ++x)
        {
            std::uint32_t h = x * 0x8da6b343U ^ y * 0xd8163841U ^ (layer + 1) * 0xcb1ab31fU;
            h ^= h >> 15;
            h *= 0x2c1b3c6dU;
            h ^= h >> 12;
            const int delta = static_cast<int>(h % static_cast<std::uint32_t>(2 * kVariation[layer] + 1)) - kVariation[layer];
            const std::size_t i = (static_cast<std::size_t>(y) * size + x) * 4;
            for (int c = 0; c < 3; ++c)
            {
                texels[i + static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(std::clamp(kBase[layer][static_cast<std::size_t>(c)] + delta, 0, 255));
            }
            texels[i + 3] = 255;
        }
    }
    return texels;
}

struct TerrainPush
{
    std::int32_t wireframe = 0;
    std::int32_t pad[3]{};
    float origin[4]{};
    float bands[4]{};
    float texture[4]{};
};
static_assert(sizeof(TerrainPush) == 64);

} // namespace

core::Result<std::unique_ptr<TerrainRenderer>> TerrainRenderer::create(rhi::Device &device, ShaderLibrary &library, const std::filesystem::path &shaderDirectory)
{
    std::unique_ptr<TerrainRenderer> renderer(new TerrainRenderer(device));
    auto program = library.loadGraphics(shaderDirectory / "terrain.vert", shaderDirectory / "terrain.frag");
    if (!program)
    {
        return std::unexpected(program.error());
    }
    renderer->program_ = *program;
    rhi::SamplerDesc sd;
    sd.minFilter = rhi::Filter::kLinear;
    sd.magFilter = rhi::Filter::kLinear;
    sd.mipFilter = rhi::Filter::kLinear;
    sd.addressU = rhi::AddressMode::kRepeat;
    sd.addressV = rhi::AddressMode::kRepeat;
    sd.debugName = "terrain.layers.sampler";
    auto sampler = device.createSampler(sd);
    if (!sampler)
    {
        return std::unexpected(sampler.error());
    }
    renderer->sampler_ = *sampler;
    std::vector<std::vector<std::uint8_t>> layers;
    for (std::uint32_t l = 0; l < kLayerCount; ++l)
    {
        layers.push_back(defaultLayer(l, kLayerSize));
    }
    if (auto r = renderer->createLayerTexture(kLayerSize, layers); !r)
    {
        return std::unexpected(r.error());
    }
    return renderer;
}

TerrainRenderer::~TerrainRenderer()
{
    tiles_.clear();
    device_.destroy(layers_);
    device_.destroy(sampler_);
}

core::Result<> TerrainRenderer::createLayerTexture(std::uint32_t size, std::span<const std::vector<std::uint8_t>> layers)
{
    if (layers.size() != kLayerCount || size == 0)
    {
        return makeError(ErrorCode::kInvalidArgument, "TerrainRenderer: exactly 4 non-empty layers are required");
    }
    for (const auto &layer : layers)
    {
        if (layer.size() != static_cast<std::size_t>(size) * size * 4)
        {
            return makeError(ErrorCode::kInvalidArgument, "TerrainRenderer: each layer must be size * size RGBA8 texels");
        }
    }
    rhi::TextureDesc desc;
    desc.dimension = rhi::TextureDimension::k2DArray;
    desc.format = rhi::Format::kRGBA8Unorm;
    desc.size = {size, size, 1};
    desc.layers = kLayerCount;
    desc.mipLevels = 0; // chaîne complète
    desc.usage = rhi::TextureUsage::kSampled | rhi::TextureUsage::kCopyDst;
    desc.debugName = "terrain.layers";
    auto texture = device_.createTexture(desc);
    if (!texture)
    {
        return std::unexpected(texture.error());
    }
    for (std::uint32_t l = 0; l < kLayerCount; ++l)
    {
        rhi::TextureCopy region;
        region.layer = l;
        region.size = {size, size, 1};
        if (auto r = device_.updateTexture(*texture, region, std::as_bytes(std::span(layers[l]))); !r)
        {
            device_.destroy(*texture);
            return r;
        }
    }
    auto encoder = device_.createCommandEncoder();
    encoder->generateMips(*texture);
    if (auto r = device_.submit(std::move(encoder)); !r)
    {
        device_.destroy(*texture);
        return r;
    }
    const rhi::TextureHandle previous = layers_;
    // Les tuiles existantes pointent vers l'ancien tableau : on refait leur groupe de textures. Transactionnel : un échec remet les
    // tuiles déjà passées sur l'ancien tableau, qui reste à son propriétaire ; le nouveau est détruit.
    const TextureBinding fresh{*texture, sampler_};
    std::vector<Tile *> done;
    for (auto &[id, tile] : tiles_)
    {
        if (auto r = tile.mesh.setTextures(std::span(&fresh, 1)); !r)
        {
            const TextureBinding old{previous, sampler_};
            for (Tile *t : done)
            {
                (void)t->mesh.setTextures(std::span(&old, 1));
            }
            device_.destroy(*texture);
            return r;
        }
        done.push_back(&tile);
    }
    layers_ = *texture;
    device_.destroy(previous);
    return {};
}

core::Result<> TerrainRenderer::setLayers(std::uint32_t size, std::span<const std::vector<std::uint8_t>> layers) { return createLayerTexture(size, layers); }

core::Result<> TerrainRenderer::updateTile(const TerrainTileGeometry &tile)
{
    if (tile.vertices.empty() || tile.vertices.size() % 6 != 0 || tile.indices.empty())
    {
        return makeError(ErrorCode::kInvalidArgument, "TerrainRenderer::updateTile: 6 floats per vertex and some indices are required");
    }
    MeshData data;
    data.vertices.assign(tile.vertices.begin(), tile.vertices.end());
    data.indices.assign(tile.indices.begin(), tile.indices.end());
    data.attributeSizes = {3, 3};
    data.name = "terrain.tile." + std::to_string(tile.id);
    auto mesh = Mesh::create(device_, data);
    if (!mesh)
    {
        return std::unexpected(mesh.error());
    }
    mesh->setProgram(program_);
    const TextureBinding binding{layers_, sampler_};
    if (auto r = mesh->setTextures(std::span(&binding, 1)); !r)
    {
        return r;
    }
    mesh->setPosition(tile.origin);
    tiles_.insert_or_assign(tile.id, Tile{std::move(*mesh), tile.origin});
    return {};
}

void TerrainRenderer::removeTile(std::uint64_t id) { tiles_.erase(id); }

void TerrainRenderer::retainOnly(std::span<const std::uint64_t> ids)
{
    std::erase_if(tiles_, [&](const auto &entry) { return std::ranges::find(ids, entry.first) == ids.end(); });
}

core::Result<> TerrainRenderer::draw(SceneRenderer &scene, const Camera &camera)
{
    TerrainPush push;
    push.bands[0] = material.sandHeight;
    push.bands[1] = material.snowHeight;
    push.bands[2] = material.rockSlope;
    push.bands[3] = material.blendWidth;
    push.texture[0] = material.textureScale;
    for (auto &[id, tile] : tiles_)
    {
        push.origin[0] = tile.origin.x;
        push.origin[1] = tile.origin.y;
        push.origin[2] = tile.origin.z;
        tile.mesh.setPushConstants(std::as_bytes(std::span(&push, 1)));
        if (auto r = scene.drawMesh(tile.mesh, camera); !r)
        {
            return r;
        }
    }
    return {};
}

} // namespace orosmo::renderer
