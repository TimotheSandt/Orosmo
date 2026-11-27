// A61 C (M8, lot L16) : graphe de passes gardé d'une image à l'autre. Le graphe n'est reconstruit que lorsqu'un plugin, une cible
// ou une taille change (ou que la signature d'un plugin change) ; entre deux reconstructions, les passes déclarées s'exécutent
// à nouveau et voient les ressources GPU de l'image (sortie, historique). A61 G : l'anneau du renderer suit les images en vol du
// périphérique. Sur chaque backend.
#include <doctest/doctest.h>

#include "renderer/repo_paths.hpp"
#include "rhi/gpu_fixture.hpp"
#include "support/result_support.hpp"
#include "support/test_support.hpp"

#include <orosmo/renderer/camera.hpp>
#include <orosmo/renderer/pipeline_cache.hpp>
#include <orosmo/renderer/render_feature.hpp>
#include <orosmo/renderer/render_graph.hpp>
#include <orosmo/renderer/scene_renderer.hpp>
#include <orosmo/renderer/shader_library.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace fs = std::filesystem;
namespace rd = orosmo::renderer;
namespace core = orosmo::core;

namespace
{

fs::path shaderDir() { return tests::repoRoot(__FILE__) / "shaders" / "rhi"; }

// Plugin qui compte ses appels : une passe générique qui lit et écrit la sortie et note la texture qu'elle y voit.
struct CountingFeature final : rd::RenderFeature
{
    explicit CountingFeature(std::string featureName) : featureName(std::move(featureName)) {}
    std::string featureName;
    std::optional<std::uint64_t> signature = 7;
    int prepared = 0;
    int declared = 0;
    int executed = 0;
    bool failNext = false;
    TextureHandle seen;

    std::string name() const override { return featureName; }
    std::optional<std::uint64_t> passSignature() const override { return signature; }
    core::Result<> prepare(const rd::FrameData &) override
    {
        ++prepared;
        return {};
    }
    core::Result<> declarePasses(rd::RenderGraphBuilder &builder) override
    {
        ++declared;
        const rd::RgTexture output = builder.resources().output;
        builder.graph()
            .addPass(featureName, rd::PassType::kGeneric,
                     [this, output](rd::RenderPassContext &ctx) -> core::Result<>
                     {
                         ++executed;
                         seen = ctx.texture(output);
                         if (std::exchange(failNext, false))
                         {
                             return core::makeError(core::ErrorCode::kUnknown, "requested failure");
                         }
                         return {};
                     })
            .read(output)
            .write(output);
        return {};
    }
};

struct Scene
{
    Gpu gpu;
    std::unique_ptr<rd::ShaderLibrary> library;
    std::unique_ptr<rd::PipelineCache> pipelines;
    std::unique_ptr<rd::SceneRenderer> renderer;
    rd::Camera camera;

    explicit Scene(Backend backend) : gpu(backend, {16, 16})
    {
        REQUIRE(gpu.device);
        library = std::make_unique<rd::ShaderLibrary>(*gpu.device, rd::ShaderLibraryConfig{.cacheDirectory = {}, .includeRoots = {shaderDir()}});
        pipelines = std::make_unique<rd::PipelineCache>(*gpu.device);
        auto created = rd::SceneRenderer::create(*gpu.device, *library, *pipelines, {.shaderDirectory = shaderDir()});
        INFO((created ? std::string() : created.error().message));
        REQUIRE(created.has_value());
        renderer = std::move(*created);
        camera.setAspect(8, 8);
    }
    ~Scene()
    {
        renderer.reset();
        pipelines.reset();
        library.reset();
    }

    // Une image complète vers `out` (taille `size`) ; rend le résultat de endFrame.
    core::Result<> frame(TextureHandle out, Extent2D size, const rd::FrameOptions &options = {})
    {
        renderer->setOutputResolution(static_cast<int>(size.width), static_cast<int>(size.height));
        auto encoder = gpu->createCommandEncoder();
        if (auto r = renderer->beginFrame(*encoder, options); !r)
        {
            return r;
        }
        if (auto r = renderer->setCamera(camera); !r)
        {
            return r;
        }
        auto ended = renderer->endFrame({out, Format::kRGBA8Unorm, size});
        if (auto r = gpu->submit(std::move(encoder)); !r)
        {
            return r;
        }
        return ended;
    }
};

CountingFeature *addCounting(rd::SceneRenderer &renderer, const std::string &name)
{
    auto added = renderer.addFeature(std::make_unique<CountingFeature>(name));
    REQUIRE(added.has_value());
    return static_cast<CountingFeature *>(*added);
}

} // namespace

