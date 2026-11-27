#include "vk_swapchain.hpp"

#include "vk_region.hpp"
#include "vk_sync.hpp"
#include "vk_translate.hpp"

#include <orosmo/core/log.hpp>
#include <orosmo/platform/window.hpp>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdlib>
#include <string_view>

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
    // Taille gardée par la Window (platform) : pas d'aller-retour avec le serveur d'affichage à chaque image.
    int w = 0;
    int h = 0;
    platform::framebufferSizeOf(window, w, h);
    return {static_cast<std::uint32_t>(std::max(w, 0)), static_cast<std::uint32_t>(std::max(h, 0))};
}

// Énumération WSI à deux appels (comptage puis remplissage) : relancée sur VK_INCOMPLETE, erreur propagée sinon (jamais de liste
// à moitié remplie).
template <typename T, typename Call>
Result<std::vector<T>> enumerateWsi(VulkanDevice &device, Call call, const char *what)
{
    std::vector<T> items;
    VkResult result = VK_SUCCESS;
    do
    {
        std::uint32_t count = 0;
        result = call(&count, static_cast<T *>(nullptr));
        if (result != VK_SUCCESS)
            break;
        items.assign(count, T{});
        result = call(&count, items.data());
        if (result == VK_SUCCESS)
            items.resize(count);
    } while (result == VK_INCOMPLETE);
    if (auto r = device.fromVk(result, what); !r)
        return std::unexpected(r.error());
    return items;
}

// Capacités que la présentation exige de la surface (copie par blit dans l'image de la chaîne, alpha annoncé).
Result<> checkSurfaceCapabilities(const VkSurfaceCapabilitiesKHR &caps)
{
    if ((caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0)
        return makeError(ErrorCode::kUnsupported, "Vulkan: the swapchain images cannot be a transfer destination");
    if (caps.supportedCompositeAlpha == 0)
        return makeError(ErrorCode::kUnsupported, "Vulkan: the surface advertises no composite alpha mode");
    return {};
}

// Format de la surface : seuls ceux qui acceptent le blit en destination ; UNORM RGBA/BGRA d'abord (la copie de l'image de fond ne
// doit rien convertir en sRGB), espace de couleur sRGB ensuite. `preferRgba` : RGBA8 UNORM avant tout (image de fond directe, G185).
Result<VkSurfaceFormatKHR> chooseSurfaceFormat(VulkanDevice &device, VkSurfaceKHR surface, bool preferRgba = false)
{
    auto formats = enumerateWsi<VkSurfaceFormatKHR>(
        device, [&](std::uint32_t *count, VkSurfaceFormatKHR *out) { return vkGetPhysicalDeviceSurfaceFormatsKHR(device.physicalDevice(), surface, count, out); },
        "vkGetPhysicalDeviceSurfaceFormatsKHR");
    if (!formats)
        return std::unexpected(formats.error());
    VkSurfaceFormatKHR chosen{};
    int best = -1;
    for (const VkSurfaceFormatKHR &f : *formats)
    {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(device.physicalDevice(), f.format, &props);
        if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) == 0)
            continue;
        int score = 0;
        if (f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            score += 10;
        if (f.format == VK_FORMAT_R8G8B8A8_UNORM || f.format == VK_FORMAT_B8G8R8A8_UNORM)
            score += 100;
        if (preferRgba && f.format == VK_FORMAT_R8G8B8A8_UNORM)
            score += 1000;
        if (score > best)
        {
            best = score;
            chosen = f;
        }
    }
    if (best < 0)
        return makeError(ErrorCode::kUnsupported, "Vulkan: the surface offers no format usable as a blit destination");
    return chosen;
}

} // namespace

// ── Image de fond ──────────────────────────────────────────────────────────

Result<> VulkanDevice::initSwapchain(const DeviceDesc &desc)
{
    // ENGINE_VK_DIRECT_SWAPCHAIN=0 : toujours la copie de l'image hors écran (comparaison, contournement d'un pilote).
    if (const char *direct = std::getenv("ENGINE_VK_DIRECT_SWAPCHAIN"); direct != nullptr && std::string_view(direct) == "0")
        directForcedOff_ = true;
    directAllowed_ = desc.directPresentation && !directForcedOff_;
    if (windowed_)
    {
        // Une surface qui ne sait pas recevoir la copie est refusée ici (le démarrage échoue, la démo peut basculer), pas au
        // premier `present()`. Les capacités ne dépendent pas de la taille : une fenêtre réduite ne gêne pas ce contrôle.
        VkSurfaceCapabilitiesKHR caps{};
        if (auto r = fromVk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice(), swapchain_->surface, &caps), "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"); !r)
            return r;
        if (auto r = checkSurfaceCapabilities(caps); !r)
            return r;
        if (auto format = chooseSurfaceFormat(*this, swapchain_->surface); !format)
            return std::unexpected(format.error());
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

