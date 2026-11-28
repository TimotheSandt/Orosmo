// RHI-01, RHI-05..RHI-11, RHI-15, RHI-16 : le CONTRAT du RHI, vérifié sur chaque backend disponible (FOR_EACH_BACKEND, voir
// rhi/backends.hpp), sur un vrai périphérique hors écran. Les shaders sont du GLSL 450 « style Vulkan » compilé par
// Gpu::shader. Sans pilote (machine sans affichage), le cas est ignoré avec un avertissement. Ce qui est propre à OpenGL
// (messages GL_KHR_debug, SPIR-V refusé, lecture synchrone au milieu d'une passe) est dans opengl_device_tests.cpp.
// Repère du RHI (A26) : ligne 0 = haut de l'image, triangle anti-horaire = face avant, profondeur NDC dans [0, 1].
#include <doctest/doctest.h>

#include "support/test_support.hpp"
#include "rhi/gpu_fixture.hpp"

#include <orosmo/rhi/device.hpp>

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

using namespace orosmo::rhi;
using orosmo::core::ErrorCode;


TEST_SUITE("rhi/backend/device")
{
    TEST_CASE("M9-L1 the validation layer is active when the CI requires it (ENGINE_TEST_REQUIRE_VALIDATION)")
    {
        // Sans couche chargée, « 0 message de validation » ne prouve rien : la CI qui l'installe pose la variable.
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            MESSAGE(tests::backendName(backend), ": validation ", gpu->capabilities().validation ? "active" : "inactive", " on ",
                    gpu->capabilities().deviceName);
            if (tests::validationRequired())
            {
                CHECK_MESSAGE(gpu->capabilities().validation, "ENGINE_TEST_REQUIRE_VALIDATION is set but the validation layer is not active (VK_LAYER_PATH?)");
            }
        }
    }

    TEST_CASE("capabilities describe the device (limits, alignment); OpenGL keeps the reserved optional features false")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            const Capabilities &c = gpu->capabilities();
            CHECK((c.backend == backend));
            CHECK_FALSE(c.deviceName.empty());
            CHECK(c.maxTextureSize >= 1024);
            CHECK(c.max3DTextureSize >= 64);
            CHECK(c.maxColorAttachments >= 4);
            CHECK(c.maxColorAttachments <= 8);
            CHECK(c.compute);
            CHECK(c.indirectDraw);
            CHECK(c.maxComputeInvocations >= 1);
            CHECK(c.maxComputeWorkGroupSize[0] >= 1);
            // OpenGL 4.3 permet un alignement inférieur à 16 : seule une puissance de deux non nulle est garantie.
            CHECK(c.minUniformAlignment >= 1);
            CHECK((c.minUniformAlignment & (c.minUniformAlignment - 1)) == 0);
            if (backend == Backend::kOpenGL)
            {
                // Propre à OpenGL : les capacités facultatives (RHI-18) sont réservées et fausses, SPIR-V n'est pas accepté ;
                // un autre backend peut légitimement les annoncer (elles sont lues dans Capabilities, jamais supposées).
                CHECK(c.timestampQueries);
                CHECK_FALSE(c.rayQuery);
                CHECK_FALSE(c.accelerationStructure);
                CHECK_FALSE(c.bindless);
                CHECK_FALSE(c.meshShader);
                CHECK_FALSE(c.bufferDeviceAddress);
                CHECK_FALSE(c.sparseResources);
                CHECK_FALSE(c.spirvShaders);
                CHECK(c.shaderFloat64);
            }
            gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("buffers: create with data, update, read back, range and stale-handle errors")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            const std::vector<std::uint32_t> initial{1, 2, 3, 4};
            const BufferHandle b = gpu.buffer(BufferUsage::kStorage | BufferUsage::kCopyDst | BufferUsage::kCopySrc, bytesOf(initial), 32, "ssbo");

            const std::uint32_t patch = 99;
            REQUIRE(gpu->updateBuffer(b, 8, bytesOf(patch)).has_value());
            auto data = gpu->readBuffer(b, 0, 16);
            REQUIRE(data.has_value());
            std::uint32_t words[4];
            std::memcpy(words, data->data(), 16);
            CHECK(words[0] == 1);
            CHECK(words[1] == 2);
            CHECK(words[2] == 99);
            CHECK(words[3] == 4);

            CHECK((tests::errorOf(gpu->updateBuffer(b, 30, bytesOf(patch))) == ErrorCode::kOutOfRange));
            CHECK((tests::errorOf(gpu->readBuffer(b, 0, 64)) == ErrorCode::kOutOfRange));

            gpu->destroy(b);
            CHECK((tests::errorOf(gpu->updateBuffer(b, 0, bytesOf(patch))) == ErrorCode::kNotFound));
            CHECK((tests::errorOf(gpu->readBuffer(b, 0, 4)) == ErrorCode::kNotFound));

            BufferDesc bad;
            CHECK((tests::errorOf(gpu->createBuffer(bad)) == ErrorCode::kInvalidArgument)); // taille 0
            gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("destruction is deferred: the handle is stale at once, the GPU object is released after the frames in flight")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            const BufferHandle b = gpu.buffer(BufferUsage::kVertex, {}, 16);
            CHECK(gpu->stats().buffers == 1);
            gpu->destroy(b);
            CHECK(gpu->stats().buffers == 0);
            CHECK(gpu->stats().pendingDestruction == 1);
            gpu->destroy(b); // double destruction : sans effet
            CHECK(gpu->stats().pendingDestruction == 1);
            REQUIRE(gpu->present().has_value());
            CHECK(gpu->stats().pendingDestruction == 1); // encore en vol
            REQUIRE(gpu->present().has_value());
            REQUIRE(gpu->present().has_value());
            CHECK(gpu->stats().pendingDestruction == 0);
            gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("textures: every portable format round-trips its bytes (RHI-05, RHI-16)")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            const Format formats[] = {Format::kR8Unorm,    Format::kRG8Unorm,    Format::kRGBA8Unorm,  Format::kBGRA8Unorm,
                                      Format::kRGBA8Srgb,  Format::kR16Float,    Format::kRG16Float,   Format::kRGBA16Float,
                                      Format::kR32Float,   Format::kRG32Float,   Format::kRGBA32Float, Format::kR32Uint};
            for (const Format format : formats)
            {
                CAPTURE(toString(format));
                TextureDesc d;
                d.format = format;
                d.size = {5, 3, 1}; // largeur impaire : vérifie que les lignes sont serrées
                d.usage = TextureUsage::kSampled | TextureUsage::kCopyDst | TextureUsage::kCopySrc;
                auto t = gpu->createTexture(d);
                REQUIRE(t.has_value());
                std::vector<std::byte> in(5u * 3u * formatInfo(format).bytesPerTexel);
                std::uint32_t seed = 12345;
                for (std::byte &b : in)
                {
                    seed = seed * 1664525u + 1013904223u;
                    b = static_cast<std::byte>(seed >> 24);
                }
                // Les flottants aléatoires peuvent être NaN : on garde des valeurs finies (octet de poids fort 0x3C).
                if (formatInfo(format).kind == FormatKind::kFloat)
                {
                    const bool half = format == Format::kR16Float || format == Format::kRG16Float || format == Format::kRGBA16Float;
                    const std::size_t width = half ? 2 : 4;
                    for (std::size_t i = 0; i < in.size() / width; ++i)
                    {
                        std::fill_n(in.begin() + static_cast<std::ptrdiff_t>(i * width), width, std::byte{0});
                        in[i * width] = static_cast<std::byte>(i % 200);
                        in[i * width + width - 1] = std::byte{0x3C};
                    }
                }
                REQUIRE(gpu->updateTexture(*t, {}, in).has_value());
                auto out = gpu->readTexture(*t);
                REQUIRE(out.has_value());
                CHECK(*out == in);
            }
            gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("texture regions: sub-rectangles, mip levels and invalid regions")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            TextureDesc d;
            d.size = {4, 4, 1};
            d.mipLevels = 0; // chaîne complète : 3 niveaux
            d.usage = TextureUsage::kSampled | TextureUsage::kCopyDst | TextureUsage::kCopySrc;
            auto t = gpu->createTexture(d);
            REQUIRE(t.has_value());

            std::vector<std::byte> zeros(4 * 4 * 4, std::byte{0});
            REQUIRE(gpu->updateTexture(*t, {}, zeros).has_value());
            const std::vector<std::uint8_t> patch{10, 20, 30, 40, 50, 60, 70, 80}; // 2 texels
            TextureCopy region;
            region.offset = {1, 2, 0};
            region.size = {2, 1, 1};
            REQUIRE(gpu->updateTexture(*t, region, std::as_bytes(std::span(patch))).has_value());
            CHECK(rgba(gpu.pixel(*t, 1, 2)) == std::array<std::uint8_t, 4>{10, 20, 30, 40});
            CHECK(rgba(gpu.pixel(*t, 2, 2)) == std::array<std::uint8_t, 4>{50, 60, 70, 80});
            CHECK(rgba(gpu.pixel(*t, 0, 2)) == std::array<std::uint8_t, 4>{0, 0, 0, 0});

            TextureCopy outside;
            outside.offset = {3, 0, 0};
            outside.size = {2, 1, 1};
            CHECK((tests::errorOf(gpu->readTexture(*t, outside)) == ErrorCode::kOutOfRange));
            TextureCopy badMip;
            badMip.mipLevel = 3;
            CHECK((tests::errorOf(gpu->readTexture(*t, badMip)) == ErrorCode::kOutOfRange));
            CHECK((tests::errorOf(gpu->updateTexture(*t, region, std::as_bytes(std::span(patch).first(4)))) == ErrorCode::kInvalidArgument));

            TextureDesc bad;
            bad.size = {0, 4, 1};
            CHECK((tests::errorOf(gpu->createTexture(bad)) == ErrorCode::kInvalidArgument));
            bad.size = {4, 4, 1};
            bad.mipLevels = 9;
            CHECK((tests::errorOf(gpu->createTexture(bad)) == ErrorCode::kInvalidArgument));
            bad.mipLevels = 1;
            bad.usage = TextureUsage::kStorage;
            bad.format = Format::kRGBA8Srgb; // image de stockage selon le pilote (règle 11) : OpenGL 4.3 jamais, MoltenVK oui
            if (gpu->capabilities().srgbStorageImages)
            {
                auto accepted = gpu->createTexture(bad);
                CHECK(accepted.has_value());
                if (accepted)
                {
                    gpu->destroy(*accepted);
                }
            }
            else
            {
                CHECK((tests::errorOf(gpu->createTexture(bad)) == ErrorCode::kUnsupported));
            }
            bad.format = Format::kD32Float; // une profondeur n'est jamais une image de stockage
            CHECK((tests::errorOf(gpu->createTexture(bad)) == ErrorCode::kUnsupported));
            gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("generateMips averages a uniform texture down to the last level")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            TextureDesc d;
            d.size = {4, 4, 1};
            d.mipLevels = 0;
            d.usage = TextureUsage::kSampled | TextureUsage::kCopyDst | TextureUsage::kCopySrc;
            auto t = gpu->createTexture(d);
            REQUIRE(t.has_value());
            std::vector<std::uint8_t> level0;
            for (int i = 0; i < 16; ++i)
            {
                level0.insert(level0.end(), {100, 150, 200, 255});
            }
            REQUIRE(gpu->updateTexture(*t, {}, std::as_bytes(std::span(level0))).has_value());
            gpu.run([&](CommandEncoder &e) { e.generateMips(*t); });
            CHECK(rgba(gpu.pixel(*t, 0, 0, 2)) == std::array<std::uint8_t, 4>{100, 150, 200, 255});
            CHECK(rgba(gpu.pixel(*t, 1, 1, 1)) == std::array<std::uint8_t, 4>{100, 150, 200, 255});
            gpu.expectNoValidationErrors();
        }
    }
}

