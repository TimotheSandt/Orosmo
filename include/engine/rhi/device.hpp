// RHI-01 : Device et CommandEncoder (docs/03 §3). Style moderne, état explicite, handles opaques.
// Aucun type d'API graphique dans cet en-tête.
#pragma once

#include "engine/core/error.hpp"
#include "engine/rhi/capabilities.hpp"
#include "engine/rhi/handle.hpp"
#include "engine/rhi/types.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace engine::rhi
{

using engine::core::Error;
using engine::core::Result;

class CommandEncoder
{
  public:
    virtual ~CommandEncoder() = default;

    // Passes (RHI-08, RHI-15). Hors passe : copies, dispatch, minuteries. Dans une passe : état, liaisons, dessins.
    virtual void beginRenderPass(const RenderPassDesc &desc) = 0;
    virtual void endRenderPass() = 0;

    virtual void setPipeline(PipelineHandle pipeline) = 0;
    virtual void setBindGroup(std::uint32_t slot, BindGroupHandle group, std::span<const std::uint32_t> dynamicOffsets = {}) = 0;
    virtual void setVertexBuffer(std::uint32_t slot, BufferHandle buffer, std::uint64_t offset = 0) = 0;
    virtual void setIndexBuffer(BufferHandle buffer, IndexType type, std::uint64_t offset = 0) = 0;
    virtual void setPushConstants(std::span<const std::byte> data) = 0;
    virtual void setViewport(const Viewport &viewport) = 0;
    virtual void setScissor(const Rect &rect) = 0;

    virtual void draw(std::uint32_t vertexCount, std::uint32_t instanceCount = 1, std::uint32_t firstVertex = 0,
                      std::uint32_t firstInstance = 0) = 0;
    virtual void drawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount = 1, std::uint32_t firstIndex = 0,
                             std::int32_t vertexOffset = 0, std::uint32_t firstInstance = 0) = 0;
    // RHI-10 : multi-draw indirect. `drawCount` commandes consécutives, `stride` octets entre deux (0 : serrées).
    virtual void drawIndirect(BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride = 0) = 0;
    virtual void drawIndexedIndirect(BufferHandle args, std::uint64_t offset, std::uint32_t drawCount, std::uint32_t stride = 0) = 0;

    // RHI-09, RHI-17
    virtual void bindComputePipeline(PipelineHandle pipeline) = 0;
    virtual void dispatch(std::uint32_t x, std::uint32_t y, std::uint32_t z) = 0;
    virtual void dispatchIndirect(BufferHandle args, std::uint64_t offset) = 0;

    virtual void copyBuffer(BufferHandle src, BufferHandle dst, const BufferCopy &region) = 0;
    virtual void copyBufferToTexture(BufferHandle src, TextureHandle dst, const TextureCopy &region) = 0;
    virtual void copyTextureToBuffer(TextureHandle src, BufferHandle dst, const TextureCopy &region) = 0;
    virtual void generateMips(TextureHandle texture) = 0;

    // RHI-11
    virtual void beginDebugGroup(const char *name) = 0;
    virtual void endDebugGroup() = 0;
    virtual void writeTimestamp(QueryPoolHandle pool, std::uint32_t index) = 0;

    // Première erreur d'usage rencontrée pendant l'enregistrement (handle périmé, pipeline absent, hors passe...).
    // Vide si tout va bien. Les commandes invalides sont ignorées et journalisées ; `Device::submit` renvoie cette erreur.
    virtual const std::optional<Error> &error() const noexcept = 0;
};

struct SurfaceDesc
{
    // Descripteur opaque fourni par `platform` : la fenêtre (OpenGL : le GLFWwindow*), créée APRÈS `prepareWindowHints`. Le RHI
    // l'adopte sans en devenir propriétaire (il ne la détruit pas). Nul : périphérique hors écran (tests, PLAT-04).
    void *nativeWindow = nullptr;
    Extent2D size{1, 1}; // taille du backbuffer hors écran ; ignorée pour une fenêtre (taille du framebuffer de la fenêtre)
};

struct DeviceDesc
{
    Backend backend = Backend::kOpenGL;
    SurfaceDesc surface{};
    bool validation = true; // GL_KHR_debug / couches de validation Vulkan ; les messages d'erreur sont comptés
    bool vsync = false;
    std::string appName = "engine";
};

// Compteurs de ressources vivantes (diagnostic et tests de fuite). `pendingDestruction` : détruites mais pas encore libérées.
struct DeviceStats
{
    std::uint32_t buffers = 0;
    std::uint32_t textures = 0;
    std::uint32_t samplers = 0;
    std::uint32_t shaders = 0;
    std::uint32_t pipelines = 0;
    std::uint32_t bindGroups = 0;
    std::uint32_t queryPools = 0;
    std::uint32_t pendingDestruction = 0;
};

class Device
{
  public:
    virtual ~Device() = default;

    virtual const Capabilities &capabilities() const noexcept = 0;

