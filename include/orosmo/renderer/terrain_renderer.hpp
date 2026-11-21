// TER-07 : rendu du terrain, plugin de la couche de rendu. Il ne connaît pas `world` (couche supérieure) : il reçoit des tuiles
// déjà maillées (positions relatives à l'origine de la tuile, normales) et les dessine par `SceneRenderer`, avec un matériau à
// quatre couches (sable, herbe, roche, neige) mélangées selon l'altitude et la pente et lues en projection triplanaire dans un
// tableau de textures. Les couches par défaut sont générées (motifs déterministes) ; `setLayers` les remplace.
#pragma once

#include "orosmo/core/error.hpp"
#include "orosmo/core/reflection.hpp"
#include "orosmo/renderer/mesh.hpp"
#include "orosmo/renderer/scene_renderer.hpp"
#include "orosmo/renderer/shader_library.hpp"
#include "orosmo/rhi/device.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace orosmo::renderer
{

struct TerrainMaterial
{
    float sandHeight = 2.0f;     // m : sable en dessous
    float snowHeight = 90.0f;    // m : neige au-dessus
    float rockSlope = 0.35f;     // 1 - normale.y : roche au-delà
    float blendWidth = 4.0f;     // m : largeur des transitions d'altitude
    float textureScale = 0.125f; // répétitions par mètre
};

struct TerrainTileGeometry
{
    std::uint64_t id = 0;
    glm::vec3 origin{0.0f};           // origine monde de la tuile (m)
    std::span<const float> vertices;  // 6 flottants par sommet : position RELATIVE à `origin`, normale
    std::span<const std::uint32_t> indices;
};

class TerrainRenderer
{
  public:
    static constexpr std::uint32_t kLayerCount = 4;
    static constexpr std::uint32_t kLayerSize = 64; // texels par côté des couches par défaut

    static core::Result<std::unique_ptr<TerrainRenderer>> create(rhi::Device &device, ShaderLibrary &library, const std::filesystem::path &shaderDirectory);
    ~TerrainRenderer();
    TerrainRenderer(const TerrainRenderer &) = delete;
    TerrainRenderer &operator=(const TerrainRenderer &) = delete;

    // Remplace les couches : `kLayerCount` images RGBA8 carrées de même taille (texels, ligne 0 = haut).
    core::Result<> setLayers(std::uint32_t size, std::span<const std::vector<std::uint8_t>> layers);

    // Crée ou remplace la tuile `id` ; `retainOnly` retire celles qui ne sont pas dans la liste.
    core::Result<> updateTile(const TerrainTileGeometry &tile);
    void removeTile(std::uint64_t id);
    void retainOnly(std::span<const std::uint64_t> ids);
    bool hasTile(std::uint64_t id) const { return tiles_.contains(id); }
    std::size_t tileCount() const noexcept { return tiles_.size(); }

    // Dessine toutes les tuiles dans la passe de scène ouverte (après `SceneRenderer::setCamera`).
    core::Result<> draw(SceneRenderer &scene, const Camera &camera);

    TerrainMaterial material;

  private:
    TerrainRenderer(rhi::Device &device) : device_(device) {}
    core::Result<> createLayerTexture(std::uint32_t size, std::span<const std::vector<std::uint8_t>> layers);

    struct Tile
    {
        Mesh mesh;
        glm::vec3 origin{0.0f};
    };

    rhi::Device &device_;
    std::shared_ptr<ShaderProgram> program_;
    rhi::TextureHandle layers_;
    rhi::SamplerHandle sampler_;
    std::unordered_map<std::uint64_t, Tile> tiles_;
};

} // namespace orosmo::renderer

template <> struct orosmo::core::Reflect<orosmo::renderer::TerrainMaterial>
{
    static constexpr int kVersion = 1;
    static constexpr auto fields()
    {
        using M = orosmo::renderer::TerrainMaterial;
        return std::make_tuple(field("sandHeight", &M::sandHeight, range(-500.0, 500.0), "sable sous cette altitude (m)"),
                               field("snowHeight", &M::snowHeight, range(-500.0, 5000.0), "neige au-dessus (m)"),
                               field("rockSlope", &M::rockSlope, range(0.0, 1.0), "roche au-delà de cette pente (1 - n.y)"),
                               field("blendWidth", &M::blendWidth, range(0.0, 100.0), "largeur des transitions (m)"),
                               field("textureScale", &M::textureScale, range(0.001, 4.0), "répétitions des textures par mètre"));
    }
};
