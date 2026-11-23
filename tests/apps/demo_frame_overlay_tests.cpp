// Perf M5 (point 6) : composition de l'image de la démo (apps/demo/src/Game/frame_overlay.*). Sans capture et à la taille du
// backbuffer, la scène est copiée directement dans le backbuffer et l'interface s'y pose : pas de cible « composed » ni de passe
// « present » (une copie plein écran de moins par image), pour la même image. Choix pur sans GPU ; graphe et pixels sur chaque backend.
#include <doctest/doctest.h>

#include "support/test_support.hpp"
#include "renderer/repo_paths.hpp"
#include "rhi/gpu_fixture.hpp"

#include "Game/frame_overlay.hpp"

#include <orosmo/renderer/camera.hpp>
#include <orosmo/renderer/fullscreen_blit.hpp>
#include <orosmo/renderer/pipeline_cache.hpp>
#include <orosmo/renderer/render_target.hpp>
#include <orosmo/renderer/scene_renderer.hpp>
#include <orosmo/renderer/shader_library.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace rd = orosmo::renderer;

namespace
{
std::filesystem::path shaderDir() { return tests::repoRoot(__FILE__) / "shaders" / "rhi"; }

std::size_t countOf(const std::vector<std::string> &order, const std::string &pass)
{
    return static_cast<std::size_t>(std::ranges::count(order, pass));
}
} // namespace

TEST_SUITE("apps/demo")
{
    TEST_CASE("frame composition: the backbuffer unless the composed image is read back, or the view differs from the backbuffer")
    {
        using demo::FrameComposition;
        const Extent2D view{1280, 720};
        CHECK(demo::chooseFrameComposition({.view = view, .backbuffer = view, .backbufferFormat = Format::kRGBA8Unorm}) ==
              FrameComposition::kBackbuffer);
        // Capture de cette image : ui.png lit la cible « composed ».
        CHECK(demo::chooseFrameComposition({.view = view, .backbuffer = view, .backbufferFormat = Format::kRGBA8Unorm, .keepComposed = true}) ==
              FrameComposition::kComposedTarget);
        // Vue d'une autre taille que le backbuffer (capture fenêtrée, --size) : la copie « present » étire l'image.
        CHECK(demo::chooseFrameComposition({.view = view, .backbuffer = {1920, 1080}, .backbufferFormat = Format::kRGBA8Unorm}) ==
              FrameComposition::kComposedTarget);
        CHECK(demo::chooseFrameComposition({.view = view, .backbuffer = view, .backbufferFormat = Format::kRGBA16Float}) ==
              FrameComposition::kComposedTarget);
        CHECK(demo::chooseFrameComposition({.view = {}, .backbuffer = {}, .backbufferFormat = Format::kRGBA8Unorm}) ==
              FrameComposition::kComposedTarget);
    }
}

