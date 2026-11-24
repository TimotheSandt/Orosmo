// Chaîne d'échange Vulkan (src/rhi/vulkan/vk_swapchain.cpp) : la seule partie du backend qui exige une fenêtre. Fenêtre
// invisible ; sans affichage ni périphérique Vulkan 1.3 capable de présenter, chaque test s'arrête avec un avertissement.
#include <doctest/doctest.h>

#include "support/test_support.hpp"

#include <orosmo/rhi/device.hpp>

#include <GLFW/glfw3.h>

#include <chrono>
#include <memory>
#include <thread>

using namespace orosmo::rhi;

namespace
{

struct VulkanWindow
{
    GLFWwindow *handle = nullptr;

    VulkanWindow()
    {
        if (glfwInit() != GLFW_TRUE || !isBackendCompiled(Backend::kVulkan))
            return;
        glfwDefaultWindowHints();
        DeviceDesc desc;
        desc.backend = Backend::kVulkan;
        if (!prepareWindowHints(desc))
            return;
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        handle = glfwCreateWindow(64, 48, "rhi-vulkan-window-test", nullptr, nullptr);
    }
    ~VulkanWindow()
    {
        if (handle != nullptr)
            glfwDestroyWindow(handle);
    }
    VulkanWindow(const VulkanWindow &) = delete;
    VulkanWindow &operator=(const VulkanWindow &) = delete;

    std::unique_ptr<Device> device(bool vsync)
    {
        DeviceDesc desc;
        desc.backend = Backend::kVulkan;
        desc.surface.nativeWindow = handle;
        desc.vsync = vsync;
        auto created = createDevice(desc);
        if (!created)
        {
            MESSAGE("no windowed Vulkan 1.3 device: ", created.error().message);
            return nullptr;
        }
        return std::move(*created);
    }
};

// Une image : efface l'image de fond et présente.
bool renderAndPresent(Device &device)
{
    auto frame = device.beginFrame();
    if (!frame)
        return false;
    auto encoder = device.createCommandEncoder();
    RenderPassDesc pass;
    pass.colors = {{.texture = *frame, .clearColor = {0.2f, 0.4f, 0.6f, 1.0f}}};
    pass.debugName = "vkwindow.pass";
    encoder->beginRenderPass(pass);
    encoder->endRenderPass();
    return device.submit(std::move(encoder)).has_value() && device.present().has_value();
}

} // namespace

TEST_SUITE("rhi/vulkan/window")
{
    TEST_CASE("a windowed Vulkan device presents several frames through the swapchain without a validation message")
    {
        VulkanWindow window;
        SKIP_TEST_IF_NULL(window.handle, "window able to host a Vulkan surface");
        auto device = window.device(true);
        SKIP_TEST_IF_NULL(device.get(), "windowed Vulkan 1.3 device");
        for (int i = 0; i < 6; ++i)
            CHECK(renderAndPresent(*device));
        CHECK(device->validationErrorCount() == 0);
    }

    TEST_CASE("the swapchain follows a window resize and a vsync change")
    {
        VulkanWindow window;
        SKIP_TEST_IF_NULL(window.handle, "window able to host a Vulkan surface");
        auto device = window.device(false);
        SKIP_TEST_IF_NULL(device.get(), "windowed Vulkan 1.3 device");
        CHECK(renderAndPresent(*device));
        glfwSetWindowSize(window.handle, 96, 72);
        for (int i = 0; i < 20; ++i) // le gestionnaire de fenêtres applique la taille de façon asynchrone
        {
            glfwPollEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        for (int i = 0; i < 3; ++i)
            CHECK(renderAndPresent(*device));
        device->setVSync(true);
        for (int i = 0; i < 3; ++i)
            CHECK(renderAndPresent(*device));
        CHECK(device->validationErrorCount() == 0);
    }

    TEST_CASE("G185: beginFrame twice without present keeps the same image; the backbuffer reads back what was drawn")
    {
        // Image de fond directe (image de la chaîne, surfaces RGBA8 : Wayland) ou copiée (X11) : même contrat. Avec une image
        // acquise, un second beginFrame ne doit pas en acquérir une autre (la chaîne s'épuiserait) ; la lecture de l'image de fond
        // avant present rend le dessin.
        VulkanWindow window;
        SKIP_TEST_IF_NULL(window.handle, "window able to host a Vulkan surface");
        auto device = window.device(false);
        SKIP_TEST_IF_NULL(device.get(), "windowed Vulkan 1.3 device");
        for (int i = 0; i < 5; ++i)
        {
            auto first = device->beginFrame();
            REQUIRE(first.has_value());
            auto again = device->beginFrame(); // image sautée par l'application : toujours la même
            REQUIRE(again.has_value());
            CHECK(*again == *first);
            auto encoder = device->createCommandEncoder();
            RenderPassDesc pass;
            pass.colors = {{.texture = *again, .clearColor = {1.0f, 0.0f, 0.0f, 1.0f}}};
            encoder->beginRenderPass(pass);
            encoder->endRenderPass();
            REQUIRE(device->submit(std::move(encoder)).has_value());
            auto desc = device->textureDesc(*again);
            REQUIRE(desc.has_value());
            TextureCopy corner;
            corner.size = {1, 1, 1};
            auto pixel = device->readTexture(*again, corner);
            REQUIRE(pixel.has_value());
            REQUIRE(pixel->size() == 4);
            CHECK(static_cast<int>((*pixel)[0]) == 255);
            CHECK(static_cast<int>((*pixel)[1]) == 0);
            CHECK((desc->format == Format::kRGBA8Unorm));
            CHECK(device->present().has_value());
        }
        CHECK(device->validationErrorCount() == 0);
    }
}
