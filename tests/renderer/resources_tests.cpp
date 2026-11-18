// RHI-02 (lot B3) : couche de ressources de rendu au-dessus du RHI : tampons, textures, cibles de rendu, images, shaders,
// pipelines, minuterie GPU. Contexte hors écran ; sans pilote OpenGL 4.3 chaque test s'arrête avec un avertissement.
#include <doctest/doctest.h>

#include "support/test_support.hpp"
#include "rhi/gpu_fixture.hpp"

#include <engine/renderer/gpu_buffer.hpp>
#include <engine/renderer/gpu_timer.hpp>
#include <engine/renderer/image.hpp>
#include <engine/renderer/pipeline_cache.hpp>
#include <engine/renderer/render_target.hpp>
#include <engine/renderer/shader_library.hpp>
#include <engine/renderer/texture.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

namespace fs = std::filesystem;
namespace rd = engine::renderer;

namespace
{

constexpr const char *kFullscreenVs450 = R"(#version 450
void main() { vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2); gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); })";

std::string redFs(const char *color = "vec4(1.0, 0.0, 0.0, 1.0)")
{
    return std::string("#version 450\nlayout(location = 0) out vec4 c;\nvoid main() { c = ") + color + "; }\n";
}

struct TempDir
{
    fs::path path;
    TempDir()
    {
        path = fs::temp_directory_path() / ("engine_renderer_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    void write(const std::string &name, const std::string &text) const { std::ofstream(path / name, std::ios::trunc | std::ios::binary) << text; }
};

// Dessine un triangle plein écran dans `target` (cible RenderTarget) avec `pipeline`, ciseau limité au coin bas-gauche si demandé.
void drawFullscreen(Gpu &gpu, const rd::RenderTarget &target, PipelineHandle pipeline, bool cornerOnly = false)
{
    gpu.run(
        [&](CommandEncoder &e)
        {
            e.beginRenderPass(target.passDesc());
            e.setPipeline(pipeline);
            if (cornerOnly)
            {
                e.setScissor({0, 0, 2, 2});
            }
            e.draw(3);
            e.endRenderPass();
        });
}

} // namespace

TEST_SUITE("renderer/buffer")
{
    TEST_CASE("GpuBuffer: create, update, read, resize with and without content, RAII release")
    {
        Gpu gpu;
        SKIP_TEST_IF_NULL(gpu.device.get(), "OpenGL 4.3 context");
        {
            const std::vector<std::uint32_t> initial{1, 2, 3, 4};
            BufferDesc desc;
            desc.size = 16;
            desc.usage = BufferUsage::kStorage | BufferUsage::kCopySrc | BufferUsage::kCopyDst;
            desc.debugName = "buf";
            auto buffer = rd::GpuBuffer::create(*gpu.device, desc, bytesOf(initial));
            REQUIRE(buffer.has_value());
            CHECK(buffer->valid());
            CHECK(buffer->size() == 16);
            CHECK(gpu->stats().buffers == 1);

            const std::uint32_t patch = 77;
            REQUIRE(buffer->update(4, bytesOf(patch)).has_value());
            REQUIRE(buffer->resize(32, true).has_value());
            CHECK(buffer->size() == 32);
            auto data = buffer->read(0, 16);
            REQUIRE(data.has_value());
            std::uint32_t words[4];
            std::memcpy(words, data->data(), 16);
            CHECK(words[0] == 1);
            CHECK(words[1] == 77);
            CHECK(words[3] == 4);

            REQUIRE(buffer->resize(8, false).has_value());
            CHECK(buffer->size() == 8);
            CHECK(gpu->stats().buffers == 1); // l'ancien est détruit (différé)

            rd::GpuBuffer moved = std::move(*buffer);
            CHECK_FALSE(buffer->valid());
            CHECK(moved.valid());
            CHECK_FALSE(moved.update(100, bytesOf(patch)).has_value()); // hors limites
            rd::GpuBuffer empty;
            CHECK((empty.update(0, bytesOf(patch)).error().code == ErrorCode::kInvalidArgument));
        }
        CHECK(gpu->stats().buffers == 0); // détruit à la sortie du bloc
        BufferDesc bad;
        CHECK_FALSE(rd::GpuBuffer::create(*gpu.device, bad).has_value());
        gpu.expectNoValidationErrors();
    }
}

TEST_SUITE("renderer/texture")
{
    TEST_CASE("Texture: create with texels, filters decide the mips, upload, resize, readback, RAII release")
    {
        Gpu gpu;
        SKIP_TEST_IF_NULL(gpu.device.get(), "OpenGL 4.3 context");
        {
            std::vector<std::uint8_t> px(8 * 8 * 4, 200);
            rd::TextureInfo info;
            info.width = 8;
            info.height = 8;
            info.debugName = "linear";
            auto linear = rd::Texture::create(*gpu.device, info, std::as_bytes(std::span(px)));
            REQUIRE(linear.has_value());
            CHECK(linear->mipLevels() == 4); // 8 -> 4 -> 2 -> 1
            CHECK(linear->byteSize() == 256);

            info.filter = rd::FilterMode::kNearest;
            auto nearest = rd::Texture::create(*gpu.device, info, std::as_bytes(std::span(px)));
            REQUIRE(nearest.has_value());
            CHECK(nearest->mipLevels() == 1);
            info.filter = rd::FilterMode::kLinearNoMipmaps;
            auto noMips = rd::Texture::create(*gpu.device, info);
            REQUIRE(noMips.has_value());
            CHECK(noMips->mipLevels() == 1);

            auto back = linear->readback();
            REQUIRE(back.has_value());
            CHECK(back->size() == 256);
            CHECK(static_cast<std::uint8_t>((*back)[0]) == 200);
            CHECK(rgba(gpu.pixel(linear->handle(), 0, 0, 3))[0] == 200); // le dernier mip existe et vaut la moyenne

            std::vector<std::uint8_t> wrong(10);
            CHECK((linear->upload(std::as_bytes(std::span(wrong))).error().code == ErrorCode::kInvalidArgument));
            std::vector<std::uint8_t> other(8 * 8 * 4, 50);
            REQUIRE(linear->upload(std::as_bytes(std::span(other))).has_value());
            CHECK(static_cast<std::uint8_t>((*linear->readback())[0]) == 50);

            REQUIRE(linear->resize(16, 4).has_value());
            CHECK(linear->width() == 16);
            CHECK(linear->height() == 4);
            CHECK(linear->mipLevels() == 5);
            CHECK(linear->readback()->size() == 16u * 4u * 4u);
            CHECK_FALSE(linear->resize(0, 4).has_value());
            CHECK(gpu->stats().textures == 3);
        }
        CHECK(gpu->stats().textures == 0);
        CHECK(gpu->stats().samplers == 0);
        gpu.expectNoValidationErrors();
    }

    TEST_CASE("Texture::fromFile decodes to RGBA8 and flips vertically to match the RHI orientation")
    {
        Gpu gpu;
        SKIP_TEST_IF_NULL(gpu.device.get(), "OpenGL 4.3 context");
        TempDir dir;
        rd::Image image;
        image.width = 2;
        image.height = 2;
        image.rgba = {255, 0, 0, 255, 0, 255, 0, 255,   // ligne du haut : rouge, vert
                      0, 0, 255, 255, 255, 255, 0, 255}; // ligne du bas : bleu, jaune
        REQUIRE(rd::savePng(image, dir.path / "tex.png").has_value());
        auto texture = rd::Texture::fromFile(*gpu.device, dir.path / "tex.png", rd::FilterMode::kNearest);
        REQUIRE(texture.has_value());
        auto bytes = texture->readback();
        REQUIRE(bytes.has_value());
        const auto *p = reinterpret_cast<const std::uint8_t *>(bytes->data());
        CHECK((p[0] == 0 && p[1] == 0 && p[2] == 255));   // première ligne lue = bas de l'image : bleu
        CHECK((p[8] == 255 && p[9] == 0 && p[10] == 0));  // puis le haut : rouge
        CHECK_FALSE(rd::Texture::fromFile(*gpu.device, dir.path / "absent.png").has_value());
        gpu.expectNoValidationErrors();
    }
}

TEST_SUITE("renderer/render_target")
{
    TEST_CASE("RenderTarget: MRT + depth, pass description, pixel reads from the top-left, resize and reconfigure")
    {
        Gpu gpu;
        SKIP_TEST_IF_NULL(gpu.device.get(), "OpenGL 4.3 context");
        rd::RenderTargetInfo info;
        info.size = {8, 8};
        info.colorFormats = {Format::kRGBA8Unorm, Format::kR32Uint};
        info.depthSampled = true;
        info.debugName = "scene";
        auto target = rd::RenderTarget::create(*gpu.device, info);
        REQUIRE(target.has_value());
        CHECK(target->colorCount() == 2);
        CHECK_FALSE(target->depth().isNull());
        CHECK(gpu->stats().textures == 3);

        const auto pass = target->passDesc();
        REQUIRE(pass.colors.size() == 2);
        CHECK(pass.depth.has_value());
        CHECK((pass.colors[0].load == LoadOp::kClear));

        auto vs = gpu.shader(ShaderStage::kVertex, kFullscreenVs);
        const char *fsSource = R"(#version 430 core
layout(location = 0) out vec4 color;
layout(location = 1) out uint id;
void main() { color = vec4(1.0, 0.0, 0.0, 1.0); id = 9u; })";
        auto fs = gpu.shader(ShaderStage::kFragment, fsSource);
        GraphicsPipelineDesc pd;
        pd.vertex = vs;
        pd.fragment = fs;
        pd.colorTargets = {ColorTargetState{.format = Format::kRGBA8Unorm}, ColorTargetState{.format = Format::kR32Uint}};
        pd.depthFormat = Format::kD32Float;
        pd.raster.cull = CullMode::kNone;
        auto pipe = gpu->createGraphicsPipeline(pd);
        REQUIRE(pipe.has_value());
        drawFullscreen(gpu, *target, *pipe, true); // ciseau : coin BAS gauche (repère du RHI)
        // Lecture depuis le coin HAUT gauche : y = hauteur - 1 est la ligne du bas.
        CHECK((*target->readPixelRgba8(0, 0, 7)) == std::array<std::uint8_t, 4>{255, 0, 0, 255});
        CHECK((*target->readPixelRgba8(0, 0, 0)) == std::array<std::uint8_t, 4>{0, 0, 0, 255});
        CHECK(*target->readPixelUInt(1, 1, 6) == 9);
        CHECK(*target->readPixelUInt(1, 7, 0) == 0);
        CHECK((target->readPixelUInt(0, 0, 0).error().code == ErrorCode::kInvalidArgument)); // pas R32Uint
        CHECK((target->readPixelRgba8(1, 0, 0).error().code == ErrorCode::kInvalidArgument));
        CHECK((target->readPixelUInt(1, 8, 0).error().code == ErrorCode::kOutOfRange));

        REQUIRE(target->resize(16, 4).has_value());
        CHECK(target->width() == 16);
        CHECK(target->height() == 4);
        CHECK(gpu->stats().textures == 3); // pas de fuite : les anciens sont détruits
        REQUIRE(target->resize(16, 4).has_value()); // même taille : sans effet
        rd::RenderTargetInfo plain;
        plain.size = {4, 4};
        plain.depth = false;
        REQUIRE(target->reconfigure(plain).has_value());
        CHECK(target->colorCount() == 1);
        CHECK(target->depth().isNull());
        CHECK(gpu->stats().textures == 1);

        rd::RenderTargetInfo tooMany;
        tooMany.colorFormats.assign(gpu->capabilities().maxColorAttachments + 1, Format::kRGBA8Unorm);
        CHECK((rd::RenderTarget::create(*gpu.device, tooMany).error().code == ErrorCode::kOutOfRange));
        CHECK(target->valid()); // la cible précédente reste utilisable après un reconfigure échoué
        CHECK_FALSE(target->reconfigure(tooMany).has_value());
        CHECK(target->valid());
        target->reset();
        CHECK(gpu->stats().textures == 0);
        gpu.expectNoValidationErrors();
    }
}

