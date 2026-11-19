// G98 (revue CodeRabbit de la PR #7) : état implicite d'OpenGL que le RHI doit contenir, validations sans débordement,
// fermeture des portées à `submit`, régions de copie. Chaque cas vise un défaut relevé par la revue.
#include <doctest/doctest.h>

#include "support/test_support.hpp"
#include "rhi/gpu_fixture.hpp"

#include <cmath>
#include <string>
#include <cstring>
#include <limits>

namespace
{

constexpr const char *kSampleFs = R"(#version 430 core
layout(binding = 8) uniform sampler2D tex;
layout(location = 0) out vec4 outColor;
void main() { outColor = texture(tex, vec2(0.5)); })";

TextureHandle solidTexture(Gpu &gpu, std::uint8_t r, std::uint8_t g, std::uint8_t b, const char *name)
{
    TextureDesc d;
    d.size = {2, 2, 1};
    d.usage = TextureUsage::kSampled | TextureUsage::kCopyDst;
    d.debugName = name;
    auto t = gpu->createTexture(d);
    REQUIRE(t.has_value());
    std::vector<std::uint8_t> texels(16, 255);
    for (std::size_t i = 0; i < 16; i += 4)
    {
        texels[i] = r;
        texels[i + 1] = g;
        texels[i + 2] = b;
    }
    REQUIRE(gpu->updateTexture(*t, {}, std::as_bytes(std::span(texels))).has_value());
    return *t;
}

} // namespace