TEST_SUITE("rhi/backend/render")
{
    TEST_CASE("a render pass clears its attachment (LoadOp::kClear)")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            const TextureHandle color = gpu.target(Format::kRGBA8Unorm, {4, 4});
            gpu.run(
                [&](CommandEncoder &e)
                {
                    e.beginRenderPass(passOn(color, {0.25f, 0.5f, 0.75f, 1.0f}));
                    e.endRenderPass();
                });
            const auto px = rgba(gpu.pixel(color, 2, 2));
            CHECK(near(px[0], 64));
            CHECK(near(px[1], 128));
            CHECK(near(px[2], 191));
            CHECK(px[3] == 255);
            gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("a fullscreen triangle fills the target; the scissor limits it")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            const TextureHandle color = gpu.target(Format::kRGBA8Unorm, {8, 8});
            const PipelineHandle pipe = gpu.pipeline(gpu.shader(ShaderStage::kVertex, kFullscreenVsGlsl450), gpu.shader(ShaderStage::kFragment, kRedFsGlsl450));
            gpu.run(
                [&](CommandEncoder &e)
                {
                    e.beginRenderPass(passOn(color, {0, 1, 0, 1}));
                    e.setPipeline(pipe);
                    e.setScissor({0, 0, 4, 4});
                    e.draw(3);
                    e.endRenderPass();
                });
            CHECK(rgba(gpu.pixel(color, 1, 1)) == std::array<std::uint8_t, 4>{255, 0, 0, 255});
            CHECK(rgba(gpu.pixel(color, 3, 3)) == std::array<std::uint8_t, 4>{255, 0, 0, 255});
            CHECK(rgba(gpu.pixel(color, 5, 5)) == std::array<std::uint8_t, 4>{0, 255, 0, 255});
            CHECK(rgba(gpu.pixel(color, 7, 1)) == std::array<std::uint8_t, 4>{0, 255, 0, 255});
            gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("MRT: three targets of different formats written by one shader (RHI-15)")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            const TextureHandle a = gpu.target(Format::kRGBA8Unorm, {4, 4}, "mrt.color");
            const TextureHandle b = gpu.target(Format::kRG16Float, {4, 4}, "mrt.motion");
            const TextureHandle c = gpu.target(Format::kR32Uint, {4, 4}, "mrt.id");
            const char *fs = R"(#version 450
layout(location = 0) out vec4 color;
layout(location = 1) out vec2 motion;
layout(location = 2) out uint objectId;
void main() { color = vec4(0.0, 1.0, 0.0, 1.0); motion = vec2(0.5, 0.25); objectId = 42u; })";
            const PipelineHandle pipe =
                gpu.pipeline(gpu.shader(ShaderStage::kVertex, kFullscreenVsGlsl450), gpu.shader(ShaderStage::kFragment, fs),
                             {ColorTargetState{.format = Format::kRGBA8Unorm}, ColorTargetState{.format = Format::kRG16Float},
                              ColorTargetState{.format = Format::kR32Uint}});
            gpu.run(
                [&](CommandEncoder &e)
                {
                    RenderPassDesc pass;
                    pass.colors = {{.texture = a}, {.texture = b}, {.texture = c, .clearColor = {7, 0, 0, 0}}};
                    e.beginRenderPass(pass);
                    e.setPipeline(pipe);
                    e.draw(3);
                    e.endRenderPass();
                });
            CHECK(rgba(gpu.pixel(a, 1, 1)) == std::array<std::uint8_t, 4>{0, 255, 0, 255});
            const auto motion = gpu.pixel(b, 1, 1);
            std::uint16_t half[2];
            std::memcpy(half, motion.data(), 4);
            CHECK(half[0] == 0x3800); // 0.5
            CHECK(half[1] == 0x3400); // 0.25
            std::uint32_t id = 0;
            const auto idBytes = gpu.pixel(c, 2, 2);
            std::memcpy(&id, idBytes.data(), 4);
            CHECK(id == 42);

            // Une passe qui dépasse la limite des cibles est refusée.
            auto encoder = gpu->createCommandEncoder();
            RenderPassDesc tooMany;
            for (std::uint32_t i = 0; i <= gpu->capabilities().maxColorAttachments; ++i)
            {
                tooMany.colors.push_back({.texture = a});
            }
            encoder->beginRenderPass(tooMany);
            CHECK((tests::errorOf(gpu->submit(std::move(encoder))) == ErrorCode::kOutOfRange));
            gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("vertex and instance attributes, firstInstance, indexed draw")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            const TextureHandle color = gpu.target(Format::kRGBA8Unorm, {8, 8});
            const std::vector<float> positions{-1, -1, 3, -1, -1, 3};
            const std::vector<float> colors{1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 1}; // rouge, vert, bleu (par instance)
            const std::vector<std::uint16_t> indices{0, 1, 2};
            const BufferHandle vb = gpu.buffer(BufferUsage::kVertex, bytesOf(positions), 0, "positions");
            const BufferHandle ib = gpu.buffer(BufferUsage::kIndex, bytesOf(indices), 0, "indices");
            const BufferHandle inst = gpu.buffer(BufferUsage::kVertex, bytesOf(colors), 0, "instances");
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

            gpu.run(
                [&](CommandEncoder &e)
                {
                    e.beginRenderPass(passOn(color));
                    e.setPipeline(*pipe);
                    e.setVertexBuffer(0, vb);
                    e.setVertexBuffer(1, inst);
                    e.draw(3, 1, 0, 1); // firstInstance = 1 : vert
                    e.endRenderPass();
                });
            CHECK(rgba(gpu.pixel(color, 4, 4)) == std::array<std::uint8_t, 4>{0, 255, 0, 255});

            gpu.run(
                [&](CommandEncoder &e)
                {
                    e.beginRenderPass(passOn(color));
                    e.setPipeline(*pipe);
                    e.setVertexBuffer(0, vb);
                    e.setVertexBuffer(1, inst);
                    e.setIndexBuffer(ib, IndexType::kUint16);
                    e.drawIndexed(3, 1, 0, 0, 2); // firstInstance = 2 : bleu
                    e.endRenderPass();
                });
            CHECK(rgba(gpu.pixel(color, 4, 4)) == std::array<std::uint8_t, 4>{0, 0, 255, 255});

            // Sans tampon de sommets lié, le dessin est refusé avec un message clair.
            auto encoder = gpu->createCommandEncoder();
            encoder->beginRenderPass(passOn(color));
            encoder->setPipeline(*pipe);
            encoder->draw(3);
            encoder->endRenderPass();
            auto r = gpu->submit(std::move(encoder));
            REQUIRE_FALSE(r.has_value());
            CHECK(r.error().message.find("vertex buffer binding") != std::string::npos);
            gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("indirect draws: two commands in one multi-draw (RHI-10)")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            const TextureHandle color = gpu.target(Format::kRGBA8Unorm, {8, 8});
            // Deux triangles : A dans le coin bas-gauche, B dans le coin au centre haut-droit.
            const std::vector<float> positions{-1, -1, 0, -1, -1, 0, 0, 0, 1, 0, 0, 1};
            const std::vector<float> colors{1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 1};
            const std::vector<DrawIndirectCommand> commands{{3, 1, 0, 0}, {3, 1, 3, 2}};
            const BufferHandle vb = gpu.buffer(BufferUsage::kVertex, bytesOf(positions));
            const BufferHandle inst = gpu.buffer(BufferUsage::kVertex, bytesOf(colors));
            const BufferHandle args = gpu.buffer(BufferUsage::kIndirect, bytesOf(commands));
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
            d.vertexBuffers = {{0, 8, VertexStepRate::kVertex}, {1, 16, VertexStepRate::kInstance}};
            d.vertexAttributes = {{0, 0, VertexFormat::kFloat2, 0}, {1, 1, VertexFormat::kFloat4, 0}};
            d.colorTargets = {ColorTargetState{}};
            d.raster.cull = CullMode::kNone;
            auto pipe = gpu->createGraphicsPipeline(d);
            REQUIRE(pipe.has_value());
            gpu.run(
                [&](CommandEncoder &e)
                {
                    e.beginRenderPass(passOn(color));
                    e.setPipeline(*pipe);
                    e.setVertexBuffer(0, vb);
                    e.setVertexBuffer(1, inst);
                    e.drawIndirect(args, 0, 2);
                    e.endRenderPass();
                });
            // Repère du RHI (A26) : NDC y = +1 est la ligne 0 (haut) ; A est en bas à gauche, B au centre vers le haut à droite.
            CHECK(rgba(gpu.pixel(color, 1, 6)) == std::array<std::uint8_t, 4>{255, 0, 0, 255});
            CHECK(rgba(gpu.pixel(color, 4, 3)) == std::array<std::uint8_t, 4>{0, 0, 255, 255});
            CHECK(rgba(gpu.pixel(color, 7, 0)) == std::array<std::uint8_t, 4>{0, 0, 0, 255});

            // Les commandes ne tiennent pas dans le tampon : refusé.
            auto encoder = gpu->createCommandEncoder();
            encoder->beginRenderPass(passOn(color));
            encoder->setPipeline(*pipe);
            encoder->setVertexBuffer(0, vb);
            encoder->setVertexBuffer(1, inst);
            encoder->drawIndirect(args, 0, 3);
            encoder->endRenderPass();
            CHECK((tests::errorOf(gpu->submit(std::move(encoder))) == ErrorCode::kOutOfRange));
            gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("resize beyond the device limit fails and keeps the current backbuffer (revue CodeRabbit)")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend, {6, 4});
            REQUIRE(gpu.device);
            auto frame = gpu->beginFrame();
            REQUIRE(frame.has_value());
            gpu.run([&](CommandEncoder &e) { e.beginRenderPass(passOn(*frame, {1, 0, 1, 1})); e.endRenderPass(); }); // contenu connu
            const auto tooBig = gpu->capabilities().maxTextureSize + 1;
            CHECK_FALSE(gpu->resize({tooBig, 4}).has_value());
            auto again = gpu->beginFrame();
            REQUIRE(again.has_value());
            auto pixels = gpu->readTexture(*again);
            REQUIRE(pixels.has_value());
            CHECK(pixels->size() == 6u * 4u * 4u);
            CHECK(rgba(gpu.pixel(*again, 5, 3)) == std::array<std::uint8_t, 4>{255, 0, 255, 255}); // le contenu aussi est intact
        }
    }

    TEST_CASE("textures beyond the per-type limits are refused instead of returning a handle with no storage (revue CodeRabbit)")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            TextureDesc d;
            d.format = Format::kRGBA8Unorm;
            d.usage = TextureUsage::kSampled;
            d.dimension = TextureDimension::k2DArray;
            d.size = {1, 1, 1};
            d.layers = 0x7fffffffu;
            CHECK_FALSE(gpu->createTexture(d).has_value());
        }
    }

    TEST_CASE("depth test with push constants: the nearer triangle wins and the depth buffer holds its depth")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            const TextureHandle color = gpu.target(Format::kRGBA8Unorm, {4, 4}, "color");
            const TextureHandle depth = gpu.target(Format::kD32Float, {4, 4}, "depth");
            const char *vs = R"(#version 450
