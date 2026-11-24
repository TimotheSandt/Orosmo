// Backend Vulkan 1.3 du RHI (privé : seul endroit avec Vulkan, volk et VMA ; GLFW n'y sert qu'à créer la surface). Voir README.md.
//
// Ce fichier est le CONTRAT entre les fichiers du backend :
//   vk_device.cpp      création, destruction, capacités, séries de soumission, libération différée, aides synchrones
//   vk_resources.cpp   tampons, textures, échantillonneurs, requêtes ; mises à jour et lectures synchrones
//   vk_pipelines.cpp   shaders, pipelines, dispositions de descripteurs, groupes de liaison, cache de pipelines
//   vk_encoder.cpp     enregistrement des commandes, barrières déduites, passes (rendu dynamique)
//   vk_swapchain.cpp   surface, chaîne d'échange, image de fond, présentation
#pragma once

#include "orosmo/rhi/device.hpp"

#include <volk.h>

#if __has_include(<vma/vk_mem_alloc.h>)
#include <vma/vk_mem_alloc.h> // vcpkg sous Windows : include/vma/ (disposition du SDK Vulkan)
#else
#include <vk_mem_alloc.h>
#endif

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <optional>
#include <vector>

namespace orosmo::rhi::vk
{

inline constexpr std::uint32_t kMaxColorTargets = 8;
// Liaisons de tampons de sommets (comme le backend OpenGL).
inline constexpr std::uint32_t kMaxVertexBindings = 16;
// Images de CPU d'avance au plus : `beginFrame` attend la série de l'image N - kFramesInFlight.
inline constexpr std::uint32_t kFramesInFlight = 2;

// Série de soumission : valeur du sémaphore à chronologie du périphérique. Chaque `submit` la fait croître de 1.
using Serial = std::uint64_t;

// Anneau de transfert (README.md, « Transferts ») : blocs de cette taille, mappés en permanence, sous-alloués par les envois.
inline constexpr std::uint64_t kStagingBlockSize = 4ull * 1024 * 1024;
// Au-delà, un envoi prend un tampon de transfert dédié (créé puis libéré) au lieu de l'anneau.
inline constexpr std::uint64_t kStagingDedicatedThreshold = 1ull * 1024 * 1024;
// Blocs gardés quand ils sont libres (les autres, créés lors d'un pic, sont détruits à leur recyclage).
inline constexpr std::size_t kStagingRetainedBlocks = 4;
// Tampons de commandes tirés d'une même réserve avant d'en changer (en plus du changement à chaque `present`).
inline constexpr std::uint32_t kCommandBuffersPerPool = 32;

// Compteurs internes des transferts et soumissions (diagnostic et tests : tests/rhi/vulkan_transfer_tests.cpp). Cumulés depuis
// la création du périphérique.
struct TransferCounters
{
    std::uint64_t submits = 0;                 // vkQueueSubmit2 acceptés
    std::uint64_t dedicatedStaging = 0;        // tampons de transfert dédiés créés (gros envois, lectures)
    std::uint64_t stagingBlocks = 0;           // blocs de l'anneau de transfert créés
    std::uint64_t ringUploads = 0;             // envois servis par l'anneau
    std::uint64_t directWrites = 0;            // `updateBuffer` écrits directement dans un tampon kUpload (sans transfert)
    std::uint64_t orderedUploadWrites = 0;     // `updateBuffer` sur un tampon kUpload encore utilisé par le GPU : copie ordonnée
    std::uint64_t copyBarriers = 0;            // barrières placées entre deux copies d'un lot (plages qui se recouvrent)
    std::uint64_t commandPools = 0;            // réserves de tampons de commandes créées
    std::uint64_t commandPoolResets = 0;       // vkResetCommandPool
    std::uint64_t commandBuffersAllocated = 0; // vkAllocateCommandBuffers
    std::uint64_t directPresents = 0;          // images présentées en dessinant directement dans l'image de la chaîne (G185)
    std::uint64_t copiedPresents = 0;          // images présentées par une copie de l'image de fond hors écran
};

// ── Ressources ────────────────────────────────────────────────────────────────

struct BufferRes
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    BufferDesc desc;
    void *mapped = nullptr; // non nul pour kUpload et kReadback (mémoire visible par le CPU, mappée en permanence)
    // Dernière série qui lit ou écrit le tampon (soumission d'un encodeur qui l'utilise, lot qui le copie). `updateBuffer` n'écrit
    // directement un tampon kUpload que si cette série est finie ; sinon, copie ordonnée (résultat identique à l'ancien chemin).
    Serial gpuUse = 0;
};

