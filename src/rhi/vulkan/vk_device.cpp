#include "vk_device.hpp"

#include "vk_swapchain.hpp"
#include "vk_sync.hpp"
#include "vk_translate.hpp"

#include <orosmo/core/log.hpp>

#include <vulkan/vulkan_beta.h> // VkPhysicalDevicePortabilitySubsetFeaturesKHR (VK_KHR_portability_subset, MoltenVK)

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <set>
#include <system_error>
#include <string_view>

namespace orosmo::rhi::vk
{

using orosmo::core::ErrorCode;
using orosmo::core::makeError;

namespace
{

constexpr const char *kValidationLayer = "VK_LAYER_KHRONOS_validation";

bool hasInstanceLayer(const char *name)
{
    std::uint32_t count = 0;
    if (vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS)
        return false;
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    return std::any_of(layers.begin(), layers.end(), [&](const VkLayerProperties &l) { return std::string_view(l.layerName) == name; });
}

std::vector<VkExtensionProperties> instanceExtensions(const char *layer)
{
    std::uint32_t count = 0;
    if (vkEnumerateInstanceExtensionProperties(layer, &count, nullptr) != VK_SUCCESS)
        return {};
    std::vector<VkExtensionProperties> list(count);
    vkEnumerateInstanceExtensionProperties(layer, &count, list.data());
    return list;
}

bool hasExtension(const std::vector<VkExtensionProperties> &list, const char *name)
{
    return std::any_of(list.begin(), list.end(), [&](const VkExtensionProperties &e) { return std::string_view(e.extensionName) == name; });
}

const char *resultName(VkResult result) noexcept
{
    switch (result)
    {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
        case VK_ERROR_OUT_OF_POOL_MEMORY: return "VK_ERROR_OUT_OF_POOL_MEMORY";
        case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
        case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
        default: return "VkResult (other)";
    }
}

bool envFlag(const char *name, bool fallback)
{
    const char *value = std::getenv(name);
    if (value == nullptr || *value == '\0')
        return fallback;
    return !(value[0] == '0' || value[0] == 'n' || value[0] == 'N' || value[0] == 'f' || value[0] == 'F');
}

bool volkReady()
{
    static const bool ready = volkInitialize() == VK_SUCCESS;
    return ready;
}

} // namespace

// Compte les messages d'erreur et d'avertissement de la couche de validation (tests : zéro attendu, CLAUDE.md).
VkBool32 VKAPI_PTR debugMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type,
                                          const VkDebugUtilsMessengerCallbackDataEXT *data, void *user)
{
    auto *device = static_cast<VulkanDevice *>(user);
    const bool error = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0;
    const bool warning = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0;
    if (error || warning)
    {
        (void)type;
        const char *text = (data != nullptr && data->pMessage != nullptr) ? data->pMessage : "";
        // MoltenVK 1.4.2 (Homebrew, M9-L1) signale « Blending is enabled » pour TOUTE cible de rendu d'un format non mélangeable
        // (R32_UINT des AOV, de l'identifiant d'objet), blendEnable vrai ou non : corrigé en amont (condition `&& pCA->blendEnable`).
        // Ce n'est pas une erreur du moteur : createGraphicsPipeline refuse déjà le mélange sur un format sans
        // VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT. Journalisé, non compté (docs/GAPS.md G504).
        if (std::string_view(text).starts_with("VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format"))
        {
            LOG_INFO("Vulkan validation (MoltenVK false positive, ignored): ", text);
            return VK_FALSE;
        }
        if (device != nullptr)
            device->validationErrors_.fetch_add(1);
        const char *message = (data != nullptr && data->pMessage != nullptr) ? data->pMessage : "(no message)";
        if (error)
        {
            LOG_ERROR(1, "Vulkan validation: ", message);
        }
        else
        {
            LOG_WARNING("Vulkan validation: ", message);
        }
    }
    return VK_FALSE; // ne pas interrompre l'appel qui a provoqué le message
}

bool loaderAvailable() noexcept { return volkReady(); }

// ── Création ──────────────────────────────────────────────────────────────

Result<std::unique_ptr<Device>> createVulkanDevice(const DeviceDesc &desc)
{
    auto created = VulkanDevice::create(desc);
    if (!created)
        return std::unexpected(created.error());
    return std::unique_ptr<Device>(std::move(*created));
}

Result<std::unique_ptr<VulkanDevice>> VulkanDevice::create(const DeviceDesc &desc)
{
    std::unique_ptr<VulkanDevice> device(new VulkanDevice());
    if (auto r = device->init(desc); !r)
        return std::unexpected(r.error());
    return device;
}

Result<> VulkanDevice::fromVk(VkResult result, const char *what) const
{
    if (result == VK_SUCCESS)
        return {};
    std::string message = std::string(what) + ": " + resultName(result);
    switch (result)
    {
        case VK_ERROR_OUT_OF_HOST_MEMORY:
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        case VK_ERROR_OUT_OF_POOL_MEMORY:
        case VK_ERROR_FRAGMENTED_POOL:
        case VK_ERROR_TOO_MANY_OBJECTS: return makeError(ErrorCode::kOutOfRange, std::move(message));
        case VK_ERROR_DEVICE_LOST:
        case VK_ERROR_SURFACE_LOST_KHR: return makeError(ErrorCode::kDeviceLost, std::move(message));
        case VK_ERROR_FORMAT_NOT_SUPPORTED:
        case VK_ERROR_FEATURE_NOT_PRESENT:
        case VK_ERROR_EXTENSION_NOT_PRESENT:
        case VK_ERROR_INCOMPATIBLE_DRIVER: return makeError(ErrorCode::kUnsupported, std::move(message));
        default: return makeError(ErrorCode::kUnknown, std::move(message));
    }
}

Result<> VulkanDevice::init(const DeviceDesc &desc)
{
    windowed_ = desc.surface.nativeWindow != nullptr;
    vsync_ = desc.vsync;
    framesInFlight_ = std::clamp(desc.framesInFlight, 1u, kMaxFramesInFlight); // createDevice a refusé les autres valeurs
    backbufferSize_ = {std::max(1u, desc.surface.size.width), std::max(1u, desc.surface.size.height)};

    if (!volkReady())
        return makeError(ErrorCode::kUnsupported, "Vulkan: loader not found (libvulkan.so.1 / vulkan-1.dll / libvulkan.dylib)");
    if (volkGetInstanceVersion() < VK_API_VERSION_1_3)
    {
        const std::uint32_t v = volkGetInstanceVersion();
        return makeError(ErrorCode::kUnsupported, "Vulkan: the loader implements version " + std::to_string(VK_VERSION_MAJOR(v)) + "." +
                                                      std::to_string(VK_VERSION_MINOR(v)) + ", the RHI needs 1.3 (dynamic rendering, synchronization2)");
    }

    if (auto r = createInstance(desc); !r)
        return r;
    if (auto r = createSurface(desc); !r) // surface (fenêtre) : avant le choix du GPU, qui doit pouvoir présenter
        return r;
    if (auto r = pickPhysicalDevice(desc); !r)
        return r;
    if (auto r = createLogicalDevice(); !r)
        return r;
    if (auto r = createAllocator(); !r)
        return r;

    auto typeInfo = makeStruct<VkSemaphoreTypeCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO);
    typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    typeInfo.initialValue = 0;
    auto semaphoreInfo = makeStruct<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
    semaphoreInfo.pNext = &typeInfo;
    if (auto r = fromVk(vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &timeline_), "vkCreateSemaphore (timeline)"); !r)
        return r;

    if (auto r = createCommandPool(); !r)
        return std::unexpected(r.error());

    fillCapabilities();
    if (auto r = initPipelines(); !r)
        return r;
    if (auto r = initSwapchain(desc); !r) // image de fond (hors écran : une texture RGBA8) et, pour une fenêtre, la chaîne d'échange
        return r;
    checkTimestampsMeasureWork();
    detectIndexedDrawWorkaround();
    return {};
}