layout(push_constant) uniform Push { vec4 color; float z; } pc;
void main() { vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2); gl_Position = vec4(p * 2.0 - 1.0, pc.z, 1.0); })";
            const char *fs = R"(#version 450
layout(push_constant) uniform Push { vec4 color; float z; } pc;
layout(location = 0) out vec4 outColor;
void main() { outColor = pc.color; })";
            const PipelineHandle pipe = gpu.pipeline(gpu.shader(ShaderStage::kVertex, vs), gpu.shader(ShaderStage::kFragment, fs),
                                                     {ColorTargetState{}}, Format::kD32Float, true);
            struct Push
            {
                float color[4];
                float z;
                float pad[3];
            };
            // Repère du RHI (A26) : profondeur NDC dans [0, 1] (z hors de [0, 1] est découpé).
            const Push green{{0, 1, 0, 1}, 0.25f, {}};
            const Push red{{1, 0, 0, 1}, 0.75f, {}};
            gpu.run(
                [&](CommandEncoder &e)
                {
                    RenderPassDesc pass = passOn(color);
                    pass.depth = DepthAttachment{.texture = depth};
                    e.beginRenderPass(pass);
                    e.setPipeline(pipe);
                    e.setPushConstants(bytesOf(green));
                    e.draw(3);
                    e.setPushConstants(bytesOf(red)); // plus loin : rejeté par le test de profondeur
                    e.draw(3);
                    e.endRenderPass();
                });
            CHECK(rgba(gpu.pixel(color, 2, 2)) == std::array<std::uint8_t, 4>{0, 255, 0, 255});
            float d = 0;
            const auto db = gpu.pixel(depth, 2, 2);
            std::memcpy(&d, db.data(), 4);
            CHECK(d == doctest::Approx(0.25f));

            // Une pipeline sans profondeur dans une passe avec profondeur est refusée.
            const PipelineHandle noDepth = gpu.pipeline(gpu.shader(ShaderStage::kVertex, kFullscreenVsGlsl450), gpu.shader(ShaderStage::kFragment, kRedFsGlsl450));
            auto encoder = gpu->createCommandEncoder();
            RenderPassDesc pass = passOn(color);
            pass.depth = DepthAttachment{.texture = depth};
            encoder->beginRenderPass(pass);
            encoder->setPipeline(noDepth);
            encoder->endRenderPass();
            CHECK((tests::errorOf(gpu->submit(std::move(encoder))) == ErrorCode::kInvalidArgument));

            std::vector<std::byte> tooBig(gpu->capabilities().maxPushConstantSize + 4); // OpenGL : 128 octets ; Vulkan : la limite du pilote
            auto e2 = gpu->createCommandEncoder();
            e2->setPushConstants(tooBig);
            CHECK((tests::errorOf(gpu->submit(std::move(e2))) == ErrorCode::kInvalidArgument));
            gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("alpha blending on a render target")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            const TextureHandle color = gpu.target(Format::kRGBA8Unorm, {4, 4});
            const char *fs = R"(#version 450
