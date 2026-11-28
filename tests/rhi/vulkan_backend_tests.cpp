// Tests « boîte blanche » du backend Vulkan (src/rhi/vulkan/) : ce que l'API du RHI ne laisse pas observer. Le test inclut
// l'en-tête interne du backend mais n'appelle aucune fonction `vk*` (règle 4). Sans pilote Vulkan, le cas est ignoré.
#include <doctest/doctest.h>

#include "support/test_support.hpp"
#include "rhi/gpu_fixture.hpp"

#include "../../src/rhi/vulkan/vk_device.hpp"

#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace
{

// Pose une variable d'environnement le temps d'un bloc, puis remet l'ancienne valeur.
class ScopedEnv
{
  public:
    ScopedEnv(const char *name, const std::string &value) : name_(name)
    {
        if (const char *old = std::getenv(name); old != nullptr)
            old_ = old;
        set(value.c_str());
    }
    ~ScopedEnv() { set(old_ ? old_->c_str() : nullptr); }
    ScopedEnv(const ScopedEnv &) = delete;
    ScopedEnv &operator=(const ScopedEnv &) = delete;

  private:
    void set(const char *value)
    {
#if defined(_WIN32)
        _putenv_s(name_, value != nullptr ? value : "");
#else
        if (value != nullptr)
            setenv(name_, value, 1);
        else
            unsetenv(name_);
#endif
    }
    const char *name_;
    std::optional<std::string> old_;
};

} // namespace

