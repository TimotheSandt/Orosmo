// Backend Vulkan (src/rhi/vulkan/) : création, capacités, ressources, envois et lectures synchrones, image de fond. Les tests
// communs aux backends (`rhi_device_tests.cpp`) couvrent le contrat du RHI ; ceux-ci ne regardent que ce qui est propre à Vulkan.
#include <doctest/doctest.h>

#include "support/result_support.hpp"
#include "rhi/gpu_fixture.hpp"
#include "support/test_support.hpp"

#include <orosmo/rhi/device.hpp>

#include <array>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace orosmo::rhi;
using orosmo::core::ErrorCode;

namespace
{

std::unique_ptr<Device> makeVulkan(Extent2D size = {8, 8})
{
    if (!isBackendCompiled(Backend::kVulkan))
        return nullptr;
    DeviceDesc desc;
    desc.backend = Backend::kVulkan;
    desc.surface.size = size;
    auto created = createDevice(desc);
    if (!created)
    {
        MESSAGE("no Vulkan 1.3 device: ", created.error().message);
        return nullptr;
    }
    return std::move(*created);
}

std::span<const std::byte> bytesOf(const std::vector<std::uint8_t> &v) { return std::as_bytes(std::span(v.data(), v.size())); }

} // namespace

TEST_SUITE("rhi/vulkan/device")
{
    TEST_CASE("a Vulkan device reports its capabilities and no validation message")
    {
        auto device = makeVulkan();
        SKIP_TEST_IF_NULL_FOR(device.get(), "vulkan", "Vulkan 1.3 device");
        const Capabilities &caps = device->capabilities();
        CHECK((caps.backend == Backend::kVulkan));
        CHECK_FALSE(caps.deviceName.empty());
        CHECK(caps.spirvShaders);
        CHECK(caps.compute);
        CHECK(caps.maxColorAttachments >= 1);
        CHECK(caps.maxColorAttachments <= 8);
        CHECK(caps.maxTextureSize >= 4096);
        CHECK(caps.maxPushConstantSize >= 128);
        CHECK(device->validationErrorCount() == 0);
    }

    TEST_CASE("buffers: GPU-only and upload buffers round-trip, ranges are checked, stale handles are refused")
    {
        auto device = makeVulkan();
        SKIP_TEST_IF_NULL_FOR(device.get(), "vulkan", "Vulkan 1.3 device");
        std::vector<std::uint8_t> data(256);
        for (std::size_t i = 0; i < data.size(); ++i)
            data[i] = static_cast<std::uint8_t>(i * 7 + 3);

        BufferDesc desc;
        desc.size = data.size();
        desc.usage = BufferUsage::kStorage | BufferUsage::kCopySrc;
        desc.debugName = "gpu-only";
        auto gpuOnly = device->createBuffer(desc, bytesOf(data));
        REQUIRE(gpuOnly.has_value());
        auto read = device->readBuffer(*gpuOnly, 0, data.size());
        REQUIRE(read.has_value());
        CHECK(std::memcmp(read->data(), data.data(), data.size()) == 0);

        const std::array<std::uint8_t, 4> patch{9, 8, 7, 6};
        REQUIRE(device->updateBuffer(*gpuOnly, 100, std::as_bytes(std::span(patch))).has_value());
        auto patched = device->readBuffer(*gpuOnly, 98, 8);
        REQUIRE(patched.has_value());
        CHECK(static_cast<std::uint8_t>((*patched)[0]) == data[98]);
        CHECK(static_cast<std::uint8_t>((*patched)[2]) == 9);
        CHECK(static_cast<std::uint8_t>((*patched)[5]) == 6);
        CHECK(static_cast<std::uint8_t>((*patched)[6]) == data[104]);

        desc.memory = MemoryKind::kUpload;
        desc.debugName = "upload";
        auto upload = device->createBuffer(desc, bytesOf(data));
        REQUIRE(upload.has_value());
        auto uploadRead = device->readBuffer(*upload, 0, 16);
        REQUIRE(uploadRead.has_value());
        CHECK(std::memcmp(uploadRead->data(), data.data(), 16) == 0);

        CHECK((tests::errorOf(device->updateBuffer(*gpuOnly, 254, std::as_bytes(std::span(patch)))) == ErrorCode::kOutOfRange));
        CHECK((tests::errorOf(device->readBuffer(*gpuOnly, 0, 257)) == ErrorCode::kOutOfRange));
        device->destroy(*gpuOnly);
        CHECK((tests::errorOf(device->readBuffer(*gpuOnly, 0, 4)) == ErrorCode::kNotFound));
        BufferDesc bad;
        CHECK((tests::errorOf(device->createBuffer(bad)) == ErrorCode::kInvalidArgument));
        device->waitIdle();
        CHECK(device->validationErrorCount() == 0);
    }

    TEST_CASE("textures: upload and read back whole levels and sub-regions, with mips, arrays and 3D")
    {
        auto device = makeVulkan();
        SKIP_TEST_IF_NULL_FOR(device.get(), "vulkan", "Vulkan 1.3 device");
        std::vector<std::uint8_t> texels(4 * 4 * 4);
        for (std::size_t i = 0; i < texels.size(); ++i)
            texels[i] = static_cast<std::uint8_t>(i);

        TextureDesc desc;
        desc.format = Format::kRGBA8Unorm;
        desc.size = {4, 4, 1};
        desc.mipLevels = 0; // chaîne complète : 3 niveaux
        desc.usage = TextureUsage::kSampled | TextureUsage::kCopySrc | TextureUsage::kCopyDst;
        desc.debugName = "2d";
        auto texture = device->createTexture(desc);
        REQUIRE(texture.has_value());
        auto resolved = device->textureDesc(*texture);
        REQUIRE(resolved.has_value());
        CHECK(resolved->mipLevels == 3);
        REQUIRE(device->updateTexture(*texture, {}, bytesOf(texels)).has_value());
        auto all = device->readTexture(*texture);
        REQUIRE(all.has_value());
        REQUIRE(all->size() == texels.size());
        CHECK(std::memcmp(all->data(), texels.data(), texels.size()) == 0);

        TextureCopy region;
        region.offset = {1, 2, 0};
        region.size = {2, 1, 1};
        auto sub = device->readTexture(*texture, region);
        REQUIRE(sub.has_value());
        REQUIRE(sub->size() == 8);
        CHECK(static_cast<std::uint8_t>((*sub)[0]) == texels[(2 * 4 + 1) * 4]);
        CHECK(static_cast<std::uint8_t>((*sub)[7]) == texels[(2 * 4 + 2) * 4 + 3]);
        TextureCopy mip1;
        mip1.mipLevel = 1;
        auto level1 = device->readTexture(*texture, mip1);
        REQUIRE(level1.has_value());
        CHECK(level1->size() == 2 * 2 * 4);
        mip1.mipLevel = 3;
        CHECK((tests::errorOf(device->readTexture(*texture, mip1)) == ErrorCode::kOutOfRange));

        TextureDesc array = desc;
        array.dimension = TextureDimension::k2DArray;
        array.layers = 3;
        array.mipLevels = 1;
        array.debugName = "array";
        auto layered = device->createTexture(array);
        REQUIRE(layered.has_value());
        TextureCopy layer2;
        layer2.layer = 2;
        layer2.size = {4, 4, 1};
        REQUIRE(device->updateTexture(*layered, layer2, bytesOf(texels)).has_value());
        auto back = device->readTexture(*layered, layer2);
        REQUIRE(back.has_value());
        CHECK(std::memcmp(back->data(), texels.data(), texels.size()) == 0);
        auto layer0 = device->readTexture(*layered, TextureCopy{.layer = 0, .size = {4, 4, 1}});
        REQUIRE(layer0.has_value());

        TextureDesc volume = desc;
        volume.dimension = TextureDimension::k3D;
        volume.size = {4, 4, 2};
        volume.mipLevels = 1;
        volume.debugName = "3d";
        CHECK(device->createTexture(volume).has_value());
        TextureDesc cube = desc;
        cube.dimension = TextureDimension::kCube;
        cube.size = {4, 2, 1};
        CHECK((tests::errorOf(device->createTexture(cube)) == ErrorCode::kInvalidArgument));
        TextureDesc empty = desc;
        empty.size = {0, 4, 1};
        CHECK((tests::errorOf(device->createTexture(empty)) == ErrorCode::kInvalidArgument));
        device->waitIdle();
        CHECK(device->validationErrorCount() == 0);
    }

    TEST_CASE("the offscreen backbuffer is a RGBA8 texture of the surface size; resize replaces it under the same handle")
    {
        auto device = makeVulkan({16, 8});
        SKIP_TEST_IF_NULL_FOR(device.get(), "vulkan", "Vulkan 1.3 device");
        auto frame = device->beginFrame();
        REQUIRE(frame.has_value());
        auto desc = device->textureDesc(*frame);
        REQUIRE(desc.has_value());
        CHECK((desc->size == Extent3D{16, 8, 1}));
        CHECK((desc->format == Format::kRGBA8Unorm));
        REQUIRE(device->present().has_value());
        REQUIRE(device->resize({4, 4}).has_value());
        auto next = device->beginFrame();
        REQUIRE(next.has_value());
        CHECK(*next == *frame);
        auto resized = device->textureDesc(*next);
        REQUIRE(resized.has_value());
        CHECK((resized->size == Extent3D{4, 4, 1}));
        auto pixels = device->readTexture(*next);
        REQUIRE(pixels.has_value());
        CHECK(pixels->size() == 4 * 4 * 4);
        REQUIRE(device->present().has_value());
        device->waitIdle();
        CHECK(device->validationErrorCount() == 0);
    }

    TEST_CASE("samplers and timestamp pools: creation, limits and the not-ready state before any write")
    {
        auto device = makeVulkan();
        SKIP_TEST_IF_NULL_FOR(device.get(), "vulkan", "Vulkan 1.3 device");
        SamplerDesc sampler;
        sampler.debugName = "linear";
        CHECK(device->createSampler(sampler).has_value());
        SamplerDesc aniso;
        aniso.maxAnisotropy = 8.0f;
        if (device->capabilities().anisotropy)
            CHECK(device->createSampler(aniso).has_value());
        else
            CHECK((tests::errorOf(device->createSampler(aniso)) == ErrorCode::kUnsupported));
        if (device->capabilities().timestampQueries)
        {
            auto pool = device->createQueryPool(4, "timestamps");
            REQUIRE(pool.has_value());
            auto ready = device->timestampsReady(*pool, 0, 2);
            REQUIRE(ready.has_value());
            CHECK_FALSE(*ready);
            CHECK((tests::errorOf(device->timestampsReady(*pool, 3, 2)) == ErrorCode::kOutOfRange));
        }
        CHECK(device->validationErrorCount() == 0);
    }

    TEST_CASE("destroyed resources leave the live counts at once and the device stops cleanly with resources left")
    {
        auto device = makeVulkan();
        SKIP_TEST_IF_NULL_FOR(device.get(), "vulkan", "Vulkan 1.3 device");
        BufferDesc desc;
        desc.size = 64;
        desc.usage = BufferUsage::kUniform;
        auto a = device->createBuffer(desc);
        auto b = device->createBuffer(desc);
        REQUIRE((a.has_value() && b.has_value()));
        CHECK(device->stats().buffers == 2);
        device->destroy(*a);
        CHECK(device->stats().buffers == 1);
        device->destroy(*a); // périmé : sans effet
        CHECK(device->stats().buffers == 1);
        // `b` reste vivant : l'arrêt du périphérique le libère
    }

    TEST_CASE("a non-zero z offset is refused for a 2D copy")
    {
        auto device = makeVulkan();
        SKIP_TEST_IF_NULL_FOR(device.get(), "vulkan", "Vulkan 1.3 device");
        TextureDesc desc;
        desc.format = Format::kRGBA8Unorm;
        desc.size = {4, 4, 1};
        desc.usage = TextureUsage::kSampled | TextureUsage::kCopySrc | TextureUsage::kCopyDst;
        auto texture = device->createTexture(desc);
        REQUIRE(texture.has_value());
        TextureCopy copy;
        copy.offset = {0, 0, 1};
        copy.size = {1, 1, 1};
        const std::array<std::uint8_t, 4> texel{1, 2, 3, 4};
        CHECK((tests::errorOf(device->updateTexture(*texture, copy, std::as_bytes(std::span(texel)))) == ErrorCode::kInvalidArgument));
        CHECK((tests::errorOf(device->readTexture(*texture, copy)) == ErrorCode::kInvalidArgument));
        CHECK(device->validationErrorCount() == 0);
    }

    TEST_CASE("a buffer destroyed right after an unsubmitted update is kept until that copy is done")
    {
        auto device = makeVulkan();
        SKIP_TEST_IF_NULL_FOR(device.get(), "vulkan", "Vulkan 1.3 device");
        BufferDesc desc;
        desc.size = 64;
        desc.usage = BufferUsage::kStorage | BufferUsage::kCopySrc;
        auto buffer = device->createBuffer(desc);
        REQUIRE(buffer.has_value());
        const std::array<std::uint8_t, 4> patch{1, 2, 3, 4};
        REQUIRE(device->updateBuffer(*buffer, 0, std::as_bytes(std::span(patch))).has_value());
        device->destroy(*buffer); // la copie est dans un lot encore ouvert
        for (int i = 0; i < 4; ++i)
        {
            REQUIRE(device->beginFrame().has_value());
            REQUIRE(device->present().has_value());
        }
        device->waitIdle();
        CHECK(device->validationErrorCount() == 0);
    }

#ifndef _WIN32
    TEST_CASE("an out-of-range ENGINE_VULKAN_DEVICE index does not throw")
    {
        setenv("ENGINE_VULKAN_DEVICE", "99999999999999999999999999999999", 1);
        std::unique_ptr<Device> device;
        CHECK_NOTHROW(device = makeVulkan());
        unsetenv("ENGINE_VULKAN_DEVICE");
    }
#endif

    TEST_CASE("encoder: several slices of one 3D mip are transitioned once, and the validation layers stay silent")
    {
        Gpu gpu(Backend::kVulkan);
        SKIP_TEST_IF_NULL_FOR(gpu.device.get(), "vulkan", "Vulkan 1.3 device");
        if (gpu->capabilities().maxColorAttachments < 2)
            return;
        TextureDesc td;
        td.dimension = TextureDimension::k3D;
        td.size = {4, 4, 2};
        td.usage = TextureUsage::kRenderTarget | TextureUsage::kCopySrc;
        auto volume = gpu->createTexture(td);
        REQUIRE(volume.has_value());
        const char *fs = R"(#version 450
layout(location = 0) out vec4 a;
layout(location = 1) out vec4 b;
void main() { a = vec4(1.0, 0.0, 0.0, 1.0); b = vec4(0.0, 1.0, 0.0, 1.0); })";
        const PipelineHandle pipe = gpu.pipeline(gpu.shader(ShaderStage::kVertex, kFullscreenVsGlsl450), gpu.shader(ShaderStage::kFragment, fs),
                                                 {ColorTargetState{}, ColorTargetState{}});
        RenderPassDesc pass;
        pass.colors.push_back({.texture = *volume, .layer = 0});
        pass.colors.push_back({.texture = *volume, .layer = 1});
        gpu.run(
            [&](CommandEncoder &e)
            {
                e.beginRenderPass(pass);
                e.setPipeline(pipe);
                e.draw(3);
                e.endRenderPass();
            });
        TextureCopy c;
        c.offset = {1, 1, 1};
        c.size = {1, 1, 1};
        auto px = gpu->readTexture(*volume, c);
        REQUIRE(px.has_value());
        CHECK(rgba(*px) == std::array<std::uint8_t, 4>{0, 255, 0, 255});
        gpu.expectNoValidationErrors();
    }

    TEST_CASE("encoder: offsets equal to the buffer size and a non-zero dynamic offset on a 'to the end' entry are refused")
    {
        Gpu gpu(Backend::kVulkan);
        SKIP_TEST_IF_NULL_FOR(gpu.device.get(), "vulkan", "Vulkan 1.3 device");
        const std::vector<std::byte> data(256, std::byte{0});
        const BufferHandle vertex = gpu.buffer(BufferUsage::kVertex | BufferUsage::kIndex, data, 0, "vi");
        auto vbad = gpu->createCommandEncoder();
        vbad->setVertexBuffer(0, vertex, 256);
        CHECK((tests::errorOf(gpu->submit(std::move(vbad))) == ErrorCode::kOutOfRange));
        auto ibad = gpu->createCommandEncoder();
        ibad->setIndexBuffer(vertex, IndexType::kUint16, 256);
        CHECK((tests::errorOf(gpu->submit(std::move(ibad))) == ErrorCode::kOutOfRange));

        const std::uint64_t align = gpu->capabilities().minUniformAlignment;
        const BufferHandle ubuf = gpu.buffer(BufferUsage::kUniform, std::vector<std::byte>(align * 2, std::byte{0}), 0, "u");
        const char *fs = R"(#version 450
layout(std140, set = 0, binding = 0) uniform U { vec4 color; } u;
layout(location = 0) out vec4 outColor;
void main() { outColor = u.color; })";
        const PipelineHandle pipe = gpu.pipeline(gpu.shader(ShaderStage::kVertex, kFullscreenVsGlsl450), gpu.shader(ShaderStage::kFragment, fs));
        BindGroupDesc gd;
        gd.entries = {{.binding = 0, .type = BindingType::kUniformBuffer, .buffer = ubuf, .size = 0, .dynamicOffset = true}};
        auto group = gpu->createBindGroup(gd);
        REQUIRE(group.has_value());
        const auto offset = static_cast<std::uint32_t>(align);
        auto bad = gpu->createCommandEncoder();
        bad->setBindGroup(0, *group, std::span(&offset, 1));
        CHECK((tests::errorOf(gpu->submit(std::move(bad))) == ErrorCode::kUnsupported));
        const std::uint32_t zero = 0;
        auto good = gpu->createCommandEncoder();
        good->setBindGroup(0, *group, std::span(&zero, 1));
        CHECK(gpu->submit(std::move(good)).has_value());
        (void)pipe;
    }

    TEST_CASE("encoder: a texture destroyed after setBindGroup makes the draw fail instead of using the stale descriptor set")
    {
        Gpu gpu(Backend::kVulkan);
        SKIP_TEST_IF_NULL_FOR(gpu.device.get(), "vulkan", "Vulkan 1.3 device");
        TextureDesc td;
        td.size = {2, 2, 1};
        td.usage = TextureUsage::kSampled | TextureUsage::kCopyDst;
        auto tex = gpu->createTexture(td);
        auto sampler = gpu->createSampler({});
        REQUIRE((tex.has_value() && sampler.has_value()));
        const char *fs = R"(#version 450
layout(set = 0, binding = 0) uniform sampler2D tex;
layout(location = 0) out vec4 outColor;
void main() { outColor = texture(tex, vec2(0.5)); })";
        const PipelineHandle pipe = gpu.pipeline(gpu.shader(ShaderStage::kVertex, kFullscreenVsGlsl450), gpu.shader(ShaderStage::kFragment, fs));
        BindGroupDesc gd;
        gd.entries = {{.binding = 0, .type = BindingType::kSampledTexture, .texture = *tex, .sampler = *sampler}};
        auto group = gpu->createBindGroup(gd);
        REQUIRE(group.has_value());
        const TextureHandle color = gpu.target(Format::kRGBA8Unorm, {4, 4});
        auto encoder = gpu->createCommandEncoder();
        encoder->beginRenderPass(passOn(color));
        encoder->setPipeline(pipe);
        encoder->setBindGroup(0, *group);
        encoder->draw(3); // crée et met en cache le jeu de descripteurs
        gpu->destroy(*tex);
        encoder->draw(3); // le groupe référence une texture détruite
        encoder->endRenderPass();
        CHECK((tests::errorOf(gpu->submit(std::move(encoder))) == ErrorCode::kNotFound));
    }

    TEST_CASE("pipelines: array bindings that overlap another binding after the stages are merged are refused")
    {
        Gpu gpu(Backend::kVulkan);
        SKIP_TEST_IF_NULL_FOR(gpu.device.get(), "vulkan", "Vulkan 1.3 device");
        const char *vs = R"(#version 450
layout(std140, set = 0, binding = 0) uniform A { vec4 a; } arr[2];
void main() { gl_Position = arr[0].a + arr[1].a; })";
        const char *fs = R"(#version 450
layout(std140, set = 0, binding = 1) uniform B { vec4 b; } single;
layout(location = 0) out vec4 outColor;
void main() { outColor = single.b; })";
        GraphicsPipelineDesc d;
        d.vertex = gpu.shader(ShaderStage::kVertex, vs);
        d.fragment = gpu.shader(ShaderStage::kFragment, fs);
        d.colorTargets = {ColorTargetState{}};
        auto r = gpu->createGraphicsPipeline(d);
        REQUIRE_FALSE(r.has_value());
        CHECK((r.error().code == ErrorCode::kInvalidArgument));
    }
}
