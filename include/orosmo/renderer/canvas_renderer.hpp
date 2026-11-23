// Exécute une `DrawList` 2D sur une texture du RHI : rectangles, glyphes MSDF, couches hors écran (conteneurs dessinés dans une
// texture puis affichés, avec défilement), découpages. Le dessinateur 2D qui accompagne le RHI (docs/03 : plugin « renderer2d ») ;
// R2D-01 : les dessins consécutifs compatibles sont regroupés en lots (rectangles instanciés, séries de glyphes et d'images fusionnées).
#pragma once

#include "orosmo/core/error.hpp"
#include "orosmo/renderer/draw_list.hpp"
#include "orosmo/renderer/batch_arena.hpp"
#include "orosmo/renderer/gpu_buffer.hpp"
#include "orosmo/renderer/pipeline_cache.hpp"
#include "orosmo/renderer/render_target.hpp"
#include "orosmo/renderer/shader_library.hpp"
#include "orosmo/renderer/texture.hpp"
#include "orosmo/rhi/device.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <vector>

namespace orosmo::renderer
{

struct CanvasTarget
{
    rhi::TextureHandle texture;
    rhi::Format format = rhi::Format::kRGBA8Unorm;
    rhi::Extent2D size{};
};

// Compteurs du dernier `render` (R2D-01).
struct CanvasStats
{
    std::uint32_t drawCommands = 0;    // commandes de la liste qui dessinent (rectangle, effacement, glyphes, image, image de couche)
    std::uint32_t batchedCommands = 0; // celles effectivement dessinées (hors découpage vide) : le nombre de dessins SANS lots
    std::uint32_t drawCalls = 0;       // dessins émis vers le RHI avec les lots
    std::uint64_t bufferBytes = 0;     // octets du tampon d'instances et de sommets envoyé
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

    // R2D-06 : image dont les pixels sont déjà sur le GPU (vue 3D incrustée dans un widget, rendu vers texture). `image` est une
    // `AtlasImage` sans pixels (`id` neuf par `newAtlasId`, `width`/`height` = taille d'affichage, `rgba` vide) que l'interface
    // dessine comme n'importe quelle image (`ImageView`, `DrawImage`) : elle utilise cette texture au lieu d'envoyer des pixels. La
    // texture n'est pas possédée (à garder vivante, `unregisterExternalImage` avant de la détruire) ; son échantillonneur est
    // celui donné. La première ligne de la texture est le HAUT de l'image. Réinscrire le même `id` remplace la texture (taille de la vue
    // changée).
    core::Result<> registerExternalImage(std::uint64_t id, rhi::TextureHandle texture, rhi::SamplerHandle sampler);
    void unregisterExternalImage(std::uint64_t id);
    std::size_t externalImageCount() const noexcept { return externals_.size(); }
    // Libère les textures d'atlas et de couches (les handles sont détruits ; la prochaine liste les recrée).
    void clear();
    std::size_t layerCount() const noexcept { return layers_.size(); }
    std::size_t atlasCount() const noexcept { return atlases_.size(); }
    const CanvasStats &stats() const noexcept { return stats_; }
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
    enum class Layout : std::uint8_t
    {
        kNone,            // quad généré par gl_VertexIndex (images de couche)
        kRectInstances,   // un rectangle par instance (attributs par instance)
        kColoredVertices, // x, y, u, v + couleur par sommet (glyphes, images)
    };
    core::Result<rhi::PipelineHandle> pipeline(const ShaderProgram &program, rhi::Format format, Blend blend, Layout layout);
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
    BatchArena arena_; // une plage par appel de render() sur un même encodeur
    CanvasStats stats_;
    std::map<std::uint64_t, Layer> layers_;
    std::map<std::uint64_t, Atlas> atlases_;
    std::map<std::uint64_t, Atlas> externals_; // textures non possédées (`texture` de l'Atlas reste vide)
    std::uint64_t renderCount_ = 0;
};

} // namespace orosmo::renderer