TEST_SUITE("renderer/image")
{
    TEST_CASE("readImage flips rows to a top-left origin; PNG round-trips; compareImages reports differences")
    {
        Gpu gpu;
        SKIP_TEST_IF_NULL(gpu.device.get(), "OpenGL 4.3 context");
        TempDir dir;
        const TextureHandle color = gpu.target(Format::kRGBA8Unorm, {4, 4}, "img");
        const PipelineHandle pipe = gpu.pipeline(gpu.shader(ShaderStage::kVertex, kFullscreenVs), gpu.shader(ShaderStage::kFragment, kRedFs));
        gpu.run(
            [&](CommandEncoder &e)
            {
                e.beginRenderPass(passOn(color, {0, 0, 1, 1}));
                e.setPipeline(pipe);
                e.setScissor({0, 0, 2, 2}); // coin bas gauche en rouge
                e.draw(3);
                e.endRenderPass();
            });
        auto image = rd::readImage(*gpu.device, color);
        REQUIRE(image.has_value());
        CHECK(image->width == 4);
        CHECK(image->height == 4);
        auto at = [&](int x, int y) { return std::array<std::uint8_t, 4>{image->rgba[(y * 4 + x) * 4], image->rgba[(y * 4 + x) * 4 + 1], image->rgba[(y * 4 + x) * 4 + 2], image->rgba[(y * 4 + x) * 4 + 3]}; };
        CHECK((at(0, 3) == std::array<std::uint8_t, 4>{255, 0, 0, 255})); // bas de l'image
        CHECK((at(0, 0) == std::array<std::uint8_t, 4>{0, 0, 255, 255})); // haut de l'image

        const fs::path png = dir.path / "out" / "shot.png";
        REQUIRE(rd::savePng(*gpu.device, color, png).has_value()); // crée le dossier
        auto loaded = rd::loadPng(png);
        REQUIRE(loaded.has_value());
        CHECK(loaded->rgba == image->rgba);

        auto same = rd::compareImages(*image, *loaded, 0);
        REQUIRE(same.has_value());
        CHECK(same->outOfTolerance == 0);
        CHECK(same->maxDelta == 0);
        rd::Image changed = *loaded;
        changed.rgba[48] = 250; // pixel (0, 3) : rouge 255 -> 250
        changed.rgba[49] = 3;
        auto diff = rd::compareImages(*image, changed, 4);
        REQUIRE(diff.has_value());
        CHECK(diff->outOfTolerance == 1);
        CHECK(diff->maxDelta == 5);
        auto tolerant = rd::compareImages(*image, changed, 5);
        CHECK(tolerant->outOfTolerance == 0);
        rd::Image small;
        small.width = 1;
        small.height = 1;
        small.rgba = {0, 0, 0, 0};
        CHECK_FALSE(rd::compareImages(*image, small, 0).has_value());
        CHECK_FALSE(rd::loadPng(dir.path / "missing.png").has_value());

        const TextureHandle wrong = gpu.target(Format::kRG16Float, {2, 2}, "rg");
        CHECK((rd::readImage(*gpu.device, wrong).error().code == ErrorCode::kUnsupported));
        gpu.expectNoValidationErrors();
    }
}

