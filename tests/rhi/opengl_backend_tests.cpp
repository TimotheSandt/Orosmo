// Tests « boîte blanche » du backend OpenGL (src/rhi/opengl/) : ce que l'API du RHI ne laisse pas observer. Le test inclut
// l'en-tête interne du backend mais n'appelle aucune fonction `gl*` (règle 4) : l'état GL est lu par des auxiliaires du backend
// (`GlDevice::inspectVertexArray`) ou par ses structures internes.
#include <doctest/doctest.h>

#include "support/test_support.hpp"
#include "rhi/gpu_fixture.hpp"

#include "../../src/rhi/opengl/gl_device.hpp"

TEST_SUITE("rhi/opengl/backend")
{
    TEST_CASE("after a pass, the pipeline's vertex array no longer references its vertex and index buffers (G98 a)")
    {
        Gpu gpu;
        SKIP_TEST_IF_NULL(gpu.device.get(), "OpenGL 4.3 context");
        auto &device = static_cast<gl::GlDevice &>(*gpu);
        const TextureHandle color = gpu.target(Format::kRGBA8Unorm, {4, 4});
        const std::vector<float> positions{-1, -1, 3, -1, -1, 3};
        const std::vector<std::uint16_t> indices{0, 1, 2};
        const BufferHandle vb = gpu.buffer(BufferUsage::kVertex, bytesOf(positions), 0, "positions");
        const BufferHandle ib = gpu.buffer(BufferUsage::kIndex, bytesOf(indices), 0, "indices");
        const char *vs = R"(#version 430 core
layout(location = 0) in vec2 position;
void main() { gl_Position = vec4(position, 0.0, 1.0); })";
        GraphicsPipelineDesc d;
        d.vertex = gpu.shader(ShaderStage::kVertex, vs);
        d.fragment = gpu.shader(ShaderStage::kFragment, kRedFs);
        d.vertexBuffers = {{0, 8, VertexStepRate::kVertex}};
        d.vertexAttributes = {{0, 0, VertexFormat::kFloat2, 0}};
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
                e.setIndexBuffer(ib, IndexType::kUint16);
                e.drawIndexed(3);
                e.endRenderPass();
            });
        CHECK(rgba(gpu.pixel(color, 1, 1)) == std::array<std::uint8_t, 4>{255, 0, 0, 255});

        // Sans détachement, détruire `vb` et `ib` laisserait leur stockage retenu par ce VAO tant qu'il vit.
        auto bindings = device.inspectVertexArray(*pipe);
        REQUIRE(bindings.has_value());
        CHECK(bindings->vertexBuffer0 == 0u);
        CHECK(bindings->indexBuffer == 0u);
        gpu.expectNoValidationErrors();
    }

    TEST_CASE("a pipeline knows whether its program accesses storage (the criterion of the barrier between draws)")
    {
        Gpu gpu;
        SKIP_TEST_IF_NULL(gpu.device.get(), "OpenGL 4.3 context");
        auto &device = static_cast<gl::GlDevice &>(*gpu);
        const PipelineHandle plain = gpu.pipeline(gpu.shader(ShaderStage::kVertex, kFullscreenVs), gpu.shader(ShaderStage::kFragment, kRedFs));
        const char *ssboFs = R"(#version 430 core
layout(std430, binding = 1) buffer Data { vec4 values[]; } data;
layout(location = 0) out vec4 color;
void main() { color = data.values[0]; })";
        const char *imageFs = R"(#version 430 core
layout(rgba8, binding = 0) uniform readonly image2D img;
layout(location = 0) out vec4 color;
void main() { color = imageLoad(img, ivec2(0)); })";
        const PipelineHandle ssbo = gpu.pipeline(gpu.shader(ShaderStage::kVertex, kFullscreenVs), gpu.shader(ShaderStage::kFragment, ssboFs));
        const PipelineHandle image = gpu.pipeline(gpu.shader(ShaderStage::kVertex, kFullscreenVs), gpu.shader(ShaderStage::kFragment, imageFs));
        REQUIRE(device.get(plain) != nullptr);
        CHECK_FALSE(device.get(plain)->usesStorage);
        CHECK(device.get(ssbo)->usesStorage);
        CHECK(device.get(image)->usesStorage);
        gpu.expectNoValidationErrors();
    }
}
