// Tests « boîte blanche » des transferts et soumissions du backend Vulkan (src/rhi/vulkan/, README.md « Transferts ») : anneau de
// transfert, barrières groupées, une soumission par image, réserves de tampons de commandes, écriture directe des tampons kUpload.
// Les compteurs internes (`VulkanDevice::transferCounters`) prouvent l'optimisation ; les lectures prouvent le résultat. Aucun
// appel `vk*` ici (règle 4). Sans pilote Vulkan, le cas est ignoré.
#include <doctest/doctest.h>

#include "support/test_support.hpp"
#include "rhi/gpu_fixture.hpp"

#include "../../src/rhi/vulkan/vk_device.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

namespace
{

bool skipWithoutVulkan()
{
    if (tests::isBackendAvailable(Backend::kVulkan))
        return false;
    if (tests::gpuRequired())
        FAIL("Required by ENGINE_TEST_REQUIRE_GPU but unavailable: Vulkan 1.3 device");
    MESSAGE("Skipped: no Vulkan device on this machine");
    return true;
}

std::vector<std::byte> pattern(std::size_t size, std::uint8_t seed)
{
    std::vector<std::byte> v(size);
    for (std::size_t i = 0; i < size; ++i)
        v[i] = static_cast<std::byte>(static_cast<std::uint8_t>(seed + i * 13));
    return v;
}

constexpr const char *kUniformColorFs = R"(#version 450
layout(std140, set = 0, binding = 0) uniform U { vec4 color; } u;
layout(location = 0) out vec4 outColor;
void main() { outColor = u.color; })";

// Pipeline plein écran qui écrit la couleur d'un tampon uniforme, et son groupe.
struct UniformDraw
{
    PipelineHandle pipeline;
    BindGroupHandle group;
    TextureHandle target;

    UniformDraw(Gpu &gpu, BufferHandle ubo)
    {
        const ShaderHandle vs = gpu.shader(ShaderStage::kVertex, kFullscreenVsGlsl450);
        const ShaderHandle fs = gpu.shader(ShaderStage::kFragment, kUniformColorFs);
        pipeline = gpu.pipeline(vs, fs);
        BindGroupDesc gd;
        gd.entries = {{.binding = 0, .type = BindingType::kUniformBuffer, .buffer = ubo}};
        auto g = gpu->createBindGroup(gd);
        REQUIRE(g.has_value());
        group = *g;
        target = gpu.target(Format::kRGBA8Unorm, {2, 2});
    }

    void draw(Gpu &gpu) const
    {
        gpu.run(
            [this](CommandEncoder &e)
            {
                e.beginRenderPass(passOn(target));
                e.setPipeline(pipeline);
                e.setBindGroup(0, group);
                e.draw(3);
                e.endRenderPass();
            });
    }
};

} // namespace