// Vue d'image : plage de niveaux et de couches, et type. Les vues sont créées à la demande et gardées par la texture.
struct ImageViewKey
{
    std::uint32_t baseMip = 0;
    std::uint32_t mipCount = 1;
    std::uint32_t baseLayer = 0;
    std::uint32_t layerCount = 1;
    VkImageViewType type = VK_IMAGE_VIEW_TYPE_2D;
    // 0 : tous les aspects de la texture (cible de rendu). Un échantillonnage de profondeur demande VK_IMAGE_ASPECT_DEPTH_BIT seul.
    VkImageAspectFlags aspect = 0;
    friend bool operator==(const ImageViewKey &, const ImageViewKey &) = default;
};

struct TextureRes
{
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr; // nul : image qui n'appartient pas à la texture (image de la chaîne d'échange)
    TextureDesc desc;                   // niveaux de mips et couches RÉSOLUS (comme `Device::textureDesc`)
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    std::uint32_t levels = 1;
    std::uint32_t layers = 1; // couches réelles (6 pour un cube)
    // Disposition « de repos » (README.md) : celle où la texture se trouve entre deux commandes. Toute commande qui en exige une
    // autre (cible de rendu, copie, mips) y entre, puis REVIENT à la disposition de repos avant de finir.
    VkImageLayout restLayout = VK_IMAGE_LAYOUT_GENERAL;
    bool layoutInitialized = false; // faux : encore UNDEFINED ; le lot de transferts la fait passer en repos (prochaine soumission)
    std::vector<std::pair<ImageViewKey, VkImageView>> views;
};

struct SamplerRes
{
    VkSampler sampler = VK_NULL_HANDLE;
};

// Ce qu'un shader déclare, lu dans son SPIR-V (SPIRV-Cross) : sert à bâtir les dispositions de descripteurs et les pipelines.
struct ReflectedDescriptor
{
    std::uint32_t set = 0;
    std::uint32_t binding = 0;
    BindingType type = BindingType::kUniformBuffer;
    std::uint32_t count = 1;
};

struct ShaderInterface
{
    std::vector<ReflectedDescriptor> descriptors; // triés par (set, binding)
    std::uint32_t pushConstantSize = 0;
    std::array<std::uint32_t, 3> localSize{0, 0, 0}; // compute
    std::vector<std::uint32_t> inputLocations;       // sommets : emplacements des entrées (chacun doit avoir un attribut)
    // Interface entre étages : sorties du shader de sommets, entrées du shader de fragments. Un pipeline dont une entrée de
    // fragment n'a pas de sortie de même type est refusé (kParse), comme l'édition de liens d'OpenGL.
    struct StageVariable
    {
        std::uint32_t location = 0;
        std::uint32_t baseType = 0; // spirv_cross::SPIRType::BaseType
        std::uint32_t vecSize = 1;
        std::uint32_t columns = 1;
        std::uint32_t arraySize = 1;
        std::string name;
    };
    std::vector<StageVariable> stageOutputs; // sommets
    std::vector<StageVariable> stageInputs;  // fragments
};

struct ShaderRes
{
    VkShaderModule module = VK_NULL_HANDLE;
    ShaderStage stage = ShaderStage::kVertex;
    std::string entryPoint = "main";
    ShaderInterface interface;
};

// Une entrée d'une disposition de descripteurs : tout tampon est dynamique (voir vk_translate.hpp).
struct DescriptorBinding
{
    std::uint32_t binding = 0;
    BindingType type = BindingType::kUniformBuffer;
    std::uint32_t count = 1;
    VkShaderStageFlags stages = 0;
    friend bool operator==(const DescriptorBinding &, const DescriptorBinding &) = default;
};

