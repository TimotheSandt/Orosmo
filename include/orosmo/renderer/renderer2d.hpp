// Dessin 2D du monde (R2D-01, R2D-02, R2D-03) : lots de sprites (quads instanciés, atlas de textures), formes (lignes, rectangles,
// cercles, polygones) et caméra orthographique `Camera2D`. Les dessins sont triés par couche `z` (croissant : petit z derrière), puis
// par type et par matériau (texture) ; les dessins consécutifs du même matériau forment UN lot, donc UN appel de dessin. À `z` égal,
// l'ordre de dépôt est gardé entre dessins du même matériau, mais les matériaux peuvent s'intercaler : pour un ordre précis entre
// deux dessins qui se recouvrent, leur donner des `z` différents. Texte : il passe par le chemin existant (`ui::TextRenderer` ->
// `DrawList` -> `CanvasRenderer::render` sur la même cible, après `render`) ; ce dessinateur ne trace pas de glyphes.
// Compagnon du RHI (docs/03 : plugin « renderer2d ») : n'appelle aucune API graphique, tout passe par `rhi::Device`.
#pragma once

#include "orosmo/core/error.hpp"
#include "orosmo/renderer/render_context.hpp"
#include "orosmo/renderer/camera2d.hpp"
#include "orosmo/renderer/batch_arena.hpp"
#include "orosmo/renderer/gpu_buffer.hpp"
#include "orosmo/renderer/pipeline_cache.hpp"
#include "orosmo/renderer/shader_library.hpp"
#include "orosmo/renderer/texture.hpp"
#include "orosmo/rhi/device.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace orosmo::renderer
{

// Matériau d'un sprite : une texture (atlas) enregistrée auprès du dessinateur ; 0 = invalide.
struct SpriteMaterial
{
    std::uint32_t id = 0;
    bool valid() const noexcept { return id != 0; }
};

struct Sprite
{
    SpriteMaterial material;
    glm::vec2 position{0.0f}; // position du pivot, en unités de monde (Y vers le haut)
    glm::vec2 size{1.0f};     // unités de monde
    glm::vec2 pivot{0.5f};    // (0, 0) = coin bas-gauche, (1, 1) = coin haut-droit
    float rotation = 0.0f;    // radians, sens trigonométrique, autour du pivot
    glm::vec2 uvMin{0.0f};    // coin HAUT-gauche du sprite dans la texture (v = 0 : première ligne envoyée)
    glm::vec2 uvMax{1.0f};    // coin BAS-droit
    glm::vec4 color{1.0f};    // teinte multipliée au texel (non prémultipliée)
    float z = 0.0f;
};

struct Renderer2DStats
{
    std::uint32_t sprites = 0;    // sprites déposés
    std::uint32_t shapes = 0;     // formes déposées (une ligne, un cercle... = une forme)
    std::uint32_t vertices = 0;   // sommets de formes
    std::uint32_t drawCalls = 0;  // appels de dessin émis par `render`
    std::uint64_t bufferBytes = 0;
};

// Options d'un `render` (R2D-05). Sans profondeur ni matrice : dessin 2D plein cible, comme avant.
struct Render2DOptions
{
    bool loadExisting = true;        // garde le contenu de la cible ; sinon effacée avec `clearColor`
    glm::vec4 clearColor{0, 0, 0, 0};
    // 2D dans le monde : profondeur de la scène (reverse-Z) testée en lecture seule (`kGreaterEqual`, jamais écrite). Nulle : pas de test.
    rhi::TextureHandle depth{};
    rhi::Format depthFormat = rhi::Format::kD32Float;
    // Remplace la matrice de la caméra 2D : (x, y) du plan 2D -> espace de découpe (ex. vue-projection 3D x placement du plan). Le
    // pixel parfait est alors sans effet (l'alignement sur la grille des pixels n'a pas de sens en perspective).
    std::optional<glm::mat4> viewProjection{};
};

class Renderer2D
{
  public:
    // `shaderDirectory` contient sprite2d et shape2d (.vert et .frag), soit shaders/rhi/.
    static core::Result<std::unique_ptr<Renderer2D>> create(rhi::Device &device, ShaderLibrary &library, PipelineCache &pipelines,
                                                            const std::filesystem::path &shaderDirectory);
    // A75.4 : même fabrique depuis un `RenderContext`.
    static core::Result<std::unique_ptr<Renderer2D>> create(const RenderContext &context)
    {
        return create(context.device(), context.library(), context.pipelines(), context.shaderDirectory());
    }
    ~Renderer2D();
    Renderer2D(const Renderer2D &) = delete;
    Renderer2D &operator=(const Renderer2D &) = delete;

    // Enregistre une texture. La texture doit vivre au moins aussi longtemps que son usage ; un même handle donne le même matériau.
    // Échantillonnage : linéaire, ou au plus proche quand la caméra du rendu est en mode pixel parfait (ou `nearest`).
    core::Result<SpriteMaterial> material(const Texture &texture, bool nearest = false);
    // Oublie les matériaux (à appeler quand une texture est détruite ou recréée).
    void clearMaterials();

    // --- Image : begin, dépôts, render ------------------------------------------------------------------------------------------
    void begin(const Camera2D &camera);
    // Matériau inconnu ou invalide : refusé (kInvalidArgument), rien n'est déposé.
    core::Result<> draw(const Sprite &sprite);

    // Formes (couleur non prémultipliée, mélange alpha). `segments` = 0 : choisi selon la taille à l'écran (déterministe).
    void line(const glm::vec2 &a, const glm::vec2 &b, float thickness, const glm::vec4 &color, float z = 0.0f);
    // Ligne brisée de `thickness` ; `closed` relie le dernier point au premier.
    void polyline(std::span<const glm::vec2> points, float thickness, const glm::vec4 &color, bool closed = false, float z = 0.0f);
    // `position` = coin bas-gauche. `rotation` autour du centre.
    void rect(const glm::vec2 &position, const glm::vec2 &size, const glm::vec4 &color, float rotation = 0.0f, float z = 0.0f);
    void rectOutline(const glm::vec2 &position, const glm::vec2 &size, float thickness, const glm::vec4 &color, float z = 0.0f);
    void circle(const glm::vec2 &center, float radius, const glm::vec4 &color, std::uint32_t segments = 0, float z = 0.0f);
    // Anneau : bord intérieur à `radius - thickness / 2`, extérieur à `radius + thickness / 2` (`thickness` <= 2 * radius).
    void circleOutline(const glm::vec2 &center, float radius, float thickness, const glm::vec4 &color, std::uint32_t segments = 0, float z = 0.0f);
    // Polygone plein, simple (sans croisement), convexe ou non, sens quelconque. Moins de 3 points ou polygone dégénéré : ignoré.
    void polygon(std::span<const glm::vec2> points, const glm::vec4 &color, float z = 0.0f);

    // Trie, regroupe et dessine tout ce qui a été déposé dans `target` (une passe). `loadExisting` garde le contenu ; sinon la cible
    // est effacée avec `clearColor`. La vue est la cible entière (la taille du viewport de la caméra est remplacée par la sienne).
    // L'encodeur ne doit avoir aucune passe ouverte. Les dépôts sont vidés à la fin, succès ou échec.
    core::Result<> render(rhi::CommandEncoder &encoder, rhi::TextureHandle target, rhi::Format format, rhi::Extent2D size, bool loadExisting = true,
                          const glm::vec4 &clearColor = {0, 0, 0, 0});
    // Idem avec profondeur et/ou matrice 3D (R2D-05).
    core::Result<> render(rhi::CommandEncoder &encoder, rhi::TextureHandle target, rhi::Format format, rhi::Extent2D size, const Render2DOptions &options);

    const Renderer2DStats &stats() const noexcept { return stats_; }

  private:
    Renderer2D(rhi::Device &device, PipelineCache &pipelines) : device_(device), pipelines_(pipelines) {}

    enum class Kind : std::uint8_t
    {
        kShape,
        kSprite,
    };
    struct Item
    {
        float z;
        Kind kind;
        std::uint32_t material; // 0 pour les formes
        std::uint32_t sequence;
        std::uint32_t first; // premier sommet (forme) ou première instance (sprite) dans le tableau d'origine
        std::uint32_t count;
    };
    struct Material
    {
        rhi::TextureHandle texture;
        rhi::BindGroupHandle linear;
        rhi::BindGroupHandle nearest;
        bool forceNearest = false;
    };

    void addShape(float z, std::uint32_t firstVertex);
    void vertex(const glm::vec2 &p, const glm::vec4 &color);
    void quad(const glm::vec2 &a, const glm::vec2 &b, const glm::vec2 &c, const glm::vec2 &d, const glm::vec4 &color);
    void ring(const glm::vec2 &center, float inner, float outer, const glm::vec4 &color, std::uint32_t segments);
    std::uint32_t segmentsFor(float radius, std::uint32_t requested) const;
    core::Result<rhi::PipelineHandle> pipeline(const ShaderProgram &program, rhi::Format format, bool sprite, rhi::Format depthFormat = rhi::Format::kUndefined);

    rhi::Device &device_;
    PipelineCache &pipelines_;
    std::shared_ptr<ShaderProgram> spriteProgram_;
    std::shared_ptr<ShaderProgram> shapeProgram_;
    rhi::SamplerHandle linearSampler_;
    rhi::SamplerHandle nearestSampler_;
    BatchArena arena_; // une plage par appel de render() sur un même encodeur
    std::map<std::uint32_t, Material> materials_;
    std::uint32_t nextMaterial_ = 1;

    Camera2D camera_;
    std::vector<Item> items_;
    std::vector<float> spriteData_; // 16 flottants par sprite
    std::vector<float> shapeData_;  // 6 flottants par sommet : x, y, r, g, b, a
    std::uint32_t sequence_ = 0;
    Renderer2DStats stats_;
};

} // namespace orosmo::renderer
