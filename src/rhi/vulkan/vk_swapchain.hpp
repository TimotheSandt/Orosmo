// Surface et chaîne d'échange du backend Vulkan (privé). L'image de fond du RHI est une texture RGBA8 du périphérique (comme hors
// écran) ; `present()` la copie (blit) dans l'image de la chaîne d'échange puis présente : le format de la chaîne ne regarde pas
// le moteur (README.md, « Présentation »).
#pragma once

#include "vk_device.hpp"

namespace orosmo::rhi::vk
{

struct Swapchain
{
    void *window = nullptr; // GLFWwindow*, adopté
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR handle = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    std::vector<VkImage> images;
    std::vector<VkSemaphore> renderDone; // une par image de la chaîne
    std::vector<VkSemaphore> acquired;   // une par image CPU en vol (+1)
    std::uint32_t acquireCursor = 0;
    // Sémaphores de l'ancienne chaîne : `vkDeviceWaitIdle` ne garantit pas que le moteur de présentation a fini de les attendre
    // (sans VK_KHR_swapchain_maintenance1, aucune fence de présentation). Ils sont détruits après assez de présentations réussies
    // sur la chaîne neuve (`retiredPresents`), ou à l'arrêt.
    std::vector<VkSemaphore> retired;
    std::uint32_t retiredPresents = 0;
    bool dirty = true; // à reconstruire (taille, vsync, périmée)

    static Result<std::vector<const char *>> requiredInstanceExtensions();
    bool presentSupported(VkPhysicalDevice device, std::uint32_t queueFamily) const;
};

} // namespace orosmo::rhi::vk