TEST_SUITE("rhi/vulkan/backend")
{
    TEST_CASE("RHI-06: the pipeline cache is written at shutdown and read back by the next device; a foreign file is ignored")
    {
        if (!tests::isBackendAvailable(Backend::kVulkan))
        {
            if (tests::backendRequired("vulkan"))
            {
                FAIL("Required by ENGINE_TEST_REQUIRE_GPU but unavailable: Vulkan 1.3 device");
            }
            MESSAGE("Skipped: no Vulkan device on this machine");
            return;
        }
        // Un dossier propre à cette exécution (plusieurs processus de test peuvent tourner en même temps).
        const std::filesystem::path dir =
            std::filesystem::temp_directory_path() /
            ("orosmo-pipeline-cache-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::remove_all(dir);
        ScopedEnv env("ENGINE_VULKAN_PIPELINE_CACHE_DIR", dir.string());

        std::string path;
        {
            Gpu gpu(Backend::kVulkan);
            REQUIRE(gpu.device);
            auto &device = static_cast<vk::VulkanDevice &>(*gpu);
            path = device.pipelineCachePath();
            CHECK(std::filesystem::path(path).parent_path() == dir);
            CHECK(device.pipelineCacheLoadedBytes() == 0); // dossier vide : rien à relire
            gpu.pipeline(gpu.shader(ShaderStage::kVertex, kFullscreenVsGlsl450), gpu.shader(ShaderStage::kFragment, kRedFsGlsl450));
            gpu.expectNoValidationErrors();
        }
        REQUIRE(std::filesystem::exists(path));
        const auto written = std::filesystem::file_size(path);
        // Certains pilotes (lavapipe, CI) ne rendent que l'en-tête : le fichier existe quand même, il n'a pas à contenir plus.
        CHECK(written >= sizeof(VkPipelineCacheHeaderVersionOne));
        {
            Gpu gpu(Backend::kVulkan);
            REQUIRE(gpu.device);
            auto &device = static_cast<vk::VulkanDevice &>(*gpu);
            // relu tel quel (même GPU, même pilote) ; un pilote qui n'a rien à relire au-delà de l'en-tête peut n'en reprendre aucun octet
            const auto loaded = device.pipelineCacheLoadedBytes();
            CHECK((loaded == written || (written == sizeof(VkPipelineCacheHeaderVersionOne) && loaded == 0)));
            gpu.expectNoValidationErrors();
        }
        {
            // En-tête d'un autre GPU : le fichier est ignoré (le pilote ne reçoit pas de données étrangères).
            std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
            file.seekp(8); // vendorID
            const std::uint32_t foreign = 0xDEADBEEF;
            file.write(reinterpret_cast<const char *>(&foreign), sizeof foreign);
        }
        {
            Gpu gpu(Backend::kVulkan);
            REQUIRE(gpu.device);
            CHECK(static_cast<vk::VulkanDevice &>(*gpu).pipelineCacheLoadedBytes() == 0);
            gpu.expectNoValidationErrors();
        }
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

    TEST_CASE("bind group descriptor sets are created per layout and shared by pipelines with the same group layout")
    {
        if (!tests::isBackendAvailable(Backend::kVulkan))
        {
            if (tests::backendRequired("vulkan"))
            {
                FAIL("Required by ENGINE_TEST_REQUIRE_GPU but unavailable: Vulkan 1.3 device");
            }
            MESSAGE("Skipped: no Vulkan device on this machine");
            return;
        }
        Gpu gpu(Backend::kVulkan);
        REQUIRE(gpu.device);
        auto &device = static_cast<vk::VulkanDevice &>(*gpu);
        const char *fs = R"(#version 450
layout(std140, set = 0, binding = 0) uniform U { vec4 color; } u;
layout(location = 0) out vec4 outColor;
void main() { outColor = u.color; })";
        const ShaderHandle vs = gpu.shader(ShaderStage::kVertex, kFullscreenVsGlsl450);
        const ShaderHandle f = gpu.shader(ShaderStage::kFragment, fs);
        const PipelineHandle a = gpu.pipeline(vs, f, {ColorTargetState{}}, Format::kUndefined, false, "a");
        const PipelineHandle b = gpu.pipeline(vs, f, {ColorTargetState{.format = Format::kRGBA16Float}}, Format::kUndefined, false, "b");
        REQUIRE(device.get(a) != nullptr);
        REQUIRE(device.get(b) != nullptr);
        CHECK(device.get(a)->sets[0] == device.get(b)->sets[0]); // même disposition de groupe
        CHECK(device.get(a)->layout == device.get(b)->layout);   // même disposition de pipeline
        CHECK(device.get(a)->sets[1] == nullptr);

        const float red[4] = {1, 0, 0, 1};
        const BufferHandle ubo = gpu.buffer(BufferUsage::kUniform, bytesOf(red), 0, "ubo");
        BindGroupDesc gd;
        gd.entries = {{.binding = 0, .type = BindingType::kUniformBuffer, .buffer = ubo}};
        auto group = gpu->createBindGroup(gd);
        REQUIRE(group.has_value());
        const TextureHandle t8 = gpu.target(Format::kRGBA8Unorm, {2, 2});
        const TextureHandle t16 = gpu.target(Format::kRGBA16Float, {2, 2});
        gpu.run(
            [&](CommandEncoder &e)
            {
                e.setBindGroup(0, *group); // avant le pipeline et la passe : liaison faite au dessin
                e.beginRenderPass(passOn(t8));
                e.setPipeline(a);
                e.draw(3);
                e.endRenderPass();
                e.beginRenderPass(passOn(t16));
                e.setPipeline(b);
                e.draw(3);
                e.endRenderPass();
            });
        CHECK(rgba(gpu.pixel(t8, 0, 0)) == std::array<std::uint8_t, 4>{255, 0, 0, 255});
        CHECK(device.get(*group)->sets.size() == 1); // un seul jeu de descripteurs pour les deux pipelines

        // Le groupe ne fournit pas la liaison 0 attendue par le pipeline : refusé au dessin.
        BindGroupDesc other;
        other.entries = {{.binding = 1, .type = BindingType::kUniformBuffer, .buffer = ubo}};
        auto wrong = gpu->createBindGroup(other);
        REQUIRE(wrong.has_value());
        auto encoder = gpu->createCommandEncoder();
        encoder->beginRenderPass(passOn(t8));
        encoder->setPipeline(a);
        encoder->setBindGroup(0, *wrong);
        encoder->draw(3);
        encoder->endRenderPass();
        CHECK((tests::errorOf(gpu->submit(std::move(encoder))) == ErrorCode::kInvalidArgument));
        // Sans groupe du tout : refusé aussi (Vulkan n'a pas de liaison par défaut).
        auto none = gpu->createCommandEncoder();
        none->beginRenderPass(passOn(t8));
        none->setPipeline(a);
        none->draw(3);
        none->endRenderPass();
        CHECK((tests::errorOf(gpu->submit(std::move(none))) == ErrorCode::kInvalidArgument));
        gpu.expectNoValidationErrors();
    }

    TEST_CASE("an encoder dropped without submit returns its command buffer and leaves textures at rest")
    {
        if (!tests::isBackendAvailable(Backend::kVulkan))
        {
            if (tests::backendRequired("vulkan"))
            {
                FAIL("Required by ENGINE_TEST_REQUIRE_GPU but unavailable: Vulkan 1.3 device");
            }
            MESSAGE("Skipped: no Vulkan device on this machine");
            return;
        }
        Gpu gpu(Backend::kVulkan);
        REQUIRE(gpu.device);
        const TextureHandle color = gpu.target(Format::kRGBA8Unorm, {4, 4});
        {
            auto dropped = gpu->createCommandEncoder();
            dropped->beginRenderPass(passOn(color, {1, 0, 0, 1}));
            // ni fin de passe ni soumission
        }
        gpu.run(
            [&](CommandEncoder &e)
            {
                e.beginRenderPass(passOn(color, {0, 0, 1, 1}));
                e.endRenderPass();
            });
        CHECK(rgba(gpu.pixel(color, 1, 1)) == std::array<std::uint8_t, 4>{0, 0, 255, 255});
        gpu.expectNoValidationErrors();
    }

    // M9-L2 (G510) : le contournement du GPU Metal paravirtualisé (dessins indexés directs joués en commandes indirectes) se force
    // par l'environnement ; les dessins donnent la même image (firstIndex, vertexOffset, firstInstance compris), au-delà de la
    // capacité du premier tampon d'arguments (256 commandes), et les tampons d'arguments sont rendus après la soumission.
    TEST_CASE("G510: ENGINE_VULKAN_INDEXED_VIA_INDIRECT records indexed draws as indirect commands with the same result")
    {
        if (!tests::isBackendAvailable(Backend::kVulkan))
        {
            if (tests::backendRequired("vulkan"))
            {
                FAIL("Required by ENGINE_TEST_REQUIRE_GPU but unavailable: Vulkan 1.3 device");
            }
            MESSAGE("Skipped: no Vulkan device on this machine");
            return;
        }
        {
            ScopedEnv off("ENGINE_VULKAN_INDEXED_VIA_INDIRECT", "0");
            Gpu gpu(Backend::kVulkan);
            REQUIRE(gpu.device);
            CHECK_FALSE(static_cast<vk::VulkanDevice &>(*gpu).indexedDrawsViaIndirect());
        }
        ScopedEnv on("ENGINE_VULKAN_INDEXED_VIA_INDIRECT", "1");
        Gpu gpu(Backend::kVulkan);
        REQUIRE(gpu.device);
        auto &device = static_cast<vk::VulkanDevice &>(*gpu);
        if (!gpu->capabilities().drawIndirectFirstInstance)
        {
            CHECK_FALSE(device.indexedDrawsViaIndirect()); // sans la fonctionnalité, pas de contournement
            return;
        }
        REQUIRE(device.indexedDrawsViaIndirect());
        const TextureHandle color = gpu.target(Format::kRGBA8Unorm, {8, 8});
        // 8x8 : quatre bandes de 2 colonnes (lignes 0..3) pour les dessins observés, les lignes 4..7 pour le remplissage.
        // Deux triangles plein écran : sommets 0..2 (inutilisés) et 3..5 ; indices 0, 0, 0 puis 0, 1, 2 (vertexOffset 3, firstIndex 3).
        const std::vector<float> positions{0, 0, 0, 0, 0, 0, -1, -1, 3, -1, -1, 3};
        // Par instance : rouge, vert, bleu, jaune, puis le blanc du remplissage.
        const std::vector<float> colors{1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1};
        const std::vector<std::uint32_t> indices{0, 0, 0, 0, 1, 2};
        const BufferHandle vb = gpu.buffer(BufferUsage::kVertex, bytesOf(positions), 0, "positions");
        const BufferHandle inst = gpu.buffer(BufferUsage::kVertex, bytesOf(colors), 0, "instances");
        const BufferHandle ib = gpu.buffer(BufferUsage::kIndex, bytesOf(indices), 0, "indices", MemoryKind::kUpload);
        const char *vs = R"(#version 450
layout(location = 0) in vec2 position;
layout(location = 1) in vec4 instanceColor;
layout(location = 0) out vec4 vColor;
void main() { gl_Position = vec4(position, 0.0, 1.0); vColor = instanceColor; })";
        const char *fs = R"(#version 450
layout(location = 0) in vec4 vColor;
layout(location = 0) out vec4 color;
void main() { color = vColor; })";
        GraphicsPipelineDesc d;
        d.vertex = gpu.shader(ShaderStage::kVertex, vs);
        d.fragment = gpu.shader(ShaderStage::kFragment, fs);
        d.vertexBuffers = {{.binding = 0, .stride = 8, .stepRate = VertexStepRate::kVertex},
                           {.binding = 1, .stride = 16, .stepRate = VertexStepRate::kInstance}};
        d.vertexAttributes = {{.location = 0, .binding = 0, .format = VertexFormat::kFloat2, .offset = 0},
                              {.location = 1, .binding = 1, .format = VertexFormat::kFloat4, .offset = 0}};
        d.colorTargets = {ColorTargetState{}};
        d.raster.cull = CullMode::kNone;
        auto pipe = gpu->createGraphicsPipeline(d);
        REQUIRE(pipe.has_value());
        const std::uint32_t buffersBefore = gpu->stats().buffers;
        constexpr int kDraws = 600; // plus que les 256 commandes du premier tampon d'arguments : un second est créé
        // Première et dernière commandes de chaque tampon d'arguments, de part et d'autre du changement (255 | 256).
        constexpr std::array<int, 4> kObserved{0, 255, 256, kDraws - 1};
        const auto record = [&](CommandEncoder &e)
        {
            e.beginRenderPass(passOn(color));
            e.setPipeline(*pipe);
            e.setVertexBuffer(0, vb);
            e.setVertexBuffer(1, inst);
            e.setIndexBuffer(ib, IndexType::kUint32);
            for (int i = 0; i < kDraws; ++i)
            {
                const auto found = std::find(kObserved.begin(), kObserved.end(), i);
                const auto zone = static_cast<std::uint32_t>(found - kObserved.begin()); // 4 : remplissage
                e.setScissor(found != kObserved.end() ? Rect{static_cast<std::int32_t>(2 * zone), 0, 2, 4} : Rect{0, 4, 8, 4});
                e.drawIndexed(3, 1, 3, 3, zone);
            }
            e.endRenderPass();
        };
        gpu.run(record);
        // Les tampons d'arguments ne sont plus vivants et n'attendent aucune image présentée (rendu hors écran sans `present`).
        CHECK(gpu->stats().buffers == buffersBefore);
        CHECK(gpu->stats().pendingDestruction == 0);
        const std::array<std::array<std::uint8_t, 4>, 4> expected{{{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}, {255, 255, 0, 255}}};
        for (std::uint32_t zone = 0; zone < expected.size(); ++zone)
        {
            INFO("draw ", kObserved[zone]);
            CHECK(rgba(gpu.pixel(color, 2 * zone + 1, 1)) == expected[zone]);
        }
        CHECK(rgba(gpu.pixel(color, 4, 6)) == std::array<std::uint8_t, 4>{255, 255, 255, 255});
        // Encodeur abandonné sans soumission : ses tampons d'arguments sont rendus de la même façon.
        {
            auto dropped = gpu->createCommandEncoder();
            record(*dropped);
        }
        CHECK(gpu->stats().buffers == buffersBefore);
        CHECK(gpu->stats().pendingDestruction == 0);
        gpu.expectNoValidationErrors();
    }
}