// Disposition d'un groupe (`VkDescriptorSetLayout`). Dédupliquée par le périphérique : deux pipelines dont un groupe a les mêmes
// liaisons partagent la MÊME disposition (même `id`), donc les mêmes descripteurs de groupe de liaison.
struct SetLayout
{
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    std::vector<DescriptorBinding> bindings; // triées par `binding`
    std::uint32_t dynamicCount = 0;          // nombre de tampons (donc de décalages dynamiques) : un par entrée tampon, dans l'ordre des bindings
    std::uint64_t id = 0;
};

struct PipelineRes
{
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE; // dédupliqué, appartient au périphérique
    bool compute = false;
    // Disposition de chaque groupe ; nul : le pipeline n'utilise pas ce groupe. Appartient au périphérique.
    std::array<const SetLayout *, kGroupCount> sets{};
    std::uint32_t pushConstantSize = 0;
    VkShaderStageFlags pushStages = 0;
    // Pour valider la compatibilité avec la passe (rendu dynamique : les formats de la passe doivent être ceux du pipeline).
    std::vector<Format> colorFormats;
    Format depthFormat = Format::kUndefined;
    PrimitiveTopology topology = PrimitiveTopology::kTriangleList;
    std::array<std::uint32_t, 3> localSize{0, 0, 0}; // compute
    // Liaisons de tampons de sommets déclarées par le pipeline : chacune doit recevoir un tampon avant un dessin.
    std::array<bool, kMaxVertexBindings> vertexBindingUsed{};
};

struct BindGroupRes
{
    std::vector<BindGroupEntry> entries;
    // Jeu de descripteurs par disposition (`SetLayout::id`), créé et rempli à la demande par `descriptorSetFor`. Libérés (dans leur
    // réserve) avec le groupe, après les images en vol.
    struct CachedSet
    {
        std::uint64_t layoutId = 0;
        VkDescriptorSet set = VK_NULL_HANDLE;
        VkDescriptorPool pool = VK_NULL_HANDLE;
    };
    std::vector<CachedSet> sets;
    std::string debugName;
};

struct QueryPoolRes
{
    VkQueryPool pool = VK_NULL_HANDLE;
    std::uint32_t count = 0;
    std::string debugName;
    // Dernière écriture (G192) : la disponibilité lue sur l'hôte reste celle de la mesure précédente tant que le GPU n'a pas
    // exécuté la remise à zéro. Une écriture enregistrée mais pas soumise (`unsubmitted`), ou soumise dans une série pas encore
    // terminée (`lastWrite`), rend la requête non prête.
    Serial lastWrite = 0;
    bool unsubmitted = false;
};

struct Swapchain; // vk_swapchain.hpp
class VkEncoder;  // vk_encoder.hpp

class VulkanDevice final : public Device
{
  public:
    ~VulkanDevice() override;

    static Result<std::unique_ptr<VulkanDevice>> create(const DeviceDesc &desc);

    // ── Interface Device ──────────────────────────────────────────────────────
    const Capabilities &capabilities() const noexcept override { return caps_; }

    Result<BufferHandle> createBuffer(const BufferDesc &desc, std::span<const std::byte> initial) override;            // vk_resources.cpp
    Result<TextureHandle> createTexture(const TextureDesc &desc) override;                                             // vk_resources.cpp
    Result<SamplerHandle> createSampler(const SamplerDesc &desc) override;                                             // vk_resources.cpp
    Result<ShaderHandle> createShader(const ShaderDesc &desc) override;                                                // vk_pipelines.cpp
    Result<PipelineHandle> createGraphicsPipeline(const GraphicsPipelineDesc &desc) override;                          // vk_pipelines.cpp
    Result<PipelineHandle> createComputePipeline(const ComputePipelineDesc &desc) override;                            // vk_pipelines.cpp
    Result<BindGroupHandle> createBindGroup(const BindGroupDesc &desc) override;                                       // vk_pipelines.cpp
    Result<QueryPoolHandle> createQueryPool(std::uint32_t count, const char *debugName) override;                      // vk_resources.cpp

    void destroy(BufferHandle handle) override;
    void destroy(TextureHandle handle) override;
    void destroy(SamplerHandle handle) override;
    void destroy(ShaderHandle handle) override;
    void destroy(PipelineHandle handle) override;
    void destroy(BindGroupHandle handle) override;
    void destroy(QueryPoolHandle handle) override;

