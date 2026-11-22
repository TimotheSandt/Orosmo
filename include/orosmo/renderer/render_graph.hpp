// REN-01 : graphe de passes déclaratif sur le RHI. Chaque image, on déclare des ressources (textures et tampons importés ou
// transitoires, nommés) et des passes qui les lisent ou les écrivent ; le graphe déduit l'ordre, écarte les passes inutiles,
// alloue les ressources transitoires (réutilisées d'une image à l'autre, partagées entre ressources de vies disjointes) et exécute
// les passes sur un encodeur, chacune mesurée par le `Profiler` (minuterie GPU quand un fournisseur est installé). Les barrières
// restent au RHI (docs/03 §3 règle 1) : le graphe ne fait qu'ordonner.
//
// Règles de dépendance (par ressource ; l'ordre de déclaration ne compte qu'entre les accès à une même ressource) :
//   - les écrivains d'une même ressource s'exécutent dans leur ordre de déclaration (une passe qui lit et écrit est un écrivain) ;
//     un écrivain ne DÉPEND du précédent (qui est alors gardé avec lui) que s'il consomme l'ancien contenu : attachement en `kLoad`,
//     `write` générique ou lecture dans la même passe ; un attachement en `kClear` / `kDontCare` ne fait qu'ordonner ;
//   - `read` voit la version laissée par le dernier écrivain déclaré AVANT la passe, et l'écrivain suivant attend la passe
//     (écriture après lecture) ; sans écrivain déclaré avant elle, la passe voit le contenu FINAL (elle suit tous les écrivains) ;
//   - `readFinal` voit toujours le contenu final, même si des écrivains sont déclarés après (ex. la mise à l'échelle du renderer,
//     déclarée avant les post-traitements des plugins) ;
//   - une ressource importée sans écrivain est lue telle qu'elle est (contenu d'avant l'image) ; une ressource TRANSITOIRE lue sans
//     producteur (aucun écrivain, ou premier attachement en `kLoad`) par une passe gardée est refusée par `compile` (kInvalidArgument).
// Une passe est gardée si elle a un effet de bord (`sideEffect`), si elle écrit une ressource importée (visible hors du graphe), ou
// si une passe gardée en consomme le résultat. Les autres sont écartées. Un cycle est refusé par `compile` (kInvalidArgument).
#pragma once