TEST_SUITE("renderer/shader")
{
    TEST_CASE("ShaderLibrary: graphics from source, merged reflection, binding-type conflicts, errors name the stage")
    {
        Gpu gpu;
        SKIP_TEST_IF_NULL(gpu.device.get(), "OpenGL 4.3 context");
        rd::ShaderLibrary library(*gpu.device);
        const std::string fs = R"(#version 450
layout(set = 1, binding = 0) uniform sampler2D albedo;
layout(push_constant) uniform Push { vec4 tint; } pc;
layout(location = 0) out vec4 c;
void main() { c = texture(albedo, vec2(0.5)) * pc.tint; })";
        auto program = library.graphicsFromSource("lit", kFullscreenVs450, fs);
        INFO((program ? std::string() : program.error().message));
        REQUIRE(program.has_value());
        CHECK_FALSE((*program)->isCompute());
        CHECK_FALSE((*program)->vertex().isNull());
        CHECK_FALSE((*program)->fragment().isNull());
        CHECK((*program)->reflection().pushConstantSize == 16);
        REQUIRE((*program)->reflection().bindings.size() == 1);
        CHECK((*program)->reflection().bindings[0].name == "albedo");
        CHECK(library.programCount() == 1);

        auto broken = library.graphicsFromSource("broken", kFullscreenVs450, "#version 450\nvoid main() { nonsense }\n");
        REQUIRE_FALSE(broken.has_value());
        CHECK(broken.error().message.find("broken.frag") != std::string::npos);

        const std::string vsBinding = "#version 450\nlayout(set = 0, binding = 0, std140) uniform U { vec4 v; } u;\nvoid main() { gl_Position = u.v; }\n";
        const std::string fsBinding = "#version 450\nlayout(set = 0, binding = 0) uniform sampler2D t;\nlayout(location = 0) out vec4 c;\nvoid main() { c = texture(t, vec2(0)); }\n";
        auto conflict = library.graphicsFromSource("conflict", vsBinding, fsBinding);
        REQUIRE_FALSE(conflict.has_value());
        CHECK(conflict.error().message.find("different type") != std::string::npos);
        gpu.expectNoValidationErrors();
        program->reset();
        CHECK(gpu->stats().shaders == 0); // les étages sont détruits avec le programme
    }

    TEST_CASE("ShaderLibrary + PipelineCache: files, hot reload bumps the revision and the cache rebuilds; a broken edit keeps the old shader")
    {
        Gpu gpu;
        SKIP_TEST_IF_NULL(gpu.device.get(), "OpenGL 4.3 context");
        TempDir dir;
        dir.write("full.vert", kFullscreenVs450);
        dir.write("c.frag", redFs());
        rd::ShaderLibrary library(*gpu.device, {.cacheDirectory = dir.path / "cache", .includeRoots = {dir.path}});
        auto program = library.loadGraphics(dir.path / "full.vert", dir.path / "c.frag");
        INFO((program ? std::string() : program.error().message));
        REQUIRE(program.has_value());
        CHECK((*program)->revision() == 1);
        CHECK(library.pollHotReload() == 0);

        rd::PipelineCache cache(*gpu.device);
        rd::RenderTarget target = *rd::RenderTarget::create(*gpu.device, {.size = {4, 4}, .depth = false, .debugName = "t"});
        rd::GraphicsState state;
        state.raster.cull = CullMode::kNone;
        auto first = cache.graphics(**program, state);
        REQUIRE(first.has_value());
        REQUIRE(cache.graphics(**program, state).has_value());
        CHECK(cache.hits() == 1);
        CHECK(cache.misses() == 1);
        drawFullscreen(gpu, target, *first);
        CHECK((*target.readPixelRgba8(0, 1, 1)) == std::array<std::uint8_t, 4>{255, 0, 0, 255});

        dir.write("c.frag", redFs("vec4(0.0, 1.0, 0.0, 1.0)")); // rechargement à chaud : devient vert
        CHECK(library.pollHotReload() == 1);
        CHECK((*program)->revision() == 2);
        auto second = cache.graphics(**program, state);
        REQUIRE(second.has_value());
        CHECK(cache.misses() == 2);
        CHECK(cache.size() == 1); // le pipeline de la révision 1 a été détruit
        drawFullscreen(gpu, target, *second);
        CHECK((*target.readPixelRgba8(0, 1, 1)) == std::array<std::uint8_t, 4>{0, 255, 0, 255});

        dir.write("c.frag", "#version 450\nvoid main() { nonsense }\n"); // édition cassée
        CHECK(library.pollHotReload() == 0);
        REQUIRE(library.lastError().has_value());
        CHECK((*program)->revision() == 2); // inchangé
        REQUIRE(cache.graphics(**program, state).has_value());
        CHECK(cache.hits() >= 2);
        drawFullscreen(gpu, target, *cache.graphics(**program, state));
        CHECK((*target.readPixelRgba8(0, 1, 1)) == std::array<std::uint8_t, 4>{0, 255, 0, 255}); // l'ancien shader tourne encore

        dir.write("c.frag", redFs("vec4(0.0, 0.0, 1.0, 1.0)")); // corrigé : bleu
        CHECK(library.pollHotReload() == 1);
        CHECK_FALSE(library.lastError().has_value());
        drawFullscreen(gpu, target, *cache.graphics(**program, state));
        CHECK((*target.readPixelRgba8(0, 1, 1)) == std::array<std::uint8_t, 4>{0, 0, 255, 255});

        // Autre état fixe => autre pipeline ; clear invalide les handles.
        rd::GraphicsState blended = state;
        blended.colorTargets[0].blend.enabled = true;
        auto other = cache.graphics(**program, blended);
        REQUIRE(other.has_value());
        CHECK(*other != *cache.graphics(**program, state));
        CHECK(cache.size() == 2);
        cache.clear();
        CHECK(cache.size() == 0);
        CHECK(gpu->stats().pipelines == 0);

        // Une seconde bibliothèque sur le même dossier de cache relit le SPIR-V depuis le disque.
        dir.write("c.frag", redFs());
        rd::ShaderLibrary again(*gpu.device, {.cacheDirectory = dir.path / "cache", .includeRoots = {dir.path}});
        REQUIRE(again.loadGraphics(dir.path / "full.vert", dir.path / "c.frag").has_value());
        CHECK(again.cache().diskHits() >= 1);
        gpu.expectNoValidationErrors();
    }

    TEST_CASE("compute programs: loadCompute and the pipeline cache")
    {
        Gpu gpu;
        SKIP_TEST_IF_NULL(gpu.device.get(), "OpenGL 4.3 context");
        TempDir dir;
        dir.write("fill.comp", R"(#version 450
layout(local_size_x = 16) in;
layout(set = 0, binding = 1, std430) buffer Out { uint data[]; };
void main() { data[gl_GlobalInvocationID.x] = 7u; })");
        rd::ShaderLibrary library(*gpu.device);
        auto program = library.loadCompute(dir.path / "fill.comp");
        INFO((program ? std::string() : program.error().message));
        REQUIRE(program.has_value());
        CHECK((*program)->isCompute());
        CHECK((*program)->reflection().localSize[0] == 16);
        rd::PipelineCache cache(*gpu.device);
        auto pipe = cache.compute(**program);
        REQUIRE(pipe.has_value());
        CHECK((cache.graphics(**program, {}).error().code == ErrorCode::kInvalidArgument));
        const BufferHandle ssbo = gpu.buffer(BufferUsage::kStorage | BufferUsage::kCopySrc, {}, 64);
        BindGroupDesc g;
        g.entries = {{.binding = 1, .type = BindingType::kStorageBuffer, .buffer = ssbo}};
        auto group = gpu->createBindGroup(g);
        REQUIRE(group.has_value());
        gpu.run([&](CommandEncoder &e) { e.bindComputePipeline(*pipe); e.setBindGroup(0, *group); e.dispatch(1, 1, 1); });
        auto data = gpu->readBuffer(ssbo, 0, 64);
        REQUIRE(data.has_value());
        std::uint32_t words[16];
        std::memcpy(words, data->data(), 64);
        for (std::uint32_t w : words)
        {
            CHECK(w == 7);
        }
        gpu.expectNoValidationErrors();
    }
}

