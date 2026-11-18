// RHI-08 : le périphérique OpenGL adopte une fenêtre créée par l'appelant (backbuffer = framebuffer par défaut, présentation,
// redimensionnement, vsync). Fenêtre invisible ; sans affichage ni pilote OpenGL 4.3, chaque test s'arrête avec un avertissement.
#include <doctest/doctest.h>

#include "support/test_support.hpp"
#include "rhi/gpu_fixture.hpp"

#include <engine/rhi/shader_compiler.hpp>

#include <GLFW/glfw3.h>

#include <chrono>
#include <thread>

namespace
{

// Fenêtre GLFW invisible créée comme le fera `platform::Window` : prepareWindowHints d'abord, ses propres attributs ensuite.
struct TestWindow
{
    GLFWwindow *handle = nullptr;

    explicit TestWindow(int width = 64, int height = 48, bool hints = true)
    {
        if (glfwInit() != GLFW_TRUE)
        {
            return;
        }
        glfwDefaultWindowHints();
        if (hints)
        {
            DeviceDesc desc;
            if (!prepareWindowHints(desc))
            {
                return;
            }
        }
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        handle = glfwCreateWindow(width, height, "rhi-window-test", nullptr, nullptr);
    }
    ~TestWindow()
    {
        if (handle != nullptr)
        {
            glfwDestroyWindow(handle);
        }
    }
    TestWindow(const TestWindow &) = delete;
    TestWindow &operator=(const TestWindow &) = delete;

    Extent2D framebufferSize() const
    {
        int w = 0, h = 0;
        glfwGetFramebufferSize(handle, &w, &h);
        return {static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h)};
    }

    std::unique_ptr<Device> device(bool vsync = false)
    {
        DeviceDesc desc;
        desc.surface.nativeWindow = handle;
        desc.vsync = vsync;
        auto created = createDevice(desc);
        if (!created)
        {
            MESSAGE("no windowed OpenGL 4.3 device: ", created.error().message);
            return nullptr;
        }
        return std::move(*created);
    }
};

constexpr const char *kFullscreen450Vs = R"(#version 450
void main() { vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2); gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); })";
constexpr const char *kBlueFs = R"(#version 450
layout(location = 0) out vec4 color;
void main() { color = vec4(0.0, 0.0, 1.0, 1.0); })";

} // namespace

