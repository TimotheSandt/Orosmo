// REN-19 : anticrénelage. Chaque technique est un plugin du graphe de passes (`RenderFeature`, comme les post-traitements de
// `post_effects.hpp`, dont il réutilise `PostContext` et `PostEffect`), activable à l'exécution (`setEnabled`) :
//
//   - FXAA (`FxaaFeature`, « aa.fxaa ») : une passe, sur les valeurs affichées (après le tonemapping) ;
//   - SMAA 1x (`SmaaFeature`, « aa.smaa ») : détection des contours, poids de mélange, mélange du voisinage ; variante sans les
//     tables d'aire et de recherche précalculées (voir la classe) ;
//   - TAA (`TaaFeature`, « aa.taa ») : jitter de Halton du renderer, reprojection par les vecteurs de mouvement (AOV), écrêtage
//     du voisinage dans l'espace YCoCg, rejet de l'historique (profondeur, mouvement), accumulation en HDR.
//
// Ordre dans le graphe (docs/03 §11, testé par `tests/renderer/antialiasing_tests.cpp`) : scène (et ses plugins : transparents,
// 2D monde) → TAA (HDR, AVANT l'exposition automatique, le bloom et le tonemapping : il accumule des valeurs de scène) → post
// HDR → tonemapping → étalonnage → FXAA ou SMAA (sur les valeurs affichées) → mise à l'échelle → superpositions. L'ordre entre
// plugins est leur ordre d'enregistrement : `addTemporalAntiAliasing` AVANT `addPostEffects`, `addEdgeAntiAliasing` APRÈS.
// MSAA : absent (le RHI n'a ni nombre d'échantillons ni résolution de cibles, voir src/renderer/README.md).
#pragma once