layout(location = 0) out vec4 color;
void main() { color = vec4(1.0, 0.0, 0.0, 0.5); })";
            ColorTargetState target;
            target.blend = {true, BlendFactor::kSrcAlpha, BlendFactor::kOneMinusSrcAlpha, BlendOp::kAdd,
                            BlendFactor::kOne, BlendFactor::kOneMinusSrcAlpha, BlendOp::kAdd};
            const PipelineHandle pipe = gpu.pipeline(gpu.shader(ShaderStage::kVertex, kFullscreenVsGlsl450), gpu.shader(ShaderStage::kFragment, fs), {target});
            gpu.run(
                [&](CommandEncoder &e)
                {
                    e.beginRenderPass(passOn(color, {0, 0, 1, 1}));
                    e.setPipeline(pipe);
                    e.draw(3);
                    e.endRenderPass();
                });
            const auto px = rgba(gpu.pixel(color, 1, 1));
            CHECK(near(px[0], 128));
            CHECK(px[1] == 0);
            CHECK(near(px[2], 128));
            gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("bind groups: dynamic uniform offsets, sampled texture + sampler; errors on bad offsets")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            // Pas d'un bloc de 16 octets arrondi à l'alignement annoncé (qui peut être inférieur à 16 sur OpenGL 4.3).
            const std::uint64_t align = gpu->capabilities().minUniformAlignment;
            REQUIRE(align > 0);
            const std::uint64_t stride = (16 + align - 1) / align * align;
            std::vector<std::byte> ubo(stride * 2, std::byte{0});
            const float red[4] = {1, 0, 0, 1};
            const float green[4] = {0, 1, 0, 1};
            std::memcpy(ubo.data(), red, 16);
            std::memcpy(ubo.data() + stride, green, 16);
            const BufferHandle ubuf = gpu.buffer(BufferUsage::kUniform, ubo, 0, "colors");
            const char *fs = R"(#version 450
