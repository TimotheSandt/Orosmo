// REN-17 (HDR, exposition, tonemapping) et REN-18 (bloom, exposition automatique par histogramme, étalonnage par LUT, vignettage).
// Chaque effet est un plugin du graphe de passes (`RenderFeature`, REN-01), activable à l'exécution (`setEnabled`) : désactivé, il
// ne déclare aucune passe. Chaque passe déclare ses entrées (`read`) et ses sorties (`write`, `writeColor`) ; l'ordre entre effets
// est leur ordre d'enregistrement (écrivains successifs de la couleur de la scène).
//
// Les effets HDR lisent une scène en virgule flottante : `SceneRenderer::setSceneColorFormat(kRGBA16Float)`. `PostEffects::sync`
// le fait seulement quand un effet HDR est actif, sinon la scène reste en RGBA8 (image inchangée au pixel près).
//
// Chaîne conseillée (ordre de `addPostEffects`) : exposition automatique (mesure la scène) → bloom (ajouté à la scène HDR) →
// tonemapping (exposition, opérateur, sRGB) → étalonnage par LUT (valeurs affichées) → vignettage. Tout s'exécute à la résolution
// de rendu, après la passe « scene » et avant la mise à l'échelle.
#pragma once

#include "orosmo/core/error.hpp"
#include "orosmo/renderer/render_context.hpp"
#include "orosmo/renderer/fullscreen_blit.hpp"
#include "orosmo/renderer/pipeline_cache.hpp"
#include "orosmo/renderer/render_feature.hpp"
#include "orosmo/renderer/shader_library.hpp"
#include "orosmo/renderer/tonemap.hpp"
#include "orosmo/rhi/device.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace orosmo::renderer
{

class SceneRenderer;

// Ressources partagées par les effets : périphérique, shaders (`<shaderDirectory>/blit.vert` et `post/*`), copie plein écran,
// échantillonneurs (linéaire et au plus proche, bords bornés).
class PostContext
{
  public:
    static core::Result<std::shared_ptr<PostContext>> create(rhi::Device &device, ShaderLibrary &library, PipelineCache &pipelines,
                                                             const std::filesystem::path &shaderDirectory);
    // A75.4 : même fabrique depuis un `RenderContext`.
    static core::Result<std::shared_ptr<PostContext>> create(const RenderContext &context)
    {
        return create(context.device(), context.library(), context.pipelines(), context.shaderDirectory());
    }
    ~PostContext();
    PostContext(const PostContext &) = delete;
    PostContext &operator=(const PostContext &) = delete;

    rhi::Device &device() const noexcept { return device_; }
    ShaderLibrary &library() const noexcept { return library_; }
    PipelineCache &pipelines() const noexcept { return pipelines_; }
    FullscreenBlit &blit() const noexcept { return *blit_; }
    rhi::SamplerHandle linearSampler() const noexcept { return linear_; }
    rhi::SamplerHandle nearestSampler() const noexcept { return nearest_; }
    // Programme plein écran : `blit.vert` + `<shaderDirectory>/post/<fragment>`.
    core::Result<std::shared_ptr<ShaderProgram>> fullscreenProgram(const std::string &fragment) const;
    core::Result<std::shared_ptr<ShaderProgram>> computeProgram(const std::string &file) const;

  private:
    PostContext(rhi::Device &device, ShaderLibrary &library, PipelineCache &pipelines) : device_(device), library_(library), pipelines_(pipelines) {}
    rhi::Device &device_;
    ShaderLibrary &library_;
    PipelineCache &pipelines_;
    std::filesystem::path shaderDirectory_;
    std::unique_ptr<FullscreenBlit> blit_;
    rhi::SamplerHandle linear_;
    rhi::SamplerHandle nearest_;
};

// Base des effets : activation, groupes de liaison de l'image (détruits à l'image suivante, destruction différée par le RHI).
class PostEffect : public RenderFeature
{
  public:
    ~PostEffect() override;
    bool enabled() const noexcept { return enabled_; }
    void setEnabled(bool enabled) noexcept { enabled_ = enabled; }
    // Vrai : l'effet a besoin d'une scène en virgule flottante (voir PostEffects::sync).
    virtual bool needsHdr() const noexcept { return true; }
    bool postProcessesScene() const noexcept override { return enabled_; }
    core::Result<> declarePasses(RenderGraphBuilder &builder) final;

  protected:
    explicit PostEffect(std::shared_ptr<PostContext> context) : context_(std::move(context)) {}
    virtual core::Result<> declare(RenderGraphBuilder &builder) = 0;
    // Groupe de liaison valable pour l'image en cours.
    core::Result<rhi::BindGroupHandle> frameGroup(const rhi::BindGroupDesc &desc);
    // Passe raster `name` qui copie `source` (transitoire) dans la couleur de la scène.
    void declareCopyToScene(RenderGraphBuilder &builder, const std::string &name, RgTexture source);
    // Pipeline plein écran d'une cible au format `format` (mélange et masque au choix).
    core::Result<rhi::PipelineHandle> fullscreenPipeline(const ShaderProgram &program, rhi::Format format, const rhi::BlendState &blend = {},
                                                         rhi::ColorWriteMask mask = rhi::ColorWriteMask::kAll);

    std::shared_ptr<PostContext> context_;

  private:
    bool enabled_ = true;
    std::vector<rhi::BindGroupHandle> groups_;
};

// ── REN-18 : exposition automatique par histogramme (compute) ─────────────────────────────────────────────────────────
struct AutoExposureSettings
{
    float minLog2 = -10.0f;       // luminance minimale mesurée (log2) ; en dessous : case 0, ignorée
    float maxLog2 = 6.0f;         // luminance maximale mesurée (log2)
    float key = 0.18f;            // gris moyen visé : exposition = key / luminance moyenne
    float minEv = -6.0f;          // bornes de l'exposition (en EV : 2^ev)
    float maxEv = 6.0f;
    float adaptationSpeed = 1.5f; // par seconde (1 - exp(-dt × vitesse)) ; <= 0 : immédiat
};

struct ExposureState
{
    float exposure = 1.0f;         // exposition appliquée (après adaptation)
    float averageLuminance = 0.0f; // luminance moyenne mesurée
    float target = 1.0f;           // exposition visée à cette image
    bool valid = false;
};

class AutoExposureFeature final : public PostEffect
{
  public:
    static constexpr const char *kName = "post.auto-exposure";
    static constexpr const char *kExposureBuffer = "post.exposure";   // tampon (vec4) lu par le tonemapping
    static constexpr const char *kHistogramBuffer = "post.histogram"; // 256 uint
    static core::Result<std::unique_ptr<AutoExposureFeature>> create(std::shared_ptr<PostContext> context);
    ~AutoExposureFeature() override;

    std::string name() const override { return kName; }
    // Compute, et des groupes de 256 invocations (histogramme 16 × 16, moyenne 256 × 1) : limites lues dans les capacités.
    static bool supportedBy(const rhi::Capabilities &capabilities) noexcept;
    bool supports(const rhi::Capabilities &capabilities) const noexcept override { return supportedBy(capabilities); }
    core::Result<> prepare(const FrameData &frame) override;

    AutoExposureSettings &settings() noexcept { return settings_; }
    // La prochaine image part de l'exposition visée, sans adaptation (changement de scène, téléportation).
    core::Result<> resetAdaptation();
    // Lecture CPU (bloquante, pour les tests et le debug) de la dernière image exécutée.
    core::Result<std::vector<std::uint32_t>> readHistogram() const;
    core::Result<ExposureState> readExposure() const;

  private:
    explicit AutoExposureFeature(std::shared_ptr<PostContext> context) : PostEffect(std::move(context)) {}
    core::Result<> declare(RenderGraphBuilder &builder) override;

    AutoExposureSettings settings_;
    std::shared_ptr<ShaderProgram> clear_;
    std::shared_ptr<ShaderProgram> histogram_;
    std::shared_ptr<ShaderProgram> average_;
    rhi::BufferHandle histogramBuffer_;
    rhi::BufferHandle exposureBuffer_;
    float deltaSeconds_ = 0.0f;
};

// ── REN-18 : bloom (réductions successives, agrandissements additifs, ajout à la scène) ──────────────────────────────────
struct BloomSettings
{
    float threshold = 1.0f;    // luminance (après exposition de la scène) à partir de laquelle un pixel rayonne
    float knee = 0.5f;         // douceur du seuil
    float intensity = 0.08f;   // part du bloom ajoutée à la scène
    float radius = 1.0f;       // rayon du filtre d'agrandissement (texels)
    float maxLuminance = 64.0f; // luminance maximale prise en compte (évite les lucioles)
    std::uint32_t levels = 5;  // niveaux de réduction (1/2, 1/4, ... de la taille de rendu), 1 à 8
};

class BloomFeature final : public PostEffect
{
  public:
    static constexpr const char *kName = "post.bloom";
    static core::Result<std::unique_ptr<BloomFeature>> create(std::shared_ptr<PostContext> context);
    std::string name() const override { return kName; }
    BloomSettings &settings() noexcept { return settings_; }

  private:
    explicit BloomFeature(std::shared_ptr<PostContext> context) : PostEffect(std::move(context)) {}
    core::Result<> declare(RenderGraphBuilder &builder) override;

    BloomSettings settings_;
    std::shared_ptr<ShaderProgram> down_;
    std::shared_ptr<ShaderProgram> up_;
};

// ── REN-17 : exposition, tonemapping, sRGB ──────────────────────────────────────────────────────────────────────────
struct TonemapSettings
{
    Tonemapper tonemapper = Tonemapper::kAces;
    float exposureEv = 0.0f;  // compensation (EV), multipliée par FrameData::exposure.x (FrameEnvironment::exposure)
    float whitePoint = 4.0f;  // Reinhard étendu : luminance qui devient le blanc
    bool encodeSrgb = true;   // sortie encodée en sRGB (cible UNORM affichée telle quelle) ; faux : sortie linéaire
    bool useAutoExposure = true; // multiplie par l'exposition de AutoExposureFeature si elle est active dans le graphe
};

class TonemapFeature final : public PostEffect
{
  public:
    static constexpr const char *kName = "post.tonemap";
    static core::Result<std::unique_ptr<TonemapFeature>> create(std::shared_ptr<PostContext> context);
    ~TonemapFeature() override;
    std::string name() const override { return kName; }
    core::Result<> prepare(const FrameData &frame) override;
    TonemapSettings &settings() noexcept { return settings_; }

  private:
    explicit TonemapFeature(std::shared_ptr<PostContext> context) : PostEffect(std::move(context)) {}
    core::Result<> declare(RenderGraphBuilder &builder) override;

    TonemapSettings settings_;
    std::shared_ptr<ShaderProgram> program_;
    rhi::BufferHandle unitExposure_; // vec4(1) : lu quand l'exposition automatique est absente
    float frameExposure_ = 1.0f;
};

// ── REN-18 : étalonnage par LUT 3D ──────────────────────────────────────────────────────────────────────────────────
class ColorGradingFeature final : public PostEffect
{
  public:
    static constexpr const char *kName = "post.color-grading";
    // Commence avec une LUT identité de 16³.
    static core::Result<std::unique_ptr<ColorGradingFeature>> create(std::shared_ptr<PostContext> context);
    ~ColorGradingFeature() override;
    std::string name() const override { return kName; }
    // Valeurs affichées (après tonemapping) : la LUT n'a pas besoin d'une scène HDR.
    bool needsHdr() const noexcept override { return false; }
    // Remplace la LUT (envoyée en RGBA16F), active à l'image suivante. kInvalidArgument si elle n'est pas valide, a une composante non finie
    // ou hors de [0, 1] ; l'ancienne reste alors en place.
    core::Result<> setLut(const ColorLut &lut);
    std::uint32_t lutSize() const noexcept { return lutSize_; }
    // 0 : image inchangée, 1 : LUT entière (bornée à [0, 1]).
    void setStrength(float strength) noexcept;
    float strength() const noexcept { return strength_; }

  private:
    explicit ColorGradingFeature(std::shared_ptr<PostContext> context) : PostEffect(std::move(context)) {}
    core::Result<> declare(RenderGraphBuilder &builder) override;

    std::shared_ptr<ShaderProgram> program_;
    rhi::TextureHandle lut_;        // LUT utilisée par les passes déclarées
    rhi::TextureHandle pendingLut_; // dernière LUT envoyée, active au prochain declare()
    std::uint32_t lutSize_ = 0;       // taille de la dernière LUT envoyée
    std::uint32_t activeLutSize_ = 0; // taille de lut_
    float strength_ = 1.0f;
};

// ── REN-18 : vignettage ─────────────────────────────────────────────────────────────────────────────────────────────
class VignetteFeature final : public PostEffect
{
  public:
    static constexpr const char *kName = "post.vignette";
    static core::Result<std::unique_ptr<VignetteFeature>> create(std::shared_ptr<PostContext> context);
    std::string name() const override { return kName; }
    bool needsHdr() const noexcept override { return false; }
    VignetteSettings &settings() noexcept { return settings_; }

  private:
    explicit VignetteFeature(std::shared_ptr<PostContext> context) : PostEffect(std::move(context)) {}
    core::Result<> declare(RenderGraphBuilder &builder) override;

    VignetteSettings settings_;
    std::shared_ptr<ShaderProgram> program_;
};

// ── Chaîne complète ─────────────────────────────────────────────────────────────────────────────────────────────────
// Effets enregistrés dans un SceneRenderer (pointeurs non possédés, valables tant que le renderer les garde). `autoExposure`
// est nul si le périphérique n'a pas de compute (Capabilities::compute) : les autres effets marchent sans lui.
struct PostEffects
{
    AutoExposureFeature *autoExposure = nullptr;
    BloomFeature *bloom = nullptr;
    TonemapFeature *tonemap = nullptr;
    ColorGradingFeature *colorGrading = nullptr;
    VignetteFeature *vignette = nullptr;

    // Vrai si un effet actif a besoin d'une scène HDR.
    bool hdrActive() const noexcept;
    // Scène en RGBA16F si un effet HDR est actif, RGBA8 sinon (à appeler après avoir changé les activations ; pris en compte au
    // prochain beginFrame).
    core::Result<> sync(SceneRenderer &renderer) const;
    void disableAll() noexcept;
};

// Crée les cinq effets (tous DÉSACTIVÉS) et les enregistre dans `renderer` dans l'ordre de la chaîne conseillée. Les plugins déjà
// enregistrés gardent leur place (une superposition qui écrit la sortie ne dépend pas de la scène).
core::Result<PostEffects> addPostEffects(SceneRenderer &renderer, rhi::Device &device, ShaderLibrary &library, PipelineCache &pipelines,
                                         const std::filesystem::path &shaderDirectory);

} // namespace orosmo::renderer
