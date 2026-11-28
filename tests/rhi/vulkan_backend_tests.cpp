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
}