#include "orosmo/core/error.hpp"
#include "orosmo/renderer/post_effects.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace orosmo::renderer
{

class SceneRenderer;

enum class AntiAliasingMode : std::uint8_t
{
    kNone,
    kFxaa,
    kSmaa,
    kTaa,
};

const char *toString(AntiAliasingMode mode) noexcept;
// « none », « fxaa », « smaa », « taa » (casse indifférente).
std::optional<AntiAliasingMode> parseAntiAliasingMode(std::string_view name) noexcept;

// ── FXAA (algorithme de T. Lottes, FXAA 3.11 « qualité », réécrit ici) ─────────────────────────────────────────────────
struct FxaaSettings
{
    float edgeThreshold = 0.125f;     // contraste local minimal (relatif à la luminance maximale du voisinage)
    float edgeThresholdMin = 0.0312f; // contraste minimal absolu (zones sombres)
    float subpixel = 0.75f;           // force du lissage sous-pixel (0 : aucun)
};

class FxaaFeature final : public PostEffect
{
  public:
    static constexpr const char *kName = "aa.fxaa";
    static core::Result<std::unique_ptr<FxaaFeature>> create(std::shared_ptr<PostContext> context);
    std::string name() const override { return kName; }
    bool needsHdr() const noexcept override { return false; }
    FxaaSettings &settings() noexcept { return settings_; }

  private:
    explicit FxaaFeature(std::shared_ptr<PostContext> context) : PostEffect(std::move(context)) {}
    core::Result<> declare(RenderGraphBuilder &builder) override;

    FxaaSettings settings_;
    std::shared_ptr<ShaderProgram> program_;
};

// ── SMAA 1x (Jimenez et al., 2012), variante analytique ─────────────────────────────────────────────────────────────
// Trois passes : `aa.smaa.edges` (contours de luminance, seuil + adaptation au contraste local, RG8 : r = contour à gauche du
// pixel, g = contour au-dessus), `aa.smaa.weights` (pour chaque contour : longueur de la ligne de part et d'autre, formes des
// extrémités, aire couverte par la ligne de reconstruction, RGBA8) et `aa.smaa.blend` (mélange du voisinage selon les poids).
// Différences avec la référence (d'où l'état « Partiel ») : la recherche le long des contours lit les texels un par un (pas de
// SearchTex ni d'astuce bilinéaire) et l'aire est calculée dans le shader par la même construction géométrique que celle qui
// produit AreaTex (formes L, Z, U ; hauteur ±1/2 aux extrémités, 0 au milieu de la ligne) : aucune table précalculée n'est
// embarquée, il n'y a donc ni provenance ni licence de données à suivre. Absents : détection des diagonales, arrondi des coins,
// seuil prédiqué par la profondeur, modes T2x/S2x/4x.
struct SmaaSettings
{
    float threshold = 0.1f;             // contraste de luminance minimal d'un contour
    float localContrastFactor = 2.0f;   // un contour plus faible que (contraste voisin maximal / facteur) est ignoré
    std::uint32_t maxSearchSteps = 16;  // texels cherchés de chaque côté d'un pixel le long d'un contour (1 à 64)
};

class SmaaFeature final : public PostEffect
{
  public:
    static constexpr const char *kName = "aa.smaa";
    static constexpr const char *kEdgesTexture = "aa.smaa.edges";
    static constexpr const char *kWeightsTexture = "aa.smaa.weights";
    static core::Result<std::unique_ptr<SmaaFeature>> create(std::shared_ptr<PostContext> context);
    std::string name() const override { return kName; }
    bool needsHdr() const noexcept override { return false; }
    SmaaSettings &settings() noexcept { return settings_; }

  private:
    explicit SmaaFeature(std::shared_ptr<PostContext> context) : PostEffect(std::move(context)) {}
    core::Result<> declare(RenderGraphBuilder &builder) override;

    SmaaSettings settings_;
    std::shared_ptr<ShaderProgram> edges_;
    std::shared_ptr<ShaderProgram> weights_;
    std::shared_ptr<ShaderProgram> blend_;
};

// ── TAA ─────────────────────────────────────────────────────────────────────────────────────────────────────────────
struct TaaSettings
{
    std::uint32_t maxAccumulatedFrames = 12; // poids minimal de l'image courante : 1 / maxAccumulatedFrames (2 à 64)
    float clipGamma = 1.0f;                  // écrêtage par la variance : moyenne ± gamma × écart type (∩ min/max du voisinage)
    bool clampHistory = true;                // faux : aucun écrêtage (debug et tests du rejet)
    float depthTolerance = 0.1f;             // écart relatif de profondeur linéaire toléré entre l'historique reprojeté et le pixel
    float motionTolerancePixels = 2.0f;      // écart de mouvement (pixels) au-delà duquel l'historique est rejeté progressivement
};

// Historique propre au plugin (deux couleurs RGBA16F et deux informations RGBA16F en double tampon, à la résolution de rendu) :
// celui de SceneRenderer (`needsHistory`, RGBA8, après les post-traitements) ne convient pas à une accumulation en HDR.
class TaaFeature final : public PostEffect
{
  public:
    static constexpr const char *kName = "aa.taa";
    static constexpr const char *kHistoryTexture = "aa.taa.history"; // couleur accumulée (RGBA16F), image précédente
    static constexpr const char *kInfoTexture = "aa.taa.info";       // RGBA32F : r profondeur linéaire, gb mouvement, a images accumulées
    static core::Result<std::unique_ptr<TaaFeature>> create(std::shared_ptr<PostContext> context);
    ~TaaFeature() override;

    std::string name() const override { return kName; }
    bool needsHdr() const noexcept override { return false; } // accepte une scène LDR ; son historique est RGBA16F de toute façon
    // Mouvement et profondeur linéaire, tant que le plugin est enregistré (même désactivé : les programmes de maillage restent
    // compilés pour les mêmes sorties).
    AovSet requiredAovs() const override { return {Aov::kMotion, Aov::kLinearDepth}; }
    bool wantsJitter() const noexcept override { return enabled(); }
    core::Result<> prepare(const FrameData &frame) override;

    TaaSettings &settings() noexcept { return settings_; }
    // La prochaine image ignore l'historique (coupure de scène, téléportation). Aussi fait automatiquement : première image,
    // changement de taille, image sautée (plugin désactivé entre-temps).
    void resetHistory() noexcept { resetRequested_ = true; }
    // Vrai si la dernière image exécutée a produit un historique.
    bool historyValid() const noexcept { return historyValid_; }
    // Lecture CPU (bloquante) de l'information d'historique de la dernière image : 4 flottants par pixel (profondeur, mouvement,
    // images accumulées), lignes de haut en bas.
    core::Result<std::vector<std::array<float, 4>>> readHistoryInfo() const;

  private:
    explicit TaaFeature(std::shared_ptr<PostContext> context) : PostEffect(std::move(context)) {}
    core::Result<> declare(RenderGraphBuilder &builder) override;
    core::Result<> ensureTargets(rhi::Extent2D size);
    void destroyTargets() noexcept;

    TaaSettings settings_;
    std::shared_ptr<ShaderProgram> program_;
    std::array<rhi::TextureHandle, 2> color_{};
    std::array<rhi::TextureHandle, 2> info_{};
    rhi::Extent2D size_{};
    std::uint32_t last_ = 0;        // tampon écrit par la dernière image exécutée (si historyValid_)
    bool historyValid_ = false;
    bool resetRequested_ = true;
    std::uint64_t prepared_ = 0;     // images préparées (prepare est appelé à chaque image, plugin activé ou non)
    std::uint64_t lastResolved_ = 0; // valeur de prepared_ à la dernière exécution (une image sautée : historique ignoré)
};

// ── Enregistrement ──────────────────────────────────────────────────────────────────────────────────────────────────
// Plugins enregistrés (pointeurs non possédés, valables tant que le renderer les garde ; nuls s'ils ne sont pas enregistrés).
struct AntiAliasing
{
    TaaFeature *taa = nullptr;
    FxaaFeature *fxaa = nullptr;
    SmaaFeature *smaa = nullptr;

    // Active la technique demandée, désactive les autres (kNone : toutes). kUnsupported si elle n'est pas enregistrée.
    core::Result<> select(AntiAliasingMode mode) noexcept;
    AntiAliasingMode mode() const noexcept;
};

// TAA (désactivé) : à enregistrer après les plugins de la scène et AVANT `addPostEffects` (il travaille sur la scène HDR).
core::Result<TaaFeature *> addTemporalAntiAliasing(SceneRenderer &renderer, std::shared_ptr<PostContext> context);
// FXAA et SMAA (désactivés) : à enregistrer APRÈS `addPostEffects` (valeurs affichées) et avant les superpositions.
core::Result<AntiAliasing> addEdgeAntiAliasing(SceneRenderer &renderer, std::shared_ptr<PostContext> context);

} // namespace orosmo::renderer