// Usages de l'image de fond (initSwapchain) exigés des images de la chaîne pour qu'elles la remplacent (G185).
constexpr VkImageUsageFlags kDirectUsage =
    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

static Result<> buildSwapchain(VulkanDevice &device, Swapchain &sc, bool vsync, bool directAllowed);

Result<TextureHandle> VulkanDevice::beginFrame()
{
    // Attendre d'abord l'image qui occupait cet emplacement, puis libérer : une ressource détruite pendant l'enregistrement de
    // cette image (encodeur pas encore soumis, donc pas couverte par la série notée à la destruction) n'est libérée qu'une
    // fois l'image terminée (sinon : VUID-vkDestroyQueryPool-queryPool-00793 quand les minuteries GPU sont coupées).
    if (auto r = waitSerial(frameSerials_[frame_ % framesInFlight_]); !r)
        return std::unexpected(r.error());
    collectReleases();
    // Image de la chaîne déjà acquise par un beginFrame sans present (image sautée par l'application) : c'est encore l'image
    // courante (une seconde acquisition épuiserait la chaîne) ; un changement de taille attendra la présentation.
    if (windowed_ && acquiredImage_)
        return backbuffer_;
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
            swapchain_->dirty = true; // Wayland : `currentExtent` ne suit pas la fenêtre, la chaîne doit être refaite à la nouvelle taille
        }
        if (directAllowed_)
        {
            if (auto r = acquireDirect(); !r)
                return std::unexpected(r.error());
        }
    }
    return backbuffer_;
}

Result<> VulkanDevice::acquireDirect()
{
    Swapchain &sc = *swapchain_;
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        if (sc.dirty || sc.handle == VK_NULL_HANDLE)
        {
            if (auto r = rebuildSwapchain(); !r)
                return r.error().code == ErrorCode::kBusy ? Result<>{} : r; // surface vide : present() s'en occupe (copie)
        }
        // Chaîne sans images utilisables, ou d'une autre taille que la fenêtre (X11 : `currentExtent` en retard d'un événement) :
        // cette image passe par la copie (`present`).
        if (!sc.direct || sc.extent.width != backbufferSize_.width || sc.extent.height != backbufferSize_.height)
            return {};
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
        // L'image acquise prend la place de l'image de fond (même poignée) ; son contenu est indéfini : UNDEFINED -> repos à la
        // prochaine soumission, qui attend aussi l'acquisition (submitRaw).
        TextureRes *slot = textures_.get(backbuffer_);
        std::swap(*slot, directImages_[imageIndex]);
        slot->layoutInitialized = false;
        markPendingInitialization(backbuffer_);
        acquiredImage_ = imageIndex;
        acquireWait_ = acquired;
        return {};
    }
    return {}; // toujours périmée : copie à `present`
}

// Remet l'image hors écran dans l'emplacement de l'image de fond (après la présentation, ou à l'arrêt avec une image acquise).
void VulkanDevice::restoreOffscreenBackbuffer() noexcept
{
    if (!acquiredImage_)
        return;
    TextureRes *slot = textures_.get(backbuffer_);
    std::swap(*slot, directImages_[*acquiredImage_]);
    directImages_[*acquiredImage_].layoutInitialized = false;
    acquiredImage_.reset();
}

void VulkanDevice::releaseDirectImages() noexcept
{
    for (TextureRes &image : directImages_)
        destroyTextureNow(image); // vues seulement : l'image appartient à la chaîne (allocation nulle)
    directImages_.clear();
}