TEST_SUITE("renderer/demo-frame")
{
    TEST_CASE("demo frame: composed in the backbuffer, one full-screen copy (no 'present' pass), the same pixels as through the composed target")
    {
        FOR_EACH_BACKEND(backend)
        {
            constexpr std::uint32_t kW = 32;
            constexpr std::uint32_t kH = 16;
            Gpu gpu(backend, {kW, kH});
            SKIP_TEST_IF_NULL(gpu.device.get(), "device");
            {
                rd::ShaderLibrary library(*gpu.device);
                rd::PipelineCache pipelines(*gpu.device);
                auto created = rd::SceneRenderer::create(*gpu.device, library, pipelines, {.shaderDirectory = shaderDir()});
                INFO((created ? std::string() : created.error().message));
                REQUIRE(created.has_value());
                std::unique_ptr<rd::SceneRenderer> renderer = std::move(*created);
                rd::FullscreenBlit &blit = renderer->blit();

                // « Interface » : un dégradé 2x2 prémultiplié semi-transparent étiré sur toute la sortie (échantillonnage linéaire).
                TextureDesc uiDesc;
                uiDesc.format = Format::kRGBA8Unorm;
                uiDesc.size = {2, 2, 1};
                uiDesc.usage = TextureUsage::kSampled | TextureUsage::kCopyDst;
                uiDesc.debugName = "test.ui";
                auto uiTexture = gpu.device->createTexture(uiDesc);
                REQUIRE(uiTexture.has_value());
                const std::vector<std::uint8_t> uiPixels = {200, 0, 0, 200, 0, 100, 0, 128, 0, 0, 60, 60, 30, 30, 30, 255};
                REQUIRE(gpu.device->updateTexture(*uiTexture, {.size = {2, 2, 1}}, std::as_bytes(std::span(uiPixels))).has_value());

                TextureHandle backbuffer;
                auto added = renderer->addFeature(std::make_unique<demo::FrameOverlay>(
                    [&](CommandEncoder &e, TextureHandle target)
                    { REQUIRE(blit.compose(e, *uiTexture, blit.linearSampler(), target, Format::kRGBA8Unorm, "test.ui").has_value()); },
                    [&](CommandEncoder &e, TextureHandle composed)
                    { REQUIRE(blit.copy(e, composed, blit.linearSampler(), backbuffer, Format::kRGBA8Unorm, "present").has_value()); }));
                REQUIRE(added.has_value());
                auto *overlay = static_cast<demo::FrameOverlay *>(*added);

                auto composedTarget = rd::RenderTarget::create(*gpu.device, {.size = {kW, kH}, .depth = false, .debugName = "composed"});
                REQUIRE(composedTarget.has_value());
                rd::Camera camera;
                camera.setAspect(kW, kH);
                renderer->setOutputResolution(static_cast<int>(kW), static_cast<int>(kH));

                auto frame = [&](demo::FrameComposition composition, bool ui)
                {
                    auto bb = gpu.device->beginFrame();
                    REQUIRE(bb.has_value());
                    backbuffer = *bb;
                    const TextureHandle output = composition == demo::FrameComposition::kBackbuffer ? backbuffer : composedTarget->color(0);
                    overlay->beginFrame(backbuffer, composition, ui);
                    gpu.run(
                        [&](CommandEncoder &e)
                        {
                            REQUIRE(renderer->beginFrame(e, {.clearColor = {0.2f, 0.4f, 0.6f, 1.0f}}).has_value());
                            REQUIRE(renderer->setCamera(camera).has_value());
                            REQUIRE(renderer->endFrame({output, Format::kRGBA8Unorm, {kW, kH}}).has_value());
                        });
                    auto pixels = gpu.device->readTexture(backbuffer);
                    REQUIRE(pixels.has_value());
                    REQUIRE(gpu.device->present().has_value());
                    return *pixels;
                };

                for (const bool ui : {true, false})
                {
                    CAPTURE(ui);
                    const std::vector<std::byte> throughComposed = frame(demo::FrameComposition::kComposedTarget, ui);
                    const std::vector<std::string> composedOrder = renderer->renderGraph().executionOrder();
                    CHECK(overlay->presented);
                    CHECK(overlay->uiDone == ui);
                    const std::vector<std::byte> direct = frame(demo::FrameComposition::kBackbuffer, ui);
                    const std::vector<std::string> directOrder = renderer->renderGraph().executionOrder();
                    CHECK_FALSE(overlay->presented);
                    CHECK(overlay->uiDone == ui);

                    // Copies plein écran de l'image (FullscreenBlit::copy) : « upscale » (1:1) et « present » avant ; « upscale » seule après.
                    CHECK(countOf(composedOrder, "upscale") == 1);
                    CHECK(countOf(composedOrder, "present") == 1);
                    CHECK(countOf(directOrder, "upscale") == 1);
                    CHECK(countOf(directOrder, "present") == 0);
                    CHECK(countOf(directOrder, "ui") == (ui ? 1u : 0u));
                    CHECK(directOrder.size() + 1 == composedOrder.size());

                    // La même image, octet pour octet.
                    REQUIRE(direct.size() == kW * kH * 4);
                    CHECK(direct == throughComposed);
                    // Et l'interface y est bien (sinon l'égalité ne prouverait rien) : pixel du coin haut gauche ≠ fond.
                    if (ui)
                    {
                        CHECK(direct[0] != throughComposed[direct.size() - 4]);
                    }
                }
                renderer->removeFeature("demo.overlay"); // les passes retiennent les lambdas de ce test
                gpu.device->waitIdle();
                gpu.device->destroy(*uiTexture);
            }
            gpu.expectNoValidationErrors();
        }
    }
}
