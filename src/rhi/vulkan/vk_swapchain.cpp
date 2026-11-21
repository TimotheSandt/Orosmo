#include "vk_swapchain.hpp"

#include "vk_region.hpp"
#include "vk_sync.hpp"
#include "vk_translate.hpp"

#include <orosmo/core/log.hpp>

#include <GLFW/glfw3.h>

#include <algorithm>

namespace orosmo::rhi::vk
{

using orosmo::core::ErrorCode;
using orosmo::core::makeError;

void applyWindowHints(const DeviceDesc &)
{
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // pas de contexte OpenGL : la surface est celle de Vulkan
}

Result<std::vector<const char *>> Swapchain::requiredInstanceExtensions()
{
    if (glfwVulkanSupported() != GLFW_TRUE)
        return makeError(ErrorCode::kUnsupported, "Vulkan: GLFW reports no Vulkan loader or window system support (glfwVulkanSupported)");
    std::uint32_t count = 0;
    const char **names = glfwGetRequiredInstanceExtensions(&count);
    if (names == nullptr || count == 0)
        return makeError(ErrorCode::kUnsupported, "Vulkan: the window system offers no surface extension");
    return std::vector<const char *>(names, names + count);
}

bool Swapchain::presentSupported(VkPhysicalDevice device, std::uint32_t queueFamily) const
{
    VkBool32 supported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(device, queueFamily, surface, &supported);
    return supported == VK_TRUE;
}

// ── Surface ──────────────────────────────────────────────────────────────

Result<> VulkanDevice::createSurface(const DeviceDesc &desc)
{
    if (desc.surface.nativeWindow == nullptr)
        return {};
    swapchain_ = std::make_unique<Swapchain>();
    swapchain_->window = desc.surface.nativeWindow;
    const VkResult result = glfwCreateWindowSurface(instance_, static_cast<GLFWwindow *>(swapchain_->window), nullptr, &swapchain_->surface);
    if (result != VK_SUCCESS)
    {
        swapchain_.reset();
        return makeError(ErrorCode::kUnsupported, "Vulkan: cannot create the window surface (was the window created after prepareWindowHints with GLFW_NO_API?): " +
                                                      std::to_string(static_cast<int>(result)));
    }
    return {};
}

namespace
{

Extent2D framebufferSizeOf(void *window)
{
    int w = 0;
    int h = 0;
    glfwGetFramebufferSize(static_cast<GLFWwindow *>(window), &w, &h);
    return {static_cast<std::uint32_t>(std::max(w, 0)), static_cast<std::uint32_t>(std::max(h, 0))};
}

} // namespace

// ── Image de fond ──────────────────────────────────────────────────────────

Result<> VulkanDevice::initSwapchain(const DeviceDesc &desc)
{
    if (windowed_)
    {
        const Extent2D size = framebufferSizeOf(swapchain_->window);
        backbufferSize_ = {std::max(1u, size.width), std::max(1u, size.height)};
    }
    else
    {
        backbufferSize_ = {std::max(1u, desc.surface.size.width), std::max(1u, desc.surface.size.height)};
    }
    TextureDesc backbuffer;
    backbuffer.format = Format::kRGBA8Unorm;
    backbuffer.size = {backbufferSize_.width, backbufferSize_.height, 1};
    backbuffer.usage = TextureUsage::kRenderTarget | TextureUsage::kSampled | TextureUsage::kCopySrc | TextureUsage::kCopyDst;
    backbuffer.debugName = "backbuffer";
    auto texture = createTexture(backbuffer);
    if (!texture)
        return std::unexpected(texture.error());
    backbuffer_ = *texture;
    return {};
}

namespace
{

// Remplace l'image de fond par une neuve de cette taille SANS changer son handle (comme le backbuffer OpenGL).
Result<> recreateBackbuffer(VulkanDevice &device, TextureHandle handle, Extent2D size, TextureRes *&current)
{
    TextureDesc desc = device.get(handle)->desc;
    desc.size = {size.width, size.height, 1};
    auto fresh = device.createTexture(desc); // handle provisoire ; ajouter une texture peut déplacer la table : les pointeurs sont pris après
    if (!fresh)
        return std::unexpected(fresh.error());
    TextureRes *old = device.get(handle);
    TextureRes *created = device.get(*fresh);
    std::swap(*old, *created); // `old` reçoit l'image neuve, `created` (le handle provisoire) l'ancienne
    old->layoutInitialized = false;
    device.markPendingInitialization(handle);
    created->layoutInitialized = true; // l'ancienne image est dans sa disposition de repos
    device.destroy(*fresh);            // libère l'ancienne image après les images en vol
    current = old;
    return {};
}

} // namespace

Result<TextureHandle> VulkanDevice::beginFrame()
{
    collectReleases();
    if (auto r = waitSerial(frameSerials_[frame_ % kFramesInFlight]); !r)
        return std::unexpected(r.error());
    if (windowed_)
    {
        const Extent2D size = framebufferSizeOf(swapchain_->window);
        if (size.width == 0 || size.height == 0)
            return makeError(ErrorCode::kBusy, "beginFrame: the window is minimized (zero-size framebuffer)");
        if (size != backbufferSize_)
        {
            TextureRes *current = nullptr;
            if (auto r = recreateBackbuffer(*this, backbuffer_, size, current); !r)
                return std::unexpected(r.error());
            backbufferSize_ = size;
        }
    }
    return backbuffer_;
}

Result<> VulkanDevice::resize(Extent2D size)
{
    if (windowed_)
        return {}; // la taille vient de la fenêtre (relue par beginFrame)
    if (size.width == 0 || size.height == 0)
        return makeError(ErrorCode::kInvalidArgument, "resize: empty size");
    if (size == backbufferSize_)
        return {};
    TextureRes *current = nullptr;
    if (auto r = recreateBackbuffer(*this, backbuffer_, size, current); !r)
        return r;
    backbufferSize_ = size;
    return {};
}

void VulkanDevice::setVSync(bool enabled)
{
    if (vsync_ == enabled)
        return;
    vsync_ = enabled;
    if (swapchain_)
        swapchain_->dirty = true; // le mode de présentation change à la reconstruction
}

// ── Chaîne d'échange ────────────────────────────────────────────────────────

namespace
{

void destroySwapchainObjects(VulkanDevice &device, Swapchain &sc, bool keepHandle)
{
    for (VkSemaphore s : sc.renderDone)
        vkDestroySemaphore(device.device(), s, nullptr);
    for (VkSemaphore s : sc.acquired)
        vkDestroySemaphore(device.device(), s, nullptr);
    sc.renderDone.clear();
    sc.acquired.clear();
    sc.images.clear();
    if (!keepHandle && sc.handle != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(device.device(), sc.handle, nullptr);
        sc.handle = VK_NULL_HANDLE;
    }
}

} // namespace

// Reconstruit la chaîne pour la taille courante du framebuffer. Renvoie kBusy si la fenêtre est réduite.
static Result<> buildSwapchain(VulkanDevice &device, Swapchain &sc, bool vsync)
{
    VkSurfaceCapabilitiesKHR caps{};
    if (auto r = device.fromVk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device.physicalDevice(), sc.surface, &caps), "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"); !r)
        return r;
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX)
    {
        const Extent2D fb = framebufferSizeOf(sc.window);
        extent.width = std::clamp(fb.width, caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = std::clamp(fb.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (extent.width == 0 || extent.height == 0)
        return makeError(ErrorCode::kBusy, "swapchain: zero-size surface");
    if ((caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0)
        return makeError(ErrorCode::kUnsupported, "Vulkan: the swapchain images cannot be a transfer destination");

    std::uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device.physicalDevice(), sc.surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(device.physicalDevice(), sc.surface, &formatCount, formats.data());
    if (formats.empty())
        return makeError(ErrorCode::kUnsupported, "Vulkan: the surface offers no format");
    // UNORM d'abord (la copie de l'image de fond ne doit rien convertir en sRGB), BGRA ou RGBA.
    VkSurfaceFormatKHR chosen = formats[0];
    int best = -1;
    for (const VkSurfaceFormatKHR &f : formats)
    {
        int score = 0;
        if (f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            score += 10;
        if (f.format == VK_FORMAT_R8G8B8A8_UNORM || f.format == VK_FORMAT_B8G8R8A8_UNORM)
            score += 100;
        if (score > best)
        {
            best = score;
            chosen = f;
        }
    }

    std::uint32_t modeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device.physicalDevice(), sc.surface, &modeCount, nullptr);
    std::vector<VkPresentModeKHR> modes(modeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(device.physicalDevice(), sc.surface, &modeCount, modes.data());
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR; // toujours disponible
    if (!vsync)
    {
        for (VkPresentModeKHR m : modes)
        {
            if (m == VK_PRESENT_MODE_MAILBOX_KHR)
                mode = m;
            else if (m == VK_PRESENT_MODE_IMMEDIATE_KHR && mode != VK_PRESENT_MODE_MAILBOX_KHR)
                mode = m;
        }
    }

    std::uint32_t imageCount = std::max(caps.minImageCount + 1, 3u);
    if (caps.maxImageCount > 0)
        imageCount = std::min(imageCount, caps.maxImageCount);

    device.waitIdle(); // l'ancienne chaîne et ses sémaphores ne servent plus
    VkSwapchainKHR old = sc.handle;
    destroySwapchainObjects(device, sc, true);

    auto info = makeStruct<VkSwapchainCreateInfoKHR>(VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);
    info.surface = sc.surface;
    info.minImageCount = imageCount;
    info.imageFormat = chosen.format;
    info.imageColorSpace = chosen.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) != 0 ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                                                                                                      : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    info.presentMode = mode;
    info.clipped = VK_TRUE;
    info.oldSwapchain = old;
    VkSwapchainKHR created = VK_NULL_HANDLE;
    const VkResult result = vkCreateSwapchainKHR(device.device(), &info, nullptr, &created);
    if (old != VK_NULL_HANDLE)
        vkDestroySwapchainKHR(device.device(), old, nullptr);
    sc.handle = VK_NULL_HANDLE;
    if (auto r = device.fromVk(result, "vkCreateSwapchainKHR"); !r)
        return r;
    sc.handle = created;
    sc.format = chosen.format;
    sc.extent = extent;
    std::uint32_t count = 0;
    vkGetSwapchainImagesKHR(device.device(), sc.handle, &count, nullptr);
    sc.images.resize(count);
    vkGetSwapchainImagesKHR(device.device(), sc.handle, &count, sc.images.data());

    auto semaphoreInfo = makeStruct<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
    for (std::uint32_t i = 0; i < count; ++i)
    {
        VkSemaphore s = VK_NULL_HANDLE;
        vkCreateSemaphore(device.device(), &semaphoreInfo, nullptr, &s);
        sc.renderDone.push_back(s);
    }
    for (std::uint32_t i = 0; i < kFramesInFlight + 1; ++i)
    {
        VkSemaphore s = VK_NULL_HANDLE;
        vkCreateSemaphore(device.device(), &semaphoreInfo, nullptr, &s);
        sc.acquired.push_back(s);
    }
    sc.dirty = false;
    return {};
}

Result<> VulkanDevice::present()
{
    collectReleases();
    if (windowed_)
    {
        Swapchain &sc = *swapchain_;
        TextureRes *source = textures_.get(backbuffer_);
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            if (sc.dirty || sc.handle == VK_NULL_HANDLE)
            {
                if (auto r = buildSwapchain(*this, sc, vsync_); !r)
                {
                    ++frame_;
                    return r.error().code == ErrorCode::kBusy ? Result<>{} : r; // fenêtre réduite : image perdue, sans erreur
                }
            }
            VkSemaphore acquired = sc.acquired[sc.acquireCursor++ % sc.acquired.size()];
            std::uint32_t imageIndex = 0;
            const VkResult acquire = vkAcquireNextImageKHR(device_, sc.handle, UINT64_MAX, acquired, VK_NULL_HANDLE, &imageIndex);
            if (acquire == VK_ERROR_OUT_OF_DATE_KHR)
            {
                sc.dirty = true;
                continue;
            }
            if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR)
                return fromVk(acquire, "vkAcquireNextImageKHR");
            if (acquire == VK_SUBOPTIMAL_KHR)
                sc.dirty = true; // utilisable cette fois, reconstruite à la suivante

            if (auto r = flushPending(); !r)
                return r;
            auto commandBuffer = acquireCommandBuffer();
            if (!commandBuffer)
                return std::unexpected(commandBuffer.error());
            const VkCommandBuffer cb = *commandBuffer;
            const VkImage target = sc.images[imageIndex];
            // Copie l'image de fond dans l'image de la chaîne (conversion de format et mise à l'échelle par le blit), puis présente.
            transitionImage(cb, source->image, source->aspect, source->restLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            transitionImage(cb, target, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkImageBlit blit{};
            blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            blit.srcOffsets[1] = {static_cast<std::int32_t>(source->desc.size.width), static_cast<std::int32_t>(source->desc.size.height), 1};
            blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            blit.dstOffsets[1] = {static_cast<std::int32_t>(sc.extent.width), static_cast<std::int32_t>(sc.extent.height), 1};
            vkCmdBlitImage(cb, source->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
            transitionImage(cb, target, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
            transitionImage(cb, source->image, source->aspect, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, source->restLayout);
            if (auto r = fromVk(vkEndCommandBuffer(cb), "vkEndCommandBuffer"); !r)
                return r;
            SubmitSync sync;
            sync.waits.emplace_back(acquired, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
            sync.signals.push_back(sc.renderDone[imageIndex]);
            const VkCommandBuffer cbs[] = {cb};
            auto serial = submitCommandBuffers(cbs, sync);
            if (!serial)
                return std::unexpected(serial.error());

            auto present = makeStruct<VkPresentInfoKHR>(VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);
            present.waitSemaphoreCount = 1;
            present.pWaitSemaphores = &sc.renderDone[imageIndex];
            present.swapchainCount = 1;
            present.pSwapchains = &sc.handle;
            present.pImageIndices = &imageIndex;
            const VkResult shown = vkQueuePresentKHR(queue_, &present);
            if (shown == VK_ERROR_OUT_OF_DATE_KHR || shown == VK_SUBOPTIMAL_KHR)
                sc.dirty = true;
            else if (shown != VK_SUCCESS)
                return fromVk(shown, "vkQueuePresentKHR");
            frameSerials_[frame_ % kFramesInFlight] = *serial;
            ++frame_;
            return {};
        }
        return makeError(ErrorCode::kUnknown, "present: the swapchain stayed out of date after a rebuild");
    }
    if (auto r = flushPending(); !r)
        return r;
    frameSerials_[frame_ % kFramesInFlight] = lastSubmitted_;
    ++frame_;
    return {};
}

void VulkanDevice::shutdownSwapchain() noexcept
{
    if (swapchain_)
    {
        destroySwapchainObjects(*this, *swapchain_, false);
        if (swapchain_->surface != VK_NULL_HANDLE)
            vkDestroySurfaceKHR(instance_, swapchain_->surface, nullptr);
        swapchain_.reset();
    }
    if (device_ != VK_NULL_HANDLE && backbuffer_)
    {
        if (auto texture = textures_.remove(backbuffer_))
            destroyTextureNow(*texture);
        backbuffer_ = {};
    }
}

} // namespace orosmo::rhi::vk