#include "orosmo/core/error.hpp"
#include "orosmo/rhi/device.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace orosmo::renderer
{

struct FrameData;

// Handles locaux au graphe (valables jusqu'au prochain `reset`).
struct RgTexture
{
    static constexpr std::uint32_t kInvalid = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t index = kInvalid;
    constexpr bool valid() const noexcept { return index != kInvalid; }
    friend constexpr bool operator==(const RgTexture &, const RgTexture &) noexcept = default;
};

struct RgBuffer
{
    static constexpr std::uint32_t kInvalid = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t index = kInvalid;
    constexpr bool valid() const noexcept { return index != kInvalid; }
    friend constexpr bool operator==(const RgBuffer &, const RgBuffer &) noexcept = default;
};

// Description d'une texture 2D du graphe. Pour une texture transitoire, les usages déduits des accès (attachement : RenderTarget,
// lecture : Sampled) s'ajoutent à `usage` ; une écriture générique (stockage, copie) doit être couverte par `usage` lui-même.
struct RgTextureDesc
{
    rhi::Format format = rhi::Format::kRGBA8Unorm;
    rhi::Extent2D size{1, 1};
    std::uint32_t mipLevels = 1;
    rhi::TextureUsage usage = rhi::TextureUsage::kRenderTarget | rhi::TextureUsage::kSampled;
    friend bool operator==(const RgTextureDesc &, const RgTextureDesc &) noexcept = default;
};

struct RgBufferDesc
{
    std::uint64_t size = 0;
    rhi::BufferUsage usage = rhi::BufferUsage::kStorage;
    friend bool operator==(const RgBufferDesc &, const RgBufferDesc &) noexcept = default;
};

enum class PassType : std::uint8_t
{
    kRaster,  // le graphe ouvre une passe de rendu sur les attachements déclarés (writeColor / writeDepth) et la referme
    kGeneric, // aucune passe ouverte : la passe enregistre ce qu'elle veut (ses propres passes de rendu, compute, copies)
};

// Ressources par image communes aux passes (REN-03), fournies par le propriétaire du graphe (SceneRenderer).
struct FrameResources
{
    rhi::BindGroupHandle group;  // groupe 0 : caméra (0), lumières (1), FrameData (2)
    rhi::BufferHandle buffer;    // tampon uniforme de FrameData
    const FrameData *data = nullptr;
    // Groupe 2 des tranches de modèle de l'image (SceneRenderer::recordModelSlice) : un bloc ModelBlock par dessin, choisi par
    // décalage dynamique à `setBindGroup(2, modelGroup, {décalage})`. Nul tant qu'aucune tranche n'a été enregistrée.
    rhi::BindGroupHandle modelGroup;
};

class RenderGraph;

// Ce que reçoit une passe à l'exécution.
class RenderPassContext
{
  public:
    rhi::Device &device() const noexcept { return device_; }
    rhi::CommandEncoder &encoder() const noexcept { return encoder_; }
    const std::string &passName() const noexcept { return passName_; }
    // Ressource GPU d'un handle du graphe (nulle pour un handle invalide).
    rhi::TextureHandle texture(RgTexture texture) const;
    rhi::BufferHandle buffer(RgBuffer buffer) const;
    const RgTextureDesc &desc(RgTexture texture) const;
    // Groupe 0 et données par image (vides si le propriétaire du graphe n'en fournit pas). Lu au moment de l'appel : le groupe
    // peut avoir été reconstruit par une passe précédente.
    const FrameResources *frame() const noexcept { return frame_; }

  private:
    friend class RenderGraph;
    RenderPassContext(rhi::Device &device, rhi::CommandEncoder &encoder, const RenderGraph &graph, const std::string &passName,
                      const FrameResources *frame)
        : device_(device), encoder_(encoder), graph_(graph), passName_(passName), frame_(frame)
    {
    }
    rhi::Device &device_;
    rhi::CommandEncoder &encoder_;
    const RenderGraph &graph_;
    const std::string &passName_;
    const FrameResources *frame_;
};

using PassExecute = std::function<core::Result<>(RenderPassContext &)>;

class RenderGraph
{
  public:
    // Déclaration d'une passe : chaînable, valable jusqu'au prochain `reset` (on peut y ajouter des accès plus tard).
    class PassBuilder
    {
      public:
        PassBuilder &read(RgTexture texture);  // version courante (voir les règles en tête du fichier)
        PassBuilder &read(RgBuffer buffer);
        PassBuilder &readFinal(RgTexture texture); // contenu final, après tous les écrivains de l'image
        PassBuilder &readFinal(RgBuffer buffer);
        PassBuilder &write(RgTexture texture); // écriture hors attachement (passe générique : sa propre passe, stockage, copie)
        PassBuilder &write(RgBuffer buffer);
        // Attachements d'une passe kRaster (même taille pour tous). `kLoad` lit l'ancien contenu (la passe suit l'écrivain précédent).
        PassBuilder &writeColor(RgTexture texture, rhi::LoadOp load = rhi::LoadOp::kClear, std::array<float, 4> clearColor = {0.0f, 0.0f, 0.0f, 1.0f});
        PassBuilder &writeDepth(RgTexture texture, rhi::LoadOp load = rhi::LoadOp::kClear, float clearDepth = 0.0f);
        PassBuilder &sideEffect(); // jamais écartée (sortie hors du graphe : lecture CPU, image générée...)
        std::uint32_t index() const noexcept { return pass_; }

      private:
        friend class RenderGraph;
        PassBuilder(RenderGraph &graph, std::uint32_t pass) : graph_(&graph), pass_(pass) {}
        RenderGraph *graph_;
        std::uint32_t pass_;
    };

    explicit RenderGraph(rhi::Device &device) : device_(device) {}
    ~RenderGraph();
    RenderGraph(const RenderGraph &) = delete;
    RenderGraph &operator=(const RenderGraph &) = delete;

    // ── Déclaration (chaque image) ─────────────────────────────────────────────────────────────────────────────────
    // Oublie les passes et les ressources déclarées ; garde la réserve de ressources transitoires.
    void reset();
    // Ressource extérieure au graphe : elle n'est ni créée ni détruite par lui ; l'écrire rend la passe visible (jamais écartée).
    RgTexture importTexture(std::string name, rhi::TextureHandle texture, const RgTextureDesc &desc);
    RgBuffer importBuffer(std::string name, rhi::BufferHandle buffer, const RgBufferDesc &desc);
    // Ressource transitoire : allouée à l'exécution si une passe gardée l'utilise, rendue à la réserve après sa dernière utilisation.
    RgTexture createTexture(std::string name, const RgTextureDesc &desc);
    RgBuffer createBuffer(std::string name, const RgBufferDesc &desc);
    RgTexture findTexture(std::string_view name) const noexcept;
    RgBuffer findBuffer(std::string_view name) const noexcept;
    PassBuilder addPass(std::string name, PassType type, PassExecute execute);
    // Passe déjà déclarée (pour lui ajouter des accès), par nom. nullopt si inconnue.
    std::optional<PassBuilder> pass(std::string_view name);

    // Groupe 0 et données par image transmis aux passes (pointeur non possédé ; nul : aucune).
    void setFrameResources(const FrameResources *frame) noexcept { frame_ = frame; }
    // Préfixe des noms du Profiler : une mesure par passe exécutée, « <préfixe><nom de la passe> ».
    void setProfilePrefix(std::string prefix) { profilePrefix_ = std::move(prefix); }
    const std::string &profilePrefix() const noexcept { return profilePrefix_; }

    // ── Compilation et exécution ───────────────────────────────────────────────────────────────────────────────────
    // Vérifie les déclarations (noms en double, handles invalides, attachements de tailles différentes, passe raster sans
    // attachement), calcule l'ordre et écarte les passes inutiles. Un cycle est refusé avec le chemin des passes ; une lecture
    // gardée d'une transitoire sans producteur aussi.
    core::Result<> compile();
    // Compile si besoin, alloue les transitoires, exécute les passes gardées dans l'ordre. Une passe en erreur arrête l'image
    // (la passe de rendu ouverte par le graphe est refermée) et l'erreur est rendue, préfixée du nom de la passe : erreur rendue
    // par la passe, sinon première commande refusée par l'encodeur pendant la passe (`CommandEncoder::error`, apparue pendant elle).
    core::Result<> execute(rhi::CommandEncoder &encoder);

    // ── Introspection (après compile) ──────────────────────────────────────────────────────────────────────────────
    std::vector<std::string> executionOrder() const;
    std::vector<std::string> culledPasses() const;
    std::size_t passCount() const noexcept { return passes_.size(); }
    // Ressource GPU d'une transitoire à la dernière exécution (nulle si elle a été écartée).
    rhi::TextureHandle physicalTexture(RgTexture texture) const;
    rhi::BufferHandle physicalBuffer(RgBuffer buffer) const;

    // Réserve des transitoires : nombre de textures et tampons créés depuis la construction (une réutilisation n'en crée pas),
    // et nombre vivant dans la réserve. Une entrée inutilisée pendant `kTransientKeepFrames` exécutions est détruite.
    static constexpr std::uint32_t kTransientKeepFrames = 8;
    std::size_t transientTexturesCreated() const noexcept { return texturesCreated_; }
    std::size_t transientBuffersCreated() const noexcept { return buffersCreated_; }
    std::size_t pooledTextureCount() const noexcept { return texturePool_.size(); }
    std::size_t pooledBufferCount() const noexcept { return bufferPool_.size(); }
    void releaseTransients(); // détruit toute la réserve (destruction différée par le RHI)

  private:
    friend class RenderPassContext;

    enum class Access : std::uint8_t
    {
        kRead,
        kReadFinal,
        kWrite,
        kColor,
        kDepth,
    };
    struct ResourceUse
    {
        bool texture = true;
        std::uint32_t resource = 0;
        Access access = Access::kRead;
        rhi::LoadOp load = rhi::LoadOp::kClear;
        std::array<float, 4> clearColor{};
        float clearDepth = 0.0f;
    };
    struct Pass
    {
        std::string name;
        PassType type = PassType::kGeneric;
        PassExecute execute;
        std::vector<ResourceUse> uses;
        bool sideEffect = false;
    };
    struct TextureResource
    {
        std::string name;
        RgTextureDesc desc;
        bool imported = false;
        rhi::TextureHandle handle; // importée : fournie ; transitoire : attribuée à l'exécution
    };
    struct BufferResource
    {
        std::string name;
        RgBufferDesc desc;
        bool imported = false;
        rhi::BufferHandle handle;
    };
    struct PooledTexture
    {
        RgTextureDesc desc;
        rhi::TextureHandle handle;
        std::uint64_t lastFrame = 0;
        std::int64_t busyUntil = -1; // position d'exécution de la dernière utilisation dans l'image courante
    };
    struct PooledBuffer
    {
        RgBufferDesc desc;
        rhi::BufferHandle handle;
        std::uint64_t lastFrame = 0;
        std::int64_t busyUntil = -1;
    };

    void addUse(std::uint32_t pass, const ResourceUse &use);
    core::Result<> validate() const;
    core::Result<rhi::TextureHandle> acquireTexture(const RgTextureDesc &desc, std::int64_t until);
    core::Result<rhi::BufferHandle> acquireBuffer(const RgBufferDesc &desc, std::int64_t until);
    core::Result<> allocateTransients();
    core::Result<> runPass(rhi::CommandEncoder &encoder, const Pass &pass);
    void trimPool();

    rhi::Device &device_;
    std::vector<Pass> passes_;
    std::vector<TextureResource> textures_;
    std::vector<BufferResource> buffers_;
    const FrameResources *frame_ = nullptr;
    std::string profilePrefix_ = "RenderGraph/";

    bool compiled_ = false;
    std::vector<std::uint32_t> order_; // passes gardées, dans l'ordre d'exécution
    std::vector<bool> kept_;

    std::vector<PooledTexture> texturePool_;
    std::vector<PooledBuffer> bufferPool_;
    std::size_t texturesCreated_ = 0;
    std::size_t buffersCreated_ = 0;
    std::uint64_t frameCounter_ = 0;
};

} // namespace orosmo::renderer