    Result<TextureDesc> textureDesc(TextureHandle texture) const override;
    Result<> updateBuffer(BufferHandle buffer, std::uint64_t offset, std::span<const std::byte> data) override;       // vk_resources.cpp
    Result<> updateTexture(TextureHandle texture, const TextureCopy &region, std::span<const std::byte> data) override; // vk_resources.cpp
    Result<std::vector<std::byte>> readBuffer(BufferHandle buffer, std::uint64_t offset, std::uint64_t size) override; // vk_resources.cpp
    Result<std::vector<std::byte>> readTexture(TextureHandle texture, const TextureCopy &region) override;            // vk_resources.cpp
    Result<bool> timestampsReady(QueryPoolHandle pool, std::uint32_t first, std::uint32_t count) override;            // vk_resources.cpp
    Result<std::vector<std::uint64_t>> readTimestamps(QueryPoolHandle pool, std::uint32_t first, std::uint32_t count) override; // vk_resources.cpp

    std::unique_ptr<CommandEncoder> createCommandEncoder() override;                                                   // vk_encoder.cpp
    Result<> submit(std::unique_ptr<CommandEncoder> encoder) override;                                                 // vk_encoder.cpp

    Result<TextureHandle> beginFrame() override;                                                                       // vk_swapchain.cpp
    Result<> present() override;                                                                                       // vk_swapchain.cpp
    Result<> resize(Extent2D size) override;                                                                           // vk_swapchain.cpp
    void setVSync(bool enabled) override;                                                                              // vk_swapchain.cpp
    void setDirectPresentation(bool enabled) override;                                                                 // vk_swapchain.cpp
    void waitIdle() override;
    // Attend le GPU et dit si l'inactivité est CONFIRMÉE (vkDeviceWaitIdle réussi, ou périphérique perdu : plus aucun travail ne
    // tourne). Sur une erreur récupérable (mémoire), rien n'est libéré : l'appelant ne doit rien détruire de ce que le GPU peut lire.
    Result<> waitIdleChecked();

    std::uint64_t validationErrorCount() const noexcept override { return validationErrors_.load(); }
    DeviceStats stats() const noexcept override;

    // ── Usage interne : recherche des ressources (handle périmé : nul) ───────────
    BufferRes *get(BufferHandle h) { return buffers_.get(h); }
    TextureRes *get(TextureHandle h) { return textures_.get(h); }
    SamplerRes *get(SamplerHandle h) { return samplers_.get(h); }
    ShaderRes *get(ShaderHandle h) { return shaders_.get(h); }
    PipelineRes *get(PipelineHandle h) { return pipelines_.get(h); }
    BindGroupRes *get(BindGroupHandle h) { return bindGroups_.get(h); }
    QueryPoolRes *get(QueryPoolHandle h) { return queryPools_.get(h); }

    // ── Usage interne : objets Vulkan du périphérique ─────────────────────────────
    VkInstance instance() const noexcept { return instance_; }
    VkPhysicalDevice physicalDevice() const noexcept { return physicalDevice_; }
    VkDevice device() const noexcept { return device_; }
    VkQueue queue() const noexcept { return queue_; }
    std::uint32_t queueFamily() const noexcept { return queueFamily_; }
    VmaAllocator allocator() const noexcept { return allocator_; }
    const VkPhysicalDeviceProperties &properties() const noexcept { return properties_; }
    bool hasDebugUtils() const noexcept { return debugUtils_; }
    // Format Vulkan d'un format du RHI (D24S8 remplacé par D32S8 si le pilote ne le prend pas en charge).
    VkFormat vkFormat(Format format) const noexcept;
    // Vrai quand `Capabilities` a accepté le format pour cet usage (vkGetPhysicalDeviceFormatProperties).
    bool formatSupports(Format format, TextureUsage usage) const noexcept;

    // Nom visible dans RenderDoc et les messages de validation (sans effet sans VK_EXT_debug_utils).
    void setObjectName(VkObjectType type, std::uint64_t handle, const std::string &name) const;

