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

#include <vk_mem_alloc.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace orosmo::rhi::vk
{

inline constexpr std::uint32_t kMaxColorTargets = 8;
// Images de CPU d'avance au plus : `beginFrame` attend la série de l'image N - kFramesInFlight.
inline constexpr std::uint32_t kFramesInFlight = 2;

// Série de soumission : valeur du sémaphore à chronologie du périphérique. Chaque `submit` la fait croître de 1.
using Serial = std::uint64_t;

// ── Ressources ────────────────────────────────────────────────────────────────

struct BufferRes
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    BufferDesc desc;
    void *mapped = nullptr; // non nul pour kUpload et kReadback (mémoire visible par le CPU, mappée en permanence)
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
    bool layoutInitialized = false; // faux : encore UNDEFINED ; `flushPending` la fait passer en repos avant la prochaine soumission
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
    void waitIdle() override;

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
    void queueRelease(std::function<void()> release);
    void collectReleases();

    // Tampon de commandes primaire prêt à enregistrer (recyclé quand la série de sa dernière soumission est terminée).
    // `retireCommandBuffer` le rend au recyclage une fois soumis avec la série `serial`.
    Result<VkCommandBuffer> acquireCommandBuffer();
    void retireCommandBuffer(VkCommandBuffer commandBuffer, Serial serial);

    // Soumet des tampons de commandes dans l'ordre, après `flushPending`. Renvoie la série qui les marque. `waits` / `signals` :
    // sémaphores binaires supplémentaires (présentation).
    struct SubmitSync
    {
        std::vector<std::pair<VkSemaphore, VkPipelineStageFlags2>> waits;
        std::vector<VkSemaphore> signals;
    };
    Result<Serial> submitCommandBuffers(std::span<const VkCommandBuffer> commandBuffers, const SubmitSync &sync = {});

    // Enregistre `record` dans un tampon à usage unique, le soumet et ATTEND qu'il soit fini (chemin lent : envois, lectures,
    // création de tampons avec données initiales). Passe d'abord par `flushPending`.
    Result<> immediate(const std::function<void(VkCommandBuffer)> &record);

    // Dispositions de texture en attente : les textures neuves sont UNDEFINED ; `flushPending` (appelé avant toute soumission et
    // tout `immediate`) les fait passer en disposition de repos, ce qui garde correct un encodeur jamais soumis.
    void markPendingInitialization(TextureHandle texture);
    Result<> flushPending();

    // ── Usage interne : descripteurs et vues (vk_pipelines.cpp, vk_resources.cpp) ─────
    // Disposition de groupe dédupliquée pour ces liaisons (créée au besoin).
    Result<const SetLayout *> setLayoutFor(const std::vector<DescriptorBinding> &bindings);
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

    // Lot de commandes de transfert en attente (envois asynchrones ordonnés avec les soumissions : mises à jour de tampons et de
    // textures, initialisation des dispositions). Ouvert à la demande, soumis par `flushPending` avant toute autre soumission.
    // `staging` : tampons à libérer une fois le lot terminé.
    Result<VkCommandBuffer> batch(std::vector<Staging> staging = {});

    // Usage de l'encodeur et des aides synchrones.
    Result<> fromVk(VkResult result, const char *what) const;

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
    // vk_swapchain.cpp : surface, chaîne d'échange, image de fond.
    Result<> createSurface(const DeviceDesc &desc); // avant le choix du GPU (il doit pouvoir présenter)
    Result<> initSwapchain(const DeviceDesc &desc); // après la création du périphérique : image de fond, chaîne d'échange
    void shutdownSwapchain() noexcept;
    void drainPendingInit(VkCommandBuffer commandBuffer);
    void destroyTextureNow(TextureRes &texture) noexcept;

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
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    struct RetiredCommandBuffer
    {
        VkCommandBuffer commandBuffer;
        Serial serial;
    };
    std::vector<VkCommandBuffer> freeCommandBuffers_;
    std::vector<RetiredCommandBuffer> retiredCommandBuffers_;

    struct Pending
    {
        Serial serial;
        std::function<void()> release;
    };
    std::vector<Pending> pending_;
    std::vector<TextureHandle> pendingInitialization_;
    VkCommandBuffer batch_ = VK_NULL_HANDLE;
    std::vector<Staging> batchStaging_;
    std::uint64_t batchBytes_ = 0;

    std::vector<VkDescriptorPool> descriptorPools_;
    std::vector<std::unique_ptr<SetLayout>> setLayouts_;
    std::uint64_t nextSetLayoutId_ = 1;
    // vk_pipelines.cpp : dispositions de pipeline dédupliquées, cache de pipelines (mémoire et disque).
    std::vector<std::pair<std::vector<std::uint64_t>, VkPipelineLayout>> pipelineLayouts_;
    VkPipelineCache pipelineCache_ = VK_NULL_HANDLE;
    std::string pipelineCachePath_;

    std::unique_ptr<Swapchain> swapchain_;
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