TEST_SUITE("rhi/opengl/window")
{
    TEST_CASE("prepareWindowHints: OpenGL accepted, backends that are not compiled refused")
    {
        DeviceDesc desc;
        desc.backend = Backend::kVulkan;
        CHECK((prepareWindowHints(desc).error().code == ErrorCode::kUnsupported));
    }

    TEST_CASE("a window is adopted: the backbuffer has the framebuffer size and the window outlives the device")
    {
        TestWindow window;
        SKIP_TEST_IF_NULL(window.handle, "OpenGL 4.3 window");
        {
            auto device = window.device();
            SKIP_TEST_IF_NULL(device.get(), "windowed OpenGL 4.3 device");
            auto frame = device->beginFrame();
            REQUIRE(frame.has_value());
            auto pixels = device->readTexture(*frame);
            REQUIRE(pixels.has_value());
            const Extent2D size = window.framebufferSize();
            CHECK(pixels->size() == static_cast<std::size_t>(size.width) * size.height * 4);
            CHECK(device->stats().textures == 0); // le backbuffer n'est pas une ressource de l'utilisateur
            CHECK(device->validationErrorCount() == 0);
        }
        // Le périphérique détruit, la fenêtre reste valide et peut en adopter un autre.
        CHECK(glfwGetWindowAttrib(window.handle, GLFW_CONTEXT_VERSION_MAJOR) >= 4);
        auto second = window.device();
        SKIP_TEST_IF_NULL(second.get(), "second windowed device");
        CHECK(second->beginFrame().has_value());
    }

    TEST_CASE("rendering into the window backbuffer: clear, draw, read back, present, several frames")
    {
        TestWindow window;
        SKIP_TEST_IF_NULL(window.handle, "OpenGL 4.3 window");
        auto device = window.device();
        SKIP_TEST_IF_NULL(device.get(), "windowed OpenGL 4.3 device");
        auto vs = createShaderFromGlsl(*device, ShaderStage::kVertex, kFullscreen450Vs, {.name = "vs"});
        auto fs = createShaderFromGlsl(*device, ShaderStage::kFragment, kBlueFs, {.name = "fs"});
        REQUIRE(vs.has_value());
        REQUIRE(fs.has_value());
        GraphicsPipelineDesc pd;
        pd.vertex = *vs;
        pd.fragment = *fs;
        pd.colorTargets.assign(1, ColorTargetState{}); // pas de liste d'initialisation : faux positif -Warray-bounds de GCC 13 sous ASan (G34)
        pd.raster.cull = CullMode::kNone;
        auto pipe = device->createGraphicsPipeline(pd);
        REQUIRE(pipe.has_value());

        for (int frameIndex = 0; frameIndex < 3; ++frameIndex)
        {
            auto frame = device->beginFrame();
            REQUIRE(frame.has_value());
            const float red = static_cast<float>(frameIndex) * 0.5f;
            auto encoder = device->createCommandEncoder();
            RenderPassDesc pass;
            pass.colors = {{.texture = *frame, .clearColor = {red, 1.0f, 0.0f, 1.0f}}};
            pass.debugName = "window.pass";
            encoder->beginRenderPass(pass);
            if (frameIndex == 2)
            {
                encoder->setPipeline(*pipe);
                encoder->setScissor({0, 0, 8, 8});
                encoder->draw(3);
            }
            encoder->endRenderPass();
            REQUIRE(device->submit(std::move(encoder)).has_value());

            TextureCopy px;
            px.offset = {20, 20, 0};
            px.size = {1, 1, 1};
            auto a = device->readTexture(*frame, px);
            REQUIRE(a.has_value());
            const auto c = rgba(*a);
            CAPTURE(frameIndex);
            CAPTURE(static_cast<int>(c[0]));
            CAPTURE(static_cast<int>(c[1]));
            CHECK(near(c[0], static_cast<int>(red * 255.0f + 0.5f)));
            CHECK(c[1] == 255);
            CHECK(c[2] == 0);
            px.offset = {2, 2, 0};
            auto b = device->readTexture(*frame, px);
            REQUIRE(b.has_value());
            CHECK(rgba(*b)[2] == (frameIndex == 2 ? 255 : 0)); // le triangle bleu n'atteint que le coin découpé
            REQUIRE(device->present().has_value());
        }
        CHECK(device->validationErrorCount() == 0);
    }

    TEST_CASE("the backbuffer is the sole attachment of its pass and cannot be sampled, uploaded or mipmapped")
    {
        TestWindow window;
        SKIP_TEST_IF_NULL(window.handle, "OpenGL 4.3 window");
        auto device = window.device();
        SKIP_TEST_IF_NULL(device.get(), "windowed OpenGL 4.3 device");
        Gpu gpu;
        gpu.device = std::move(device);
        auto frame = gpu->beginFrame();
        REQUIRE(frame.has_value());
        const Extent2D size = window.framebufferSize();
        const TextureHandle other = gpu.target(Format::kRGBA8Unorm, size, "other");
        const TextureHandle depth = gpu.target(Format::kD32Float, size, "depth");

        auto withDepth = gpu->createCommandEncoder();
        RenderPassDesc pd;
        pd.colors = {{.texture = *frame}};
        pd.depth = DepthAttachment{.texture = depth};
        withDepth->beginRenderPass(pd);
        CHECK((gpu->submit(std::move(withDepth)).error().code == ErrorCode::kInvalidArgument));

        auto mrt = gpu->createCommandEncoder();
        RenderPassDesc md;
        md.colors = {{.texture = *frame}, {.texture = other}};
        mrt->beginRenderPass(md);
        CHECK((gpu->submit(std::move(mrt)).error().code == ErrorCode::kInvalidArgument));

        BindGroupDesc bd;
        auto sampler = gpu->createSampler({});
        REQUIRE(sampler.has_value());
        bd.entries = {{.binding = 0, .type = BindingType::kSampledTexture, .texture = *frame, .sampler = *sampler}};
        CHECK((gpu->createBindGroup(bd).error().code == ErrorCode::kInvalidArgument)); // pas d'usage Sampled

        std::vector<std::byte> data(static_cast<std::size_t>(size.width) * size.height * 4);
        CHECK((gpu->updateTexture(*frame, {}, data).error().code == ErrorCode::kInvalidArgument));
        auto mips = gpu->createCommandEncoder();
        mips->generateMips(*frame);
        CHECK_FALSE(gpu->submit(std::move(mips)).has_value());
        gpu->destroy(*frame); // appartient au périphérique : sans effet
        CHECK(gpu->beginFrame().has_value());
        gpu.expectNoValidationErrors();
    }

    TEST_CASE("resize re-reads the window framebuffer; vsync can be switched")
    {
        TestWindow window(64, 48);
        SKIP_TEST_IF_NULL(window.handle, "OpenGL 4.3 window");
        auto device = window.device(true);
        SKIP_TEST_IF_NULL(device.get(), "windowed OpenGL 4.3 device");
        glfwSetWindowSize(window.handle, 96, 72);
        // Le redimensionnement X11 est asynchrone : on attend qu'il soit appliqué (sans gestionnaire de fenêtres il peut ne
        // jamais l'être : le test compare alors à la taille stable courante).
        for (int i = 0; i < 200 && !(window.framebufferSize() == Extent2D{96, 72}); ++i)
        {
            glfwPollEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        REQUIRE(device->resize({1, 1}).has_value()); // l'argument est ignoré pour une fenêtre
        auto frame = device->beginFrame();
        REQUIRE(frame.has_value());
        auto pixels = device->readTexture(*frame);
        REQUIRE(pixels.has_value());
        const Extent2D size = window.framebufferSize();
        CHECK(pixels->size() == static_cast<std::size_t>(size.width) * size.height * 4);
        device->setVSync(false);
        device->setVSync(true);
        REQUIRE(device->present().has_value());
        CHECK(device->validationErrorCount() == 0);
    }
}