    virtual Result<BufferHandle> createBuffer(const BufferDesc &desc, std::span<const std::byte> initial = {}) = 0;
    virtual Result<TextureHandle> createTexture(const TextureDesc &desc) = 0;
    virtual Result<SamplerHandle> createSampler(const SamplerDesc &desc) = 0;
    virtual Result<ShaderHandle> createShader(const ShaderDesc &desc) = 0;
    virtual Result<PipelineHandle> createGraphicsPipeline(const GraphicsPipelineDesc &desc) = 0;
    virtual Result<PipelineHandle> createComputePipeline(const ComputePipelineDesc &desc) = 0;
    virtual Result<BindGroupHandle> createBindGroup(const BindGroupDesc &desc) = 0;
    virtual Result<QueryPoolHandle> createQueryPool(std::uint32_t count, const char *debugName = "") = 0;

    // Destruction différée (docs/03 §3 règle 2) : le handle devient périmé tout de suite, la ressource GPU est libérée
    // une fois les images en vol terminées. Détruire un handle nul ou déjà périmé est sans effet.
    virtual void destroy(BufferHandle handle) = 0;
    virtual void destroy(TextureHandle handle) = 0;
    virtual void destroy(SamplerHandle handle) = 0;
    virtual void destroy(ShaderHandle handle) = 0;
    virtual void destroy(PipelineHandle handle) = 0;
    virtual void destroy(BindGroupHandle handle) = 0;
    virtual void destroy(QueryPoolHandle handle) = 0;

    // Description réelle d'une texture vivante (niveaux de mips résolus, couches, taille courante du backbuffer).
    virtual Result<TextureDesc> textureDesc(TextureHandle texture) const = 0;

    // Mises à jour et lectures synchrones (chemin lent : chargement, captures, tests). Les copies asynchrones passent par
    // CommandEncoder::copy*.
    virtual Result<> updateBuffer(BufferHandle buffer, std::uint64_t offset, std::span<const std::byte> data) = 0;
    virtual Result<> updateTexture(TextureHandle texture, const TextureCopy &region, std::span<const std::byte> data) = 0;
    virtual Result<std::vector<std::byte>> readBuffer(BufferHandle buffer, std::uint64_t offset, std::uint64_t size) = 0;
    // Texels de la région, lignes serrées, sans remplissage. Repère : première ligne = bas de l'image (OpenGL).
    virtual Result<std::vector<std::byte>> readTexture(TextureHandle texture, const TextureCopy &region = {}) = 0;
    // Horodatages en nanosecondes (valeurs absolues du compteur GPU ; la différence de deux valeurs est une durée).
    // Vrai quand les `count` horodatages sont disponibles sans attente (readTimestamps ne bloquera pas). Faux tant qu'un n'a pas
    // été écrit puis terminé par le GPU.
    virtual Result<bool> timestampsReady(QueryPoolHandle pool, std::uint32_t first, std::uint32_t count) = 0;
    virtual Result<std::vector<std::uint64_t>> readTimestamps(QueryPoolHandle pool, std::uint32_t first, std::uint32_t count) = 0;

    virtual std::unique_ptr<CommandEncoder> createCommandEncoder() = 0;
    virtual Result<> submit(std::unique_ptr<CommandEncoder> encoder) = 0;

    // Image courante. Hors écran : une texture de la taille de `SurfaceDesc::size`, format RGBA8.
    // Fenêtre : le backbuffer prend la taille courante du framebuffer de la fenêtre (HiDPI compris) ; une fenêtre réduite
    // (framebuffer de taille nulle) renvoie kBusy : l'appelant saute l'image.
    virtual Result<TextureHandle> beginFrame() = 0;
    // Termine l'image : présentation (fenêtre) et libération des ressources détruites. Hors écran : seulement la libération.
    virtual Result<> present() = 0;
    // Hors écran : réalloue le backbuffer à `size`. Fenêtre : relit la taille du framebuffer de la fenêtre (`size` est ignoré :
    // c'est la couche fenêtre qui redimensionne).
    virtual Result<> resize(Extent2D size) = 0;
    // Synchronisation verticale (RHI-08) ; sans effet hors écran.
    virtual void setVSync(bool enabled) = 0;
    virtual void waitIdle() = 0;

    // Nombre de messages d'erreur ou d'avertissement reçus de la couche de validation depuis la création (GL_KHR_debug,
    // couches Vulkan). Les tests exigent 0 (définition de « terminé », CLAUDE.md).
    virtual std::uint64_t validationErrorCount() const noexcept = 0;
    virtual DeviceStats stats() const noexcept = 0;
};

// Crée le périphérique du backend demandé. Un backend non compilé ou indisponible sur la machine renvoie kUnsupported
// avec la raison (RHI-04).
Result<std::unique_ptr<Device>> createDevice(const DeviceDesc &desc);

// À appeler par la couche fenêtre AVANT de créer la fenêtre que `SurfaceDesc::nativeWindow` recevra : pose les attributs de
// contexte que le backend exige (OpenGL : 4.3 core, contexte de debug si `desc.validation`, backbuffer RGBA8 linéaire (préférence ; le backend coupe la conversion sRGB à l'écriture dans la fenêtre), double tampon).
// N'efface pas les attributs déjà posés (taille, bordure, visibilité restent ceux de l'appelant).
Result<> prepareWindowHints(const DeviceDesc &desc);

// Backends compilés dans cette bibliothèque.
bool isBackendCompiled(Backend backend) noexcept;

} // namespace engine::rhi