TEST_SUITE("renderer/graph-cache")
{
    TEST_CASE("the graph is built once and reused while plugins, targets and sizes stay the same; prepare and the passes run every frame")
    {
        FOR_EACH_BACKEND(backend)
        {
            Scene s(backend);
            CHECK(s.renderer->renderGraphCaching());
            CountingFeature *feature = addCounting(*s.renderer, "counting");
            const TextureHandle out = s.gpu.target(Format::kRGBA8Unorm, {8, 8}, "output");
            for (int f = 0; f < 6; ++f)
            {
                REQUIRE(s.frame(out, {8, 8}).has_value());
                CHECK(s.renderer->graphReused() == (f > 0));
            }
            CHECK(s.renderer->graphBuildCount() == 1);
            CHECK(feature->declared == 1);
            CHECK(feature->prepared == 6);
            CHECK(feature->executed == 6);
            s.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("a new output texture of the same format and size keeps the graph; the passes see the texture of the frame")
    {
        FOR_EACH_BACKEND(backend)
        {
            Scene s(backend);
            CountingFeature *feature = addCounting(*s.renderer, "counting");
            const TextureHandle a = s.gpu.target(Format::kRGBA8Unorm, {8, 8}, "output.a");
            const TextureHandle b = s.gpu.target(Format::kRGBA8Unorm, {8, 8}, "output.b");
            rd::FrameOptions red;
            red.clearColor = {1.0f, 0.0f, 0.0f, 1.0f};
            REQUIRE(s.frame(a, {8, 8}, red).has_value());
            CHECK(feature->seen == a);
            REQUIRE(s.frame(b, {8, 8}, red).has_value());
            CHECK(feature->seen == b);
            CHECK(s.renderer->graphReused());
            CHECK(s.renderer->graphBuildCount() == 1);
            CHECK(rgba(s.gpu.pixel(b, 4, 4))[0] > 200); // la copie de la scène a écrit la nouvelle sortie (fond rouge)
            REQUIRE(s.frame(a, {8, 8}, red).has_value());
            CHECK(feature->seen == a);
            CHECK(s.renderer->graphBuildCount() == 1);
            s.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("a size, a clear option or a plugin change rebuilds the graph once, then it is reused again")
    {
        FOR_EACH_BACKEND(backend)
        {
            Scene s(backend);
            CountingFeature *first = addCounting(*s.renderer, "first");
            const TextureHandle small = s.gpu.target(Format::kRGBA8Unorm, {8, 8}, "output.small");
            const TextureHandle large = s.gpu.target(Format::kRGBA8Unorm, {12, 12}, "output.large");
            REQUIRE(s.frame(small, {8, 8}).has_value());
            REQUIRE(s.frame(small, {8, 8}).has_value());
            CHECK(s.renderer->graphBuildCount() == 1);

            // Taille de sortie et de rendu.
            REQUIRE(s.frame(large, {12, 12}).has_value());
            CHECK_FALSE(s.renderer->graphReused());
            REQUIRE(s.frame(large, {12, 12}).has_value());
            CHECK(s.renderer->graphReused());
            CHECK(s.renderer->graphBuildCount() == 2);

            // Couleur de fond (inscrite dans les attachements déclarés).
            rd::FrameOptions blue;
            blue.clearColor = {0.0f, 0.0f, 1.0f, 1.0f};
            REQUIRE(s.frame(large, {12, 12}, blue).has_value());
            REQUIRE(s.frame(large, {12, 12}, blue).has_value());
            CHECK(s.renderer->graphBuildCount() == 3);
            CHECK(rgba(s.gpu.pixel(large, 6, 6))[2] > 200);

            // Plugin ajouté puis retiré.
            CountingFeature *second = addCounting(*s.renderer, "second");
            REQUIRE(s.frame(large, {12, 12}, blue).has_value());
            REQUIRE(s.frame(large, {12, 12}, blue).has_value());
            CHECK(s.renderer->graphBuildCount() == 4);
            CHECK(second->declared == 1);
            CHECK(second->executed == 2);
            s.renderer->removeFeature("second");
            REQUIRE(s.frame(large, {12, 12}, blue).has_value());
            REQUIRE(s.frame(large, {12, 12}, blue).has_value());
            CHECK(s.renderer->graphBuildCount() == 5);
            CHECK(first->declared == 5);
            CHECK(first->executed == 10);
            s.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("a plugin signature change rebuilds; a plugin without signature or disabled caching rebuilds every frame")
    {
        FOR_EACH_BACKEND(backend)
        {
            Scene s(backend);
            CountingFeature *feature = addCounting(*s.renderer, "counting");
            const TextureHandle out = s.gpu.target(Format::kRGBA8Unorm, {8, 8}, "output");
            REQUIRE(s.frame(out, {8, 8}).has_value());
            feature->signature = 8;
            REQUIRE(s.frame(out, {8, 8}).has_value());
            REQUIRE(s.frame(out, {8, 8}).has_value());
            CHECK(feature->declared == 2);

            feature->signature = std::nullopt; // plugin d'avant M8 : aucun cache
            for (int f = 0; f < 3; ++f)
            {
                REQUIRE(s.frame(out, {8, 8}).has_value());
                CHECK_FALSE(s.renderer->graphReused());
            }
            CHECK(feature->declared == 5);

            feature->signature = 9;
            s.renderer->setRenderGraphCaching(false);
            CHECK_FALSE(s.renderer->renderGraphCaching());
            for (int f = 0; f < 3; ++f)
            {
                REQUIRE(s.frame(out, {8, 8}).has_value());
                CHECK_FALSE(s.renderer->graphReused());
            }
            CHECK(feature->declared == 8);
            s.renderer->setRenderGraphCaching(true);
            REQUIRE(s.frame(out, {8, 8}).has_value());
            REQUIRE(s.frame(out, {8, 8}).has_value());
            CHECK(s.renderer->graphReused());
            CHECK(feature->declared == 9);
            CHECK(feature->executed == 11);
            s.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("a failed frame drops the kept graph: the next frame declares it again")
    {
        FOR_EACH_BACKEND(backend)
        {
            Scene s(backend);
            CountingFeature *feature = addCounting(*s.renderer, "counting");
            const TextureHandle out = s.gpu.target(Format::kRGBA8Unorm, {8, 8}, "output");
            REQUIRE(s.frame(out, {8, 8}).has_value());
            REQUIRE(s.frame(out, {8, 8}).has_value());
            feature->failNext = true;
            CHECK_FALSE(s.frame(out, {8, 8}).has_value());
            REQUIRE(s.frame(out, {8, 8}).has_value());
            CHECK_FALSE(s.renderer->graphReused());
            REQUIRE(s.frame(out, {8, 8}).has_value());
            CHECK(s.renderer->graphReused());
            CHECK(feature->declared == 2);
        }
    }
}

TEST_SUITE("renderer/frames-in-flight")
{
    TEST_CASE("SceneRenderer takes the device's frames in flight by default and refuses a shallower ring")
    {
        FOR_EACH_BACKEND(backend)
        {
            for (std::uint32_t n = 1; n <= kMaxFramesInFlight; ++n)
            {
                CAPTURE(n);
                DeviceDesc desc;
                desc.backend = backend;
                desc.surface.size = {8, 8};
                desc.framesInFlight = n;
                auto device = createDevice(desc);
                REQUIRE(device.has_value());
                Gpu gpu(std::move(*device));
                rd::ShaderLibrary library(*gpu.device);
                rd::PipelineCache pipelines(*gpu.device);
                auto renderer = rd::SceneRenderer::create(*gpu.device, library, pipelines, {.shaderDirectory = shaderDir()});
                INFO((renderer ? std::string() : renderer.error().message));
                REQUIRE(renderer.has_value());
                CHECK((*renderer)->framesInFlight() == n);
                if (n > 1)
                {
                    CHECK((tests::errorOf(rd::SceneRenderer::create(*gpu.device, library, pipelines, {.shaderDirectory = shaderDir(), .framesInFlight = n - 1})) ==
                           ErrorCode::kInvalidArgument));
                }
                auto deeper = rd::SceneRenderer::create(*gpu.device, library, pipelines, {.shaderDirectory = shaderDir(), .framesInFlight = n + 1});
                REQUIRE(deeper.has_value());
                CHECK((*deeper)->framesInFlight() == n + 1);
            }
        }
    }
}
