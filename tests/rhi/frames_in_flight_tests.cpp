// A61 G (M8, lot L16) : nombre d'images en vol réglable (DeviceDesc::framesInFlight, de 1 à kMaxFramesInFlight), respecté par
// les deux backends. Deux bornes vérifiées sur chaque backend disponible (FOR_EACH_BACKEND) :
//   - au plus : quand `beginFrame` de l'image k + N rend la main, le travail de l'image k est fini (ses horodatages sont lisibles
//     sans attente) ;
//   - au moins : une ressource détruite pendant l'image k n'est libérée qu'après N présentations (le GPU peut encore lire les
//     N images précédentes).
#include <doctest/doctest.h>

#include "rhi/gpu_fixture.hpp"
#include "support/result_support.hpp"

#include <orosmo/rhi/device.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace
{

// Périphérique hors écran avec `framesInFlight` images en vol (nul si le backend refuse : le test échoue alors).
Gpu makeGpu(Backend backend, std::uint32_t framesInFlight)
{
    DeviceDesc desc;
    desc.backend = backend;
    desc.surface.size = {8, 8};
    desc.framesInFlight = framesInFlight;
    auto created = createDevice(desc);
    INFO("createDevice(", tests::backendName(backend), ", framesInFlight = ", framesInFlight, "): ", created ? "" : created.error().message);
    REQUIRE(created.has_value());
    Gpu gpu(std::move(*created));
    gpu.backend = backend;
    return gpu;
}

} // namespace

TEST_SUITE("rhi/frames-in-flight")
{
    TEST_CASE("createDevice refuses 0 and more than kMaxFramesInFlight frames in flight, before touching the backend")
    {
        static_assert(kDefaultFramesInFlight == 2, "A61 G : deux images en vol par défaut tant que la mesure ne montre pas de gain");
        static_assert(kMaxFramesInFlight == 3);
        for (const Backend backend : {Backend::kOpenGL, Backend::kVulkan, Backend::kMetal})
        {
            for (const std::uint32_t wrong : {0u, kMaxFramesInFlight + 1, 16u})
            {
                DeviceDesc desc;
                desc.backend = backend;
                desc.surface.size = {8, 8};
                desc.framesInFlight = wrong;
                CHECK((tests::errorOf(createDevice(desc)) == ErrorCode::kInvalidArgument));
            }
        }
        CHECK(DeviceDesc{}.framesInFlight == kDefaultFramesInFlight);
    }
}

TEST_SUITE("rhi/backend/frames-in-flight")
{
    TEST_CASE("the device reports the requested number of frames in flight; the default is two")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu defaults(backend);
            REQUIRE(defaults.device);
            CHECK(defaults->framesInFlight() == kDefaultFramesInFlight);
            for (std::uint32_t n = 1; n <= kMaxFramesInFlight; ++n)
            {
                Gpu gpu = makeGpu(backend, n);
                CHECK(gpu->framesInFlight() == n);
                gpu.expectNoValidationErrors();
            }
        }
    }

    TEST_CASE("a destroyed resource is released after exactly framesInFlight presents (lower bound of the CPU advance)")
    {
        FOR_EACH_BACKEND(backend)
        {
            for (std::uint32_t n = 1; n <= kMaxFramesInFlight; ++n)
            {
                CAPTURE(n);
                Gpu gpu = makeGpu(backend, n);
                for (std::uint32_t warm = 0; warm < kMaxFramesInFlight + 1; ++warm) // anneaux pleins : régime établi
                {
                    REQUIRE(gpu->beginFrame().has_value());
                    REQUIRE(gpu->present().has_value());
                }
                REQUIRE(gpu->beginFrame().has_value());
                const BufferHandle b = gpu.buffer(BufferUsage::kVertex, {}, 16);
                gpu->destroy(b); // pendant l'image k
                CHECK(gpu->stats().pendingDestruction == 1);
                for (std::uint32_t presents = 1; presents < n; ++presents)
                {
                    REQUIRE(gpu->present().has_value());
                    REQUIRE(gpu->beginFrame().has_value());
                    CHECK(gpu->stats().pendingDestruction == 1); // une des n images en vol peut encore le lire
                }
                REQUIRE(gpu->present().has_value());
                REQUIRE(gpu->beginFrame().has_value());
                CHECK(gpu->stats().pendingDestruction == 0);
                REQUIRE(gpu->present().has_value());
                gpu.expectNoValidationErrors();
            }
        }
    }

    TEST_CASE("when beginFrame of frame k + framesInFlight returns, frame k is finished (upper bound of the CPU advance)")
    {
        FOR_EACH_BACKEND(backend)
        {
            for (std::uint32_t n = 1; n <= kMaxFramesInFlight; ++n)
            {
                CAPTURE(n);
                Gpu gpu = makeGpu(backend, n);
                if (!gpu->capabilities().timestampQueries)
                {
                    MESSAGE("Skipped: no timestamp queries on ", tests::backendName(backend));
                    break;
                }
                // Un pool par image : la disponibilité d'un pool suit sa dernière écriture (G192), pas celle d'une requête.
                constexpr std::uint32_t kFrames = 8;
                std::vector<QueryPoolHandle> pools;
                for (std::uint32_t frame = 0; frame < kFrames; ++frame)
                {
                    auto pool = gpu->createQueryPool(1, "frames-in-flight");
                    REQUIRE(pool.has_value());
                    pools.push_back(*pool);
                }
                const TextureHandle color = gpu.target(Format::kRGBA8Unorm, {256, 256});
                const PipelineHandle pipe =
                    gpu.pipeline(gpu.shader(ShaderStage::kVertex, kFullscreenVsGlsl450), gpu.shader(ShaderStage::kFragment, kRedFsGlsl450));
                for (std::uint32_t frame = 0; frame < kFrames; ++frame)
                {
                    REQUIRE(gpu->beginFrame().has_value());
                    if (frame >= n)
                    {
                        auto ready = gpu->timestampsReady(pools[frame - n], 0, 1);
                        REQUIRE(ready.has_value());
                        CHECK(*ready); // image frame - n terminée par le GPU, sans attente supplémentaire
                    }
                    gpu.run(
                        [&](CommandEncoder &e)
                        {
                            e.beginRenderPass(passOn(color));
                            e.setPipeline(pipe);
                            e.draw(3, 64); // du travail pour le GPU : l'image n'est pas finie au moment de sa soumission
                            e.endRenderPass();
                            e.writeTimestamp(pools[frame], 0);
                        });
                    REQUIRE(gpu->present().has_value());
                }
                for (const QueryPoolHandle pool : pools)
                {
                    gpu.device->destroy(pool);
                }
                gpu.expectNoValidationErrors();
            }
        }
    }
}