    // ── Séries de soumission et libération différée (vk_device.cpp) ─────────────────
    // Série de la dernière soumission faite ; série terminée par le GPU (lecture du sémaphore à chronologie).
    Serial lastSubmitted() const noexcept { return lastSubmitted_; }
    Serial completedSerial() const;
    Result<> waitSerial(Serial serial);
    // Exécute `release` quand le GPU a fini tout ce qui a été soumis jusqu'ici (série `lastSubmitted()` à cet instant).
    // Comme le backend OpenGL, la libération attend aussi kFramesInFlight images présentées (`present`) après la destruction ;
    // `waitIdle` (GPU arrêté) libère sans attendre d'image.
    // `waitFrames` faux : ressource interne (tampons de transfert), libérée dès que le GPU a fini.
    void queueRelease(std::function<void()> release, bool waitFrames = true);
    void collectReleases(bool ignoreFrames = false);

    // Tampon de commandes primaire prêt à enregistrer, tiré de la réserve courante (README.md, « Tampons de commandes »).
    // `retireCommandBuffer` : soumis avec la série `serial`. `releaseUnsubmitted` : jamais soumis (abandon, refus), quel que soit
    // son état. Une réserve est remise à zéro en bloc quand tous ses tampons sont rendus et que le GPU les a finis.
    Result<VkCommandBuffer> acquireCommandBuffer();
    void retireCommandBuffer(VkCommandBuffer commandBuffer, Serial serial);
    void releaseUnsubmitted(VkCommandBuffer commandBuffer);
    // Change de réserve courante (appelé à chaque image, et quand la réserve courante a donné kCommandBuffersPerPool tampons).
    void rotateCommandPool();

    // Soumet des tampons de commandes dans l'ordre, précédés du lot de transferts en attente DANS LA MÊME soumission (un seul
    // vkQueueSubmit2). Renvoie la série qui les marque. `waits` / `signals` : sémaphores binaires supplémentaires (présentation).
    struct SubmitSync
    {
        std::vector<std::pair<VkSemaphore, VkPipelineStageFlags2>> waits;
        std::vector<VkSemaphore> signals;
    };
    Result<Serial> submitCommandBuffers(std::span<const VkCommandBuffer> commandBuffers, const SubmitSync &sync = {});

    // Enregistre `record` dans un tampon à usage unique, le soumet (après le lot en attente, même soumission) et ATTEND qu'il soit
    // fini (chemin lent : lectures, copies bloquantes).
    // `accepted` (facultatif) dit si la soumission a été acceptée : après un échec d'attente, la copie peut encore être en vol et
    // les tampons qu'elle touche doivent attendre la série acceptée (`queueRelease`) au lieu d'être détruits.
    Result<> immediate(const std::function<void(VkCommandBuffer)> &record, bool *accepted = nullptr);

    // Dispositions de texture en attente : les textures neuves sont UNDEFINED ; le lot de transferts (joint en tête de toute
    // soumission) les fait passer en disposition de repos, ce qui garde correct un encodeur jamais soumis.
    void markPendingInitialization(TextureHandle texture);
    // Soumet seul le lot en attente (rien à faire sans lot) : quand aucune autre soumission ne part (present, waitIdle, lecture).
    Result<> flushPending();

    // ── Usage interne : descripteurs et vues (vk_pipelines.cpp, vk_resources.cpp) ─────
    // Disposition de groupe dédupliquée pour ces liaisons (créée au besoin).
    Result<const SetLayout *> setLayoutFor(const std::vector<DescriptorBinding> &bindings);
    // Disposition de pipeline dédupliquée : groupes (nul : inutilisé ; complété par la disposition vide) et push constants.
    Result<VkPipelineLayout> pipelineLayoutFor(const std::array<const SetLayout *, kGroupCount> &sets, std::uint32_t pushSize, VkShaderStageFlags pushStages);
    // Jeu de descripteurs de ce groupe de liaison pour cette disposition : créé et rempli au premier usage puis gardé. Erreur
    // `kInvalidArgument` (avec le numéro de liaison) si le groupe ne fournit pas une liaison de la disposition, ou d'un autre type,
    // ou avec une ressource périmée. Un groupe peut avoir des entrées en trop : elles sont ignorées pour cette disposition.
    Result<VkDescriptorSet> descriptorSetFor(BindGroupHandle group, const SetLayout &layout);
    // Vue d'une texture pour la plage demandée (créée au besoin, détruite avec la texture).
    Result<VkImageView> imageView(TextureHandle texture, const ImageViewKey &key);
    // Vue complète (tous niveaux et couches, type de la dimension) : celle de l'échantillonnage.
    Result<VkImageView> fullView(TextureHandle texture);