// MoltenVK n'écrit des heures GPU que si le périphérique Metal échantillonne ses compteurs ; sinon (GPU paravirtualisé du runner
// macOS, M9-L1) chaque horodatage reçoit l'heure CPU de la fin de son tampon de commandes : deux horodatages qui encadrent du travail
// sont égaux, et rien dans les propriétés Vulkan ne le signale (timestampPeriod 1, timestampValidBits 64). On le constate ici sur un
// remplissage de tampon encadré par deux horodatages : s'ils sont égaux, `timestampQueries` est faux (règle 11). Limité à MoltenVK :
// les autres pilotes écrivent l'heure à l'exécution.
void VulkanDevice::checkTimestampsMeasureWork()
{
    if (!caps_.timestampQueries)
        return;
    auto driver = makeStruct<VkPhysicalDeviceDriverProperties>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES);
    auto properties2 = makeStruct<VkPhysicalDeviceProperties2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2);
    properties2.pNext = &driver;
    vkGetPhysicalDeviceProperties2(physicalDevice_, &properties2);
    if (driver.driverID != VK_DRIVER_ID_MOLTENVK)
        return;

    constexpr VkDeviceSize kProbeSize = 64u << 20; // 64 Mio remplis une fois : assez de travail pour séparer deux heures GPU
    auto bufferInfo = makeStruct<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
    bufferInfo.size = kProbeSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo alloc{};
    alloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    auto queryInfo = makeStruct<VkQueryPoolCreateInfo>(VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO);
    queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    queryInfo.queryCount = 2;
    VkQueryPool pool = VK_NULL_HANDLE;
    bool measured = false;
    std::uint64_t stamps[2] = {0, 0};
    if (vmaCreateBuffer(allocator_, &bufferInfo, &alloc, &buffer, &allocation, nullptr) == VK_SUCCESS &&
        vkCreateQueryPool(device_, &queryInfo, nullptr, &pool) == VK_SUCCESS)
    {
        vkResetQueryPool(device_, pool, 0, 2);
        const auto submitted = immediate(
            [&](VkCommandBuffer commandBuffer)
            {
                vkCmdWriteTimestamp2(commandBuffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, pool, 0);
                vkCmdFillBuffer(commandBuffer, buffer, 0, VK_WHOLE_SIZE, 0x5A5A5A5Au); // un seul remplissage : aucun risque d'écriture après écriture
                vkCmdWriteTimestamp2(commandBuffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, pool, 1);
            });
        measured = submitted.has_value() && vkGetQueryPoolResults(device_, pool, 0, 2, sizeof(stamps), stamps, sizeof(std::uint64_t),
                                                                  VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) == VK_SUCCESS;
    }
    if (pool != VK_NULL_HANDLE)
        vkDestroyQueryPool(device_, pool, nullptr);
    if (buffer != VK_NULL_HANDLE)
        vmaDestroyBuffer(allocator_, buffer, allocation);
    if (!measured)
    {
        LOG_WARNING("Vulkan (MoltenVK): the timestamp check could not run; timestamp queries are kept");
        return;
    }
    if (stamps[1] <= stamps[0])
    {
        caps_.timestampQueries = false;
        LOG_WARNING("Vulkan (MoltenVK): timestamps do not measure GPU work (no Metal counter sampling on this GPU): timestampQueries = false");
    }
}

// M9-L2 (G510) : sur le GPU Metal paravirtualisé des machines virtuelles macOS (« Apple Paravirtual device », runner macOS de la
// CI), MoltenVK traduit vkCmdDrawIndexed en drawIndexedPrimitives direct, et ce dessin ne lit pas les indices de la plupart des
// tampons d'index : rien n'est tracé (aucun message, ni de Vulkan ni de la validation Metal). Le même dessin en indirect
// (drawIndexedPrimitives:indirectBuffer:) lit les mêmes indices. Constaté en CI : tampon d'index en mémoire visible de l'hôte, tampon
// sous-alloué loin dans son bloc ; pas de cause publiée connue. Sur ce périphérique, chaque dessin indexé direct passe donc par une
// commande indirecte d'un dessin (VkEncoder::drawIndexed). Les autres GPU Apple ne sont pas concernés (non vérifié : pas de Mac
// réel en CI, G510). ENGINE_VULKAN_INDEXED_VIA_INDIRECT=0 ou 1 force le choix (diagnostic).
void VulkanDevice::detectIndexedDrawWorkaround()
{
    auto driver = makeStruct<VkPhysicalDeviceDriverProperties>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES);
    auto properties2 = makeStruct<VkPhysicalDeviceProperties2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2);
    properties2.pNext = &driver;
    vkGetPhysicalDeviceProperties2(physicalDevice_, &properties2);
    bool wanted = driver.driverID == VK_DRIVER_ID_MOLTENVK && std::string_view(properties_.deviceName).find("Paravirtual") != std::string_view::npos;
    if (const char *forced = std::getenv("ENGINE_VULKAN_INDEXED_VIA_INDIRECT"); forced != nullptr && (forced[0] == '0' || forced[0] == '1'))
        wanted = forced[0] == '1';
    // Une commande indirecte porte firstInstance : sans drawIndirectFirstInstance, seuls les dessins à firstInstance nul pourraient
    // passer ; le contournement est alors abandonné (dessins directs, comme avant).
    indexedDrawsViaIndirect_ = wanted && features_.drawIndirectFirstInstance != VK_FALSE;
    if (indexedDrawsViaIndirect_)
        LOG_INFO("Vulkan: indexed draws are recorded as one-command indirect draws on this device (", properties_.deviceName, ", G510)");
    else if (wanted)
        LOG_WARNING("Vulkan: indexed draws should go through indirect commands on this device, but drawIndirectFirstInstance is missing");
}

