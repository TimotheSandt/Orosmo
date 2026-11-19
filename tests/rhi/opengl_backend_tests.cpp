// Tests « boîte blanche » du backend OpenGL (src/rhi/opengl/) : ce que l'API du RHI ne laisse pas observer, lu directement
// dans l'état OpenGL. Seul fichier de test qui inclut un en-tête interne du backend et appelle `gl*` (règle 4 : ces appels
// restent confinés au backend et à ses propres tests).
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
        const gl::PipelineRes *p = device.get(*pipe);
        REQUIRE(p != nullptr);
        glBindVertexArray(p->vao);
        GLint vertexBuffer = -1;
        GLint indexBuffer = -1;
        glGetIntegeri_v(GL_VERTEX_BINDING_BUFFER, 0, &vertexBuffer);
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &indexBuffer);
        glBindVertexArray(device.emptyVertexArray());
        CHECK(vertexBuffer == 0);
        CHECK(indexBuffer == 0);
        gpu.expectNoValidationErrors();
    }
}
