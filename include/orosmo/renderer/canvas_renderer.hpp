// Exécute une `DrawList` 2D sur une texture du RHI : rectangles, glyphes MSDF, couches hors écran (conteneurs dessinés dans une
// texture puis affichés, avec défilement), découpages. Le dessinateur 2D qui accompagne le RHI (docs/03 : plugin « renderer2d ») ;
// le batch par instances de R2D-01 viendra en M5 (QUESTIONS A21).
#pragma once

#include "engine/core/error.hpp"
#include "engine/renderer/draw_list.hpp"
#include "engine/renderer/gpu_buffer.hpp"
#include "engine/renderer/pipeline_cache.hpp"
#include "engine/renderer/render_target.hpp"
#include "engine/renderer/shader_library.hpp"
#include "engine/renderer/texture.hpp"
#include "engine/rhi/device.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <vector>

namespace engine::renderer
{

struct CanvasTarget
{
    rhi::TextureHandle texture;
    rhi::Format format = rhi::Format::kRGBA8Unorm;
    rhi::Extent2D size{};
};

class CanvasRenderer
{
  public:
    // `shaderDirectory` contient ui_rect, ui_glyph et ui_layer (.vert et .frag), soit shaders/rhi/.
    static core::Result<std::unique_ptr<CanvasRenderer>> create(rhi::Device &device, ShaderLibrary &library, PipelineCache &pipelines,
                                                                const std::filesystem::path &shaderDirectory);
    ~CanvasRenderer();
    CanvasRenderer(const CanvasRenderer &) = delete;
    CanvasRenderer &operator=(const CanvasRenderer &) = delete;

    // Dessine `list` sur `target` ; `loadExisting` garde le contenu actuel de la cible (l'interface se pose sur la scène), sinon elle
    // est d'abord effacée en transparent. L'encodeur ne doit avoir aucune passe ouverte. Les couches contiennent une couleur
    // prémultipliée et sont composées comme telle. Erreurs : liste mal formée, taille de couche invalide, erreur de l'encodeur
    // pendant le rendu, ou kNotFound si une `DrawLayerImage` vise une couche qui n'existe pas (le contenu mis en cache par
    // par l'appelant est perdu : il doit redemander une liste complète, sans image de couche).
    core::Result<> render(rhi::CommandEncoder &encoder, const DrawList &list, const CanvasTarget &target, bool loadExisting = true);

    // Libère les textures d'atlas et de couches (les handles sont détruits ; la prochaine liste les recrée).
    void clear();
    std::size_t layerCount() const noexcept { return layers_.size(); }
    std::size_t atlasCount() const noexcept { return atlases_.size(); }
    // Une image (atlas de glyphes, image de thème ou du jeu) absente de ce nombre de rendus successifs est libérée.
    static constexpr std::uint64_t kAtlasIdleRenders = 240;

  private:
    struct Layer
    {
        RenderTarget target;
        rhi::BindGroupHandle group;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        bool used = false;
        std::vector<std::uint64_t> children; // couches dont l'image est dessinée dans celle-ci : gardées tant qu'elle l'est
    };
    struct Atlas
    {
        Texture texture;
        rhi::BindGroupHandle group;
        std::uint64_t version = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint64_t lastUsed = 0; // numéro du dernier rendu qui l'a dessinée (éviction des images qui ne servent plus)
    };
    // Découpage en pixels de la cible. Repère du RHI (A26) : `bottom` est la PREMIÈRE ligne (depuis le haut), `top` la ligne qui
    // suit la dernière ; les noms viennent de l'ancien repère OpenGL (ligne 0 en bas).
    struct Clip
    {
        int left = 0;
        int bottom = 0;
        int right = 0;
        int top = 0;
        bool empty() const { return right <= left || top <= bottom; }
    };
    struct Surface
    {
        rhi::TextureHandle texture;
        rhi::Format format = rhi::Format::kRGBA8Unorm;
        rhi::Extent2D px{};
        glm::vec2 logical{0.0f};
        std::vector<Clip> clips;
        std::uint64_t layerId = 0; // 0 : surface principale
    };

    CanvasRenderer(rhi::Device &device, PipelineCache &pipelines) : device_(device), pipelines_(pipelines) {}
    enum class Blend : std::uint8_t
    {
        kReplace,       // écrit tel quel (effacement d'une zone)
        kStraight,      // couleur non prémultipliée (rectangles, glyphes) : src * a + dst * (1 - a)
        kPremultiplied, // image de couche, déjà prémultipliée : src + dst * (1 - a)
    };
    core::Result<rhi::PipelineHandle> pipeline(const ShaderProgram &program, rhi::Format format, Blend blend, bool glyph);
    core::Result<Atlas *> atlasFor(const AtlasImage &image);
    core::Result<Layer *> layerFor(std::uint64_t id, std::uint32_t width, std::uint32_t height, bool &created);
    void beginPass(rhi::CommandEncoder &encoder, const Surface &surface, rhi::LoadOp load, const char *name);
    void applyClip(rhi::CommandEncoder &encoder, const Surface &surface);
    Clip fullClip(const Surface &surface) const;
    void releaseLayer(Layer &layer);

    rhi::Device &device_;
    PipelineCache &pipelines_;
    std::shared_ptr<ShaderProgram> rect_;
    std::shared_ptr<ShaderProgram> glyph_;
    std::shared_ptr<ShaderProgram> layerImage_;
    std::shared_ptr<ShaderProgram> image_;
    rhi::SamplerHandle atlasSampler_;
    GpuBuffer glyphBuffer_;
    std::map<std::uint64_t, Layer> layers_;
    std::map<std::uint64_t, Atlas> atlases_;
    std::uint64_t renderCount_ = 0;
};

} // namespace engine::renderer