TEST_SUITE("renderer/gpu_timer")
{
    TEST_CASE("GpuTimer measures a pass without blocking: not ready before submission, a duration afterwards")
    {
        Gpu gpu;
        SKIP_TEST_IF_NULL(gpu.device.get(), "OpenGL 4.3 context");
        rd::GpuTimer timer(*gpu.device, "pass");
        REQUIRE(timer.valid());
        CHECK_FALSE(timer.isReady());
        CHECK_FALSE(timer.elapsed().has_value());
        const TextureHandle color = gpu.target(Format::kRGBA8Unorm, {64, 64});
        const PipelineHandle pipe = gpu.pipeline(gpu.shader(ShaderStage::kVertex, kFullscreenVs), gpu.shader(ShaderStage::kFragment, kRedFs));
        gpu.run(
            [&](CommandEncoder &e)
            {
                timer.begin(e);
                e.beginRenderPass(passOn(color));
                e.setPipeline(pipe);
                e.draw(3);
                e.endRenderPass();
                timer.end(e);
            });
        std::optional<std::chrono::nanoseconds> elapsed;
        for (int i = 0; i < 200 && !(elapsed = timer.elapsed()); ++i)
        {
            (void)gpu->present();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        REQUIRE(elapsed.has_value());
        CHECK(elapsed->count() >= 0);
        CHECK(timer.isReady());
        gpu.expectNoValidationErrors();
    }
}

TEST_SUITE("renderer/gpu_timer/profiler")
{
    TEST_CASE("the Profiler timer provider writes into the current encoder and reports a GPU duration; without an encoder it measures nothing")
    {
        Gpu gpu;
        SKIP_TEST_IF_NULL(gpu.device.get(), "OpenGL 4.3 context");
        rd::ProfilerGpuTimerFactory factory(*gpu.device);
        auto idle = factory.CreateTimer("idle");
        REQUIRE(idle != nullptr);
        idle->Begin(); // aucun encodeur : sans effet
        idle->End();
        CHECK_FALSE(idle->IsReady());

        const TextureHandle color = gpu.target(Format::kRGBA8Unorm, {64, 64});
        const PipelineHandle pipe = gpu.pipeline(gpu.shader(ShaderStage::kVertex, kFullscreenVs), gpu.shader(ShaderStage::kFragment, kRedFs));
        auto timer = factory.CreateTimer("pass");
        REQUIRE(timer != nullptr);
        auto encoder = gpu->createCommandEncoder();
        factory.setEncoder(encoder.get());
        timer->Begin();
        encoder->beginRenderPass(passOn(color));
        encoder->setPipeline(pipe);
        encoder->draw(3);
        encoder->endRenderPass();
        timer->End();
        factory.setEncoder(nullptr);
        REQUIRE(gpu->submit(std::move(encoder)).has_value());
        for (int i = 0; i < 200 && !timer->IsReady(); ++i)
        {
            (void)gpu->present();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        REQUIRE(timer->IsReady());
        CHECK(timer->GetElapsedTime().count() >= 0);
        gpu.expectNoValidationErrors();
    }
}