TEST_SUITE("rhi/vulkan/transfer")
{
    TEST_CASE("updates go through the persistent staging ring: no staging buffer per update, very large ones get their own")
    {
        if (skipWithoutVulkan())
            return;
        Gpu gpu(Backend::kVulkan);
        REQUIRE(gpu.device);
        auto &device = static_cast<vk::VulkanDevice &>(*gpu);
        const BufferHandle buffer = gpu.buffer(BufferUsage::kStorage | BufferUsage::kCopySrc, {}, 64 * 1024, "target");
        std::vector<std::byte> expected(64 * 1024);
        const vk::TransferCounters before = device.transferCounters();
        constexpr int kFrames = 300;
        for (int frame = 0; frame < kFrames; ++frame)
        {
            REQUIRE(gpu->beginFrame().has_value());
            const auto data = pattern(1000, static_cast<std::uint8_t>(frame));
            const std::size_t offset = static_cast<std::size_t>(frame % 64) * 1000;
            REQUIRE(gpu->updateBuffer(buffer, offset, data).has_value());
            std::memcpy(expected.data() + offset, data.data(), data.size());
            REQUIRE(gpu->present().has_value());
        }
        const vk::TransferCounters after = device.transferCounters();
        CHECK(after.ringUploads - before.ringUploads == kFrames);
        CHECK(after.dedicatedStaging == before.dedicatedStaging); // avant l'anneau : un tampon de transfert par mise à jour
        CHECK(after.stagingBlocks - before.stagingBlocks <= 1);     // 300 Kio en tout : un bloc, recyclé
        auto read = gpu->readBuffer(buffer, 0, expected.size());
        REQUIRE(read.has_value());
        CHECK(*read == expected);

        // Au-delà du seuil : un tampon dédié, libéré avec le lot.
        BufferDesc big;
        big.size = vk::kStagingDedicatedThreshold + 4096;
        big.usage = BufferUsage::kStorage | BufferUsage::kCopySrc;
        big.debugName = "big";
        auto bigBuffer = gpu->createBuffer(big);
        REQUIRE(bigBuffer.has_value());
        const auto bigData = pattern(big.size, 7);
        REQUIRE(gpu->updateBuffer(*bigBuffer, 0, bigData).has_value());
        CHECK(device.transferCounters().dedicatedStaging == after.dedicatedStaging + 1);
        auto bigRead = gpu->readBuffer(*bigBuffer, 0, big.size);
        REQUIRE(bigRead.has_value());
        CHECK(*bigRead == bigData);
        gpu->waitIdle();
        gpu.expectNoValidationErrors();
    }

    TEST_CASE("copies of one batch get a barrier only when their ranges overlap, and the last update wins")
    {
        if (skipWithoutVulkan())
            return;
        Gpu gpu(Backend::kVulkan);
        REQUIRE(gpu.device);
        auto &device = static_cast<vk::VulkanDevice &>(*gpu);
        const BufferHandle a = gpu.buffer(BufferUsage::kStorage | BufferUsage::kCopySrc, {}, 4096, "a");
        const BufferHandle b = gpu.buffer(BufferUsage::kStorage | BufferUsage::kCopySrc, {}, 4096, "b");
        REQUIRE(gpu->present().has_value());
        const std::uint64_t barriers = device.transferCounters().copyBarriers;
        // Huit plages disjointes de `a` et la même plage de `b` : aucune barrière entre ces copies.
        for (std::uint64_t i = 0; i < 8; ++i)
            REQUIRE(gpu->updateBuffer(a, i * 256, pattern(256, static_cast<std::uint8_t>(i))).has_value());
        REQUIRE(gpu->updateBuffer(b, 0, pattern(256, 99)).has_value());
        CHECK(device.transferCounters().copyBarriers == barriers);
        // Deux écritures qui se recouvrent dans le même lot : une barrière, et la seconde gagne.
        REQUIRE(gpu->updateBuffer(a, 100, pattern(300, 1)).has_value());
        CHECK(device.transferCounters().copyBarriers == barriers + 1);
        const auto last = pattern(300, 200);
        REQUIRE(gpu->updateBuffer(a, 128, last).has_value()); // recouvre aussi la précédente : barrière
        CHECK(device.transferCounters().copyBarriers == barriers + 2);
        auto read = gpu->readBuffer(a, 128, last.size());
        REQUIRE(read.has_value());
        CHECK(*read == last);
        auto first = gpu->readBuffer(a, 0, 100);
        REQUIRE(first.has_value());
        const auto untouched = pattern(256, 0); // première copie, avant les plages recouvertes
        CHECK(std::equal(first->begin(), first->end(), untouched.begin()));
        gpu->waitIdle();
        gpu.expectNoValidationErrors();
    }

    TEST_CASE("pending uploads ride with the next submission: one vkQueueSubmit per frame, the draw sees the update")
    {
        if (skipWithoutVulkan())
            return;
        Gpu gpu(Backend::kVulkan);
        REQUIRE(gpu.device);
        auto &device = static_cast<vk::VulkanDevice &>(*gpu);
        const float black[4] = {0, 0, 0, 1};
        const BufferHandle ubo = gpu.buffer(BufferUsage::kUniform, bytesOf(black), 0, "ubo");
        const UniformDraw drawing(gpu, ubo);
        REQUIRE(gpu->present().has_value());
        const std::uint64_t submits = device.transferCounters().submits;
        constexpr int kFrames = 20;
        for (int frame = 0; frame < kFrames; ++frame)
        {
            REQUIRE(gpu->beginFrame().has_value());
            const float color[4] = {frame % 2 == 0 ? 1.0f : 0.0f, 0, frame % 2 == 0 ? 0.0f : 1.0f, 1};
            REQUIRE(gpu->updateBuffer(ubo, 0, bytesOf(color)).has_value());
            drawing.draw(gpu);
            REQUIRE(gpu->present().has_value());
        }
        // Avant : le lot de transferts partait dans sa propre soumission (deux par image).
        CHECK(device.transferCounters().submits - submits == kFrames);
        // Dernière image (impaire) : bleu.
        CHECK(rgba(gpu.pixel(drawing.target, 0, 0)) == std::array<std::uint8_t, 4>{0, 0, 255, 255});
        gpu.expectNoValidationErrors();
    }

    TEST_CASE("timestamps already available are read without a submission")
    {
        if (skipWithoutVulkan())
            return;
        Gpu gpu(Backend::kVulkan);
        REQUIRE(gpu.device);
        auto &device = static_cast<vk::VulkanDevice &>(*gpu);
        if (!gpu->capabilities().timestampQueries)
        {
            MESSAGE("Skipped: no timestamp queries");
            return;
        }
        auto pool = gpu->createQueryPool(2, "timestamps");
        REQUIRE(pool.has_value());
        gpu.run(
            [&](CommandEncoder &e)
            {
                e.writeTimestamp(*pool, 0);
                e.writeTimestamp(*pool, 1);
            });
        gpu->waitIdle();
        const BufferHandle buffer = gpu.buffer(BufferUsage::kStorage | BufferUsage::kCopySrc, {}, 256, "pending");
        REQUIRE(gpu->updateBuffer(buffer, 0, pattern(64, 3)).has_value()); // un lot en attente
        const std::uint64_t submits = device.transferCounters().submits;
        auto ready = gpu->timestampsReady(*pool, 0, 2);
        REQUIRE(ready.has_value());
        CHECK(*ready);
        auto values = gpu->readTimestamps(*pool, 0, 2);
        REQUIRE(values.has_value());
        CHECK((*values)[1] >= (*values)[0]);
        CHECK(device.transferCounters().submits == submits); // avant : readTimestamps soumettait le lot en attente
        auto read = gpu->readBuffer(buffer, 0, 64);              // le lot part ici, avec la lecture
        REQUIRE(read.has_value());
        CHECK(*read == pattern(64, 3));
        CHECK(device.transferCounters().submits == submits + 1);
        gpu.expectNoValidationErrors();
    }

    TEST_CASE("updateBuffer on a kUpload buffer writes its mapped memory directly, without transfer or submission")
    {
        if (skipWithoutVulkan())
            return;
        Gpu gpu(Backend::kVulkan);
        REQUIRE(gpu.device);
        auto &device = static_cast<vk::VulkanDevice &>(*gpu);
        const float black[4] = {0, 0, 0, 1};
        // Aucun travail du GPU n'utilise le tampon (rien de soumis depuis sa création) : écriture directe.
        const BufferHandle ubo = gpu.buffer(BufferUsage::kUniform, bytesOf(black), 0, "ubo", MemoryKind::kUpload);
        const UniformDraw drawing(gpu, ubo);
        REQUIRE(gpu->present().has_value());
        const vk::TransferCounters before = device.transferCounters();
        const float green[4] = {0, 1, 0, 1};
        REQUIRE(gpu->updateBuffer(ubo, 0, bytesOf(green)).has_value());
        const vk::TransferCounters written = device.transferCounters();
        CHECK(written.directWrites == before.directWrites + 1);
        CHECK(written.ringUploads == before.ringUploads);
        REQUIRE(gpu->present().has_value()); // aucun lot à soumettre
        CHECK(device.transferCounters().submits == before.submits);
        drawing.draw(gpu);
        CHECK(rgba(gpu.pixel(drawing.target, 0, 0)) == std::array<std::uint8_t, 4>{0, 255, 0, 255});
        auto read = gpu->readBuffer(ubo, 0, sizeof(green));
        REQUIRE(read.has_value());
        CHECK(std::memcmp(read->data(), green, sizeof(green)) == 0);
        gpu.expectNoValidationErrors();
    }

    TEST_CASE("updateBuffer on a kUpload buffer still used by a submission in flight takes the ordered path, with the same result")
    {
        if (skipWithoutVulkan())
            return;
        Gpu gpu(Backend::kVulkan);
        REQUIRE(gpu.device);
        auto &device = static_cast<vk::VulkanDevice &>(*gpu);
        const float red[4] = {1, 0, 0, 1};
        const float green[4] = {0, 1, 0, 1};
        const float blue[4] = {0, 0, 1, 1};
        const BufferHandle ubo = gpu.buffer(BufferUsage::kUniform, bytesOf(red), 0, "ubo", MemoryKind::kUpload);
        const UniformDraw drawing(gpu, ubo);
        gpu->waitIdle();

        // Déterministe : le tampon est marqué utilisé par une série pas encore finie (lot en vol) -> copie ordonnée, pas d'écriture
        // directe ; la copie en attente marque à son tour le tampon, donc la mise à jour suivante est ordonnée elle aussi.
        vk::BufferRes *res = device.get(ubo);
        REQUIRE(res != nullptr);
        res->gpuUse = device.lastSubmitted() + 1;
        const vk::TransferCounters before = device.transferCounters();
        REQUIRE(gpu->updateBuffer(ubo, 0, bytesOf(blue)).has_value());
        REQUIRE(gpu->updateBuffer(ubo, 0, bytesOf(green)).has_value());
        const vk::TransferCounters after = device.transferCounters();
        CHECK(after.orderedUploadWrites == before.orderedUploadWrites + 2);
        CHECK(after.directWrites == before.directWrites);
        drawing.draw(gpu); // le lot (bleu puis vert) part en tête de cette soumission
        CHECK(rgba(gpu.pixel(drawing.target, 0, 0)) == std::array<std::uint8_t, 4>{0, 255, 0, 255});
        gpu->waitIdle();
        REQUIRE(gpu->updateBuffer(ubo, 0, bytesOf(red)).has_value()); // GPU fini : écriture directe de nouveau
        CHECK(device.transferCounters().directWrites == after.directWrites + 1);

        // Réel : un dessin soumis (peut-être encore en vol) lit la couleur courante ; la réécrire juste après ne doit jamais changer
        // ce qu'il dessine (ancien chemin : copie ordonnée avec les soumissions).
        int ordered = 0;
        for (int i = 0; i < 20; ++i)
        {
            const float *before = i % 2 == 0 ? red : blue;
            const float *next = i % 2 == 0 ? blue : red;
            REQUIRE(gpu->updateBuffer(ubo, 0, std::as_bytes(std::span(before, 4))).has_value());
            drawing.draw(gpu);
            const std::uint64_t orderedBefore = device.transferCounters().orderedUploadWrites;
            REQUIRE(gpu->updateBuffer(ubo, 0, std::as_bytes(std::span(next, 4))).has_value());
            ordered += device.transferCounters().orderedUploadWrites > orderedBefore ? 1 : 0;
            const std::array<std::uint8_t, 4> drawn = rgba(gpu.pixel(drawing.target, 0, 0));
            CHECK(drawn == (i % 2 == 0 ? std::array<std::uint8_t, 4>{255, 0, 0, 255} : std::array<std::uint8_t, 4>{0, 0, 255, 255}));
        }
        MESSAGE("updates of a kUpload buffer read by a submission still in flight: ", ordered, " / 20 took the ordered path");
        gpu.expectNoValidationErrors();
    }

    TEST_CASE("texture uploads after an odd-sized buffer update land at an aligned staging offset")
    {
        if (skipWithoutVulkan())
            return;
        Gpu gpu(Backend::kVulkan);
        REQUIRE(gpu.device);
        const BufferHandle buffer = gpu.buffer(BufferUsage::kStorage | BufferUsage::kCopySrc, {}, 64, "odd");
        REQUIRE(gpu->updateBuffer(buffer, 0, pattern(3, 1)).has_value()); // place de 3 octets dans l'anneau
        TextureDesc desc;
        desc.format = Format::kRGBA32Float; // 16 octets par texel
        desc.size = {4, 4, 1};
        desc.mipLevels = 1;
        desc.usage = TextureUsage::kSampled | TextureUsage::kCopySrc | TextureUsage::kCopyDst;
        desc.debugName = "rgba32f";
        auto texture = gpu->createTexture(desc);
        REQUIRE(texture.has_value());
        const auto texels = pattern(4 * 4 * 16, 5);
        REQUIRE(gpu->updateTexture(*texture, {}, texels).has_value());
        auto read = gpu->readTexture(*texture);
        REQUIRE(read.has_value());
        CHECK(*read == texels);
        gpu.expectNoValidationErrors(); // un bufferOffset mal aligné serait signalé par la validation
    }

    TEST_CASE("command buffers come from one pool per frame in flight, reset as a whole")
    {
        if (skipWithoutVulkan())
            return;
        Gpu gpu(Backend::kVulkan);
        REQUIRE(gpu.device);
        auto &device = static_cast<vk::VulkanDevice &>(*gpu);
        const TextureHandle target = gpu.target(Format::kRGBA8Unorm, {2, 2});
        REQUIRE(gpu->present().has_value());
        const vk::TransferCounters before = device.transferCounters();
        constexpr int kFrames = 100;
        for (int frame = 0; frame < kFrames; ++frame)
        {
            REQUIRE(gpu->beginFrame().has_value());
            for (int i = 0; i < 3; ++i)
                gpu.run([&](CommandEncoder &e) { e.beginRenderPass(passOn(target)); e.endRenderPass(); });
            {
                auto dropped = gpu->createCommandEncoder(); // abandonné : rendu à sa réserve sans soumission
                dropped->beginRenderPass(passOn(target));
            }
            REQUIRE(gpu->present().has_value());
        }
        const vk::TransferCounters after = device.transferCounters();
        CHECK(after.commandPools <= vk::kFramesInFlight + 2);
        CHECK(after.commandBuffersAllocated <= (vk::kFramesInFlight + 2) * 4);
        CHECK(after.commandPoolResets - before.commandPoolResets >= kFrames - (vk::kFramesInFlight + 2)); // une remise à zéro par image
        gpu->waitIdle();
        gpu.expectNoValidationErrors();
    }
}