TEST_SUITE("rhi/opengl/state")
{
    TEST_CASE("a texture update during a pass does not replace the texture bound by setBindGroup (reserved texture unit)")
    {
        Gpu gpu;
        SKIP_TEST_IF_NULL(gpu.device.get(), "OpenGL 4.3 context");
        const TextureHandle red = solidTexture(gpu, 255, 0, 0, "red");
        const TextureHandle green = solidTexture(gpu, 0, 255, 0, "green");
        auto sampler = gpu->createSampler({.minFilter = Filter::kNearest, .magFilter = Filter::kNearest, .mipFilter = std::nullopt});
        REQUIRE(sampler.has_value());
        BindGroupDesc gd;
        gd.entries = {{.binding = 0, .type = BindingType::kSampledTexture, .texture = red, .sampler = *sampler}};
        auto group = gpu->createBindGroup(gd);
        REQUIRE(group.has_value());
        const PipelineHandle pipe = gpu.pipeline(gpu.shader(ShaderStage::kVertex, kFullscreenVs), gpu.shader(ShaderStage::kFragment, kSampleFs));
        const TextureHandle out = gpu.target(Format::kRGBA8Unorm, {2, 2});
        const std::vector<std::uint8_t> blue{0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255};
        gpu.run(
            [&](CommandEncoder &e)
            {
                e.beginRenderPass(passOn(out));
                e.setPipeline(pipe);
                e.setBindGroup(1, *group); // rouge sur l'unité 8, devenue l'unité active
                REQUIRE(gpu->updateTexture(green, {}, std::as_bytes(std::span(blue))).has_value());
                e.draw(3);
                e.endRenderPass();
            });
        CHECK(rgba(gpu.pixel(out, 1, 1)) == std::array<std::uint8_t, 4>{255, 0, 0, 255});
        gpu.expectNoValidationErrors();
    }

    TEST_CASE("submit refuses an encoder whose render pass or debug group is still open, and leaves no scope behind")
    {
        Gpu gpu;
        SKIP_TEST_IF_NULL(gpu.device.get(), "OpenGL 4.3 context");
        const TextureHandle color = gpu.target(Format::kRGBA8Unorm, {2, 2});
        auto openPass = gpu->createCommandEncoder();
        openPass->beginRenderPass(passOn(color, {1, 0, 0, 1}));
        auto r1 = gpu->submit(std::move(openPass));
        REQUIRE_FALSE(r1.has_value());
        CHECK((tests::errorOf(r1) == ErrorCode::kInvalidArgument));

        auto openGroup = gpu->createCommandEncoder();
        openGroup->beginDebugGroup("left open");
        auto r2 = gpu->submit(std::move(openGroup));
        REQUIRE_FALSE(r2.has_value());
        CHECK((tests::errorOf(r2) == ErrorCode::kInvalidArgument));

        // Le contexte reste propre : une passe normale fonctionne ensuite.
        gpu.run([&](CommandEncoder &e) { e.beginRenderPass(passOn(color, {0, 1, 0, 1})); e.endRenderPass(); });
        CHECK(rgba(gpu.pixel(color, 0, 0)) == std::array<std::uint8_t, 4>{0, 255, 0, 255});
        gpu.expectNoValidationErrors();
    }

    TEST_CASE("a slice of a 3D texture is a render target; a slice beyond the depth of the mip level is refused")
    {
        Gpu gpu;
        SKIP_TEST_IF_NULL(gpu.device.get(), "OpenGL 4.3 context");
        TextureDesc d;
        d.dimension = TextureDimension::k3D;
        d.size = {4, 4, 4};
        d.mipLevels = 2; // le niveau 1 existe (2 tranches) : le refus plus bas vient de la tranche, pas du niveau
        d.usage = TextureUsage::kRenderTarget | TextureUsage::kCopySrc;
        d.debugName = "volume";
        auto volume = gpu->createTexture(d);
        REQUIRE(volume.has_value());
        gpu.run(
            [&](CommandEncoder &e)
            {
                RenderPassDesc pass;
                pass.colors.push_back({.texture = *volume, .layer = 2, .clearColor = {0, 0, 1, 1}});
                e.beginRenderPass(pass);
                e.endRenderPass();
            });
        TextureCopy slice;
        slice.offset = {1, 1, 2};
        slice.size = {1, 1, 1};
        auto px = gpu->readTexture(*volume, slice);
        REQUIRE(px.has_value());
        CHECK(rgba(*px) == std::array<std::uint8_t, 4>{0, 0, 255, 255});

        gpu.run(
            [&](CommandEncoder &e)
            {
                RenderPassDesc inside; // niveau 1, tranche 1 : la dernière qui existe
                inside.colors.push_back({.texture = *volume, .mipLevel = 1, .layer = 1});
                e.beginRenderPass(inside);
                e.endRenderPass();
            });
        auto beyond = gpu->createCommandEncoder();
        RenderPassDesc pass;
        pass.colors.push_back({.texture = *volume, .mipLevel = 1, .layer = 2}); // le niveau 1 n'a que 2 tranches
        beyond->beginRenderPass(pass);
        auto r = gpu->submit(std::move(beyond));
        REQUIRE_FALSE(r.has_value());
        CHECK((tests::errorOf(r) == ErrorCode::kOutOfRange));
        gpu.expectNoValidationErrors();
    }

    TEST_CASE("the clear value of an integer target must be representable (NaN, negative, 2^32 refused)")
    {
        Gpu gpu;
        SKIP_TEST_IF_NULL(gpu.device.get(), "OpenGL 4.3 context");
        const TextureHandle ids = gpu.target(Format::kR32Uint, {2, 2}, "ids");
        for (const float bad : {std::numeric_limits<float>::quiet_NaN(), -1.0f, 4294967296.0f})
        {
            auto e = gpu->createCommandEncoder();
            e->beginRenderPass(passOn(ids, {bad, 0, 0, 0}));
            e->endRenderPass(); // passe refermée : seule la valeur d'effacement peut faire échouer submit
            auto r = gpu->submit(std::move(e));
            REQUIRE_FALSE(r.has_value());
            CHECK((tests::errorOf(r) == ErrorCode::kInvalidArgument));
        }
        gpu.run([&](CommandEncoder &e) { e.beginRenderPass(passOn(ids, {7, 0, 0, 0})); e.endRenderPass(); });
        auto px = gpu.pixel(ids, 0, 0);
        std::uint32_t value = 0;
        std::memcpy(&value, px.data(), 4);
        CHECK(value == 7u);
        gpu.expectNoValidationErrors();
    }

    TEST_CASE("indirect draws and copies: misaligned offsets and ranges that would wrap around are refused")
    {
        Gpu gpu;
        SKIP_TEST_IF_NULL(gpu.device.get(), "OpenGL 4.3 context");
        const PipelineHandle pipe = gpu.pipeline(gpu.shader(ShaderStage::kVertex, kFullscreenVs), gpu.shader(ShaderStage::kFragment, kRedFs));
        const TextureHandle color = gpu.target(Format::kRGBA8Unorm, {2, 2});
        const std::vector<DrawIndirectCommand> commands{{3, 1, 0, 0}, {3, 1, 0, 0}};
        const BufferHandle args = gpu.buffer(BufferUsage::kIndirect, bytesOf(commands));
        auto drawWith = [&](std::uint64_t offset, std::uint32_t count, std::uint32_t stride)
        {
            auto e = gpu->createCommandEncoder();
            e->beginRenderPass(passOn(color));
            e->setPipeline(pipe);
            e->drawIndirect(args, offset, count, stride);
            e->endRenderPass();
            return gpu->submit(std::move(e));
        };
        CHECK(drawWith(0, 2, 0).has_value());
        CHECK((tests::errorOf(drawWith(2, 1, 0)) == ErrorCode::kInvalidArgument));  // décalage non multiple de 4
        CHECK((tests::errorOf(drawWith(0, 2, 6)) == ErrorCode::kInvalidArgument));  // pas non multiple de 4
        CHECK((tests::errorOf(drawWith(0, 2, 8)) == ErrorCode::kInvalidArgument));  // pas plus court qu'une commande
        CHECK((tests::errorOf(drawWith(std::numeric_limits<std::uint64_t>::max() - 3, 1, 0)) == ErrorCode::kOutOfRange)); // rebouclerait

        const BufferHandle src = gpu.buffer(BufferUsage::kCopySrc, {}, 64);
        const BufferHandle dst = gpu.buffer(BufferUsage::kCopyDst, {}, 64);
        auto copy = gpu->createCommandEncoder();
        copy->copyBuffer(src, dst, {.srcOffset = std::numeric_limits<std::uint64_t>::max() - 7, .dstOffset = 0, .size = 16});
        auto r = gpu->submit(std::move(copy));
        REQUIRE_FALSE(r.has_value());
        CHECK((tests::errorOf(r) == ErrorCode::kOutOfRange));
        gpu.expectNoValidationErrors();
    }

    TEST_CASE("buffer <-> texture copies need only the bytes actually transferred (no padding after the last row)")
    {
        Gpu gpu;
        SKIP_TEST_IF_NULL(gpu.device.get(), "OpenGL 4.3 context");
        // 2x2 RGBA8 au pas de 4 texels : 16 octets jusqu'au début de la dernière ligne, puis 8 : 24 octets suffisent.
        std::vector<std::uint8_t> staging(24, 0);
        const std::uint8_t a[4] = {10, 20, 30, 255}, b[4] = {40, 50, 60, 255}, c[4] = {70, 80, 90, 255}, d[4] = {100, 110, 120, 255};
        std::memcpy(&staging[0], a, 4);
        std::memcpy(&staging[4], b, 4);
        std::memcpy(&staging[16], c, 4);
        std::memcpy(&staging[20], d, 4);
        const BufferHandle src = gpu.buffer(BufferUsage::kCopySrc, std::as_bytes(std::span(staging)));
        TextureDesc td;
        td.size = {2, 2, 1};
        td.usage = TextureUsage::kCopyDst | TextureUsage::kCopySrc;
        auto tex = gpu->createTexture(td);
        REQUIRE(tex.has_value());
        gpu.run([&](CommandEncoder &e) { e.copyBufferToTexture(src, *tex, {.bufferRowLength = 4}); });
        CHECK(rgba(gpu.pixel(*tex, 0, 0)) == std::array<std::uint8_t, 4>{10, 20, 30, 255});
        CHECK(rgba(gpu.pixel(*tex, 1, 1)) == std::array<std::uint8_t, 4>{100, 110, 120, 255});

        const BufferHandle back = gpu.buffer(BufferUsage::kCopyDst, {}, 24, "back", MemoryKind::kReadback);
        gpu.run([&](CommandEncoder &e) { e.copyTextureToBuffer(*tex, back, {.bufferRowLength = 4}); });
        auto read = gpu->readBuffer(back, 16, 8);
        REQUIRE(read.has_value());
        CHECK(static_cast<std::uint8_t>((*read)[4]) == 100);

        auto shortRow = gpu->createCommandEncoder();
        shortRow->copyBufferToTexture(src, *tex, {.bufferRowLength = 1}); // pas plus court que la région
        auto r = gpu->submit(std::move(shortRow));
        REQUIRE_FALSE(r.has_value());
        CHECK((tests::errorOf(r) == ErrorCode::kInvalidArgument));
        gpu.expectNoValidationErrors();
    }

    TEST_CASE("updateTexture reads tight rows from CPU memory: bufferRowLength only applies to buffer copies")
    {
        Gpu gpu;
        SKIP_TEST_IF_NULL(gpu.device.get(), "OpenGL 4.3 context");
        TextureDesc td;
        td.size = {2, 2, 1};
        td.usage = TextureUsage::kSampled | TextureUsage::kCopyDst | TextureUsage::kCopySrc;
        auto tex = gpu->createTexture(td);
        REQUIRE(tex.has_value());
        const std::vector<std::uint8_t> texels{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
        REQUIRE(gpu->updateTexture(*tex, {.bufferRowLength = 4}, std::as_bytes(std::span(texels))).has_value());
        auto all = gpu->readTexture(*tex);
        REQUIRE(all.has_value());
        CHECK(*all == toVec(std::as_bytes(std::span(texels))));
        gpu.expectNoValidationErrors();
    }

    TEST_CASE("capabilities expose the storage buffer binding limit; a binding beyond it is refused by setBindGroup")
    {
        Gpu gpu;
        SKIP_TEST_IF_NULL(gpu.device.get(), "OpenGL 4.3 context");
        CHECK(gpu->capabilities().maxStorageBufferBindings >= 8u); // minimum d'OpenGL 4.3
        const BufferHandle ssbo = gpu.buffer(BufferUsage::kStorage, {}, 64);
        BindGroupDesc gd;
        gd.entries = {{.binding = 7, .type = BindingType::kStorageBuffer, .buffer = ssbo}};
        auto group = gpu->createBindGroup(gd);
        REQUIRE(group.has_value());
        auto e = gpu->createCommandEncoder();
        e->setBindGroup(2, *group); // emplacement plat 23
        auto r = gpu->submit(std::move(e));
        CHECK(r.has_value() == (gpu->capabilities().maxStorageBufferBindings > 23u));
        gpu.expectNoValidationErrors();
    }

    TEST_CASE("destroyed resources are released only after the frames in flight AND the GPU fence of their frame")
    {
        Gpu gpu;
        SKIP_TEST_IF_NULL(gpu.device.get(), "OpenGL 4.3 context");
        const TextureHandle color = gpu.target(Format::kRGBA8Unorm, {2, 2});
        const BufferHandle b = gpu.buffer(BufferUsage::kVertex, {}, 16);
        gpu.run([&](CommandEncoder &e) { e.beginRenderPass(passOn(color)); e.endRenderPass(); });
        gpu->destroy(b);
        for (std::uint32_t i = 0; i < 2; ++i)
        {
            CHECK(gpu->stats().pendingDestruction == 1);
            REQUIRE(gpu->present().has_value());
        }
        CHECK(gpu->stats().pendingDestruction == 0); // la clôture de l'image a été attendue, pas seulement comptée
        gpu.expectNoValidationErrors();
    }

    TEST_CASE("MRT up to Capabilities::maxColorAttachments (8 on OpenGL 4.3+) with a distinct output per target (RHI-15)")
    {
        Gpu gpu;
        SKIP_TEST_IF_NULL(gpu.device.get(), "OpenGL 4.3 context");
        const std::uint32_t count = gpu->capabilities().maxColorAttachments;
        REQUIRE(count >= 1);
        REQUIRE(count <= 8);
        std::string fs = "#version 430 core\n";
        for (std::uint32_t i = 0; i < count; ++i)
        {
            fs += "layout(location = " + std::to_string(i) + ") out vec4 o" + std::to_string(i) + ";\n";
        }
        fs += "void main() {\n";
        for (std::uint32_t i = 0; i < count; ++i)
        {
            // Cible i : rouge = i / 8, vert = 1 - i / 8 : chaque cible reçoit une valeur différente.
            fs += "  o" + std::to_string(i) + " = vec4(" + std::to_string(i) + ".0 / 8.0, 1.0 - " + std::to_string(i) + ".0 / 8.0, 0.0, 1.0);\n";
        }
        fs += "}\n";
        std::vector<TextureHandle> targets;
        std::vector<ColorTargetState> states;
        RenderPassDesc pass;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            targets.push_back(gpu.target(Format::kRGBA8Unorm, {2, 2}, "mrt"));
            states.push_back(ColorTargetState{});
            pass.colors.push_back({.texture = targets.back()});
        }
        const PipelineHandle pipe = gpu.pipeline(gpu.shader(ShaderStage::kVertex, kFullscreenVs), gpu.shader(ShaderStage::kFragment, fs.c_str()), states);
        gpu.run(
            [&](CommandEncoder &e)
            {
                e.beginRenderPass(pass);
                e.setPipeline(pipe);
                e.draw(3);
                e.endRenderPass();
            });
        for (std::uint32_t i = 0; i < count; ++i)
        {
            const auto px = rgba(gpu.pixel(targets[i], 1, 1));
            INFO("target ", i);
            CHECK(near(px[0], static_cast<int>(std::lround(255.0 * i / 8.0))));
            CHECK(near(px[1], static_cast<int>(std::lround(255.0 * (1.0 - i / 8.0)))));
        }
        if (count < 8) // la liste des cibles reste celle du pilote : RHI-15 n'est prouvé à 8 que là où il en offre 8
        {
            MESSAGE("maxColorAttachments = ", count, " (< 8 on this driver)");
        }
        gpu.expectNoValidationErrors();
    }

    TEST_CASE("a refused clear value leaves every target of the pass untouched (all clears validated first)")
    {
        Gpu gpu;
        SKIP_TEST_IF_NULL(gpu.device.get(), "OpenGL 4.3 context");
        const TextureHandle color = gpu.target(Format::kRGBA8Unorm, {2, 2}, "color");
        const TextureHandle ids = gpu.target(Format::kR32Uint, {2, 2}, "ids");
        gpu.run([&](CommandEncoder &e) { e.beginRenderPass(passOn(color, {0, 0, 1, 1})); e.endRenderPass(); }); // bleu
        auto e = gpu->createCommandEncoder();
        RenderPassDesc pass;
        pass.colors.push_back({.texture = color, .clearColor = {1, 0, 0, 1}}); // rouge, valide
        pass.colors.push_back({.texture = ids, .clearColor = {-1, 0, 0, 0}}); // refusé
        e->beginRenderPass(pass);
        e->endRenderPass();
        CHECK((tests::errorOf(gpu->submit(std::move(e))) == ErrorCode::kInvalidArgument));
        CHECK(rgba(gpu.pixel(color, 0, 0)) == std::array<std::uint8_t, 4>{0, 0, 255, 255}); // toujours bleu
        gpu.expectNoValidationErrors();
    }

    TEST_CASE("a bufferRowLength that does not fit in a GLint is refused in both copy directions")
    {
        Gpu gpu;
        SKIP_TEST_IF_NULL(gpu.device.get(), "OpenGL 4.3 context");
        TextureDesc td;
        td.size = {1, 1, 1};
        td.usage = TextureUsage::kCopyDst | TextureUsage::kCopySrc;
        auto tex = gpu->createTexture(td);
        REQUIRE(tex.has_value());
        const BufferHandle src = gpu.buffer(BufferUsage::kCopySrc, {}, 4);
        const BufferHandle dst = gpu.buffer(BufferUsage::kCopyDst, {}, 4);
        auto up = gpu->createCommandEncoder();
        up->copyBufferToTexture(src, *tex, {.bufferRowLength = 0x80000000u});
        CHECK((tests::errorOf(gpu->submit(std::move(up))) == ErrorCode::kOutOfRange));
        auto down = gpu->createCommandEncoder();
        down->copyTextureToBuffer(*tex, dst, {.bufferRowLength = 0x80000000u});
        CHECK((tests::errorOf(gpu->submit(std::move(down))) == ErrorCode::kOutOfRange));
        gpu.expectNoValidationErrors();
    }
}
