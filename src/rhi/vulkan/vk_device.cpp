#include "vk_device.hpp"

#include "vk_swapchain.hpp"
#include "vk_sync.hpp"
#include "vk_translate.hpp"

#include <orosmo/core/log.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <set>
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

    auto poolInfo = makeStruct<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamily_;
    if (auto r = fromVk(vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_), "vkCreateCommandPool"); !r)
        return r;

    fillCapabilities();
    if (auto r = initPipelines(); !r)
        return r;
    return initSwapchain(desc); // image de fond (hors écran : une texture RGBA8) et, pour une fenêtre, la chaîne d'échange
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

    const VkResult result = vkCreateInstance(&info, nullptr, &instance_);
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
    vkEnumeratePhysicalDevices(instance_, &count, devices.data());

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
            const bool index = std::all_of(w.begin(), w.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
            if ((index && std::stoul(w) == i) || (!index && name.find(w) != std::string::npos))
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
    if (hasExtension(exts, "VK_KHR_portability_subset"))
        extensions.push_back("VK_KHR_portability_subset"); // exigé par MoltenVK quand il est présent

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
    caps_.timestampQueries = l.timestampComputeAndGraphics != VK_FALSE && l.timestampPeriod > 0.0f;
    caps_.anisotropy = features_.samplerAnisotropy != VK_FALSE;
    caps_.bc7Textures = features_.textureCompressionBC != VK_FALSE;
    caps_.wireframe = features_.fillModeNonSolid != VK_FALSE;
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

void VulkanDevice::queueRelease(std::function<void()> release) { pending_.push_back({lastSubmitted_, std::move(release)}); }

void VulkanDevice::collectReleases()
{
    const Serial done = completedSerial();
    // Les tampons de commandes soumis jusqu'à `done` sont réutilisables.
    for (auto it = retiredCommandBuffers_.begin(); it != retiredCommandBuffers_.end();)
    {
        if (it->serial <= done)
        {
            vkResetCommandBuffer(it->commandBuffer, 0);
            freeCommandBuffers_.push_back(it->commandBuffer);
            it = retiredCommandBuffers_.erase(it);
        }
        else
        {
            ++it;
        }
    }
    // Les libérations peuvent en provoquer d'autres (jamais de nouvelle soumission) : on garde l'ordre de mise en file.
    std::vector<Pending> ready;
    for (auto it = pending_.begin(); it != pending_.end();)
    {
        if (it->serial <= done)
        {
            ready.push_back(std::move(*it));
            it = pending_.erase(it);
        }
        else
        {
            ++it;
        }
    }
    for (Pending &p : ready)
        p.release();
}

Result<VkCommandBuffer> VulkanDevice::acquireCommandBuffer()
{
    collectReleases();
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (!freeCommandBuffers_.empty())
    {
        commandBuffer = freeCommandBuffers_.back();
        freeCommandBuffers_.pop_back();
    }
    else
    {
        auto info = makeStruct<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
        info.commandPool = commandPool_;
        info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        info.commandBufferCount = 1;
        if (auto r = fromVk(vkAllocateCommandBuffers(device_, &info, &commandBuffer), "vkAllocateCommandBuffers"); !r)
            return std::unexpected(r.error());
    }
    auto begin = makeStruct<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (auto r = fromVk(vkBeginCommandBuffer(commandBuffer, &begin), "vkBeginCommandBuffer"); !r)
    {
        freeCommandBuffers_.push_back(commandBuffer);
        return std::unexpected(r.error());
    }
    return commandBuffer;
}

void VulkanDevice::retireCommandBuffer(VkCommandBuffer commandBuffer, Serial serial) { retiredCommandBuffers_.push_back({commandBuffer, serial}); }

Result<Serial> VulkanDevice::submitCommandBuffers(std::span<const VkCommandBuffer> commandBuffers, const SubmitSync &sync)
{
    if (auto r = flushPending(); !r)
        return std::unexpected(r.error());
    if (auto r = submitRaw(commandBuffers, sync); !r)
        return std::unexpected(r.error());
    return lastSubmitted_;
}

Result<> VulkanDevice::submitRaw(std::span<const VkCommandBuffer> commandBuffers, const SubmitSync &sync)
{
    std::vector<VkCommandBufferSubmitInfo> cbInfos;
    for (VkCommandBuffer cb : commandBuffers)
    {
        auto info = makeStruct<VkCommandBufferSubmitInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO);
        info.commandBuffer = cb;
        cbInfos.push_back(info);
    }
    std::vector<VkSemaphoreSubmitInfo> waits;
    for (const auto &[semaphore, stage] : sync.waits)
    {
        auto info = makeStruct<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
        info.semaphore = semaphore;
        info.stageMask = stage;
        waits.push_back(info);
    }
    std::vector<VkSemaphoreSubmitInfo> signals;
    auto timelineSignal = makeStruct<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
    timelineSignal.semaphore = timeline_;
    timelineSignal.value = lastSubmitted_ + 1;
    timelineSignal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    signals.push_back(timelineSignal);
    for (VkSemaphore semaphore : sync.signals)
    {
        auto info = makeStruct<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
        info.semaphore = semaphore;
        info.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        signals.push_back(info);
    }
    auto submit = makeStruct<VkSubmitInfo2>(VK_STRUCTURE_TYPE_SUBMIT_INFO_2);
    submit.waitSemaphoreInfoCount = static_cast<std::uint32_t>(waits.size());
    submit.pWaitSemaphoreInfos = waits.data();
    submit.commandBufferInfoCount = static_cast<std::uint32_t>(cbInfos.size());
    submit.pCommandBufferInfos = cbInfos.data();
    submit.signalSemaphoreInfoCount = static_cast<std::uint32_t>(signals.size());
    submit.pSignalSemaphoreInfos = signals.data();
    if (auto r = fromVk(vkQueueSubmit2(queue_, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit2"); !r)
        return r;
    ++lastSubmitted_;
    for (VkCommandBuffer cb : commandBuffers)
        retireCommandBuffer(cb, lastSubmitted_);
    return {};
}

Result<> VulkanDevice::immediate(const std::function<void(VkCommandBuffer)> &record)
{
    if (auto r = flushPending(); !r)
        return r;
    auto commandBuffer = acquireCommandBuffer();
    if (!commandBuffer)
        return std::unexpected(commandBuffer.error());
    record(*commandBuffer);
    if (auto r = fromVk(vkEndCommandBuffer(*commandBuffer), "vkEndCommandBuffer"); !r)
    {
        freeCommandBuffers_.push_back(*commandBuffer);
        return r;
    }
    const VkCommandBuffer cbs[] = {*commandBuffer};
    if (auto r = submitRaw(cbs, {}); !r)
        return r;
    return waitSerial(lastSubmitted_);
}

void VulkanDevice::markPendingInitialization(TextureHandle texture) { pendingInitialization_.push_back(texture); }

// Fait passer en disposition de repos les textures neuves (UNDEFINED) : leur première commande les suppose au repos.
void VulkanDevice::drainPendingInit(VkCommandBuffer commandBuffer)
{
    for (TextureHandle handle : pendingInitialization_)
    {
        TextureRes *texture = textures_.get(handle);
        if (texture == nullptr || texture->layoutInitialized)
            continue; // détruite avant sa première utilisation, ou déjà initialisée
        transitionImage(commandBuffer, texture->image, texture->aspect, VK_IMAGE_LAYOUT_UNDEFINED, texture->restLayout);
        texture->layoutInitialized = true;
    }
    pendingInitialization_.clear();
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

Result<> VulkanDevice::flushPending()
{
    if (batch_ == VK_NULL_HANDLE && pendingInitialization_.empty())
        return {};
    auto commandBuffer = batch();
    if (!commandBuffer)
        return std::unexpected(commandBuffer.error());
    const VkCommandBuffer cb = *commandBuffer;
    memoryBarrierAll(cb); // nos écritures sont visibles pour tout ce qui sera soumis après
    batch_ = VK_NULL_HANDLE;
    std::vector<Staging> staging = std::move(batchStaging_);
    batchStaging_.clear();
    batchBytes_ = 0;
    if (auto r = fromVk(vkEndCommandBuffer(cb), "vkEndCommandBuffer"); !r)
    {
        freeCommandBuffers_.push_back(cb);
        for (const Staging &s : staging)
            destroyStaging(s);
        return r;
    }
    const VkCommandBuffer cbs[] = {cb};
    if (auto r = submitRaw(cbs, {}); !r)
    {
        for (const Staging &s : staging)
            destroyStaging(s);
        return r;
    }
    if (!staging.empty())
        queueRelease([this, staging = std::move(staging)]() { for (const Staging &s : staging) destroyStaging(s); });
    return {};
}

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
        return makeError(ErrorCode::kInvalidArgument, "textureDesc: stale or null texture handle");
    return t->desc;
}

DeviceStats VulkanDevice::stats() const noexcept
{
    DeviceStats s;
    s.buffers = static_cast<std::uint32_t>(buffers_.size());
    s.textures = static_cast<std::uint32_t>(textures_.size());
    s.samplers = static_cast<std::uint32_t>(samplers_.size());
    s.shaders = static_cast<std::uint32_t>(shaders_.size());
    s.pipelines = static_cast<std::uint32_t>(pipelines_.size());
    s.bindGroups = static_cast<std::uint32_t>(bindGroups_.size());
    s.queryPools = static_cast<std::uint32_t>(queryPools_.size());
    s.pendingDestruction = static_cast<std::uint32_t>(pending_.size());
    return s;
}

void VulkanDevice::waitIdle()
{
    if (device_ == VK_NULL_HANDLE)
        return;
    (void)flushPending();
    vkDeviceWaitIdle(device_);
    collectReleases();
}

// ── Arrêt ─────────────────────────────────────────────────────────────

VulkanDevice::~VulkanDevice() { shutdown(); }

void VulkanDevice::shutdown() noexcept
{
    if (device_ != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(device_);
        collectReleases();
        for (Pending &p : pending_)
            p.release();
        pending_.clear();
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
        if (commandPool_ != VK_NULL_HANDLE)
            vkDestroyCommandPool(device_, commandPool_, nullptr); // libère aussi tous ses tampons de commandes
        if (timeline_ != VK_NULL_HANDLE)
            vkDestroySemaphore(device_, timeline_, nullptr);
    }
    if (allocator_ != nullptr)
        vmaDestroyAllocator(allocator_);
    if (device_ != VK_NULL_HANDLE)
        vkDestroyDevice(device_, nullptr);
    if (instance_ != VK_NULL_HANDLE)
    {
        if (messenger_ != VK_NULL_HANDLE)
            vkDestroyDebugUtilsMessengerEXT(instance_, messenger_, nullptr);
        vkDestroyInstance(instance_, nullptr);
    }
    device_ = VK_NULL_HANDLE;
    instance_ = VK_NULL_HANDLE;
    allocator_ = nullptr;
}

} // namespace orosmo::rhi::vk