    // Tampon de transfert (visible par le CPU, mappé). `readback` : lu par le CPU après une copie du GPU.
    struct Staging
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = nullptr;
        void *mapped = nullptr;
        std::uint64_t size = 0;
    };
    Result<Staging> makeStaging(std::uint64_t size, bool readback = false);
    void destroyStaging(const Staging &staging) noexcept;

    // Place de transfert pour un envoi de `size` octets, à un décalage multiple de `alignment` : dans l'anneau (pas de création),
    // ou dans un tampon dédié au-delà de kStagingDedicatedThreshold (alors libéré avec le lot).
    struct UploadSpace
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        std::uint64_t offset = 0;
        std::byte *mapped = nullptr; // adresse de `offset`
        VmaAllocation allocation = nullptr;
        std::uint64_t allocationOffset = 0; // décalage de `offset` dans `allocation` (vmaFlushAllocation)
        bool dedicated = false;
        Staging staging; // si `dedicated`
    };
    // Copie `data` dans une place de transfert, la rend visible au GPU et ouvre le lot (qui prend le tampon dédié éventuel). Le
    // lot renvoyé reçoit ensuite la copie depuis `space`.
    Result<VkCommandBuffer> stageUpload(std::span<const std::byte> data, std::uint64_t alignment, UploadSpace &space);

    // Lot de commandes de transfert en attente (envois asynchrones ordonnés avec les soumissions : mises à jour de tampons et de
    // textures, initialisation des dispositions). Ouvert à la demande, joint à la prochaine soumission (avant ses commandes).
    // `staging` : tampons à libérer une fois le lot terminé.
    Result<VkCommandBuffer> batch(std::vector<Staging> staging = {});
    // Barrière groupée : à appeler avant une copie vers `[offset, offset + size)` de `buffer` dans le lot. Une barrière n'est
    // posée que si la plage recouvre une copie du lot depuis la dernière barrière (ordre des écritures, comme glBufferSubData).
    // Les transitions de texture ne comptent pas : une barrière d'image ne couvre pas la mémoire des tampons.
    void batchCopyBarrier(VkBuffer buffer, std::uint64_t offset, std::uint64_t size);

    const TransferCounters &transferCounters() const noexcept { return counters_; }

    // Usage de l'encodeur et des aides synchrones.
    Result<> fromVk(VkResult result, const char *what) const;

    // Cache de pipelines sur disque (RHI-06) : fichier lu à la création et réécrit à l'arrêt (vide : pas de dossier utilisable),
    // et nombre d'octets repris du fichier (0 : absent, ou d'un autre GPU ou pilote). Diagnostic et tests.
    const std::string &pipelineCachePath() const noexcept { return pipelineCachePath_; }
    std::size_t pipelineCacheLoadedBytes() const noexcept { return pipelineCacheLoadedBytes_; }

  private:
    VulkanDevice() = default;

    Result<> init(const DeviceDesc &desc);
    Result<> createInstance(const DeviceDesc &desc);
    Result<> pickPhysicalDevice(const DeviceDesc &desc);
    Result<> createLogicalDevice();
    Result<> createAllocator();
    void fillCapabilities();
    void shutdown() noexcept;
    Result<> submitRaw(std::span<const VkCommandBuffer> commandBuffers, const SubmitSync &sync);
    // vk_pipelines.cpp : cache de pipelines (chargé du disque à la création, enregistré à l'arrêt) et dispositions.
    Result<> initPipelines();
    void shutdownPipelines() noexcept;
    // Alloue un jeu de descripteurs dans une réserve qui a de la place (une réserve neuve est créée au besoin).
    Result<std::pair<VkDescriptorSet, VkDescriptorPool>> allocateDescriptorSet(VkDescriptorSetLayout layout);
    // vk_swapchain.cpp : surface, chaîne d'échange, image de fond.
    Result<> createSurface(const DeviceDesc &desc); // avant le choix du GPU (il doit pouvoir présenter)
    Result<> initSwapchain(const DeviceDesc &desc); // après la création du périphérique : image de fond, chaîne d'échange
    void shutdownSwapchain() noexcept;
    // Image de fond = image de la chaîne (G185, vk_swapchain.cpp) : reconstruction de la chaîne et de ses textures, acquisition
    // au début de l'image, retour de l'image hors écran dans l'emplacement de l'image de fond.
    Result<> rebuildSwapchain();
    Result<> acquireDirect();
    Result<> presentDirect();
    void releaseDirectImages() noexcept;
    void restoreOffscreenBackbuffer() noexcept;
    void drainPendingInit(VkCommandBuffer commandBuffer);
    void releaseReadback(const Staging &staging, bool immediateRelease);
    Result<> checkDepthCopy(const TextureRes &texture, const char *what) const;
    void abandonBatchInit(); // le lot n'est pas parti : ses textures neuves redeviennent UNDEFINED
    void destroyTextureNow(TextureRes &texture) noexcept;
    // Lot fermé (vkEndCommandBuffer fait), prêt à partir en tête d'une soumission.
    struct ClosedBatch
    {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        std::vector<Staging> staging;
    };
    Result<ClosedBatch> closeBatch();
    void batchSubmitted(ClosedBatch &closed);
    void batchRejected(ClosedBatch &closed);
    // Soumet le lot en attente (s'il y en a un) suivi de `commandBuffers`, en un seul vkQueueSubmit2. En cas d'échec, rien n'est
    // parti : le lot est abandonné ici, les tampons de `commandBuffers` restent à l'appelant.
    Result<> submitWithBatch(std::span<const VkCommandBuffer> commandBuffers, const SubmitSync &sync);
    // Anneau de transfert : sous-alloue `size` octets (bloc recyclé ou neuf).
    Result<UploadSpace> ringAllocate(std::uint64_t size, std::uint64_t alignment);
    void destroyStagingRing() noexcept;
    void destroyCommandPools() noexcept;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    std::uint32_t queueFamily_ = 0;
    VmaAllocator allocator_ = nullptr;
    VkPhysicalDeviceProperties properties_{};
    VkPhysicalDeviceFeatures features_{};
    bool debugUtils_ = false;
    bool validationEnabled_ = false;
    bool windowed_ = false;
    Capabilities caps_;
    VkFormat d24s8Replacement_ = VK_FORMAT_D24_UNORM_S8_UINT;

    VkSemaphore timeline_ = VK_NULL_HANDLE;
    Serial lastSubmitted_ = 0;
    // Réserves de tampons de commandes (README.md, « Tampons de commandes ») : une réserve courante donne ses tampons dans
    // l'ordre ; elle est remise à zéro EN BLOC (vkResetCommandPool) quand elle n'est plus courante, que tous ses tampons sont
    // rendus (`outstanding` nul) et que le GPU a fini la plus grande série de ses soumissions (`lastSerial`).
    struct CommandPoolSlot
    {
        VkCommandPool pool = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> buffers; // alloués (réutilisés dans l'ordre après une remise à zéro)
        std::uint32_t next = 0;               // prochain tampon à donner
        std::uint32_t outstanding = 0;        // donnés, ni soumis ni rendus
        Serial lastSerial = 0;
    };
    std::vector<CommandPoolSlot> commandPools_;
    std::size_t currentPool_ = 0;
    CommandPoolSlot *poolOf(VkCommandBuffer commandBuffer) noexcept;
    Result<std::size_t> createCommandPool();

    struct Pending
    {
        Serial serial;
        std::uint64_t frame; // image courante à la destruction
        bool waitFrames;
        std::function<void()> release;
    };
    std::vector<Pending> pending_;
    std::vector<Pending> readyScratch_; // réutilisé par `collectReleases` (pas d'allocation par image)
    std::vector<TextureHandle> pendingInitialization_;
    std::vector<TextureHandle> batchInitialized_; // textures initialisées par le lot ouvert (rétablies si le lot est abandonné)
    VkCommandBuffer batch_ = VK_NULL_HANDLE;
    std::vector<Staging> batchStaging_;
    std::uint64_t batchBytes_ = 0;
    // Destinations des copies du lot depuis la dernière barrière (barrières groupées : `batchCopyBarrier`).
    struct DirtyRange
    {
        VkBuffer buffer;
        std::uint64_t begin;
        std::uint64_t end;
    };
    std::vector<DirtyRange> batchDirty_;

    // Anneau de transfert : blocs mappés en permanence. Le bloc courant est rempli linéairement ; un bloc n'est réécrit depuis
    // son début que quand le GPU a fini la dernière série qui l'a lu (`serial`).
    struct StagingBlock
    {
        Staging staging;
        std::uint64_t used = 0;
        Serial serial = 0;
    };
    std::vector<StagingBlock> stagingBlocks_;
    std::size_t currentBlock_ = 0;

    // Tableaux de soumission réutilisés (submitRaw).
    std::vector<VkCommandBuffer> submitList_;
    std::vector<VkCommandBufferSubmitInfo> submitInfos_;
    std::vector<VkSemaphoreSubmitInfo> submitWaits_;
    std::vector<VkSemaphoreSubmitInfo> submitSignals_;

    TransferCounters counters_;

    std::vector<VkDescriptorPool> descriptorPools_;
    std::vector<std::unique_ptr<SetLayout>> setLayouts_;
    std::uint64_t nextSetLayoutId_ = 1;
    // vk_pipelines.cpp : dispositions de pipeline dédupliquées, cache de pipelines (mémoire et disque).
    std::vector<std::pair<std::vector<std::uint64_t>, VkPipelineLayout>> pipelineLayouts_;
    VkPipelineCache pipelineCache_ = VK_NULL_HANDLE;
    std::string pipelineCachePath_;
    std::size_t pipelineCacheLoadedBytes_ = 0;

    std::unique_ptr<Swapchain> swapchain_;
    // G185 : les images de la chaîne servent directement d'image de fond quand la surface accepte RGBA8 UNORM avec les usages de
    // l'image de fond et que le dessin direct est demandé (DeviceDesc::directPresentation, setDirectPresentation ; sinon, ou
    // ENGINE_VK_DIRECT_SWAPCHAIN=0 : copie de l'image hors écran à `present`). Entre beginFrame et present, l'emplacement de
    // `backbuffer_` contient l'image acquise ; l'image hors écran attend dans `directImages_`.
    bool directAllowed_ = true;
    bool directForcedOff_ = false; // ENGINE_VK_DIRECT_SWAPCHAIN=0 : l'emporte sur setDirectPresentation
    std::vector<TextureRes> directImages_;
    std::optional<std::uint32_t> acquiredImage_;
    VkSemaphore acquireWait_ = VK_NULL_HANDLE; // attendu par la prochaine soumission (premier usage de l'image acquise)
    TextureHandle backbuffer_;
    Extent2D backbufferSize_{1, 1};
    std::uint64_t frame_ = 0;
    std::array<Serial, kFramesInFlight> frameSerials_{};
    bool vsync_ = false;
    std::atomic<std::uint64_t> validationErrors_{0};

    HandlePool<BufferTag, BufferRes> buffers_;
    HandlePool<TextureTag, TextureRes> textures_;
    HandlePool<SamplerTag, SamplerRes> samplers_;
    HandlePool<ShaderTag, ShaderRes> shaders_;
    HandlePool<PipelineTag, PipelineRes> pipelines_;
    HandlePool<BindGroupTag, BindGroupRes> bindGroups_;
    HandlePool<QueryPoolTag, QueryPoolRes> queryPools_;

    friend class VkEncoder;
    friend struct Swapchain;
    friend VkBool32 VKAPI_PTR debugMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT, VkDebugUtilsMessageTypeFlagsEXT,
                                                     const VkDebugUtilsMessengerCallbackDataEXT *, void *);
};

Result<std::unique_ptr<Device>> createVulkanDevice(const DeviceDesc &desc);
// Attributs de fenêtre exigés par le backend (GLFW_CLIENT_API = GLFW_NO_API).
void applyWindowHints(const DeviceDesc &desc);
// Vrai si le chargeur Vulkan (volk) est présent sur cette machine (sans créer de périphérique).
bool loaderAvailable() noexcept;

} // namespace orosmo::rhi::vk