Result<> VulkanDevice::rebuildSwapchain()
{
    Swapchain &sc = *swapchain_;
    // Les vues des images de la chaîne ne sont libérées qu'une fois le GPU confirmé inactif (docs/03 §3.2) ; sinon l'erreur remonte
    // et elles restent en place.
    if (auto idle = waitIdleChecked(); !idle)
        return idle;
    releaseDirectImages();
    if (auto r = buildSwapchain(*this, sc, vsync_, directAllowed_); !r)
        return r;
    if (sc.direct)
    {
        const TextureRes *offscreen = textures_.get(backbuffer_);
        for (VkImage image : sc.images)
        {
            TextureRes res;
            res.image = image;
            res.allocation = nullptr; // appartient à la chaîne
            res.desc = offscreen->desc;
            res.desc.size = {sc.extent.width, sc.extent.height, 1};
            res.format = VK_FORMAT_R8G8B8A8_UNORM;
            res.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
            res.levels = 1;
            res.layers = 1;
            res.restLayout = offscreen->restLayout;
            res.layoutInitialized = false;
            directImages_.push_back(std::move(res));
        }
    }
    return {};
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

void VulkanDevice::setDirectPresentation(bool enabled)
{
    const bool allowed = enabled && !directForcedOff_;
    if (directAllowed_ == allowed)
        return;
    directAllowed_ = allowed;
    // Les usages des images de la chaîne changent à la reconstruction ; une image déjà acquise est d'abord présentée (`present`),
    // la reconstruction n'a lieu qu'à l'acquisition suivante.
    if (swapchain_)
        swapchain_->dirty = true;
}

// ── Chaîne d'échange ────────────────────────────────────────────────────────

namespace
{

void destroySemaphores(VulkanDevice &device, std::vector<VkSemaphore> &list)
{
    for (VkSemaphore s : list)
        vkDestroySemaphore(device.device(), s, nullptr);
    list.clear();
}

// `retire` vrai (reconstruction) : les sémaphores de présentation passent dans `retired`, détruits plus tard. Faux (arrêt, GPU
// arrêté) : détruits tout de suite.
void destroySwapchainObjects(VulkanDevice &device, Swapchain &sc, bool keepHandle)
{
    if (keepHandle)
    {
        sc.retired.insert(sc.retired.end(), sc.renderDone.begin(), sc.renderDone.end());
        sc.retired.insert(sc.retired.end(), sc.acquired.begin(), sc.acquired.end());
        sc.renderDone.clear();
        sc.acquired.clear();
        sc.retiredPresents = 0;
    }
    else
    {
        destroySemaphores(device, sc.renderDone);
        destroySemaphores(device, sc.acquired);
        destroySemaphores(device, sc.retired);
    }
    sc.images.clear();
    if (!keepHandle && sc.handle != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(device.device(), sc.handle, nullptr);
        sc.handle = VK_NULL_HANDLE;
    }
}

} // namespace

// Reconstruit la chaîne pour la taille courante du framebuffer. Renvoie kBusy si la fenêtre est réduite.
static Result<> buildSwapchain(VulkanDevice &device, Swapchain &sc, bool vsync, bool directAllowed)
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
    if (auto r = checkSurfaceCapabilities(caps); !r)
        return r;

    auto chosen = chooseSurfaceFormat(device, sc.surface, directAllowed);
    if (!chosen)
        return std::unexpected(chosen.error());
    const bool direct = directAllowed && chosen->format == VK_FORMAT_R8G8B8A8_UNORM && (caps.supportedUsageFlags & kDirectUsage) == kDirectUsage;

    auto modes = enumerateWsi<VkPresentModeKHR>(
        device, [&](std::uint32_t *count, VkPresentModeKHR *out) { return vkGetPhysicalDeviceSurfacePresentModesKHR(device.physicalDevice(), sc.surface, count, out); },
        "vkGetPhysicalDeviceSurfacePresentModesKHR");
    if (!modes)
        return std::unexpected(modes.error());
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR; // toujours disponible
    if (!vsync)
    {
        for (VkPresentModeKHR m : *modes)
        {
            if (m == VK_PRESENT_MODE_MAILBOX_KHR)
                mode = m;
            else if (m == VK_PRESENT_MODE_IMMEDIATE_KHR && mode != VK_PRESENT_MODE_MAILBOX_KHR)
                mode = m;
        }
    }

    // Mode alpha : opaque s'il est annoncé, sinon le premier annoncé (la valeur interdite par le masque rendrait la création invalide).
    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    for (VkCompositeAlphaFlagBitsKHR candidate : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                                                  VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR})
    {
        if ((caps.supportedCompositeAlpha & candidate) != 0)
        {
            alpha = candidate;
            break;
        }
    }

    std::uint32_t imageCount = std::max(caps.minImageCount + 1, 3u);
    if (caps.maxImageCount > 0)
        imageCount = std::min(imageCount, caps.maxImageCount);

    device.waitIdle(); // l'ancienne chaîne ne sert plus ; ses sémaphores sont mis de côté (`retired`), pas détruits
    VkSwapchainKHR old = sc.handle;
    destroySwapchainObjects(device, sc, true);

    auto info = makeStruct<VkSwapchainCreateInfoKHR>(VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);
    info.surface = sc.surface;
    info.minImageCount = imageCount;
    info.imageFormat = chosen->format;
    info.imageColorSpace = chosen->colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = direct ? kDirectUsage : static_cast<VkImageUsageFlags>(VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = alpha;
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

    // Tout ce qui suit est construit à part : la chaîne n'est activée (`sc.handle`, `dirty = false`) que si tout a réussi ;
    // sinon la chaîne partielle est détruite et la reconstruction reste à refaire à la présentation suivante.
    std::vector<VkImage> images;
    std::vector<VkSemaphore> renderDone;
    std::vector<VkSemaphore> acquired;
    auto fail = [&](const Error &error) -> Result<>
    {
        destroySemaphores(device, renderDone);
        destroySemaphores(device, acquired);
        vkDestroySwapchainKHR(device.device(), created, nullptr);
        return std::unexpected(error);
    };
    auto list = enumerateWsi<VkImage>(
        device, [&](std::uint32_t *count, VkImage *out) { return vkGetSwapchainImagesKHR(device.device(), created, count, out); }, "vkGetSwapchainImagesKHR");
    if (!list)
        return fail(list.error());
    images = std::move(*list);
    if (images.empty())
        return fail(makeError(ErrorCode::kUnknown, "swapchain: the driver returned no image").error());

    auto semaphoreInfo = makeStruct<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
    const auto createSemaphores = [&](std::vector<VkSemaphore> &out, std::size_t count) -> Result<>
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            VkSemaphore s = VK_NULL_HANDLE;
            if (auto r = device.fromVk(vkCreateSemaphore(device.device(), &semaphoreInfo, nullptr, &s), "vkCreateSemaphore"); !r)
                return r;
            out.push_back(s);
        }
        return {};
    };
    if (auto r = createSemaphores(renderDone, images.size()); !r)
        return fail(r.error());
    if (auto r = createSemaphores(acquired, device.framesInFlight() + 1); !r)
        return fail(r.error());

    sc.handle = created;
    sc.format = chosen->format;
    sc.extent = extent;
    sc.images = std::move(images);
    sc.renderDone = std::move(renderDone);
    sc.acquired = std::move(acquired);
    sc.dirty = false;
    sc.direct = direct;
    return {};
}