layout(std140, set = 0, binding = 0) uniform U { vec4 color; } u;
layout(location = 0) out vec4 outColor;
void main() { outColor = u.color; })";
            const PipelineHandle pipe = gpu.pipeline(gpu.shader(ShaderStage::kVertex, kFullscreenVsGlsl450), gpu.shader(ShaderStage::kFragment, fs));
            BindGroupDesc gd;
            gd.entries = {{.binding = 0, .type = BindingType::kUniformBuffer, .buffer = ubuf, .size = 16, .dynamicOffset = true}};
            gd.debugName = "colors";
            auto group = gpu->createBindGroup(gd);
            REQUIRE(group.has_value());
            const TextureHandle color = gpu.target(Format::kRGBA8Unorm, {4, 4});
            for (int i = 0; i < 2; ++i)
            {
                const std::uint32_t offset = i == 0 ? 0u : static_cast<std::uint32_t>(stride);
                gpu.run(
                    [&](CommandEncoder &e)
                    {
                        e.beginRenderPass(passOn(color));
                        e.setPipeline(pipe);
                        e.setBindGroup(0, *group, std::span(&offset, 1));
                        e.draw(3);
                        e.endRenderPass();
                    });
                CHECK(rgba(gpu.pixel(color, 1, 1)) == (i == 0 ? std::array<std::uint8_t, 4>{255, 0, 0, 255} : std::array<std::uint8_t, 4>{0, 255, 0, 255}));
            }
            if (align > 1) // un décalage désaligné n'existe que si l'alignement dépasse 1
            {
                const auto misaligned = static_cast<std::uint32_t>(align / 2);
                auto bad = gpu->createCommandEncoder();
                bad->setBindGroup(0, *group, std::span(&misaligned, 1));
                CHECK((tests::errorOf(gpu->submit(std::move(bad))) == ErrorCode::kInvalidArgument));
            }
            auto missing = gpu->createCommandEncoder();
            missing->setBindGroup(0, *group, {});
            CHECK((tests::errorOf(gpu->submit(std::move(missing))) == ErrorCode::kInvalidArgument));

            // Texture échantillonnée (groupe 1, binding 0).
            TextureDesc td;
            td.size = {2, 2, 1};
            td.usage = TextureUsage::kSampled | TextureUsage::kCopyDst;
            auto tex = gpu->createTexture(td);
            REQUIRE(tex.has_value());
            const std::vector<std::uint8_t> texels{255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
            REQUIRE(gpu->updateTexture(*tex, {}, std::as_bytes(std::span(texels))).has_value());
            auto sampler = gpu->createSampler({.minFilter = Filter::kNearest, .magFilter = Filter::kNearest, .mipFilter = std::nullopt});
            REQUIRE(sampler.has_value());
            const char *sfs = R"(#version 450
layout(set = 1, binding = 0) uniform sampler2D tex;
layout(location = 0) out vec4 outColor;
void main() { outColor = texture(tex, gl_FragCoord.xy / 2.0); })";
            const PipelineHandle spipe = gpu.pipeline(gpu.shader(ShaderStage::kVertex, kFullscreenVsGlsl450), gpu.shader(ShaderStage::kFragment, sfs));
            BindGroupDesc sd;
            sd.entries = {{.binding = 0, .type = BindingType::kSampledTexture, .texture = *tex, .sampler = *sampler}};
            auto sgroup = gpu->createBindGroup(sd);
            REQUIRE(sgroup.has_value());
            const TextureHandle out = gpu.target(Format::kRGBA8Unorm, {2, 2}, "sample.out");
            gpu.run(
                [&](CommandEncoder &e)
                {
                    e.beginRenderPass(passOn(out));
                    e.setPipeline(spipe);
                    e.setBindGroup(1, *sgroup);
                    e.draw(3);
                    e.endRenderPass();
                });
            auto result = gpu->readTexture(out);
            REQUIRE(result.has_value());
            CHECK(*result == toVec(std::as_bytes(std::span(texels))));
            gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("bind group creation validates usage, ranges and stale handles")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            const BufferHandle vertexOnly = gpu.buffer(BufferUsage::kVertex, {}, 256);
            BindGroupDesc d;
            d.entries = {{.binding = 0, .type = BindingType::kUniformBuffer, .buffer = vertexOnly}};
            CHECK((tests::errorOf(gpu->createBindGroup(d)) == ErrorCode::kInvalidArgument));
            const BufferHandle ubo = gpu.buffer(BufferUsage::kUniform, {}, 256);
            d.entries = {{.binding = kBindingsPerGroup, .type = BindingType::kUniformBuffer, .buffer = ubo}};
            CHECK((tests::errorOf(gpu->createBindGroup(d)) == ErrorCode::kOutOfRange));
            d.entries = {{.binding = 0, .type = BindingType::kUniformBuffer, .buffer = ubo},
                         {.binding = 0, .type = BindingType::kUniformBuffer, .buffer = ubo}};
            CHECK((tests::errorOf(gpu->createBindGroup(d)) == ErrorCode::kAlreadyExists));
            d.entries = {{.binding = 0, .type = BindingType::kUniformBuffer, .buffer = ubo}};
            auto ok = gpu->createBindGroup(d);
            REQUIRE(ok.has_value());
            gpu->destroy(ubo);
            auto encoder = gpu->createCommandEncoder();
            encoder->setBindGroup(0, *ok, {});
            CHECK((tests::errorOf(gpu->submit(std::move(encoder))) == ErrorCode::kNotFound)); // la ressource du groupe a été détruite
            gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("layers and cube faces are valid render targets")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            const TextureHandle array = gpu.target(Format::kRGBA8Unorm, {4, 4}, "array", TextureDimension::k2DArray, 3);
            const TextureHandle cube = gpu.target(Format::kRGBA8Unorm, {4, 4}, "cube", TextureDimension::kCube);
            const std::array<float, 4> colors[3] = {{1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 1, 1}};
            gpu.run(
                [&](CommandEncoder &e)
                {
                    for (std::uint32_t layer = 0; layer < 3; ++layer)
                    {
                        RenderPassDesc pass;
                        pass.colors.push_back({.texture = array, .layer = layer, .clearColor = colors[layer]});
                        e.beginRenderPass(pass);
                        e.endRenderPass();
                    }
                    RenderPassDesc face;
                    face.colors.push_back({.texture = cube, .layer = 3, .clearColor = {1, 1, 0, 1}});
                    e.beginRenderPass(face);
                    e.endRenderPass();
                });
            CHECK(rgba(gpu.pixel(array, 1, 1, 0, 0)) == std::array<std::uint8_t, 4>{255, 0, 0, 255});
            CHECK(rgba(gpu.pixel(array, 1, 1, 0, 1)) == std::array<std::uint8_t, 4>{0, 255, 0, 255});
            CHECK(rgba(gpu.pixel(array, 1, 1, 0, 2)) == std::array<std::uint8_t, 4>{0, 0, 255, 255});
            CHECK(rgba(gpu.pixel(cube, 1, 1, 0, 3)) == std::array<std::uint8_t, 4>{255, 255, 0, 255});
            TextureCopy outside;
            outside.layer = 3;
            CHECK((tests::errorOf(gpu->readTexture(array, outside)) == ErrorCode::kOutOfRange));
            gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("encoder misuse is reported by submit and the bad commands are ignored")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            const PipelineHandle pipe = gpu.pipeline(gpu.shader(ShaderStage::kVertex, kFullscreenVsGlsl450), gpu.shader(ShaderStage::kFragment, kRedFsGlsl450));
            const TextureHandle color = gpu.target(Format::kRGBA8Unorm, {4, 4});

            auto drawOutside = gpu->createCommandEncoder();
            drawOutside->draw(3);
            CHECK((tests::errorOf(gpu->submit(std::move(drawOutside))) == ErrorCode::kInvalidArgument));

            auto endWithoutBegin = gpu->createCommandEncoder();
            endWithoutBegin->endRenderPass();
            CHECK_FALSE(gpu->submit(std::move(endWithoutBegin)).has_value());

            auto noPipeline = gpu->createCommandEncoder();
            noPipeline->beginRenderPass(passOn(color));
            noPipeline->draw(3);
            noPipeline->endRenderPass();
            CHECK_FALSE(gpu->submit(std::move(noPipeline)).has_value());

            // Format de cible différent de celui de la passe.
            const PipelineHandle wrongFormat =
                gpu.pipeline(gpu.shader(ShaderStage::kVertex, kFullscreenVsGlsl450), gpu.shader(ShaderStage::kFragment, kRedFsGlsl450), {ColorTargetState{.format = Format::kRGBA16Float}});
            auto mismatch = gpu->createCommandEncoder();
            mismatch->beginRenderPass(passOn(color));
            mismatch->setPipeline(wrongFormat);
            mismatch->endRenderPass();
            CHECK((tests::errorOf(gpu->submit(std::move(mismatch))) == ErrorCode::kInvalidArgument));

            // Pipeline détruit : handle périmé.
            gpu->destroy(pipe);
            auto stale = gpu->createCommandEncoder();
            stale->beginRenderPass(passOn(color));
            stale->setPipeline(pipe);
            stale->endRenderPass();
            CHECK((tests::errorOf(gpu->submit(std::move(stale))) == ErrorCode::kNotFound));

            CHECK_FALSE(gpu->submit(nullptr).has_value());
            gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("a valid frame loop: beginFrame, render to the backbuffer, present, resize keeps the handle")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend, {6, 4});
            REQUIRE(gpu.device);
            auto frame = gpu->beginFrame();
            REQUIRE(frame.has_value());
            gpu.run([&](CommandEncoder &e) { e.beginRenderPass(passOn(*frame, {1, 0, 1, 1})); e.endRenderPass(); });
            CHECK(rgba(gpu.pixel(*frame, 5, 3)) == std::array<std::uint8_t, 4>{255, 0, 255, 255});
            REQUIRE(gpu->present().has_value());

            REQUIRE(gpu->resize({10, 7}).has_value());
            auto again = gpu->beginFrame();
            REQUIRE(again.has_value());
            CHECK(*again == *frame); // même handle après redimensionnement
            auto all = gpu->readTexture(*again);
            REQUIRE(all.has_value());
            CHECK(all->size() == 10u * 7u * 4u);
            CHECK((tests::errorOf(gpu->resize({0, 5})) == ErrorCode::kInvalidArgument));
            gpu->destroy(*again); // le backbuffer appartient au périphérique : sans effet
            CHECK(gpu->readTexture(*again).has_value());
            gpu.expectNoValidationErrors();
        }
    }
}