Result<> VulkanDevice::createInstance(const DeviceDesc &desc)
{
    auto app = makeStruct<VkApplicationInfo>(VK_STRUCTURE_TYPE_APPLICATION_INFO);
    app.pApplicationName = desc.appName.c_str();
    app.applicationVersion = 1;
    app.pEngineName = "Orosmo";
    app.engineVersion = 1;
    app.apiVersion = VK_API_VERSION_1_3;

    std::vector<const char *> layers;
    std::vector<const char *> extensions;
    const auto available = instanceExtensions(nullptr);

    validationEnabled_ = false;
    bool validationFeatures = false;
    if (desc.validation)
    {
        if (hasInstanceLayer(kValidationLayer) && hasExtension(available, VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
        {
            layers.push_back(kValidationLayer);
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            validationEnabled_ = true;
            debugUtils_ = true;
            validationFeatures = envFlag("ENGINE_VULKAN_SYNC_VALIDATION", true) && hasExtension(instanceExtensions(kValidationLayer), VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
            if (validationFeatures)
                extensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
        }
        else
        {
            LOG_WARNING("Vulkan: validation requested but ", kValidationLayer, " is not installed (vulkan-validationlayers): no validation");
            if (hasExtension(available, VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
            {
                extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
                debugUtils_ = true; // les noms d'objets restent utiles
            }
        }
    }
    else if (hasExtension(available, VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
    {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        debugUtils_ = true;
    }

    VkInstanceCreateFlags flags = 0;
    if (hasExtension(available, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
    {
        extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
    if (windowed_)
    {
        auto required = Swapchain::requiredInstanceExtensions();
        if (!required)
            return std::unexpected(required.error());
        for (const char *name : *required)
        {
            if (!hasExtension(available, name))
                return makeError(ErrorCode::kUnsupported, std::string("Vulkan: the instance lacks the window system extension ") + name);
            if (std::find_if(extensions.begin(), extensions.end(), [&](const char *e) { return std::string_view(e) == name; }) == extensions.end())
                extensions.push_back(name);
        }
    }

    auto messengerInfo = makeStruct<VkDebugUtilsMessengerCreateInfoEXT>(VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT);
    messengerInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    messengerInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    messengerInfo.pfnUserCallback = debugMessengerCallback;
    messengerInfo.pUserData = this;

    const VkValidationFeatureEnableEXT enables[] = {VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT};
    auto features = makeStruct<VkValidationFeaturesEXT>(VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT);
    features.enabledValidationFeatureCount = 1;
    features.pEnabledValidationFeatures = enables;

    auto info = makeStruct<VkInstanceCreateInfo>(VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);
    info.flags = flags;
    info.pApplicationInfo = &app;
    info.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
    info.ppEnabledLayerNames = layers.data();
    info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();
    const void *chain = nullptr;
    if (validationEnabled_)
    {
        messengerInfo.pNext = validationFeatures ? &features : nullptr; // messages de création et de destruction de l'instance
        chain = &messengerInfo;
    }
    info.pNext = chain;

    VkResult result = vkCreateInstance(&info, nullptr, &instance_);
    if (result == VK_ERROR_LAYER_NOT_PRESENT && validationEnabled_)
    {
        // Manifeste trouvé mais bibliothèque introuvable (macOS : `library_path` relatif que dlopen ne cherche pas dans le préfixe
        // Homebrew, M9-L1) : le périphérique est créé sans validation, comme quand la couche n'est pas installée.
        LOG_WARNING("Vulkan: ", kValidationLayer, " is listed but its library cannot be loaded (check VK_LAYER_PATH and the manifest's library_path): no validation");
        layers.clear();
        std::erase_if(extensions, [](const char *e) { return std::string_view(e) == VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME; });
        info.enabledLayerCount = 0;
        info.ppEnabledLayerNames = nullptr;
        info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
        info.ppEnabledExtensionNames = extensions.data();
        info.pNext = nullptr;
        validationEnabled_ = false;
        validationErrors_.store(0); // messages du chargeur pendant l'essai refusé : pas des messages de validation
        result = vkCreateInstance(&info, nullptr, &instance_);
    }
    if (result != VK_SUCCESS)
    {
        instance_ = VK_NULL_HANDLE;
        return fromVk(result, "vkCreateInstance");
    }
    volkLoadInstance(instance_);

    if (validationEnabled_ && vkCreateDebugUtilsMessengerEXT != nullptr)
    {
        messengerInfo.pNext = nullptr;
        if (vkCreateDebugUtilsMessengerEXT(instance_, &messengerInfo, nullptr, &messenger_) != VK_SUCCESS)
            messenger_ = VK_NULL_HANDLE;
    }
    return {};
}

Result<> VulkanDevice::pickPhysicalDevice(const DeviceDesc &)
{
    std::uint32_t count = 0;
    if (auto r = fromVk(vkEnumeratePhysicalDevices(instance_, &count, nullptr), "vkEnumeratePhysicalDevices"); !r)
        return r;
    if (count == 0)
        return makeError(ErrorCode::kUnsupported, "Vulkan: no physical device (no GPU driver with an ICD)");
    std::vector<VkPhysicalDevice> devices(count);
    if (const VkResult listed = vkEnumeratePhysicalDevices(instance_, &count, devices.data()); listed != VK_SUCCESS && listed != VK_INCOMPLETE)
        return fromVk(listed, "vkEnumeratePhysicalDevices");
    devices.resize(count); // le nombre peut avoir baissé entre les deux appels

    const char *wanted = std::getenv("ENGINE_VULKAN_DEVICE"); // indice, ou fragment du nom du GPU
    std::string reasons;
    int bestScore = -1;
    VkPhysicalDevice best = VK_NULL_HANDLE;
    std::uint32_t bestFamily = 0;
    for (std::uint32_t i = 0; i < devices.size(); ++i)
    {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(devices[i], &props);
        const std::string name = props.deviceName;
        auto reject = [&](const std::string &why) { reasons += "\n  - " + name + ": " + why; };

        if (props.apiVersion < VK_API_VERSION_1_3)
        {
            reject("Vulkan " + std::to_string(VK_VERSION_MAJOR(props.apiVersion)) + "." + std::to_string(VK_VERSION_MINOR(props.apiVersion)) + " (1.3 needed)");
            continue;
        }
        auto f13 = makeStruct<VkPhysicalDeviceVulkan13Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES);
        auto f12 = makeStruct<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
        f12.pNext = &f13;
        auto f2 = makeStruct<VkPhysicalDeviceFeatures2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
        f2.pNext = &f12;
        vkGetPhysicalDeviceFeatures2(devices[i], &f2);
        if (!f13.dynamicRendering || !f13.synchronization2 || !f12.timelineSemaphore || !f12.hostQueryReset)
        {
            reject("missing feature (dynamicRendering, synchronization2, timelineSemaphore, hostQueryReset)");
            continue;
        }
        std::uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(devices[i], nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> exts(extCount);
        vkEnumerateDeviceExtensionProperties(devices[i], nullptr, &extCount, exts.data());
        if (windowed_ && !hasExtension(exts, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
        {
            reject("no VK_KHR_swapchain");
            continue;
        }
        std::uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &familyCount, families.data());
        std::int64_t family = -1;
        for (std::uint32_t f = 0; f < familyCount; ++f)
        {
            const VkQueueFlags need = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
            if ((families[f].queueFlags & need) != need || families[f].queueCount == 0)
                continue;
            if (windowed_ && !swapchain_->presentSupported(devices[i], f))
                continue;
            family = f;
            break;
        }
        if (family < 0)
        {
            reject(windowed_ ? "no graphics+compute queue able to present to the window" : "no graphics+compute queue");
            continue;
        }

        int score = 0;
        switch (props.deviceType)
        {
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: score = 1000; break;
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: score = 500; break;
            case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: score = 100; break;
            default: score = 10; break; // CPU (lavapipe) : seulement faute de mieux
        }
        if (wanted != nullptr && *wanted != '\0')
        {
            const std::string w = wanted;
            const bool index = !w.empty() && std::all_of(w.begin(), w.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
            bool match = false;
            if (index)
            {
                // Indice non représentable (trop de chiffres) : aucun GPU ne correspond, sans exception.
                std::uint64_t wantedIndex = 0;
                const auto parsed = std::from_chars(w.data(), w.data() + w.size(), wantedIndex);
                match = parsed.ec == std::errc{} && wantedIndex == i;
            }
            else
            {
                match = name.find(w) != std::string::npos;
            }
            if (match)
                score += 100000;
        }
        if (score > bestScore)
        {
            bestScore = score;
            best = devices[i];
            bestFamily = static_cast<std::uint32_t>(family);
        }
    }
    if (best == VK_NULL_HANDLE)
        return makeError(ErrorCode::kUnsupported, "Vulkan: no usable physical device:" + reasons);
    physicalDevice_ = best;
    queueFamily_ = bestFamily;
    vkGetPhysicalDeviceProperties(physicalDevice_, &properties_);
    return {};
}

Result<> VulkanDevice::createLogicalDevice()
{
    VkPhysicalDeviceFeatures supported{};
    vkGetPhysicalDeviceFeatures(physicalDevice_, &supported);
    auto s11 = makeStruct<VkPhysicalDeviceVulkan11Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES);
    auto s2 = makeStruct<VkPhysicalDeviceFeatures2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
    s2.pNext = &s11;
    vkGetPhysicalDeviceFeatures2(physicalDevice_, &s2);

    // Fonctions facultatives : activées quand le pilote les a (les capacités les annoncent ensuite).
    VkPhysicalDeviceFeatures enabled{};
    enabled.samplerAnisotropy = supported.samplerAnisotropy;
    enabled.fillModeNonSolid = supported.fillModeNonSolid;
    enabled.multiDrawIndirect = supported.multiDrawIndirect;
    enabled.drawIndirectFirstInstance = supported.drawIndirectFirstInstance;
    enabled.independentBlend = supported.independentBlend;
    enabled.shaderFloat64 = supported.shaderFloat64;
    enabled.textureCompressionBC = supported.textureCompressionBC;
    enabled.textureCompressionASTC_LDR = supported.textureCompressionASTC_LDR;
    enabled.fragmentStoresAndAtomics = supported.fragmentStoresAndAtomics;
    enabled.vertexPipelineStoresAndAtomics = supported.vertexPipelineStoresAndAtomics;
    enabled.imageCubeArray = supported.imageCubeArray;
    enabled.shaderStorageImageExtendedFormats = supported.shaderStorageImageExtendedFormats;
    enabled.shaderStorageImageReadWithoutFormat = supported.shaderStorageImageReadWithoutFormat;
    enabled.shaderStorageImageWriteWithoutFormat = supported.shaderStorageImageWriteWithoutFormat;
    features_ = enabled;

    auto f13 = makeStruct<VkPhysicalDeviceVulkan13Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES);
    f13.dynamicRendering = VK_TRUE;
    f13.synchronization2 = VK_TRUE;
    auto f12 = makeStruct<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
    f12.pNext = &f13;
    f12.timelineSemaphore = VK_TRUE;
    f12.hostQueryReset = VK_TRUE;
    auto f11 = makeStruct<VkPhysicalDeviceVulkan11Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES);
    f11.pNext = &f12;
    f11.shaderDrawParameters = s11.shaderDrawParameters;
    auto f2 = makeStruct<VkPhysicalDeviceFeatures2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
    f2.pNext = &f11;
    f2.features = enabled;

    std::vector<const char *> extensions;
    std::uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> exts(extCount);
    vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr, &extCount, exts.data());
    if (windowed_)
        extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    // MoltenVK : l'extension est exigée quand elle est présente, et ses fonctions (échantillonneurs de comparaison modifiables, vue 2D
    // d'une image 3D...) ne sont utilisables qu'ACTIVÉES : sans la structure, la validation signale chaque usage (« portability
    // error », M9-L1). Toutes celles que le pilote offre sont activées ; les absentes restent fausses.
    // VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR n'est déclaré qu'avec VK_ENABLE_BETA_EXTENSIONS : sa valeur.
    constexpr auto kPortabilitySubsetFeatures = static_cast<VkStructureType>(1000163000);
    auto portability = makeStruct<VkPhysicalDevicePortabilitySubsetFeaturesKHR>(kPortabilitySubsetFeatures);
    if (hasExtension(exts, "VK_KHR_portability_subset"))
    {
        extensions.push_back("VK_KHR_portability_subset");
        auto query = makeStruct<VkPhysicalDeviceFeatures2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
        query.pNext = &portability;
        vkGetPhysicalDeviceFeatures2(physicalDevice_, &query);
        portability.pNext = nullptr;
        f13.pNext = &portability;
    }

    const float priority = 1.0f;
    auto queueInfo = makeStruct<VkDeviceQueueCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
    queueInfo.queueFamilyIndex = queueFamily_;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    auto info = makeStruct<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
    info.pNext = &f2;
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queueInfo;
    info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();
    if (auto r = fromVk(vkCreateDevice(physicalDevice_, &info, nullptr, &device_), "vkCreateDevice"); !r)
    {
        device_ = VK_NULL_HANDLE;
        return r;
    }
    volkLoadDevice(device_);
    vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);
    return {};
}

Result<> VulkanDevice::createAllocator()
{
    VmaVulkanFunctions functions{};
    functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    VmaAllocatorCreateInfo info{};
    info.physicalDevice = physicalDevice_;
    info.device = device_;
    info.instance = instance_;
    info.vulkanApiVersion = VK_API_VERSION_1_3;
    info.pVulkanFunctions = &functions;
    return fromVk(vmaCreateAllocator(&info, &allocator_), "vmaCreateAllocator");
}

void VulkanDevice::fillCapabilities()
{
    const VkPhysicalDeviceLimits &l = properties_.limits;
    caps_ = Capabilities{};
    caps_.backend = Backend::kVulkan;
    caps_.deviceName = properties_.deviceName;
    const std::uint32_t v = properties_.driverVersion;
    caps_.driverVersion = "Vulkan " + std::to_string(VK_VERSION_MAJOR(properties_.apiVersion)) + "." + std::to_string(VK_VERSION_MINOR(properties_.apiVersion)) +
                          "." + std::to_string(VK_VERSION_PATCH(properties_.apiVersion)) + " (driver " + std::to_string(v) + ")";
    caps_.maxTextureSize = l.maxImageDimension2D;
    caps_.max3DTextureSize = l.maxImageDimension3D;
    caps_.maxColorAttachments = std::min<std::uint32_t>(l.maxColorAttachments, kMaxColorTargets);
    caps_.maxComputeWorkGroupSize = {l.maxComputeWorkGroupSize[0], l.maxComputeWorkGroupSize[1], l.maxComputeWorkGroupSize[2]};
    caps_.maxComputeWorkGroupCount = {l.maxComputeWorkGroupCount[0], l.maxComputeWorkGroupCount[1], l.maxComputeWorkGroupCount[2]};
    caps_.maxComputeInvocations = l.maxComputeWorkGroupInvocations;
    caps_.minUniformAlignment = l.minUniformBufferOffsetAlignment;
    caps_.minStorageAlignment = l.minStorageBufferOffsetAlignment;
    caps_.maxUniformBufferSize = l.maxUniformBufferRange;
    caps_.maxPushConstantSize = l.maxPushConstantsSize;
    caps_.maxStorageBufferBindings = l.maxPerStageDescriptorStorageBuffers;
    caps_.compute = true;
    caps_.indirectDraw = true;
    caps_.multiDrawIndirect = features_.multiDrawIndirect != VK_FALSE;
    caps_.drawIndirectFirstInstance = features_.drawIndirectFirstInstance != VK_FALSE;
    // Les horodatages dépendent de la famille de files CHOISIE (timestampValidBits), pas seulement de timestampComputeAndGraphics.
    std::uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &familyCount, families.data());
    const bool familyWritesTimestamps = queueFamily_ < familyCount && families[queueFamily_].timestampValidBits > 0;
    caps_.timestampQueries = familyWritesTimestamps && l.timestampPeriod > 0.0f;
    caps_.anisotropy = features_.samplerAnisotropy != VK_FALSE;
    caps_.bc7Textures = features_.textureCompressionBC != VK_FALSE && formatSupports(Format::kBC7Unorm, TextureUsage::kSampled | TextureUsage::kCopyDst);
    caps_.astcTextures = features_.textureCompressionASTC_LDR != VK_FALSE && formatSupports(Format::kASTC4x4Unorm, TextureUsage::kSampled | TextureUsage::kCopyDst);
    caps_.wireframe = features_.fillModeNonSolid != VK_FALSE;
    caps_.srgbStorageImages = formatSupports(Format::kRGBA8Srgb, TextureUsage::kStorage);
    caps_.validation = validationEnabled_;
    caps_.spirvShaders = true;
    caps_.shaderFloat64 = features_.shaderFloat64 != VK_FALSE;
    // Capacités réservées (RHI-18) : faux tant que non implémentées.

    // D24S8 n'existe pas partout (AMD) : D32S8 le remplace.
    VkFormatProperties props{};
    vkGetPhysicalDeviceFormatProperties(physicalDevice_, VK_FORMAT_D24_UNORM_S8_UINT, &props);
    d24s8Replacement_ = (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0 ? VK_FORMAT_D24_UNORM_S8_UINT
                                                                                                             : VK_FORMAT_D32_SFLOAT_S8_UINT;
}

VkFormat VulkanDevice::vkFormat(Format format) const noexcept { return toVk(format, d24s8Replacement_); }

bool VulkanDevice::formatSupports(Format format, TextureUsage usage) const noexcept
{
    const VkFormat vkf = vkFormat(format);
    if (vkf == VK_FORMAT_UNDEFINED)
        return false;
    VkFormatProperties props{};
    vkGetPhysicalDeviceFormatProperties(physicalDevice_, vkf, &props);
    const VkFormatFeatureFlags f = props.optimalTilingFeatures;
    if (hasFlag(usage, TextureUsage::kSampled) && (f & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) == 0)
        return false;
    if (hasFlag(usage, TextureUsage::kStorage) && (f & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) == 0)
        return false;
    if (hasFlag(usage, TextureUsage::kRenderTarget))
    {
        const VkFormatFeatureFlags need = formatInfo(format).isDepth() ? VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
        if ((f & need) == 0)
            return false;
    }
    if (hasFlag(usage, TextureUsage::kCopySrc) && (f & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT) == 0)
        return false;
    if (hasFlag(usage, TextureUsage::kCopyDst) && (f & VK_FORMAT_FEATURE_TRANSFER_DST_BIT) == 0)
        return false;
    return true;
}

void VulkanDevice::setObjectName(VkObjectType type, std::uint64_t handle, const std::string &name) const
{
    if (!debugUtils_ || name.empty() || vkSetDebugUtilsObjectNameEXT == nullptr)
        return;
    auto info = makeStruct<VkDebugUtilsObjectNameInfoEXT>(VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT);
    info.objectType = type;
    info.objectHandle = handle;
    info.pObjectName = name.c_str();
    vkSetDebugUtilsObjectNameEXT(device_, &info);
}

// ── Séries, libération différée, soumission ─────────────────────────────────

Serial VulkanDevice::completedSerial() const
{
    std::uint64_t value = 0;
    if (timeline_ != VK_NULL_HANDLE)
        vkGetSemaphoreCounterValue(device_, timeline_, &value);
    return value;
}

Result<> VulkanDevice::waitSerial(Serial serial)
{
    if (serial == 0 || completedSerial() >= serial)
        return {};
    auto info = makeStruct<VkSemaphoreWaitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO);
    info.semaphoreCount = 1;
    info.pSemaphores = &timeline_;
    info.pValues = &serial;
    return fromVk(vkWaitSemaphores(device_, &info, UINT64_MAX), "vkWaitSemaphores");
}

void VulkanDevice::queueRelease(std::function<void()> release, bool waitFrames)
{
    // Un lot de transfert ouvert part avec la série SUIVANTE : une ressource détruite après y avoir été copiée doit attendre cette
    // série, pas celle de la dernière soumission (sinon elle serait libérée avant la fin de la copie).
    const Serial serial = batch_ != VK_NULL_HANDLE ? lastSubmitted_ + 1 : lastSubmitted_;
    pending_.push_back({serial, frame_, waitFrames, std::move(release)});
}

void VulkanDevice::collectReleases(bool ignoreFrames)
{
    if (pending_.empty())
        return;
    const Serial done = completedSerial();
    // Les libérations prêtes passent dans un tableau réutilisé (pas d'allocation par image) ; les autres sont compactées sur place,
    // dans l'ordre de mise en file. Les libérations peuvent en provoquer d'autres (jamais de nouvelle soumission).
    std::vector<Pending> ready = std::move(readyScratch_);
    ready.clear();
    std::size_t kept = 0;
    for (std::size_t i = 0; i < pending_.size(); ++i)
    {
        Pending &p = pending_[i];
        if (p.serial <= done && (ignoreFrames || !p.waitFrames || frame_ >= p.frame + framesInFlight_))
        {
            ready.push_back(std::move(p));
        }
        else
        {
            if (kept != i)
                pending_[kept] = std::move(p);
            ++kept;
        }
    }
    pending_.erase(pending_.begin() + static_cast<std::ptrdiff_t>(kept), pending_.end());
    for (Pending &p : ready)
        p.release();
    ready.clear();
    readyScratch_ = std::move(ready);
}

// ── Réserves de tampons de commandes ───────────────────────────────────────

Result<std::size_t> VulkanDevice::createCommandPool()
{
    auto poolInfo = makeStruct<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT; // remise à zéro en bloc seulement (vkResetCommandPool)
    poolInfo.queueFamilyIndex = queueFamily_;
    CommandPoolSlot slot;
    if (auto r = fromVk(vkCreateCommandPool(device_, &poolInfo, nullptr, &slot.pool), "vkCreateCommandPool"); !r)
        return std::unexpected(r.error());
    commandPools_.push_back(std::move(slot));
    ++counters_.commandPools;
    return commandPools_.size() - 1;
}

VulkanDevice::CommandPoolSlot *VulkanDevice::poolOf(VkCommandBuffer commandBuffer) noexcept
{
    // La réserve courante d'abord (cas courant), puis les autres : quelques réserves de quelques dizaines de tampons.
    const std::size_t count = commandPools_.size();
    for (std::size_t k = 0; k < count; ++k)
    {
        CommandPoolSlot &slot = commandPools_[(currentPool_ + count - k) % count];
        if (std::find(slot.buffers.begin(), slot.buffers.begin() + slot.next, commandBuffer) != slot.buffers.begin() + slot.next)
            return &slot;
    }
    return nullptr;
}

void VulkanDevice::rotateCommandPool()
{
    if (commandPools_.empty() || commandPools_[currentPool_].next == 0)
        return; // la réserve courante n'a encore rien donné
    const Serial done = completedSerial();
    // Une réserve dont tous les tampons sont rendus et finis par le GPU est remise à zéro en bloc et devient courante ; les
    // autres réserves d'abord (dans l'ordre de l'anneau), la courante en dernier.
    const std::size_t count = commandPools_.size();
    for (std::size_t k = 1; k <= count; ++k)
    {
        const std::size_t i = (currentPool_ + k) % count;
        CommandPoolSlot &slot = commandPools_[i];
        if (slot.outstanding != 0 || slot.lastSerial > done)
            continue;
        if (slot.next != 0)
        {
            if (vkResetCommandPool(device_, slot.pool, 0) != VK_SUCCESS)
                continue; // erreur de mémoire : la réserve reste telle quelle
            ++counters_.commandPoolResets;
            slot.next = 0;
        }
        currentPool_ = i;
        return;
    }
    if (auto created = createCommandPool(); created)
        currentPool_ = *created;
    // Sinon (mémoire) : la réserve courante continue d'allouer.
}

Result<VkCommandBuffer> VulkanDevice::acquireCommandBuffer()
{
    collectReleases();
    if (commandPools_[currentPool_].next >= kCommandBuffersPerPool)
        rotateCommandPool();
    CommandPoolSlot &slot = commandPools_[currentPool_];
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (slot.next < slot.buffers.size())
    {
        commandBuffer = slot.buffers[slot.next]; // remis à zéro avec la réserve : état initial
    }
    else
    {
        auto info = makeStruct<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
        info.commandPool = slot.pool;
        info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        info.commandBufferCount = 1;
        if (auto r = fromVk(vkAllocateCommandBuffers(device_, &info, &commandBuffer), "vkAllocateCommandBuffers"); !r)
            return std::unexpected(r.error());
        slot.buffers.push_back(commandBuffer);
        ++counters_.commandBuffersAllocated;
    }
    ++slot.next;
    auto begin = makeStruct<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (auto r = fromVk(vkBeginCommandBuffer(commandBuffer, &begin), "vkBeginCommandBuffer"); !r)
        return std::unexpected(r.error()); // donné mais jamais rendu en usage : `outstanding` n'a pas bougé
    ++slot.outstanding;
    return commandBuffer;
}

void VulkanDevice::retireCommandBuffer(VkCommandBuffer commandBuffer, Serial serial)
{
    if (CommandPoolSlot *slot = poolOf(commandBuffer); slot != nullptr)
    {
        --slot->outstanding;
        slot->lastSerial = std::max(slot->lastSerial, serial);
    }
}

void VulkanDevice::releaseUnsubmitted(VkCommandBuffer commandBuffer)
{
    if (CommandPoolSlot *slot = poolOf(commandBuffer); slot != nullptr)
        --slot->outstanding; // jamais soumis : rien à attendre du GPU ; remis à zéro avec la réserve
}

void VulkanDevice::destroyCommandPools() noexcept
{
    for (CommandPoolSlot &slot : commandPools_)
        vkDestroyCommandPool(device_, slot.pool, nullptr); // libère aussi tous ses tampons de commandes
    commandPools_.clear();
    currentPool_ = 0;
}

// ── Soumission ──────────────────────────────────────────────────────────

Result<Serial> VulkanDevice::submitCommandBuffers(std::span<const VkCommandBuffer> commandBuffers, const SubmitSync &sync)
{
    if (auto r = submitWithBatch(commandBuffers, sync); !r)
        return std::unexpected(r.error());
    return lastSubmitted_;
}

Result<> VulkanDevice::submitRaw(std::span<const VkCommandBuffer> commandBuffers, const SubmitSync &sync)
{
    submitInfos_.clear();
    for (VkCommandBuffer cb : commandBuffers)
    {
        auto info = makeStruct<VkCommandBufferSubmitInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO);
        info.commandBuffer = cb;
        submitInfos_.push_back(info);
    }
    submitWaits_.clear();
    for (const auto &[semaphore, stage] : sync.waits)
    {
        auto info = makeStruct<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
        info.semaphore = semaphore;
        info.stageMask = stage;
        submitWaits_.push_back(info);
    }
    // Image de la chaîne acquise par beginFrame (G185) : la première soumission qui suit attend l'acquisition, quelle qu'elle soit
    // (lot de transferts, encodeur de l'image, présentation).
    if (acquireWait_ != VK_NULL_HANDLE)
    {
        auto info = makeStruct<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
        info.semaphore = acquireWait_;
        info.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        submitWaits_.push_back(info);
    }
    submitSignals_.clear();
    auto timelineSignal = makeStruct<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
    timelineSignal.semaphore = timeline_;
    timelineSignal.value = lastSubmitted_ + 1;
    timelineSignal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    submitSignals_.push_back(timelineSignal);
    for (VkSemaphore semaphore : sync.signals)
    {
        auto info = makeStruct<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
        info.semaphore = semaphore;
        info.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        submitSignals_.push_back(info);
    }
    auto submit = makeStruct<VkSubmitInfo2>(VK_STRUCTURE_TYPE_SUBMIT_INFO_2);
    submit.waitSemaphoreInfoCount = static_cast<std::uint32_t>(submitWaits_.size());
    submit.pWaitSemaphoreInfos = submitWaits_.data();
    submit.commandBufferInfoCount = static_cast<std::uint32_t>(submitInfos_.size());
    submit.pCommandBufferInfos = submitInfos_.data();
    submit.signalSemaphoreInfoCount = static_cast<std::uint32_t>(submitSignals_.size());
    submit.pSignalSemaphoreInfos = submitSignals_.data();
    if (auto r = fromVk(vkQueueSubmit2(queue_, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit2"); !r)
        return r;
    ++lastSubmitted_;
    ++counters_.submits;
    acquireWait_ = VK_NULL_HANDLE; // attente consommée
    for (VkCommandBuffer cb : commandBuffers)
        retireCommandBuffer(cb, lastSubmitted_);
    return {};
}

Result<> VulkanDevice::submitWithBatch(std::span<const VkCommandBuffer> commandBuffers, const SubmitSync &sync)
{
    auto closed = closeBatch();
    if (!closed)
        return std::unexpected(closed.error());
    if (closed->commandBuffer == VK_NULL_HANDLE)
        return submitRaw(commandBuffers, sync);
    // Le lot passe en tête de la MÊME soumission : ses barrières (fin du lot) ordonnent ses copies avant les commandes qui suivent.
    submitList_.clear();
    submitList_.push_back(closed->commandBuffer);
    submitList_.insert(submitList_.end(), commandBuffers.begin(), commandBuffers.end());
    if (auto r = submitRaw(submitList_, sync); !r)
    {
        batchRejected(*closed);
        return r;
    }
    batchSubmitted(*closed);
    return {};
}

Result<> VulkanDevice::immediate(const std::function<void(VkCommandBuffer)> &record, bool *accepted)
{
    if (accepted != nullptr)
        *accepted = false;
    auto commandBuffer = acquireCommandBuffer();
    if (!commandBuffer)
        return std::unexpected(commandBuffer.error());
    record(*commandBuffer);
    if (auto r = fromVk(vkEndCommandBuffer(*commandBuffer), "vkEndCommandBuffer"); !r)
    {
        releaseUnsubmitted(*commandBuffer);
        return r;
    }
    const VkCommandBuffer cbs[] = {*commandBuffer};
    if (auto r = submitWithBatch(cbs, {}); !r)
    {
        releaseUnsubmitted(*commandBuffer);
        return r;
    }
    if (accepted != nullptr)
        *accepted = true;
    return waitSerial(lastSubmitted_);
}

void VulkanDevice::markPendingInitialization(TextureHandle texture) { pendingInitialization_.push_back(texture); }

// Fait passer en disposition de repos les textures neuves (UNDEFINED) : leur première commande les suppose au repos.
void VulkanDevice::drainPendingInit(VkCommandBuffer commandBuffer)
{
    if (pendingInitialization_.empty())
        return;
    for (TextureHandle handle : pendingInitialization_)
    {
        TextureRes *texture = textures_.get(handle);
        if (texture == nullptr || texture->layoutInitialized)
            continue; // détruite avant sa première utilisation, ou déjà initialisée
        transitionImage(commandBuffer, texture->image, texture->aspect, VK_IMAGE_LAYOUT_UNDEFINED, texture->restLayout);
        texture->layoutInitialized = true;
        batchInitialized_.push_back(handle);
    }
    pendingInitialization_.clear();
}

// Le lot ouvert est abandonné (fin ou soumission refusée) : la transition UNDEFINED -> repos n'a pas eu lieu, la prochaine
// soumission doit la refaire.
void VulkanDevice::abandonBatchInit()
{
    for (TextureHandle handle : batchInitialized_)
    {
        if (TextureRes *texture = textures_.get(handle); texture != nullptr)
        {
            texture->layoutInitialized = false;
            pendingInitialization_.push_back(handle);
        }
    }
    batchInitialized_.clear();
}

Result<VkCommandBuffer> VulkanDevice::batch(std::vector<Staging> staging)
{
    if (batch_ == VK_NULL_HANDLE)
    {
        auto commandBuffer = acquireCommandBuffer();
        if (!commandBuffer)
        {
            for (const Staging &s : staging)
                destroyStaging(s);
            return std::unexpected(commandBuffer.error());
        }
        batch_ = *commandBuffer;
        batchDirty_.clear();
        memoryBarrierAll(batch_); // tout ce qui a été soumis avant est fini avant nos écritures
    }
    for (Staging &s : staging)
    {
        batchBytes_ += s.size;
        batchStaging_.push_back(s);
    }
    drainPendingInit(batch_);
    return batch_;
}

void VulkanDevice::batchCopyBarrier(VkBuffer buffer, std::uint64_t offset, std::uint64_t size)
{
    // Deux copies du même lot vers des plages disjointes n'ont pas d'ordre à garder ; une plage qui en recouvre une autre doit
    // être écrite après elle (la dernière mise à jour gagne, comme glBufferSubData) : une barrière, puis on repart de zéro.
    const std::uint64_t end = offset + size;
    for (const DirtyRange &d : batchDirty_)
    {
        if (d.buffer == buffer && offset < d.end && d.begin < end)
        {
            memoryBarrierAll(batch_);
            ++counters_.copyBarriers;
            batchDirty_.clear();
            break;
        }
    }
    batchDirty_.push_back({buffer, offset, end});
}

Result<VulkanDevice::ClosedBatch> VulkanDevice::closeBatch()
{
    ClosedBatch closed;
    if (batch_ == VK_NULL_HANDLE && pendingInitialization_.empty())
        return closed;
    auto commandBuffer = batch();
    if (!commandBuffer)
        return std::unexpected(commandBuffer.error());
    closed.commandBuffer = *commandBuffer;
    memoryBarrierAll(closed.commandBuffer); // les écritures du lot sont visibles pour tout ce qui suit (même soumission comprise)
    batch_ = VK_NULL_HANDLE;
    batchDirty_.clear();
    closed.staging = std::move(batchStaging_);
    batchStaging_.clear();
    batchBytes_ = 0;
    if (auto r = fromVk(vkEndCommandBuffer(closed.commandBuffer), "vkEndCommandBuffer"); !r)
    {
        batchRejected(closed);
        return std::unexpected(r.error());
    }
    return closed;
}

void VulkanDevice::batchSubmitted(ClosedBatch &closed)
{
    batchInitialized_.clear();
    if (!closed.staging.empty())
        queueRelease([this, staging = std::move(closed.staging)]() { for (const Staging &s : staging) destroyStaging(s); }, false);
}

// Le lot n'est pas parti : son tampon retourne à sa réserve, ses tampons dédiés sont libérés, ses textures neuves redeviennent
// UNDEFINED. Les places prises dans l'anneau seront recyclées avec la série suivante.
void VulkanDevice::batchRejected(ClosedBatch &closed)
{
    if (closed.commandBuffer != VK_NULL_HANDLE)
        releaseUnsubmitted(closed.commandBuffer);
    closed.commandBuffer = VK_NULL_HANDLE;
    for (const Staging &s : closed.staging)
        destroyStaging(s);
    closed.staging.clear();
    abandonBatchInit();
}

Result<> VulkanDevice::flushPending()
{
    auto closed = closeBatch();
    if (!closed)
        return std::unexpected(closed.error());
    if (closed->commandBuffer == VK_NULL_HANDLE)
        return {};
    const VkCommandBuffer cbs[] = {closed->commandBuffer};
    if (auto r = submitRaw(cbs, {}); !r)
    {
        batchRejected(*closed);
        return r;
    }
    batchSubmitted(*closed);
    return {};
}

// ── Transferts ──────────────────────────────────────────────────────────

Result<VulkanDevice::Staging> VulkanDevice::makeStaging(std::uint64_t size, bool readback)
{
    auto info = makeStruct<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
    info.size = std::max<std::uint64_t>(size, 4);
    info.usage = readback ? VK_BUFFER_USAGE_TRANSFER_DST_BIT : VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo alloc{};
    alloc.usage = readback ? VMA_MEMORY_USAGE_AUTO_PREFER_HOST : VMA_MEMORY_USAGE_AUTO;
    alloc.flags = (readback ? VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT : VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT) | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    Staging staging;
    staging.size = size;
    VmaAllocationInfo allocInfo{};
    if (auto r = fromVk(vmaCreateBuffer(allocator_, &info, &alloc, &staging.buffer, &staging.allocation, &allocInfo), "vmaCreateBuffer (staging)"); !r)
        return std::unexpected(r.error());
    staging.mapped = allocInfo.pMappedData;
    return staging;
}

void VulkanDevice::destroyStaging(const Staging &staging) noexcept
{
    if (staging.buffer != VK_NULL_HANDLE)
        vmaDestroyBuffer(allocator_, staging.buffer, staging.allocation);
}

Result<VulkanDevice::UploadSpace> VulkanDevice::ringAllocate(std::uint64_t size, std::uint64_t requested)
{
    // Chaque envoi commence sur un atome non cohérent (puissances de deux : le max est le PPCM) : le vidage d'un envoi ne touche
    // jamais l'atome d'un envoi précédent que le GPU lit peut-être encore (mémoire non HOST_COHERENT, revue CodeRabbit PR #12).
    const std::uint64_t alignment = std::max<std::uint64_t>({requested, properties_.limits.nonCoherentAtomSize, 1});
    const auto alignUp = [alignment](std::uint64_t v) { return (v + alignment - 1) / alignment * alignment; };
    if (stagingBlocks_.empty() || alignUp(stagingBlocks_[currentBlock_].used) + size > stagingBlocks_[currentBlock_].staging.size)
    {
        // Bloc courant plein : le premier bloc que le GPU a fini de lire (dans l'ordre de l'anneau, le courant en dernier) repart de
        // son début ; sinon un bloc neuf.
        const Serial done = completedSerial();
        const std::size_t count = stagingBlocks_.size();
        std::size_t chosen = count;
        for (std::size_t k = 1; k <= count; ++k)
        {
            const std::size_t i = (currentBlock_ + k) % count;
            if (stagingBlocks_[i].serial <= done)
            {
                chosen = i;
                break;
            }
        }
        if (chosen == count)
        {
            auto staging = makeStaging(kStagingBlockSize);
            if (!staging)
                return std::unexpected(staging.error());
            setObjectName(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<std::uint64_t>(staging->buffer), "rhi staging ring");
            stagingBlocks_.push_back({*staging, 0, 0});
            ++counters_.stagingBlocks;
        }
        else if (count > kStagingRetainedBlocks)
        {
            // Après un pic : les blocs libres en trop sont détruits (le choisi reste).
            for (std::size_t i = count; i-- > 0 && stagingBlocks_.size() > kStagingRetainedBlocks;)
            {
                if (i == chosen || stagingBlocks_[i].serial > done)
                    continue;
                destroyStaging(stagingBlocks_[i].staging);
                stagingBlocks_.erase(stagingBlocks_.begin() + static_cast<std::ptrdiff_t>(i));
                if (chosen > i)
                    --chosen;
            }
        }
        currentBlock_ = chosen;
        stagingBlocks_[currentBlock_].used = 0;
    }
    StagingBlock &block = stagingBlocks_[currentBlock_];
    UploadSpace space;
    space.buffer = block.staging.buffer;
    space.offset = alignUp(block.used);
    space.mapped = static_cast<std::byte *>(block.staging.mapped) + space.offset;
    space.allocation = block.staging.allocation;
    space.allocationOffset = space.offset;
    block.used = space.offset + size;
    // Le lot ouvert (ou celui qui va s'ouvrir) part avec la prochaine soumission : le bloc est lu jusqu'à cette série.
    block.serial = lastSubmitted_ + 1;
    ++counters_.ringUploads;
    return space;
}

Result<VkCommandBuffer> VulkanDevice::stageUpload(std::span<const std::byte> data, std::uint64_t alignment, UploadSpace &space)
{
    const std::uint64_t size = data.size();
    if (size > kStagingDedicatedThreshold)
    {
        auto staging = makeStaging(size);
        if (!staging)
            return std::unexpected(staging.error());
        ++counters_.dedicatedStaging;
        space = {};
        space.buffer = staging->buffer;
        space.mapped = static_cast<std::byte *>(staging->mapped);
        space.allocation = staging->allocation;
        space.dedicated = true;
        space.staging = *staging;
    }
    else
    {
        auto ring = ringAllocate(size, alignment);
        if (!ring)
            return std::unexpected(ring.error());
        space = *ring;
    }
    std::memcpy(space.mapped, data.data(), size);
    vmaFlushAllocation(allocator_, space.allocation, space.allocationOffset, size);
    if (space.dedicated)
        return batch({space.staging}); // libéré avec le lot (ou tout de suite si le lot ne s'ouvre pas)
    batchBytes_ += size;
    return batch();
}

void VulkanDevice::destroyStagingRing() noexcept
{
    for (const StagingBlock &block : stagingBlocks_)
        destroyStaging(block.staging);
    stagingBlocks_.clear();
    currentBlock_ = 0;
}

// ── Destruction ──────────────────────────────────────────────────────────

void VulkanDevice::destroy(BufferHandle handle)
{
    auto buffer = buffers_.remove(handle);
    if (!buffer)
        return;
    queueRelease([this, b = *buffer]() { vmaDestroyBuffer(allocator_, b.buffer, b.allocation); });
}

void VulkanDevice::destroyTextureNow(TextureRes &texture) noexcept
{
    for (auto &[key, view] : texture.views)
        vkDestroyImageView(device_, view, nullptr);
    texture.views.clear();
    if (texture.allocation != nullptr)
        vmaDestroyImage(allocator_, texture.image, texture.allocation);
}

void VulkanDevice::destroy(TextureHandle handle)
{
    if (handle == backbuffer_)
        return; // l'image de fond appartient au périphérique
    auto texture = textures_.remove(handle);
    if (!texture)
        return;
    queueRelease([this, t = std::move(*texture)]() mutable { destroyTextureNow(t); });
}

void VulkanDevice::destroy(SamplerHandle handle)
{
    auto sampler = samplers_.remove(handle);
    if (!sampler)
        return;
    queueRelease([this, s = *sampler]() { vkDestroySampler(device_, s.sampler, nullptr); });
}

void VulkanDevice::destroy(ShaderHandle handle)
{
    auto shader = shaders_.remove(handle);
    if (!shader)
        return;
    queueRelease([this, s = *shader]() { vkDestroyShaderModule(device_, s.module, nullptr); });
}

void VulkanDevice::destroy(PipelineHandle handle)
{
    auto pipeline = pipelines_.remove(handle);
    if (!pipeline)
        return;
    queueRelease([this, p = *pipeline]() { vkDestroyPipeline(device_, p.pipeline, nullptr); }); // la disposition appartient au périphérique
}

void VulkanDevice::destroy(BindGroupHandle handle)
{
    auto group = bindGroups_.remove(handle);
    if (!group)
        return;
    queueRelease(
        [this, g = std::move(*group)]()
        {
            for (const auto &cached : g.sets)
                vkFreeDescriptorSets(device_, cached.pool, 1, &cached.set);
        });
}

void VulkanDevice::destroy(QueryPoolHandle handle)
{
    auto pool = queryPools_.remove(handle);
    if (!pool)
        return;
    queueRelease([this, q = *pool]() { vkDestroyQueryPool(device_, q.pool, nullptr); });
}

Result<TextureDesc> VulkanDevice::textureDesc(TextureHandle texture) const
{
    const TextureRes *t = textures_.get(texture);
    if (t == nullptr)
        return makeError(ErrorCode::kNotFound, "textureDesc: stale or null texture handle");
    return t->desc;
}

DeviceStats VulkanDevice::stats() const noexcept
{
    DeviceStats s;
    s.buffers = static_cast<std::uint32_t>(buffers_.size());
    // Le backbuffer est une ressource interne : il ne compte pas parmi les textures de l'utilisateur (comme OpenGL).
    s.textures = static_cast<std::uint32_t>(textures_.size()) - (backbuffer_ ? 1u : 0u);
    s.samplers = static_cast<std::uint32_t>(samplers_.size());
    s.shaders = static_cast<std::uint32_t>(shaders_.size());
    s.pipelines = static_cast<std::uint32_t>(pipelines_.size());
    s.bindGroups = static_cast<std::uint32_t>(bindGroups_.size());
    s.queryPools = static_cast<std::uint32_t>(queryPools_.size());
    // Ressources de l'utilisateur seulement (pas les tampons de transfert internes).
    s.pendingDestruction = static_cast<std::uint32_t>(std::count_if(pending_.begin(), pending_.end(), [](const Pending &p) { return p.waitFrames; }));
    return s;
}

Result<> VulkanDevice::waitIdleChecked()
{
    if (device_ == VK_NULL_HANDLE)
        return {};
    (void)flushPending();
    const VkResult idle = vkDeviceWaitIdle(device_);
    if (idle == VK_SUCCESS || idle == VK_ERROR_DEVICE_LOST)
    {
        // Périphérique perdu : plus aucun travail ne s'exécute, la libération est sûre. L'erreur est tout de même rendue.
        collectReleases(true);
        return idle == VK_SUCCESS ? Result<>{} : fromVk(idle, "vkDeviceWaitIdle");
    }
    // Inactivité NON confirmée (erreur de mémoire) : on ne libère rien que le GPU puisse encore lire.
    return fromVk(idle, "vkDeviceWaitIdle");
}

void VulkanDevice::waitIdle()
{
    if (auto r = waitIdleChecked(); !r)
        LOG_ERROR(1, "Vulkan waitIdle: ", r.error().message);
}

// ── Arrêt ─────────────────────────────────────────────────────────────

VulkanDevice::~VulkanDevice() { shutdown(); }

void VulkanDevice::shutdown() noexcept
{
    if (device_ != VK_NULL_HANDLE)
    {
        VkResult idle = vkDeviceWaitIdle(device_);
        if (idle != VK_SUCCESS && idle != VK_ERROR_DEVICE_LOST)
            idle = vkDeviceWaitIdle(device_); // erreur de mémoire passagère : un second essai
        if (idle == VK_SUCCESS || idle == VK_ERROR_DEVICE_LOST)
        {
            collectReleases(true);
            for (Pending &p : pending_)
                p.release();
        }
        else
        {
            // Inactivité jamais confirmée : on ne détruit pas ce que le GPU peut lire (fuite assumée, journalisée).
            LOG_ERROR(1, "Vulkan shutdown: vkDeviceWaitIdle failed (", static_cast<int>(idle), "), ", pending_.size(), " deferred release(s) leaked");
        }
        pending_.clear();
        // Lot de transferts jamais soumis (mise à jour suivie d'aucune soumission) : ses tampons de transfert partent ici, son
        // tampon de commandes avec la réserve.
        for (const Staging &s : batchStaging_)
            destroyStaging(s);
        batchStaging_.clear();
        batch_ = VK_NULL_HANDLE;
    }
    shutdownSwapchain(); // chaîne d'échange, image de fond (une texture du périphérique)
    if (device_ != VK_NULL_HANDLE)
    {
        // Ressources oubliées par l'appelant : libérées ici (le périphérique les possède encore).
        buffers_.forEach([this](BufferHandle, BufferRes &b) { vmaDestroyBuffer(allocator_, b.buffer, b.allocation); });
        textures_.forEach([this](TextureHandle, TextureRes &t) { destroyTextureNow(t); });
        samplers_.forEach([this](SamplerHandle, SamplerRes &s) { vkDestroySampler(device_, s.sampler, nullptr); });
        shaders_.forEach([this](ShaderHandle, ShaderRes &s) { vkDestroyShaderModule(device_, s.module, nullptr); });
        pipelines_.forEach([this](PipelineHandle, PipelineRes &p) { vkDestroyPipeline(device_, p.pipeline, nullptr); });
        queryPools_.forEach([this](QueryPoolHandle, QueryPoolRes &q) { vkDestroyQueryPool(device_, q.pool, nullptr); });
        // Les groupes de liaison disparaissent avec leurs réserves de descripteurs.
        shutdownPipelines(); // dispositions, cache de pipelines (enregistré sur disque)
        for (VkDescriptorPool pool : descriptorPools_)
            vkDestroyDescriptorPool(device_, pool, nullptr);
        descriptorPools_.clear();
        destroyStagingRing();
        destroyCommandPools();
        if (timeline_ != VK_NULL_HANDLE)
            vkDestroySemaphore(device_, timeline_, nullptr);
    }
    if (allocator_ != nullptr)
        vmaDestroyAllocator(allocator_);
    if (device_ != VK_NULL_HANDLE)
        vkDestroyDevice(device_, nullptr);
    if (windowed_ && counters_.directPresents + counters_.copiedPresents > 0)
        LOG_INFO("Vulkan present: ", counters_.directPresents, " frame(s) drawn into the swapchain image, ", counters_.copiedPresents,
                 " copied from the offscreen backbuffer");
    if (instance_ != VK_NULL_HANDLE)
    {
        if (messenger_ != VK_NULL_HANDLE)
        {
            // Ligne de bilan lue par `make demo-vulkan-check` : le nombre doit être 0 (validation de synchronisation comprise).
            LOG_INFO("Vulkan validation: ", validationErrors_.load(), " error/warning message(s) during the run");
            vkDestroyDebugUtilsMessengerEXT(instance_, messenger_, nullptr);
        }
        vkDestroyInstance(instance_, nullptr);
    }
    device_ = VK_NULL_HANDLE;
    instance_ = VK_NULL_HANDLE;
    allocator_ = nullptr;
}

} // namespace orosmo::rhi::vk