Result<> VulkanDevice::present()
{
    collectReleases();
    if (windowed_ && acquiredImage_)
        return presentDirect();
    if (windowed_)
    {
        Swapchain &sc = *swapchain_;
        TextureRes *source = textures_.get(backbuffer_);
        VkCommandBuffer cb = VK_NULL_HANDLE;
        // Un tampon non soumis (échec, chaîne périmée) retourne à sa réserve (remis à zéro avec elle, même en cours d'enregistrement).
        const auto giveBack = [&]
        {
            if (cb != VK_NULL_HANDLE)
            {
                releaseUnsubmitted(cb);
                cb = VK_NULL_HANDLE;
            }
        };
        // Après l'acquisition, un échec laisse une image acquise et jamais présentée : la chaîne est retirée (reconstruite à la
        // prochaine présentation, ce qui rend toutes ses images).
        const auto abandonChain = [&](Result<> error) -> Result<>
        {
            giveBack();
            sc.dirty = true;
            return error;
        };
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            if (sc.dirty || sc.handle == VK_NULL_HANDLE)
            {
                if (auto r = rebuildSwapchain(); !r)
                {
                    giveBack();
                    ++frame_;
                    return r.error().code == ErrorCode::kBusy ? Result<>{} : r; // fenêtre réduite : image perdue, sans erreur
                }
            }
            // Préparatifs faillibles avant l'acquisition : une fois l'image acquise, il ne reste que la soumission et la présentation.
            if (cb == VK_NULL_HANDLE)
            {
                if (auto r = flushPending(); !r)
                    return r;
                auto commandBuffer = acquireCommandBuffer();
                if (!commandBuffer)
                    return std::unexpected(commandBuffer.error());
                cb = *commandBuffer;
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
            {
                giveBack();
                return fromVk(acquire, "vkAcquireNextImageKHR");
            }
            if (acquire == VK_SUBOPTIMAL_KHR)
                sc.dirty = true; // utilisable cette fois, reconstruite à la suivante

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
                return abandonChain(r);
            SubmitSync sync;
            sync.waits.emplace_back(acquired, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
            sync.signals.push_back(sc.renderDone[imageIndex]);
            const VkCommandBuffer cbs[] = {cb};
            auto serial = submitCommandBuffers(cbs, sync);
            if (!serial)
                return abandonChain(std::unexpected(serial.error())); // non soumis : le tampon retourne à la réserve
            cb = VK_NULL_HANDLE;                                    // soumis : `submitRaw` l'a mis en retrait avec sa série
            ++counters_.copiedPresents;
            frameSerials_[frame_ % framesInFlight_] = *serial;
            ++frame_;
            rotateCommandPool(); // une réserve de tampons de commandes par image en vol

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
                return abandonChain(fromVk(shown, "vkQueuePresentKHR"));
            else if (!sc.retired.empty() && ++sc.retiredPresents >= sc.images.size() + framesInFlight_)
                destroySemaphores(*this, sc.retired); // assez de présentations depuis la reconstruction : le moteur n'attend plus ces sémaphores
            return {};
        }
        giveBack();
        return makeError(ErrorCode::kUnknown, "present: the swapchain stayed out of date after a rebuild");
    }
    if (auto r = flushPending(); !r)
        return r;
    frameSerials_[frame_ % framesInFlight_] = lastSubmitted_;
    ++frame_;
    rotateCommandPool(); // une réserve de tampons de commandes par image en vol
    return {};
}

// Image de fond = image acquise (G185) : passage en disposition de présentation, soumission (qui attend l'acquisition si aucune
// soumission de l'image ne l'a déjà fait), présentation, puis l'image hors écran reprend sa place.
Result<> VulkanDevice::presentDirect()
{
    Swapchain &sc = *swapchain_;
    const std::uint32_t imageIndex = *acquiredImage_;
    // Après la soumission ou son échec, l'image hors écran reprend sa place ; un échec laisse une image acquise et jamais
    // présentée : la chaîne est retirée (reconstruite à la prochaine image).
    const auto abandonChain = [&](Result<> error) -> Result<>
    {
        restoreOffscreenBackbuffer();
        acquireWait_ = VK_NULL_HANDLE;
        sc.dirty = true;
        return error;
    };
    auto commandBuffer = acquireCommandBuffer();
    if (!commandBuffer)
        return abandonChain(std::unexpected(commandBuffer.error()));
    const VkCommandBuffer cb = *commandBuffer;
    TextureRes *image = textures_.get(backbuffer_);
    const VkImageLayout from = image->layoutInitialized ? image->restLayout : VK_IMAGE_LAYOUT_UNDEFINED;
    transitionImage(cb, image->image, image->aspect, from, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    if (auto r = fromVk(vkEndCommandBuffer(cb), "vkEndCommandBuffer"); !r)
    {
        releaseUnsubmitted(cb);
        return abandonChain(r);
    }
    SubmitSync sync; // l'attente de l'acquisition est ajoutée par submitRaw si elle n'a pas encore été consommée
    sync.signals.push_back(sc.renderDone[imageIndex]);
    const VkCommandBuffer cbs[] = {cb};
    auto serial = submitCommandBuffers(cbs, sync);
    if (!serial)
    {
        releaseUnsubmitted(cb);
        return abandonChain(std::unexpected(serial.error()));
    }
    restoreOffscreenBackbuffer();
    ++counters_.directPresents;
    frameSerials_[frame_ % framesInFlight_] = *serial;
    ++frame_;
    rotateCommandPool(); // une réserve de tampons de commandes par image en vol

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
    {
        sc.dirty = true;
        return fromVk(shown, "vkQueuePresentKHR");
    }
    else if (!sc.retired.empty() && ++sc.retiredPresents >= sc.images.size() + framesInFlight_)
        destroySemaphores(*this, sc.retired);
    return {};
}

void VulkanDevice::shutdownSwapchain() noexcept
{
    if (swapchain_)
    {
        restoreOffscreenBackbuffer();
        releaseDirectImages();
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