TEST_SUITE("rhi/backend/compute")
{
    TEST_CASE("compute writes a storage buffer; dispatchIndirect reads its group count from a buffer (RHI-09, RHI-17)")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            if (!gpu->capabilities().compute)
            {
                MESSAGE("Skipped: no compute shaders on ", tests::backendName(backend));
                continue;
            }
            const char *cs = R"(#version 450
layout(local_size_x = 16) in;
layout(std430, set = 0, binding = 1) buffer Out { uint data[]; };
void main() { data[gl_GlobalInvocationID.x] = gl_GlobalInvocationID.x * 2u + 1u; })";
            const ShaderHandle shader = gpu.shader(ShaderStage::kCompute, cs, "double");
            auto pipe = gpu->createComputePipeline({.compute = shader, .debugName = "double"});
            REQUIRE(pipe.has_value());

            const BufferHandle ssbo = gpu.buffer(BufferUsage::kStorage | BufferUsage::kCopySrc, {}, 64 * 4, "out");
            BindGroupDesc gd;
            gd.entries = {{.binding = 1, .type = BindingType::kStorageBuffer, .buffer = ssbo}};
            auto group = gpu->createBindGroup(gd);
            REQUIRE(group.has_value());

            gpu.run(
                [&](CommandEncoder &e)
                {
                    e.bindComputePipeline(*pipe);
                    e.setBindGroup(0, *group);
                    e.dispatch(4, 1, 1);
                });
            auto data = gpu->readBuffer(ssbo, 0, 64 * 4);
            REQUIRE(data.has_value());
            std::uint32_t words[64];
            std::memcpy(words, data->data(), sizeof words);
            for (std::uint32_t i = 0; i < 64; ++i)
            {
                CHECK(words[i] == i * 2 + 1);
            }

            // Indirect : 2 groupes seulement (32 premiers éléments réécrits avec une autre valeur).
            const std::vector<std::byte> zeros(64 * 4, std::byte{0});
            REQUIRE(gpu->updateBuffer(ssbo, 0, zeros).has_value());
            const DispatchIndirectCommand cmd{2, 1, 1};
            const BufferHandle args = gpu.buffer(BufferUsage::kIndirect, bytesOf(cmd));
            gpu.run(
                [&](CommandEncoder &e)
                {
                    e.bindComputePipeline(*pipe);
                    e.setBindGroup(0, *group);
                    e.dispatchIndirect(args, 0);
                });
            data = gpu->readBuffer(ssbo, 0, 64 * 4);
            REQUIRE(data.has_value());
            std::memcpy(words, data->data(), sizeof words);
            CHECK(words[31] == 63);
            CHECK(words[32] == 0);

            // Dépasser la limite de groupes est refusé.
            auto big = gpu->createCommandEncoder();
            big->bindComputePipeline(*pipe);
            big->dispatch(gpu->capabilities().maxComputeWorkGroupCount[0] + 1, 1, 1);
            CHECK((tests::errorOf(gpu->submit(std::move(big))) == ErrorCode::kOutOfRange));
            auto noPipe = gpu->createCommandEncoder();
            noPipe->dispatch(1, 1, 1);
            CHECK_FALSE(gpu->submit(std::move(noPipe)).has_value());
            gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("compute writes a 3D R32F storage image, read back slice by slice (RHI-16)")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            TextureDesc d;
            d.dimension = TextureDimension::k3D;
            d.format = Format::kR32Float;
            d.size = {4, 4, 4};
            d.usage = TextureUsage::kStorage | TextureUsage::kSampled | TextureUsage::kCopySrc;
            d.debugName = "volume";
            auto volume = gpu->createTexture(d);
            REQUIRE(volume.has_value());
            const char *cs = R"(#version 450
layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;
layout(set = 0, binding = 0, r32f) uniform image3D img;
void main() { ivec3 p = ivec3(gl_GlobalInvocationID); imageStore(img, p, vec4(float(p.x + 10 * p.y + 100 * p.z))); })";
            const ShaderHandle shader = gpu.shader(ShaderStage::kCompute, cs, "volume");
            auto pipe = gpu->createComputePipeline({.compute = shader});
            REQUIRE(pipe.has_value());
            BindGroupDesc gd;
            gd.entries = {{.binding = 0, .type = BindingType::kStorageImage, .texture = *volume}};
            auto group = gpu->createBindGroup(gd);
            REQUIRE(group.has_value());
            gpu.run(
                [&](CommandEncoder &e)
                {
                    e.bindComputePipeline(*pipe);
                    e.setBindGroup(0, *group);
                    e.dispatch(1, 1, 1);
                });
            auto data = gpu->readTexture(*volume);
            REQUIRE(data.has_value());
            REQUIRE(data->size() == 4u * 4u * 4u * 4u);
            float values[64];
            std::memcpy(values, data->data(), sizeof values);
            for (int z = 0; z < 4; ++z)
            {
                for (int y = 0; y < 4; ++y)
                {
                    for (int x = 0; x < 4; ++x)
                    {
                        CHECK(values[z * 16 + y * 4 + x] == doctest::Approx(static_cast<float>(x + 10 * y + 100 * z)));
                    }
                }
            }
            gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("copies: buffer to buffer, buffer to texture and texture to buffer")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            const std::vector<std::uint8_t> texels{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}; // 2x2 RGBA8
            const BufferHandle src = gpu.buffer(BufferUsage::kCopySrc, std::as_bytes(std::span(texels)), 0, "src");
            const BufferHandle mid = gpu.buffer(BufferUsage::kCopySrc | BufferUsage::kCopyDst, {}, 16, "mid");
            const BufferHandle dst = gpu.buffer(BufferUsage::kCopyDst, {}, 16, "dst", MemoryKind::kReadback);
            TextureDesc td;
            td.size = {2, 2, 1};
            td.usage = TextureUsage::kSampled | TextureUsage::kCopyDst | TextureUsage::kCopySrc;
            auto tex = gpu->createTexture(td);
            REQUIRE(tex.has_value());
            gpu.run(
                [&](CommandEncoder &e)
                {
                    e.copyBuffer(src, mid, {.size = 16});
                    e.copyBufferToTexture(mid, *tex, {});
                    e.copyTextureToBuffer(*tex, dst, {});
                });
            auto out = gpu->readBuffer(dst, 0, 16);
            REQUIRE(out.has_value());
            CHECK(*out == toVec(std::as_bytes(std::span(texels))));

            auto overlap = gpu->createCommandEncoder();
            overlap->copyBuffer(mid, mid, {.srcOffset = 0, .dstOffset = 4, .size = 8});
            CHECK((tests::errorOf(gpu->submit(std::move(overlap))) == ErrorCode::kInvalidArgument));
            auto noUsage = gpu->createCommandEncoder();
            noUsage->copyBuffer(dst, mid, {.size = 4}); // dst n'a pas CopySrc
            CHECK((tests::errorOf(gpu->submit(std::move(noUsage))) == ErrorCode::kInvalidArgument));
            gpu.expectNoValidationErrors();
        }
    }
}

TEST_SUITE("rhi/backend/timing")
{
    TEST_CASE("GPU timestamps bracket a draw, in increasing order (RHI-11)")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            if (!gpu->capabilities().timestampQueries)
            {
                MESSAGE("Skipped: no timestamp queries on ", tests::backendName(backend));
                continue;
            }
            auto pool = gpu->createQueryPool(2, "frame");
            REQUIRE(pool.has_value());
            const TextureHandle color = gpu.target(Format::kRGBA8Unorm, {64, 64});
            const PipelineHandle pipe = gpu.pipeline(gpu.shader(ShaderStage::kVertex, kFullscreenVsGlsl450), gpu.shader(ShaderStage::kFragment, kRedFsGlsl450));
            // Les valeurs ne sont pas lisibles avant écriture.
            CHECK_FALSE(gpu->readTimestamps(*pool, 0, 2).has_value());
            gpu.run(
                [&](CommandEncoder &e)
                {
                    e.beginDebugGroup("timed");
                    e.writeTimestamp(*pool, 0);
                    e.beginRenderPass(passOn(color));
                    e.setPipeline(pipe);
                    e.draw(3);
                    e.endRenderPass();
                    e.writeTimestamp(*pool, 1);
                    e.endDebugGroup();
                });
            auto t = gpu->readTimestamps(*pool, 0, 2);
            REQUIRE(t.has_value());
            CHECK((*t)[0] > 0);
            CHECK((*t)[1] >= (*t)[0]);
            CHECK((tests::errorOf(gpu->readTimestamps(*pool, 1, 2)) == ErrorCode::kOutOfRange));
            auto bad = gpu->createCommandEncoder();
            bad->writeTimestamp(*pool, 2);
            CHECK((tests::errorOf(gpu->submit(std::move(bad))) == ErrorCode::kOutOfRange));
            auto unbalanced = gpu->createCommandEncoder();
            unbalanced->endDebugGroup();
            CHECK_FALSE(gpu->submit(std::move(unbalanced)).has_value());
            gpu.expectNoValidationErrors();
        }
    }
}

TEST_SUITE("rhi/backend/query")
{
    TEST_CASE("textureDesc reports the resolved description; stale handles are refused")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend, {6, 4});
            REQUIRE(gpu.device);
            TextureDesc d;
            d.size = {8, 4, 1};
            d.mipLevels = 0;
            d.usage = TextureUsage::kSampled;
            auto t = gpu->createTexture(d);
            REQUIRE(t.has_value());
            auto real = gpu->textureDesc(*t);
            REQUIRE(real.has_value());
            CHECK(real->mipLevels == 4);
            CHECK(real->size.width == 8);
            auto frame = gpu->beginFrame();
            REQUIRE(frame.has_value());
            auto frameDesc = gpu->textureDesc(*frame);
            REQUIRE(frameDesc.has_value());
            CHECK(frameDesc->size.width == 6);
            gpu->destroy(*t);
            CHECK((tests::errorOf(gpu->textureDesc(*t)) == ErrorCode::kNotFound));
        }
    }

    TEST_CASE("timestampsReady is false until the timestamps were written and finished by the GPU")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            if (!gpu->capabilities().timestampQueries)
            {
                MESSAGE("Skipped: no timestamp queries on ", tests::backendName(backend));
                continue;
            }
            auto pool = gpu->createQueryPool(2, "ready");
            REQUIRE(pool.has_value());
            CHECK_FALSE(tests::valueOf(gpu->timestampsReady(*pool, 0, 2)));
            gpu.run([&](CommandEncoder &e) { e.writeTimestamp(*pool, 0); e.writeTimestamp(*pool, 1); });
            gpu->waitIdle();
            CHECK(tests::valueOf(gpu->timestampsReady(*pool, 0, 2)));
            CHECK((tests::errorOf(gpu->timestampsReady(*pool, 1, 5)) == ErrorCode::kOutOfRange));
            gpu->destroy(*pool);
            CHECK((tests::errorOf(gpu->timestampsReady(*pool, 0, 1)) == ErrorCode::kNotFound));
        }
    }
}
